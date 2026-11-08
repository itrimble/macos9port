/* MacTCP .IPP driver control calls over the broker: TCP streams, ASR
 * notifications and completion routines, sync and async calls. Field
 * offsets follow the public TCPiopb layout (68k alignment): csCode @26,
 * tcpStream @28, csParam @32. */
#include "gn_mactcp_int.h"

#include <stdlib.h>

GnMacTcp *gn_mactcp_new(GnBroker *b, GnResolver *r, uint32_t our_ip) {
    GnMacTcp *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->b = b;
    m->r = r;
    m->our_ip = our_ip ? our_ip : 0x0A000202u; /* 10.0.2.2: a private, guest-visible address */
    return m;
}

void gn_mactcp_free(GnMacTcp *m) {
    if (!m) return;
    for (int i = 0; i < GN_STREAMS; i++) {
        if (!m->s[i].used) continue;
        if (m->s[i].conn >= 0) gn_release(m->b, m->s[i].conn);
        if (m->s[i].listener >= 0) gn_release(m->b, m->s[i].listener);
    }
    free(m);
}

GnStream *gn_mt_stream(GnMacTcp *m, uint32_t ptr, int *index) {
    if (ptr < GN_STREAM_BASE || (ptr - GN_STREAM_BASE) % 16) return NULL;
    uint32_t i = (ptr - GN_STREAM_BASE) / 16;
    if (i >= GN_STREAMS || !m->s[i].used) return NULL;
    if (index) *index = (int)i;
    return &m->s[i];
}

int gn_mt_new_stream(GnMacTcp *m, int udp, uint32_t rcv, uint32_t len, uint32_t notify, uint32_t user, int *index) {
    for (int i = 0; i < GN_STREAMS; i++) {
        GnStream *s = &m->s[i];
        if (s->used && rcv < s->rcv + s->rcv_len && s->rcv < rcv + len) return MT_STREAM_ALREADY_OPEN;
    }
    for (int i = 0; i < GN_STREAMS; i++) {
        if (m->s[i].used) continue;
        GnStream *s = &m->s[i];
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->udp = udp;
        s->rcv = rcv;
        s->rcv_len = len;
        s->notify = notify;
        s->user = user;
        s->conn = s->listener = -1;
        *index = i;
        return 0;
    }
    return MT_INSUFFICIENT_RESOURCES;
}

void gn_mt_queue(GnMacTcp *m, int kind, uint32_t proc, uint32_t pb, uint32_t stream, uint32_t user, uint16_t ev,
                 uint16_t reason) {
    if (!proc || m->nq >= (int)(sizeof m->q / sizeof *m->q)) return;
    GnGuestCall *c = &m->q[m->nq++];
    c->kind = kind;
    c->proc = proc;
    c->pb = pb;
    c->stream = stream;
    c->user_data = user;
    c->event = ev;
    c->reason = reason;
}

int gn_mt_gather(GnMem *mem, uint32_t wds, uint8_t *out, size_t cap, size_t *len) {
    *len = 0;
    for (int i = 0; i < 64; i++) {
        if (!gm_ok(mem, wds + (uint32_t)i * 6, 6)) return MT_INVALID_WDS;
        uint32_t n = gm_rd(mem, wds + (uint32_t)i * 6, 2), p = gm_rd(mem, wds + (uint32_t)i * 6 + 2, 4);
        if (!n) return 0;
        if (!gm_ok(mem, p, n) || *len + n > cap) return MT_INVALID_WDS;
        memcpy(out + *len, mem->ram + p, n);
        *len += n;
    }
    return MT_INVALID_WDS;
}

const char *gn_mt_host_for(GnMacTcp *m, uint32_t ip) {
    for (int i = 0; i < GN_CACHE; i++) if (m->cache[i].name[0] && m->cache[i].ip == ip) return m->cache[i].name;
    return NULL;
}

static void sin4(struct sockaddr_in *a, uint32_t ip, uint16_t port) {
    memset(a, 0, sizeof *a);
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(ip);
    a->sin_port = htons(port);
}

static uint16_t local_port(GnMacTcp *m, int conn) {
    GnInfo in;
    if (gn_info(m->b, conn, &in)) return 0;
    return ntohs(((struct sockaddr_in *)&in.local)->sin_port);
}

