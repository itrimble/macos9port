/* L2CAP data plane: logical channels over a baseband link.
 *
 * Each baseband link owns its own CID space starting at 0x0040. The
 * signalling channel (CID 0x0001) carries connection, configuration and
 * disconnection requests; data channels carry a length/CID header.
 *
 * Inbound payloads are reassembled before they are queued, and every
 * buffer is bounded: a peer that announces a huge SDU is cut off at
 * BT_MAX_SDU instead of making the desktop allocate. The signalling
 * exchange lives in bt_l2cap_sig.c. */
#include "bt_l2cap_int.h"

#include <stdlib.h>

BtL2cap *bt_l2cap_new(void) {
    BtL2cap *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->tx = malloc(BT_HCI_BUF);
    if (!l->tx) { free(l); return NULL; }
    return l;
}

void bt_l2cap_chan_release(BtL2capChan *c) {
    free(c->rx);
    free(c->q);
    memset(c, 0, sizeof *c);
}

void bt_l2cap_free(BtL2cap *l) {
    if (!l) return;
    for (int i = 0; i < BT_MAX_CHANS; i++)
        if (l->chans[i].used) bt_l2cap_chan_release(&l->chans[i]);
    free(l->tx);
    free(l);
}

void bt_l2cap_set_acl_sender(BtL2cap *l, BtAclSendFn fn, void *ctx) {
    if (!l) return;
    l->send = fn;
    l->send_ctx = ctx;
}

void bt_l2cap_set_close_fn(BtL2cap *l, BtL2capCloseFn fn, void *ctx) {
    if (!l) return;
    l->on_close = fn;
    l->close_ctx = ctx;
}

/* ---- links ---- */
void bt_l2cap_link_up(BtL2cap *l, uint16_t handle, const BtAddress *peer) {
    if (!l) return;
    for (int i = 0; i < BT_MAX_LINKS; i++)
        if (l->links[i].used && l->links[i].handle == handle) return;
    for (int i = 0; i < BT_MAX_LINKS; i++) {
        if (l->links[i].used) continue;
        l->links[i].used = 1;
        l->links[i].handle = handle;
        l->links[i].rx_cid = -1;
        if (peer) l->links[i].peer = *peer;
        return;
    }
}

void bt_l2cap_link_down(BtL2cap *l, uint16_t handle) {
    if (!l) return;
    for (int i = 0; i < BT_MAX_LINKS; i++)
        if (l->links[i].used && l->links[i].handle == handle) l->links[i].used = 0;
    for (int i = 0; i < BT_MAX_CHANS; i++) {
        BtL2capChan *c = &l->chans[i];
        if (c->used && c->handle == handle) bt_l2cap_chan_close(l, c);
    }
}

const BtAddress *bt_l2cap_link_peer(BtL2cap *l, uint16_t handle) {
    for (int i = 0; i < BT_MAX_LINKS; i++)
        if (l->links[i].used && l->links[i].handle == handle) return &l->links[i].peer;
    return NULL;
}

/* The channel that is currently being reassembled on a link. A
 * continuation fragment carries no header, so the receiver has to
 * remember where the fragmented PDU started. */
int bt_l2cap_link_rx_cid(BtL2cap *l, uint16_t handle) {
    for (int i = 0; i < BT_MAX_LINKS; i++)
        if (l->links[i].used && l->links[i].handle == handle) return l->links[i].rx_cid;
    return -1;
}

void bt_l2cap_link_set_rx_cid(BtL2cap *l, uint16_t handle, int cid) {
    for (int i = 0; i < BT_MAX_LINKS; i++)
        if (l->links[i].used && l->links[i].handle == handle) l->links[i].rx_cid = cid;
}

/* ---- channels ---- */
BtL2capChan *bt_l2cap_chan_find(BtL2cap *l, int cid) {
    for (int i = 0; i < BT_MAX_CHANS; i++)
        if (l->chans[i].used && l->chans[i].cid == cid) return &l->chans[i];
    return NULL;
}

