/* TCP over the PAN: a compact endpoint state machine. One segment is in
 * flight at a time, the window is fixed and a retransmit timer covers a
 * lost segment. That is enough for the guest's own TCP over a personal
 * area network, where the link is short and rarely drops a frame. */
#include "gn_pan_int.h"

#include <stdlib.h>

static int tcp_index(GnPan *p, const GnPanTcp *t) { return (int)(t - p->tcp); }

/* Find the endpoint a segment belongs to: an exact match first, then a
 * listener waiting for a connection on that port. */
static GnPanTcp *tcp_find(GnPan *p, uint32_t src_ip, uint16_t sport, uint16_t dport) {
    GnPanTcp *listener = NULL;
    for (int i = 0; i < GN_PAN_TCP_MAX; i++) {
        GnPanTcp *t = &p->tcp[i];
        if (!t->used || t->local_port != dport) continue;
        if (t->state == GN_TS_LISTEN) {
            if (!listener) listener = t;
            continue;
        }
        if (t->state == GN_TS_CLOSED) continue;
        if (t->peer_ip == src_ip && t->peer_port == sport) return t;
    }
    return listener;
}

static int tcp_alloc(GnPan *p, GnPanTcp **out) {
    for (int i = 0; i < GN_PAN_TCP_MAX; i++) {
        GnPanTcp *t = &p->tcp[i];
        if (t->used) continue;
        memset(t, 0, sizeof *t);
        t->pan = p;
        t->used = 1;
        t->local_ip = p->local_ip;
        if (gn_ring_init(&t->tx, GN_PAN_BUF) || gn_ring_init(&t->unacked, GN_PAN_BUF) ||
            gn_ring_init(&t->rx, GN_PAN_BUF)) {
            gn_ring_free(&t->tx);
            gn_ring_free(&t->unacked);
            gn_ring_free(&t->rx);
            memset(t, 0, sizeof *t);
            return GN_ENOMEM;
        }
        *out = t;
        return GN_OK;
    }
    return GN_ELIMIT;
}

/* ---- sending ---- */

/* One segment with the given sequence and acknowledgement numbers. */
static void tcp_send(GnPanTcp *t, uint32_t seq, uint32_t ack, uint8_t flags, const uint8_t *data,
                     size_t len) {
    gn_pan_send_tcp(t->pan, t->peer_ip, t->local_port, t->peer_port, seq, ack, flags, data, len);
}

/* Move queued bytes onto the wire, one segment at a time. */
static void tcp_flush(GnPanTcp *t) {
    if (t->state != GN_TS_ESTABLISHED && t->state != GN_TS_CLOSE_WAIT) return;
    if (t->snd_una != t->snd_nxt) return; /* a segment is already in flight */
    size_t n = t->tx.len > GN_PAN_MSS ? GN_PAN_MSS : t->tx.len;
    if (!n && !t->pending_fin) return;
    uint8_t buf[GN_PAN_MSS];
    if (n) gn_ring_peek(&t->tx, buf, n);
    uint8_t flags = GN_TCP_ACK;
    if (n) flags |= GN_TCP_PSH;
    if (t->pending_fin) flags |= GN_TCP_FIN;
    tcp_send(t, t->snd_nxt, t->rcv_nxt, flags, n ? buf : NULL, n);
    if (n) {
        gn_ring_drop(&t->tx, n);
        gn_ring_put(&t->unacked, buf, n);
    }
    t->snd_nxt += (uint32_t)n;
    t->in_flight = n ? 0 : t->in_flight;
    if (t->pending_fin) {
        t->pending_fin = 0;
        t->fin_sent = 1;
        t->in_flight |= GN_TCP_FIN;
        t->snd_nxt++;
        t->state = t->fin_next ? t->fin_next : GN_TS_FIN_WAIT_1;
    }
    t->rto_at = t->pan->now + GN_PAN_RTO_MS;
    t->retries = 0;
}