static int state_code(GnMacTcp *m, GnStream *s) {
    GnInfo in;
    if (s->listener >= 0) return 2;                            /* listen */
    if (s->conn < 0 || gn_info(m->b, s->conn, &in)) return 0;  /* closed */
    switch (in.state) {
    case GN_S_CONNECTING: return 6;                            /* SYN sent */
    case GN_S_OPEN: return s->closing ? 10 : 8;                /* FIN wait 1 / established */
    case GN_S_PEER_CLOSED: return s->closing ? 18 : 14;        /* last ACK / close wait */
    default: return 0;
    }
}

static int add_op(GnMacTcp *m, uint32_t pb, int code, int stream, int async, uint64_t deadline) {
    for (int i = 0; i < GN_OPS; i++) {
        if (m->ops[i].used) continue;
        m->ops[i] = (GnOp){1, async, pb, code, stream, deadline, 0};
        return 0;
    }
    return MT_INSUFFICIENT_RESOURCES;
}

/* One attempt at a pending (or new) call. Returns 1 while it must wait. */
static int progress(GnMacTcp *m, GnMem *mem, GnOp *op, uint64_t now, int *result) {
    if (op->code >= GN_UDP_CREATE && op->code <= GN_UDP_MTU) return gn_mt_udp_progress(m, mem, op, now, result);
    GnStream *s = &m->s[op->stream];
    uint32_t pb = op->pb;
    if (!s->used) { *result = MT_INVALID_STREAM; return 0; }
    GnInfo in;
    int have = s->conn >= 0 && !gn_info(m->b, s->conn, &in);
    switch (op->code) {
    case GN_TCP_ACTIVE_OPEN:
        if (!have || in.state == GN_S_FAILED || in.state == GN_S_CLOSED) { *result = MT_OPEN_FAILED; return 0; }
        if (in.state == GN_S_CONNECTING) {
            if (op->deadline && now >= op->deadline) { gn_abort(m->b, s->conn); *result = MT_OPEN_FAILED; return 0; }
            return 1;
        }
        gm_wr(mem, pb + 42, 4, m->our_ip);
        gm_wr(mem, pb + 46, 2, local_port(m, s->conn));
        *result = 0;
        return 0;
    case GN_TCP_PASSIVE_OPEN: {
        int id;
        while (gn_tcp_accept(m->b, s->listener, &id) == GN_OK) {
            GnInfo ai;
            gn_info(m->b, id, &ai);
            const struct sockaddr_in *r = (const struct sockaddr_in *)&ai.remote;
            uint32_t rip = ntohl(r->sin_addr.s_addr);
            uint16_t rport = ntohs(r->sin_port);
            if ((s->filter_ip && s->filter_ip != rip) || (s->filter_port && s->filter_port != rport)) {
                gn_abort(m->b, id);
                gn_release(m->b, id);
                continue;
            }
            s->conn = id;
            s->remote_ip = rip;
            s->remote_port = rport;
            gn_release(m->b, s->listener);
            s->listener = -1;
            gm_wr(mem, pb + 36, 4, rip);
            gm_wr(mem, pb + 40, 2, rport);
            gm_wr(mem, pb + 42, 4, m->our_ip);
            gm_wr(mem, pb + 46, 2, s->local_port);
            *result = 0;
            return 0;
        }
        if (op->deadline && now >= op->deadline) {
            gn_release(m->b, s->listener);
            s->listener = -1;
            *result = MT_COMMAND_TIMEOUT;
            return 0;
        }
        return 1;
    }
    case GN_TCP_SEND: {
        uint8_t buf[65535];
        size_t len;
        if ((*result = gn_mt_gather(mem, gm_rd(mem, pb + 38, 4), buf, sizeof buf, &len))) return 0;
        if (!have || in.state == GN_S_FAILED || in.state == GN_S_CLOSED) { *result = MT_CONNECTION_TERMINATED; return 0; }
        if (in.state == GN_S_CONNECTING) return 1;
        long k = gn_send(m->b, s->conn, buf + op->sent, len - op->sent);
        if (k < 0) { *result = MT_CONNECTION_CLOSING; return 0; }
        op->sent += (uint32_t)k;
        if (op->sent < len) return 1;
        *result = 0;
        return 0;
    }
    case GN_TCP_RCV: case GN_TCP_NOCOPY_RCV: {
        if (!have) { *result = MT_CONNECTION_DOESNT_EXIST; return 0; }
        uint32_t dst, cap;
        if (op->code == GN_TCP_RCV) {
            dst = gm_rd(mem, pb + 36, 4);
            cap = gm_rd(mem, pb + 40, 2);
            if (!gm_ok(mem, dst, cap) || !cap) { *result = MT_INVALID_BUF_PTR; return 0; }
        } else {
            if (s->lent) { /* the area is out on loan; wait for TCPRcvBfrReturn */
                if (op->deadline && now >= op->deadline) { *result = MT_COMMAND_TIMEOUT; return 0; }
                return 1;
            }
            dst = s->rcv;
            cap = s->rcv_len > 65535 ? 65535 : s->rcv_len;
        }
        long k = in.unread ? gn_recv(m->b, s->conn, mem->ram + dst, cap) : 0;
        if (k <= 0) {
            if (in.state == GN_S_FAILED || in.state == GN_S_CLOSED) { *result = MT_CONNECTION_TERMINATED; return 0; }
            if (in.state == GN_S_PEER_CLOSED) { *result = MT_CONNECTION_CLOSING; return 0; }
            if (op->deadline && now >= op->deadline) { *result = MT_COMMAND_TIMEOUT; return 0; }
            return 1;
        }
        gm_wr(mem, pb + 34, 1, 0); /* markFlag */
        gm_wr(mem, pb + 35, 1, 0); /* urgentFlag */
        if (op->code == GN_TCP_RCV) {
            gm_wr(mem, pb + 40, 2, (uint32_t)k);
        } else {
            uint32_t rds = gm_rd(mem, pb + 42, 4), n = gm_rd(mem, pb + 46, 2);
            if (n < 1 || !gm_ok(mem, rds, (n + 1) * 6)) { *result = MT_INVALID_RDS; return 0; }
            gm_wr(mem, rds, 2, (uint32_t)k);
            gm_wr(mem, rds + 2, 4, dst);
            gm_wr(mem, rds + 6, 2, 0);
            s->lent = (int)k;
        }
        s->note_data = 0;
        *result = 0;
        return 0;
    }
    case GN_TCP_CLOSE:
        if (!have || in.state == GN_S_FAILED || in.state == GN_S_CLOSED) { *result = 0; return 0; }
        if (in.unsent && !(op->deadline && now >= op->deadline)) return 1;
        *result = 0;
        return 0;
    default:
        *result = MT_INVALID_CSCODE;
        return 0;
    }
}