BtL2capChan *bt_l2cap_chan_find_link(BtL2cap *l, uint16_t handle, int cid) {
    for (int i = 0; i < BT_MAX_CHANS; i++)
        if (l->chans[i].used && l->chans[i].cid == cid && l->chans[i].handle == handle)
            return &l->chans[i];
    return NULL;
}

/* One CID space per link. */
static int alloc_cid(BtL2cap *l, uint16_t handle) {
    for (int cid = BT_L2CAP_CID_FIRST; cid < BT_L2CAP_CID_FIRST + BT_MAX_CHANS; cid++) {
        int taken = 0;
        for (int i = 0; i < BT_MAX_CHANS; i++) {
            BtL2capChan *c = &l->chans[i];
            if (c->used && c->handle == handle && c->cid == cid) taken = 1;
        }
        if (!taken) return cid;
    }
    return -1;
}

BtL2capChan *bt_l2cap_chan_alloc(BtL2cap *l, uint16_t handle, int cid, uint16_t psm) {
    for (int i = 0; i < BT_MAX_CHANS; i++) {
        BtL2capChan *c = &l->chans[i];
        if (c->used) continue;
        memset(c, 0, sizeof *c);
        c->used = 1;
        c->cid = cid;
        c->psm = psm;
        c->handle = handle;
        c->state = BT_L2CAP_WAIT_CONFIG_RSP;
        c->mtu = BT_L2CAP_MTU_DEFAULT;
        c->peer_mtu = BT_L2CAP_MTU_DEFAULT;
        c->rx_cap = BT_MAX_SDU;
        c->rx = malloc(c->rx_cap);
        c->q_cap = BT_MAX_SDU;
        c->q = malloc(c->q_cap);
        if (!c->rx || !c->q) {
            bt_l2cap_chan_release(c);
            return NULL;
        }
        const BtAddress *peer = bt_l2cap_link_peer(l, handle);
        if (peer) c->peer = *peer;
        return c;
    }
    return NULL;
}

void bt_l2cap_chan_close(BtL2cap *l, BtL2capChan *c) {
    if (!c->used) return;
    c->state = BT_L2CAP_CLOSED;
    if (c->chan_close) c->chan_close(c->chan_close_ctx, c);
    else if (l->on_close) l->on_close(l->close_ctx, c);
    bt_l2cap_chan_release(c);
}

int bt_l2cap_chan_cid(const BtL2capChan *chan) {
    return chan ? chan->cid : -1;
}

int bt_l2cap_chan_peer(const BtL2capChan *chan, BtAddress *out) {
    if (!chan || !out) return BT_EINVAL;
    *out = chan->peer;
    return BT_OK;
}

void bt_l2cap_set_chan_close_fn(BtL2capChan *chan, BtL2capCloseFn fn, void *ctx) {
    if (!chan) return;
    chan->chan_close = fn;
    chan->chan_close_ctx = ctx;
}

void bt_l2cap_set_chan_data_fn(BtL2capChan *chan, BtL2capDataFn fn, void *ctx) {
    if (!chan) return;
    chan->data_fn = fn;
    chan->data_ctx = ctx;
}

BtL2capChan *bt_l2cap_chan(BtL2cap *l, int cid) {
    if (!l) return NULL;
    return bt_l2cap_chan_find(l, cid);
}

uint8_t bt_l2cap_next_lid(BtL2cap *l) {
    l->lid = (uint8_t)((l->lid + 1) & 0x7F);
    if (!l->lid) l->lid = 1;
    return l->lid;
}

void bt_l2cap_detach_context(BtL2cap *l, void *ctx) {
    if (!l || !ctx) return;
    for (int i = 0; i < BT_MAX_CHANS; i++) {
        BtL2capChan *c = &l->chans[i];
        if (c->open_ctx == ctx) { c->on_open = NULL; c->open_ctx = NULL; }
        if (c->data_ctx == ctx) { c->data_fn = NULL; c->data_ctx = NULL; }
        if (c->chan_close_ctx == ctx) { c->chan_close = NULL; c->chan_close_ctx = NULL; }
    }
    for (int i = l->nlistens - 1; i >= 0; i--)
        if (l->listens[i].ctx == ctx) bt_l2cap_unlisten(l, l->listens[i].psm);
}

