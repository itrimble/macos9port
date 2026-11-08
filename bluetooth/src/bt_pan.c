/* PAN socket: a link-layer socket over BNEP.
 *
 * The API mirrors the broker in net/ so the guest network stack can treat
 * a Bluetooth personal area network the same way it treats a TCP or UDP
 * connection: open, send, receive, query, close. Frames are routed by the
 * source MAC of the peer. */
#include "bt_internal.h"

#include <stdlib.h>

typedef struct {
    int used;
    int cid;                 /* BNEP channel */
    uint8_t peer_mac[6];
    int state;
    uint8_t *rx; size_t rx_cap, rx_len, rx_head;
    size_t unsent;
    uint64_t bytes_in, bytes_out;
} BtPanSock;

struct BtPan {
    BtBnep *bnep;
    uint8_t local_mac[6];
    BtPanSock socks[BT_MAX_PAN];
};

static void fifo_push(uint8_t *buf, size_t cap, size_t *head, size_t *len, const uint8_t *p, size_t n) {
    size_t tail = (*head + *len) % cap;
    if (cap - *len < n) {
        memmove(buf, buf + *head, *len);
        *head = 0;
        tail = *len;
        if (cap - *len < n) return;
    }
    for (size_t i = 0; i < n; i++) buf[(tail + i) % cap] = p[i];
    *len += n;
}

static size_t fifo_pop(uint8_t *buf, size_t cap, size_t *head, size_t *len, uint8_t *p, size_t cap_out) {
    size_t n = *len < cap_out ? *len : cap_out;
    for (size_t i = 0; i < n; i++) p[i] = buf[(*head + i) % cap];
    *head = (*head + n) % cap;
    *len -= n;
    return n;
}

static void pan_on_frame(void *ctx, BtBnepChan *chan, const uint8_t *frame, size_t len) {
    BtPan *p = ctx;
    (void)chan;
    if (!frame || len < 14) return;
    /* The source MAC is the second half of the Ethernet header. */
    bt_pan_route(p, frame + 6, frame, len);
}

BtPan *bt_pan_new(BtBnep *b) {
    if (!b) return NULL;
    BtPan *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->bnep = b;
    /* A locally administered address so the peer never sees our host MAC. */
    p->local_mac[0] = 0x02;
    p->local_mac[1] = 0x00;
    p->local_mac[2] = 0x00;
    p->local_mac[3] = 0x00;
    p->local_mac[4] = 0x00;
    p->local_mac[5] = 0x01;
    bt_bnep_set_frame_fn(b, pan_on_frame, p);
    return p;
}

void bt_pan_free(BtPan *p) {
    if (!p) return;
    for (int i = 0; i < BT_MAX_PAN; i++) free(p->socks[i].rx);
    free(p);
}

int bt_pan_set_local_mac(BtPan *p, const uint8_t mac[6]) {
    if (!p || !mac) return BT_EINVAL;
    memcpy(p->local_mac, mac, 6);
    return BT_OK;
}

int bt_pan_open(BtPan *p, uint16_t handle, const BtAddress *peer, const uint8_t peer_mac[6],
                int *out_id) {
    if (!p || !peer_mac || !out_id) return BT_EINVAL;
    /* Reuse an existing socket to the same peer. */
    for (int i = 0; i < BT_MAX_PAN; i++) {
        BtPanSock *s = &p->socks[i];
        if (!s->used) continue;
        if (memcmp(s->peer_mac, peer_mac, 6) != 0) continue;
        *out_id = i;
        s->state = BT_PAN_UP;
        return BT_OK;
    }
    int cid = 0;
    if (bt_bnep_find_by_mac(p->bnep, peer_mac, &cid) != BT_OK) {
        int rc = bt_bnep_connect(p->bnep, handle, peer, &cid);
        if (rc != BT_OK) return rc;
    }
    for (int i = 0; i < BT_MAX_PAN; i++) {
        BtPanSock *s = &p->socks[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->used = 1;
        s->cid = cid;
        s->state = BT_PAN_CONNECTING;
        s->rx_cap = BT_MAX_FRAME * 4;
        s->rx = malloc(s->rx_cap);
        if (!s->rx) {
            memset(s, 0, sizeof *s);
            return BT_ENOMEM;
        }
        memcpy(s->peer_mac, peer_mac, 6);
        *out_id = i;
        return BT_OK;
    }
    return BT_ELIMIT;
}

int bt_pan_send(BtPan *p, int id, const void *frame, size_t len) {
    if (!p || (!frame && len)) return BT_EINVAL;
    if (id < 0 || id >= BT_MAX_PAN || !p->socks[id].used) return BT_ENOENT;
    BtPanSock *s = &p->socks[id];
    if (len < 14) return BT_EINVAL;
    const uint8_t *f = frame;
    uint16_t ethertype = bt_rd16(f + 12);
    int rc = bt_bnep_send(p->bnep, s->cid, f, p->local_mac, ethertype, f + 14, len - 14);
    if (rc != BT_OK) return rc;
    s->bytes_out += len;
    return (int)len;
}

long bt_pan_recv(BtPan *p, int id, void *frame, size_t cap) {
    if (!p || !frame) return BT_EINVAL;
    if (id < 0 || id >= BT_MAX_PAN || !p->socks[id].used) return BT_ENOENT;
    BtPanSock *s = &p->socks[id];
    size_t n = fifo_pop(s->rx, s->rx_cap, &s->rx_head, &s->rx_len, frame, cap);
    if (n) s->bytes_in += n;
    return (long)n;
}

int bt_pan_info(BtPan *p, int id, BtPanInfo *out) {
    if (!p || !out) return BT_EINVAL;
    if (id < 0 || id >= BT_MAX_PAN || !p->socks[id].used) return BT_ENOENT;
    BtPanSock *s = &p->socks[id];
    memset(out, 0, sizeof *out);
    out->state = s->state;
    out->up = s->state == BT_PAN_UP;
    memcpy(out->peer_mac, s->peer_mac, 6);
    out->unread = s->rx_len;
    out->unsent = s->unsent;
    out->bytes_in = s->bytes_in;
    out->bytes_out = s->bytes_out;
    return BT_OK;
}

int bt_pan_close(BtPan *p, int id) {
    if (!p) return BT_EINVAL;
    if (id < 0 || id >= BT_MAX_PAN || !p->socks[id].used) return BT_ENOENT;
    BtPanSock *s = &p->socks[id];
    s->state = BT_PAN_CLOSED;
    free(s->rx);
    memset(s, 0, sizeof *s);
    return BT_OK;
}

/* Route an inbound frame to the socket that owns its source MAC. */
int bt_pan_route(BtPan *p, const uint8_t src_mac[6], const uint8_t *frame, size_t len) {
    if (!p || !src_mac || !frame) return BT_EINVAL;
    for (int i = 0; i < BT_MAX_PAN; i++) {
        BtPanSock *s = &p->socks[i];
        if (!s->used) continue;
        if (memcmp(s->peer_mac, src_mac, 6) != 0) continue;
        fifo_push(s->rx, s->rx_cap, &s->rx_head, &s->rx_len, frame, len);
        s->bytes_in += len;
        return BT_OK;
    }
    return BT_ENOENT;
}