static int finish(GnMacTcp *m, GnMem *mem, uint32_t pb, int async, int result) {
    gm_wr(mem, pb + 16, 2, (uint32_t)(uint16_t)(int16_t)result);
    if (async) gn_mt_queue(m, 1, gm_rd(mem, pb + 12, 4), pb, 0, 0, 0, 0);
    return result;
}

static uint64_t secs(uint64_t now, uint32_t s) { return s ? now + (uint64_t)s * 1000 : 0; }

int gn_mactcp_control(GnMacTcp *m, GnMem *mem, uint32_t pb, int async, uint64_t now) {
    if (!m || !mem || (pb & 1) || !gm_ok(mem, pb, 104)) return MT_PARAM_ERR;
    int code = (int16_t)gm_rd(mem, pb + 26, 2), result = 0, idx = -1;
    uint32_t sp = gm_rd(mem, pb + 28, 4);
    if (code == GN_IPCTL_GETADDR) {
        gm_wr(mem, pb + 28, 4, m->our_ip);
        gm_wr(mem, pb + 32, 4, 0xFFFFFF00u);
        return finish(m, mem, pb, async, 0);
    }
    if (code >= GN_UDP_CREATE && code <= GN_UDP_MTU) {
        if (gn_mt_udp(m, mem, pb, code, now, &result)) goto pending;
        return finish(m, mem, pb, async, result);
    }
    if (code == GN_TCP_CREATE) {
        uint32_t rcv = gm_rd(mem, pb + 32, 4), len = gm_rd(mem, pb + 36, 4);
        if (!rcv || !gm_ok(mem, rcv, len)) return finish(m, mem, pb, async, MT_INVALID_BUF_PTR);
        if (len < 4096) return finish(m, mem, pb, async, MT_INVALID_LENGTH);
        result = gn_mt_new_stream(m, 0, rcv, len, gm_rd(mem, pb + 40, 4), gm_rd(mem, pb + 44, 4), &idx);
        if (!result) gm_wr(mem, pb + 28, 4, GN_STREAM_BASE + (uint32_t)idx * 16);
        return finish(m, mem, pb, async, result);
    }
    GnStream *s = gn_mt_stream(m, sp, &idx);
    if (!s || s->udp) return finish(m, mem, pb, async, MT_INVALID_STREAM);
    switch (code) {
    case GN_TCP_RELEASE:
        if (s->conn >= 0) { gn_abort(m->b, s->conn); gn_release(m->b, s->conn); }
        if (s->listener >= 0) gn_release(m->b, s->listener);
        gm_wr(mem, pb + 32, 4, s->rcv);
        gm_wr(mem, pb + 36, 4, s->rcv_len);
        for (int i = 0; i < GN_OPS; i++)
            if (m->ops[i].used && m->ops[i].stream == idx) {
                finish(m, mem, m->ops[i].pb, m->ops[i].async, MT_CONNECTION_TERMINATED);
                m->ops[i].used = 0;
            }
        s->used = 0;
        return finish(m, mem, pb, async, 0);
    case GN_TCP_ACTIVE_OPEN: case GN_TCP_PASSIVE_OPEN: {
        if (s->conn >= 0 || s->listener >= 0) return finish(m, mem, pb, async, MT_CONNECTION_EXISTS);
        uint32_t rip = gm_rd(mem, pb + 36, 4);
        uint16_t rport = (uint16_t)gm_rd(mem, pb + 40, 2), lport = (uint16_t)gm_rd(mem, pb + 46, 2);
        uint8_t valid = (uint8_t)gm_rd(mem, pb + 34, 1);
        s->closing = 0;
        s->note_data = s->note_close = s->note_term = 0;
        if (code == GN_TCP_ACTIVE_OPEN) {
            struct sockaddr_in a;
            sin4(&a, rip, rport);
            if (!rip || !rport) return finish(m, mem, pb, async, MT_OPEN_FAILED);
            if (gn_tcp_connect(m->b, (struct sockaddr *)&a, sizeof a, gn_mt_host_for(m, rip), &s->conn))
                return finish(m, mem, pb, async, MT_OPEN_FAILED);
            s->remote_ip = rip;
            s->remote_port = rport;
            uint32_t ulp = (valid & 0x80) ? gm_rd(mem, pb + 32, 1) : 30;
            if (add_op(m, pb, code, idx, async, secs(now, ulp ? ulp : 30))) return finish(m, mem, pb, async, MT_INSUFFICIENT_RESOURCES);
        } else {
            if (gn_tcp_listen(m->b, AF_INET, lport, &s->listener)) return finish(m, mem, pb, async, MT_DUPLICATE_SOCKET);
            GnInfo li;
            gn_info(m->b, s->listener, &li);
            s->local_port = ntohs(((struct sockaddr_in *)&li.local)->sin_port);
            s->filter_ip = rip;
            s->filter_port = rport;
            gm_wr(mem, pb + 46, 2, s->local_port);
            if (add_op(m, pb, code, idx, async, secs(now, gm_rd(mem, pb + 35, 1))))
                return finish(m, mem, pb, async, MT_INSUFFICIENT_RESOURCES);
        }
        break;
    }
    case GN_TCP_SEND: case GN_TCP_RCV: case GN_TCP_NOCOPY_RCV: case GN_TCP_CLOSE: {
        if (s->conn < 0) return finish(m, mem, pb, async, MT_CONNECTION_DOESNT_EXIST);
        if (code == GN_TCP_SEND && s->closing) return finish(m, mem, pb, async, MT_CONNECTION_CLOSING);
        if (code == GN_TCP_CLOSE) { s->closing = 1; gn_shutdown(m->b, s->conn); }
        uint32_t t = code == GN_TCP_RCV || code == GN_TCP_NOCOPY_RCV ? gm_rd(mem, pb + 32, 1)
                   : (gm_rd(mem, pb + 34, 1) & 0x80) ? gm_rd(mem, pb + 32, 1) : 0;
        if (add_op(m, pb, code, idx, async, secs(now, t))) return finish(m, mem, pb, async, MT_INSUFFICIENT_RESOURCES);
        break;
    }
    case GN_TCP_RCV_BFR_RETURN:
        s->lent = 0;
        return finish(m, mem, pb, async, 0);
    case GN_TCP_ABORT:
        if (s->conn < 0 && s->listener < 0) return finish(m, mem, pb, async, MT_CONNECTION_DOESNT_EXIST);
        if (s->conn >= 0) { gn_abort(m->b, s->conn); gn_release(m->b, s->conn); s->conn = -1; }
        if (s->listener >= 0) { gn_release(m->b, s->listener); s->listener = -1; }
        for (int i = 0; i < GN_OPS; i++)
            if (m->ops[i].used && m->ops[i].stream == idx) {
                finish(m, mem, m->ops[i].pb, m->ops[i].async, MT_CONNECTION_TERMINATED);
                m->ops[i].used = 0;
            }
        return finish(m, mem, pb, async, 0);
    case GN_TCP_STATUS: {
        GnInfo in;
        int have = s->conn >= 0 && !gn_info(m->b, s->conn, &in);
        gm_wr(mem, pb + 38, 4, s->remote_ip);
        gm_wr(mem, pb + 42, 2, s->remote_port);
        gm_wr(mem, pb + 44, 4, m->our_ip);
        gm_wr(mem, pb + 48, 2, s->conn >= 0 ? local_port(m, s->conn) : s->local_port);
        gm_wr(mem, pb + 52, 1, (uint32_t)state_code(m, s));
        gm_wr(mem, pb + 54, 2, have ? (uint32_t)(65535 - (in.unsent > 65535 ? 65535 : in.unsent)) : 0);
        gm_wr(mem, pb + 56, 2, (uint32_t)(s->rcv_len > 65535 ? 65535 : s->rcv_len));
        gm_wr(mem, pb + 58, 2, have ? (uint32_t)(in.unsent > 65535 ? 65535 : in.unsent) : 0);
        gm_wr(mem, pb + 60, 2, have ? (uint32_t)(in.unread > 65535 ? 65535 : in.unread) : 0);
        return finish(m, mem, pb, async, have || s->listener >= 0 ? 0 : MT_CONNECTION_DOESNT_EXIST);
    }
    default:
        return finish(m, mem, pb, async, MT_INVALID_CSCODE);
    }
    /* The call was queued: try it once right away. */
    for (int i = 0; i < GN_OPS; i++) {
        GnOp *op = &m->ops[i];
        if (!op->used || op->pb != pb) continue;
        gn_pump(m->b, 0);
        if (!progress(m, mem, op, now, &result)) { op->used = 0; return finish(m, mem, pb, async, result); }
        break;
    }
pending:
    gm_wr(mem, pb + 16, 2, 1);
    return 1;
}