int bt_l2cap_unlisten(BtL2cap *l, uint16_t psm) {
    if (!l) return BT_EINVAL;
    for (int i = 0; i < l->nlistens; i++) {
        if (l->listens[i].psm != psm) continue;
        memmove(l->listens + i, l->listens + i + 1,
                (size_t)(l->nlistens - i - 1) * sizeof *l->listens);
        l->nlistens--;
        return BT_OK;
    }
    return BT_ENOENT;
}

int bt_l2cap_listen(BtL2cap *l, uint16_t psm, BtL2capOpenFn on_open, void *ctx) {
    if (!l || !psm || psm == BT_L2CAP_CID_SIG) return BT_EINVAL;
    for (int i = 0; i < l->nlistens; i++)
        if (l->listens[i].psm == psm) return BT_EBUSY;
    if (l->nlistens >= BT_MAX_CHANS) return BT_ELIMIT;
    l->listens[l->nlistens].psm = psm;
    l->listens[l->nlistens].on_open = on_open;
    l->listens[l->nlistens].ctx = ctx;
    l->nlistens++;
    return BT_OK;
}

/* ---- connect ---- */
int bt_l2cap_connect(BtL2cap *l, uint16_t handle, const BtAddress *peer, uint16_t psm,
                     uint16_t mtu, BtL2capOpenFn on_open, void *ctx, int *out_cid) {
    if (!l || !l->send || !psm || !out_cid || psm == BT_L2CAP_CID_SIG) return BT_EINVAL;
    int cid = alloc_cid(l, handle);
    if (cid < 0) return BT_ELIMIT;
    BtL2capChan *c = bt_l2cap_chan_alloc(l, handle, cid, psm);
    if (!c) return BT_ENOMEM;
    c->state = BT_L2CAP_WAIT_CONNECT_RSP;
    c->on_open = on_open;
    c->open_ctx = ctx;
    if (mtu) c->mtu = mtu < BT_L2CAP_MTU_MIN ? BT_L2CAP_MTU_MIN : mtu;
    if (peer) c->peer = *peer;
    else {
        const BtAddress *lp = bt_l2cap_link_peer(l, handle);
        if (lp) c->peer = *lp;
    }
    uint8_t d[4];
    bt_wr16(d, psm);
    bt_wr16(d + 2, (uint16_t)cid);
    int rc = bt_l2cap_sig_send(l, handle, BT_SIG_CONN_REQ, bt_l2cap_next_lid(l), d, sizeof d);
    if (rc != BT_OK) {
        bt_l2cap_chan_release(c);
        return rc;
    }
    *out_cid = cid;
    return BT_OK;
}

int bt_l2cap_disconnect(BtL2cap *l, int cid) {
    if (!l) return BT_EINVAL;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (!c) return BT_ENOENT;
    if (c->state == BT_L2CAP_CLOSED) return BT_OK;
    if (c->adopted) return BT_EUNSUP; /* host closes its channel, then reports pipe_closed */
    uint8_t d[2];
    bt_wr16(d, (uint16_t)c->remote_cid);
    c->state = BT_L2CAP_WAIT_DISCONNECT_RSP;
    return bt_l2cap_sig_send(l, c->handle, BT_SIG_DISCONN_REQ, bt_l2cap_next_lid(l), d, sizeof d);
}

int bt_l2cap_info(BtL2cap *l, int cid, BtL2capInfo *out) {
    if (!l || !out) return BT_EINVAL;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (!c) return BT_ENOENT;
    memset(out, 0, sizeof *out);
    out->state = c->state;
    out->cid = (uint16_t)c->cid;
    out->remote_cid = (uint16_t)c->remote_cid;
    out->psm = c->psm;
    out->mtu = c->mtu;
    out->peer_mtu = c->peer_mtu;
    out->peer = c->peer;
    return BT_OK;
}