/* Send the segment that is still waiting for its acknowledgement again. */
static void tcp_retransmit(GnPanTcp *t) {
    size_t n = t->unacked.len > GN_PAN_MSS ? GN_PAN_MSS : t->unacked.len;
    uint8_t buf[GN_PAN_MSS];
    if (n) gn_ring_peek(&t->unacked, buf, n);
    uint8_t flags = 0;
    if (t->rcv_nxt) flags |= GN_TCP_ACK;
    if (t->in_flight & GN_TCP_SYN) flags |= GN_TCP_SYN;
    if (t->in_flight & GN_TCP_FIN) flags |= GN_TCP_FIN;
    if (n) flags |= GN_TCP_PSH;
    tcp_send(t, t->snd_una, t->rcv_nxt, flags, n ? buf : NULL, n);
    t->rto_at = t->pan->now + GN_PAN_RTO_MS;
    t->retries++;
}

/* Answer a segment nobody wants with a reset. */
static void tcp_reset_peer(GnPan *p, uint32_t src_ip, uint16_t sport, uint16_t dport, uint32_t seq,
                           uint32_t ack, uint8_t flags) {
    uint32_t rseq = 0, rack = 0;
    uint8_t rflags = GN_TCP_RST;
    if (flags & GN_TCP_ACK) {
        rseq = ack;
    } else {
        rack = seq + 1;
        rflags |= GN_TCP_ACK;
    }
    gn_pan_send_tcp(p, src_ip, dport, sport, rseq, rack, rflags, NULL, 0);
}

/* ---- receiving ---- */

void gn_pan_tcp_input(GnPan *p, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len) {
    (void)dst_ip;
    if (len < GN_TCP_HDR_LEN) return;
    uint16_t sport = gn_rd16(seg), dport = gn_rd16(seg + 2);
    uint32_t seq = gn_rd32(seg + 4), ack = gn_rd32(seg + 8);
    size_t doff = (size_t)(seg[12] >> 4) * 4;
    uint8_t flags = seg[13];
    if (doff < GN_TCP_HDR_LEN || doff > len) return;
    const uint8_t *data = seg + doff;
    size_t dlen = len - doff;

    GnPanTcp *t = tcp_find(p, src_ip, sport, dport);
    if (!t) {
        tcp_reset_peer(p, src_ip, sport, dport, seq, ack, flags);
        return;
    }
    if (flags & GN_TCP_RST) {
        t->state = GN_TS_CLOSED;
        t->reset = 1;
        t->eof = 1;
        return;
    }
    if (t->state == GN_TS_LISTEN) {
        if (!(flags & GN_TCP_SYN)) return;
        GnPanTcp *c = NULL;
        if (tcp_alloc(p, &c) != GN_OK) return;
        c->peer_ip = src_ip;
        c->peer_port = sport;
        c->local_port = dport;
        c->isn = p->next_isn;
        p->next_isn += 7919;
        c->snd_una = c->snd_nxt = c->isn;
        c->rcv_nxt = seq + 1;
        c->state = GN_TS_SYN_RECEIVED;
        tcp_send(c, c->isn, c->rcv_nxt, GN_TCP_SYN | GN_TCP_ACK, NULL, 0);
        c->snd_nxt = c->isn + 1;
        c->in_flight = GN_TCP_SYN;
        c->rto_at = p->now + GN_PAN_RTO_MS;
        if (t->naccepted < GN_PAN_TCP_MAX) t->accepted[t->naccepted++] = tcp_index(p, c);
        return;
    }
    if (t->state == GN_TS_SYN_SENT) {
        if (!(flags & GN_TCP_SYN)) return; /* a reset was handled above */
        if (flags & GN_TCP_ACK) t->snd_una = ack;
        t->rcv_nxt = seq + 1;
        tcp_send(t, t->snd_nxt, t->rcv_nxt, GN_TCP_ACK, NULL, 0);
        t->in_flight = 0;
        t->state = GN_TS_ESTABLISHED;
        return;
    }
    if (t->state == GN_TS_SYN_RECEIVED) {
        if (!(flags & GN_TCP_ACK) || ack != t->snd_nxt) return;
        t->snd_una = ack;
        t->in_flight = 0;
        t->state = GN_TS_ESTABLISHED;
    }
    /* The peer confirms what it has received. */
    if (flags & GN_TCP_ACK) {
        int32_t diff = (int32_t)(ack - t->snd_una);
        if (diff > 0 && (int32_t)(ack - t->snd_nxt) <= 0) {
            gn_ring_drop(&t->unacked, (size_t)diff);
            t->snd_una = ack;
            t->in_flight = 0;
            t->rto_at = t->pan->now + GN_PAN_RTO_MS;
            t->retries = 0;
        }
        if (t->fin_sent && ack == t->snd_nxt) {
            if (t->state == GN_TS_FIN_WAIT_1) t->state = GN_TS_FIN_WAIT_2;
            else if (t->state == GN_TS_LAST_ACK) t->state = GN_TS_CLOSED;
            else if (t->state == GN_TS_CLOSING) t->state = GN_TS_TIME_WAIT;
        }
    }
    /* Data that arrives in order is kept and acknowledged. */
    if (dlen && seq == t->rcv_nxt) {
        gn_ring_put(&t->rx, data, dlen);
        t->rcv_nxt += (uint32_t)dlen;
        tcp_send(t, t->snd_nxt, t->rcv_nxt, GN_TCP_ACK, NULL, 0);
    }
    /* The peer closes. */
    if ((flags & GN_TCP_FIN) && seq + (uint32_t)dlen == t->rcv_nxt) {
        t->rcv_nxt++;
        t->peer_fin = 1;
        tcp_send(t, t->snd_nxt, t->rcv_nxt, GN_TCP_ACK, NULL, 0);
        if (t->state == GN_TS_ESTABLISHED) t->state = GN_TS_CLOSE_WAIT;
        else if (t->state == GN_TS_FIN_WAIT_1) t->state = GN_TS_CLOSING;
        else if (t->state == GN_TS_FIN_WAIT_2) t->state = GN_TS_TIME_WAIT;
    }
    tcp_flush(t);
}