int gn_mactcp_poll(GnMacTcp *m, GnMem *mem, uint64_t now, GnGuestCall *calls, int cap) {
    if (!m || !mem) return 0;
    gn_pump(m->b, 0);
    for (int i = 0; i < GN_OPS; i++) {
        GnOp *op = &m->ops[i];
        int result;
        if (!op->used || progress(m, mem, op, now, &result)) continue;
        op->used = 0;
        finish(m, mem, op->pb, op->async, result);
    }
    /* ASR notifications: data waiting with no receive outstanding, peer close, termination. */
    for (int i = 0; i < GN_STREAMS; i++) {
        GnStream *s = &m->s[i];
        GnInfo in;
        if (!s->used || s->udp || s->conn < 0 || !s->notify || gn_info(m->b, s->conn, &in)) continue;
        uint32_t sp = GN_STREAM_BASE + (uint32_t)i * 16;
        int rcv_waiting = 0;
        for (int k = 0; k < GN_OPS; k++)
            rcv_waiting |= m->ops[k].used && m->ops[k].stream == i &&
                           (m->ops[k].code == GN_TCP_RCV || m->ops[k].code == GN_TCP_NOCOPY_RCV);
        if (in.unread && !rcv_waiting && !s->note_data) {
            s->note_data = 1;
            gn_mt_queue(m, 2, s->notify, 0, sp, s->user, 4, 0); /* TCPDataArrival */
        }
        if (in.state == GN_S_PEER_CLOSED && !in.unread && !s->note_close) {
            s->note_close = 1;
            gn_mt_queue(m, 2, s->notify, 0, sp, s->user, 1, 0); /* TCPClosing */
        }
        if (in.state == GN_S_FAILED && !s->note_term) {
            s->note_term = 1;
            gn_mt_queue(m, 2, s->notify, 0, sp, s->user, 3, in.error == GN_ETIMEOUT ? 5 : 2); /* TCPTerminate */
        }
    }
    int n = m->nq < cap ? m->nq : cap;
    if (n > 0 && calls) {
        memcpy(calls, m->q, (size_t)n * sizeof *calls);
        memmove(m->q, m->q + n, (size_t)(m->nq - n) * sizeof *m->q);
        m->nq -= n;
    }
    return calls ? n : 0;
}