/* ---- data ---- */
void bt_l2cap_queue_push(BtL2capChan *c, const uint8_t *p, size_t n) {
    if (n + 2 > c->q_cap) return; /* bounded: drop what does not fit */
    size_t tail = (c->q_head + c->q_len) % c->q_cap;
    if (c->q_cap - c->q_len < n + 2) {
        if (c->q_head) {
            memmove(c->q, c->q + c->q_head, c->q_len);
            c->q_head = 0;
            tail = c->q_len;
        }
        if (c->q_cap - c->q_len < n + 2) return;
    }
    c->q[tail] = (uint8_t)(n >> 8);
    c->q[(tail + 1) % c->q_cap] = (uint8_t)n;
    for (size_t i = 0; i < n; i++) c->q[(tail + 2 + i) % c->q_cap] = p[i];
    c->q_len += n + 2;
}

size_t bt_l2cap_queue_pop(BtL2capChan *c, uint8_t *p, size_t cap) {
    if (c->q_len < 2) return 0;
    size_t n = ((size_t)c->q[c->q_head] << 8) | c->q[(c->q_head + 1) % c->q_cap];
    if (n > cap) return 0;
    for (size_t i = 0; i < n; i++) p[i] = c->q[(c->q_head + 2 + i) % c->q_cap];
    c->q_head = (c->q_head + 2 + n) % c->q_cap;
    c->q_len -= n + 2;
    return n;
}

long bt_l2cap_recv(BtL2cap *l, int cid, void *p, size_t cap) {
    if (!l || !p) return BT_EINVAL;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (!c) return BT_ENOENT;
    size_t n = bt_l2cap_queue_pop(c, p, cap);
    if (n) return (long)n;
    if (c->state == BT_L2CAP_CLOSED) return BT_ECLOSED;
    return 0;
}

/* The payload we may put on the wire is bounded by both MTUs. */
static size_t chan_chunk(BtL2capChan *c) {
    uint16_t m = c->peer_mtu < c->mtu ? c->peer_mtu : c->mtu;
    if (m < BT_L2CAP_MTU_MIN) m = BT_L2CAP_MTU_MIN;
    size_t chunk = (size_t)m - 4;
    if (chunk > BT_HCI_BUF - 16) chunk = BT_HCI_BUF - 16;
    return chunk;
}

int bt_l2cap_send(BtL2cap *l, int cid, const void *p, size_t n) {
    if (!l || (!p && n)) return BT_EINVAL;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (!c) return BT_ENOENT;
    if (c->state != BT_L2CAP_OPEN) return BT_ECLOSED;
    if (c->adopted) {
        /* The system framed the channel, so the payload goes as it is. */
        if (n > c->peer_mtu) return BT_ELIMIT;
        if (!c->pipe.write) return BT_EIO;
        long w = c->pipe.write(c->pipe.ctx, p, n);
        return w == (long)n ? BT_OK : BT_EIO;
    }
    if (!l->send) return BT_EIO;
    size_t chunk = chan_chunk(c);
    if (!chunk) return BT_EINVAL;
    const uint8_t *src = p;
    size_t off = 0;
    int first = 1;
    do {
        size_t take = (n - off) < chunk ? (n - off) : chunk;
        uint8_t *pkt = l->tx;
        if (first) {
            /* Only the first fragment carries the L2CAP header. */
            bt_wr16(pkt, (uint16_t)n);
            bt_wr16(pkt + 2, (uint16_t)c->remote_cid);
        }
        if (take) memcpy(pkt + (first ? 4 : 0), src + off, take);
        size_t len = (first ? 4 : 0) + take;
        long w = l->send(l->send_ctx, c->handle, first ? BT_ACL_PB_FIRST : BT_ACL_PB_CONT,
                         pkt, len);
        if (w < 0) return BT_EIO;
        off += take;
        first = 0;
    } while (off < n);
    return BT_OK;
}