/* ---- the endpoints ---- */

GnPanTcp *gn_pan_tcp_new(GnPan *p) {
    GnPanTcp *t = NULL;
    if (!p) return NULL;
    if (tcp_alloc(p, &t) != GN_OK) return NULL;
    return t;
}

void gn_pan_tcp_free(GnPanTcp *t) {
    if (!t || !t->used) return;
    GnPan *p = t->pan;
    int idx = tcp_index(p, t);
    for (int i = 0; i < GN_PAN_TCP_MAX; i++) {
        GnPanTcp *l = &p->tcp[i];
        if (!l->used) continue;
        for (int k = 0; k < l->naccepted; k++) {
            if (l->accepted[k] != idx) continue;
            memmove(l->accepted + k, l->accepted + k + 1,
                    (size_t)(l->naccepted - k - 1) * sizeof *l->accepted);
            l->naccepted--;
            break;
        }
    }
    gn_ring_free(&t->tx);
    gn_ring_free(&t->unacked);
    gn_ring_free(&t->rx);
    memset(t, 0, sizeof *t);
}

int gn_pan_tcp_connect(GnPanTcp *t, uint32_t peer_ip, uint16_t peer_port, uint16_t local_port) {
    if (!t || !t->used) return GN_EINVAL;
    GnPan *p = t->pan;
    if (!p->have_peer) return GN_EIO;
    if (t->state != GN_TS_CLOSED) return GN_EINVAL;
    if (!peer_ip || !peer_port) return GN_EINVAL;
    t->peer_ip = peer_ip;
    t->peer_port = peer_port;
    p->next_port++;
    if (p->next_port < 49152) p->next_port = 49152;
    t->local_port = local_port ? local_port : p->next_port;
    t->local_ip = p->local_ip;
    t->isn = p->next_isn;
    p->next_isn += 7919;
    t->snd_una = t->snd_nxt = t->isn;
    t->rcv_nxt = 0;
    t->state = GN_TS_SYN_SENT;
    tcp_send(t, t->isn, 0, GN_TCP_SYN, NULL, 0);
    t->snd_nxt = t->isn + 1;
    t->in_flight = GN_TCP_SYN;
    t->rto_at = p->now + GN_PAN_RTO_MS;
    return GN_OK;
}

int gn_pan_tcp_listen(GnPanTcp *t, uint16_t local_port) {
    if (!t || !t->used) return GN_EINVAL;
    if (t->state != GN_TS_CLOSED) return GN_EINVAL;
    GnPan *p = t->pan;
    if (!local_port) {
        p->next_port++;
        if (p->next_port < 49152) p->next_port = 49152;
        local_port = p->next_port;
    }
    t->local_port = local_port;
    t->local_ip = p->local_ip;
    t->state = GN_TS_LISTEN;
    return GN_OK;
}

int gn_pan_tcp_accept(GnPanTcp *t, GnPanTcp **out) {
    if (!t || !t->used || !out) return GN_EINVAL;
    if (t->state != GN_TS_LISTEN) return GN_EINVAL;
    if (!t->naccepted) return GN_EAGAIN;
    int idx = t->accepted[0];
    memmove(t->accepted, t->accepted + 1, (size_t)(--t->naccepted) * sizeof *t->accepted);
    *out = &t->pan->tcp[idx];
    return GN_OK;
}

long gn_pan_tcp_send(GnPanTcp *t, const void *p, size_t n) {
    if (!t || !t->used || (!p && n)) return GN_EINVAL;
    if (t->state == GN_TS_CLOSED || t->reset) return GN_ECLOSED;
    size_t k = gn_ring_put(&t->tx, p, n);
    tcp_flush(t);
    return (long)k;
}

long gn_pan_tcp_recv(GnPanTcp *t, void *p, size_t n) {
    if (!t || !t->used || !p) return GN_EINVAL;
    size_t k = gn_ring_get(&t->rx, p, n);
    if (!k && (t->state == GN_TS_CLOSED || t->reset || t->peer_fin)) return GN_ECLOSED;
    return (long)k;
}

int gn_pan_tcp_state(GnPanTcp *t) { return t ? t->state : GN_TS_CLOSED; }

uint16_t gn_pan_tcp_local_port(GnPanTcp *t) { return t ? t->local_port : 0; }

int gn_pan_tcp_shutdown(GnPanTcp *t) {
    if (!t || !t->used) return GN_EINVAL;
    if (t->state == GN_TS_ESTABLISHED) {
        t->pending_fin = 1;
        t->fin_next = GN_TS_FIN_WAIT_1;
    } else if (t->state == GN_TS_CLOSE_WAIT) {
        t->pending_fin = 1;
        t->fin_next = GN_TS_LAST_ACK;
    } else {
        return GN_OK;
    }
    tcp_flush(t);
    return GN_OK;
}

int gn_pan_tcp_abort(GnPanTcp *t) {
    if (!t || !t->used) return GN_EINVAL;
    if (t->state != GN_TS_CLOSED)
        tcp_send(t, t->snd_nxt, t->rcv_nxt, GN_TCP_RST | GN_TCP_ACK, NULL, 0);
    t->state = GN_TS_CLOSED;
    t->reset = 1;
    t->eof = 1;
    return GN_OK;
}

/* ---- the timer ---- */

int gn_pan_tick_endpoints(GnPan *p) {
    int work = 0;
    for (int i = 0; i < GN_PAN_TCP_MAX; i++) {
        GnPanTcp *t = &p->tcp[i];
        if (!t->used) continue;
        if (t->state == GN_TS_TIME_WAIT) {
            t->state = GN_TS_CLOSED;
            work = 1;
            continue;
        }
        if (t->snd_una != t->snd_nxt && p->now >= t->rto_at) {
            if (t->retries >= GN_PAN_MAX_RETRY) {
                gn_pan_tcp_abort(t);
                work = 1;
                continue;
            }
            tcp_retransmit(t);
            work = 1;
        }
        tcp_flush(t);
    }
    return work;
}