/* Adopt a channel the operating system opened and framed. */
int bt_l2cap_adopt(BtL2cap *l, uint16_t handle, const BtAddress *peer, uint16_t psm,
                   uint16_t mtu, const BtL2capPipe *pipe, int *out_cid) {
    if (!l || !pipe || !pipe->write || !out_cid) return BT_EINVAL;
    int cid = -1;
    for (int candidate = BT_L2CAP_CID_FIRST; candidate < BT_L2CAP_CID_FIRST + BT_MAX_CHANS;
         candidate++) {
        int taken = 0;
        for (int i = 0; i < BT_MAX_CHANS; i++) {
            BtL2capChan *e = &l->chans[i];
            if (e->used && e->handle == handle && e->cid == candidate) taken = 1;
        }
        if (!taken) { cid = candidate; break; }
    }
    if (cid < 0) return BT_ELIMIT;
    BtL2capChan *c = bt_l2cap_chan_alloc(l, handle, cid, psm);
    if (!c) return BT_ENOMEM;
    c->pipe = *pipe;
    c->adopted = 1;
    c->state = BT_L2CAP_OPEN;
    c->mtu = mtu ? mtu : BT_L2CAP_MTU_DEFAULT;
    c->peer_mtu = c->mtu;
    if (peer) c->peer = *peer;
    else {
        const BtAddress *lp = bt_l2cap_link_peer(l, handle);
        if (lp) c->peer = *lp;
    }
    *out_cid = cid;
    return BT_OK;
}

void bt_l2cap_pipe_closed(BtL2cap *l, int cid) {
    if (!l) return;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (c && c->adopted) bt_l2cap_chan_close(l, c);
}

void bt_l2cap_pipe_rx(BtL2cap *l, int cid, const uint8_t *p, size_t n) {
    if (!l || !p || !n) return;
    BtL2capChan *c = bt_l2cap_chan_find(l, cid);
    if (!c || !c->adopted) return;
    if (n > c->rx_cap) return;
    memcpy(c->rx, p, n);
    bt_l2cap_queue_push(c, c->rx, n);
    if (c->data_fn) c->data_fn(c->data_ctx, c, c->rx, n);
}

/* ---- receive ---- */
static void handle_data(BtL2cap *l, uint16_t handle, uint8_t flags, const uint8_t *p, size_t len) {
    int is_first = (flags & 0x03) != BT_ACL_PB_CONT;
    uint16_t cid;
    size_t payload;
    if (is_first) {
        if (len < 4) return;
        cid = bt_rd16(p + 2);
        payload = len - 4;
    } else {
        /* A continuation fragment carries no L2CAP header. */
        cid = (uint16_t)bt_l2cap_link_rx_cid(l, handle);
        if (cid < 0) return;
        payload = len;
    }
    BtL2capChan *c = bt_l2cap_chan_find_link(l, handle, (int)cid);
    if (!c) return;

    if (is_first) {
        uint16_t sdu_len = bt_rd16(p);     /* payload length, header excluded */
        c->rx_len = 0;
        c->rx_expect = sdu_len;
        c->rx_overflow = 0;
        bt_l2cap_link_set_rx_cid(l, handle, (int)cid);
        if (payload > c->rx_cap) {
            c->rx_overflow = 1;
            return;
        }
        memcpy(c->rx, p + 4, payload);
        c->rx_len = payload;
    } else {
        if (!c->rx_expect) return; /* continuation without a start */
        if (c->rx_len + payload > c->rx_cap) {
            c->rx_overflow = 1;
            return;
        }
        memcpy(c->rx + c->rx_len, p, payload);
        c->rx_len += payload;
    }
    if (c->rx_overflow) {
        c->rx_len = 0;
        c->rx_expect = 0;
        bt_l2cap_link_set_rx_cid(l, handle, -1);
        return;
    }
    if (c->rx_expect && c->rx_len >= c->rx_expect) {
        size_t n = c->rx_expect;
        bt_l2cap_queue_push(c, c->rx, n);
        c->rx_len = 0;
        c->rx_expect = 0;
        bt_l2cap_link_set_rx_cid(l, handle, -1);
        if (c->data_fn) c->data_fn(c->data_ctx, c, c->rx, n);
    }
}

void bt_l2cap_rx(BtL2cap *l, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    if (!l || !data || !len) return;
    int is_first = (flags & 0x03) != BT_ACL_PB_CONT;
    if (is_first) {
        if (len < 4) return;
        if (bt_rd16(data + 2) == BT_L2CAP_CID_SIG) {
            bt_l2cap_handle_sig(l, handle, data, len);
            return;
        }
    }
    handle_data(l, handle, flags, data, len);
}
