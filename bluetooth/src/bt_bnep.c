/* BNEP: Bluetooth Network Encapsulation Protocol over L2CAP (PSM 0x000F).
 *
 * Data travels in Ethernet encapsulation: a 16 byte Ethernet header
 * (destination MAC, source MAC, ethertype) followed by the payload.
 * Control messages travel in BNEP encapsulation, where the BNEP header
 * carries the control message type.
 *
 * The filter profile implemented here is deliberately small: the
 * multicast filter message carries a mode byte (0 = accept every
 * group, 1 = accept none) followed by a 6 byte group address. */
#include "bt_internal.h"

#include <stdlib.h>

enum { BT_BNEP_ETH_HDR_LEN = 14, BT_BNEP_HDR_LEN = 4 };

struct BtBnepChan {
    int used;
    int cid;                  /* L2CAP channel */
    int local_is_server;
    int state;
    uint8_t peer_mac[6];
    uint8_t our_mac[6];
    uint8_t filter_mode;
    uint8_t filter_group[6];
    uint16_t ethertype;        /* last protocol seen on this link */
};

enum { BT_BNEP_S_WAIT = 0, BT_BNEP_S_REQ, BT_BNEP_S_CONNECTED };

struct BtBnep {
    BtL2cap *l2;
    int listening;
    int outgoing[BT_MAX_BNEP];   /* L2CAP cids we opened */
    BtBnepChan chans[BT_MAX_BNEP];
    BtBnepOpenFn on_open;
    void *open_ctx;
    BtBnepFrameFn on_frame;
    void *frame_ctx;
    BtBnepCloseFn on_close;
    void *close_ctx;
    uint8_t txbuf[BT_MAX_FRAME];
};

static void bnep_send_raw(BtBnep *b, int idx, const uint8_t *p, size_t n) {
    BtBnepChan *c = &b->chans[idx];
    if (n > sizeof b->txbuf) return;
    memcpy(b->txbuf, p, n);
    bt_l2cap_send(b->l2, c->cid, b->txbuf, n);
}

static int bnep_alloc(BtBnep *b, int cid, int local_is_server) {
    for (int i = 0; i < BT_MAX_BNEP; i++) {
        BtBnepChan *c = &b->chans[i];
        if (c->used) continue;
        memset(c, 0, sizeof *c);
        c->used = 1;
        c->cid = cid;
        c->local_is_server = local_is_server;
        c->state = BT_BNEP_S_WAIT;
        c->filter_mode = BT_BNEP_FILTER_ALL;
        return i;
    }
    return -1;
}

static int bnep_find(BtBnep *b, int cid) {
    for (int i = 0; i < BT_MAX_BNEP; i++)
        if (b->chans[i].used && b->chans[i].cid == cid) return i;
    return -1;
}

/* Send a control message: BNEP encapsulation with the message type in
 * the BNEP header. */
static void bnep_send_cmd(BtBnep *b, int idx, uint8_t cmd, const uint8_t *payload, size_t n) {
    uint8_t pkt[BT_MAX_FRAME];
    if (n + BT_BNEP_HDR_LEN + 1 > sizeof pkt) return;
    pkt[0] = BT_BNEP_HDR_BNEP;
    pkt[1] = cmd;
    pkt[2] = 0;
    pkt[3] = 0;
    pkt[4] = 0;
    if (n) memcpy(pkt + 5, payload, n);
    bnep_send_raw(b, idx, pkt, 5 + n);
}

static void bnep_on_l2cap_data(void *ctx, BtL2capChan *chan, const uint8_t *p, size_t n) {
    BtBnep *b = ctx;
    bt_bnep_rx(b, bt_l2cap_chan_cid(chan), p, n);
}

static void bnep_on_l2cap_open(void *ctx, BtL2capChan *chan) {
    BtBnep *b = ctx;
    int cid = bt_l2cap_chan_cid(chan);
    int client = 0;
    for (int i = 0; i < BT_MAX_BNEP; i++)
        if (b->outgoing[i] == cid) client = 1;
    int idx = bnep_alloc(b, cid, !client);
    if (idx < 0) return;
    BtAddress peer;
    if (bt_l2cap_chan_peer(chan, &peer) == BT_OK) {
        /* The device address seeds the peer MAC. */
        memcpy(b->chans[idx].peer_mac, peer.b, 6);
        /* Our own address is locally administered and one above it. */
        memcpy(b->chans[idx].our_mac, peer.b, 6);
        b->chans[idx].our_mac[0] |= 0x02;
        b->chans[idx].our_mac[5] = (uint8_t)(peer.b[5] + 1);
    }
    bt_l2cap_set_chan_data_fn(chan, bnep_on_l2cap_data, b);
    if (client) {
        b->chans[idx].state = BT_BNEP_S_REQ;
        bnep_send_cmd(b, idx, BT_BNEP_CMD_CONN_REQ, NULL, 0);
    } else {
        b->chans[idx].state = BT_BNEP_S_WAIT;
    }
    if (b->on_open) b->on_open(b->open_ctx, (BtBnepChan *)&b->chans[idx]);
}

BtBnep *bt_bnep_new(BtL2cap *l) {
    if (!l) return NULL;
    BtBnep *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->l2 = l;
    for (int i = 0; i < BT_MAX_BNEP; i++) b->outgoing[i] = -1;
    return b;
}

void bt_bnep_free(BtBnep *b) {
    free(b);
}

void bt_bnep_set_frame_fn(BtBnep *b, BtBnepFrameFn fn, void *ctx) {
    if (!b) return;
    b->on_frame = fn;
    b->frame_ctx = ctx;
}

void bt_bnep_set_close_fn(BtBnep *b, BtBnepCloseFn fn, void *ctx) {
    if (!b) return;
    b->on_close = fn;
    b->close_ctx = ctx;
}

int bt_bnep_listen(BtBnep *b, BtBnepOpenFn on_open, void *ctx) {
    if (!b) return BT_EINVAL;
    b->on_open = on_open;
    b->open_ctx = ctx;
    if (b->listening) return BT_OK;
    int rc = bt_l2cap_listen(b->l2, BT_PSM_BNEP, bnep_on_l2cap_open, b);
    if (rc != BT_OK) return rc;
    b->listening = 1;
    return BT_OK;
}

int bt_bnep_connect(BtBnep *b, uint16_t handle, const BtAddress *peer, int *out_cid) {
    if (!b || !out_cid) return BT_EINVAL;
    int cid = 0;
    int rc = bt_l2cap_connect(b->l2, handle, peer, BT_PSM_BNEP, BT_L2CAP_MTU_DEFAULT,
                              bnep_on_l2cap_open, b, &cid);
    if (rc != BT_OK) return rc;
    for (int i = 0; i < BT_MAX_BNEP; i++) {
        if (b->outgoing[i] < 0) {
            b->outgoing[i] = cid;
            break;
        }
    }
    *out_cid = cid;
    return BT_OK;
}

int bt_bnep_send(BtBnep *b, int cid, const uint8_t dst[6], const uint8_t src[6],
                 uint16_t ethertype, const void *payload, size_t len) {
    if (!b || !dst || !src) return BT_EINVAL;
    int idx = bnep_find(b, cid);
    if (idx < 0) return BT_ENOENT;
    if (b->chans[idx].state != BT_BNEP_S_CONNECTED) return BT_ECLOSED;
    if (len + BT_BNEP_ETH_HDR_LEN + 1 > sizeof b->txbuf) return BT_ELIMIT;
    uint8_t *pkt = b->txbuf;
    pkt[0] = BT_BNEP_HDR_ETHERNET;
    memcpy(pkt + 1, dst, 6);
    memcpy(pkt + 7, src, 6);
    bt_wr16(pkt + 13, ethertype);
    if (len) memcpy(pkt + BT_BNEP_ETH_HDR_LEN + 1, payload, len);
    bnep_send_raw(b, idx, pkt, BT_BNEP_ETH_HDR_LEN + 1 + len);
    b->chans[idx].ethertype = ethertype;
    return BT_OK;
}

int bt_bnep_set_filter(BtBnep *b, int cid, int mode, const uint8_t group[6]) {
    if (!b) return BT_EINVAL;
    int idx = bnep_find(b, cid);
    if (idx < 0) return BT_ENOENT;
    uint8_t payload[7];
    payload[0] = (uint8_t)(mode == BT_BNEP_FILTER_NONE ? BT_BNEP_FILTER_NONE : BT_BNEP_FILTER_ALL);
    if (group) memcpy(payload + 1, group, 6);
    else memset(payload + 1, 0, 6);
    bnep_send_cmd(b, idx, BT_BNEP_CMD_FILTER, payload, sizeof payload);
    b->chans[idx].filter_mode = payload[0];
    if (group) memcpy(b->chans[idx].filter_group, group, 6);
    return BT_OK;
}

int bt_bnep_find_by_mac(BtBnep *b, const uint8_t mac[6], int *out_cid) {
    if (!b || !mac || !out_cid) return BT_EINVAL;
    for (int i = 0; i < BT_MAX_BNEP; i++) {
        BtBnepChan *c = &b->chans[i];
        if (!c->used) continue;
        if (memcmp(c->peer_mac, mac, 6) != 0) continue;
        *out_cid = c->cid;
        return BT_OK;
    }
    return BT_ENOENT;
}

int bt_bnep_info(BtBnep *b, int cid, BtBnepInfo *out) {
    if (!b || !out) return BT_EINVAL;
    int idx = bnep_find(b, cid);
    if (idx < 0) return BT_ENOENT;
    BtBnepChan *c = &b->chans[idx];
    memset(out, 0, sizeof *out);
    out->up = c->state == BT_BNEP_S_CONNECTED;
    out->state = c->state;
    memcpy(out->dest_mac, c->peer_mac, 6);
    memcpy(out->src_mac, c->our_mac, 6);
    out->ethertype = c->ethertype;
    return BT_OK;
}

/* ---- receive ---- */
static void bnep_handle_control(BtBnep *b, int idx, uint8_t cmd, const uint8_t *p, size_t n) {
    BtBnepChan *c = &b->chans[idx];
    switch (cmd) {
    case BT_BNEP_CMD_CONN_REQ:
        c->state = BT_BNEP_S_CONNECTED;
        {
            uint8_t rsp[1] = {0x00};
            bnep_send_cmd(b, idx, BT_BNEP_CMD_CONN_RSP, rsp, sizeof rsp);
        }
        break;
    case BT_BNEP_CMD_CONN_RSP:
        if (n >= 1 && p[0] == 0x00) c->state = BT_BNEP_S_CONNECTED;
        else c->state = BT_BNEP_S_WAIT;
        break;
    case BT_BNEP_CMD_DISCONN_REQ: {
        uint8_t rsp[1] = {0x00};
        bnep_send_cmd(b, idx, BT_BNEP_CMD_DISCONN_RSP, rsp, sizeof rsp);
        c->state = BT_BNEP_S_WAIT;
        if (b->on_close) b->on_close(b->close_ctx, (BtBnepChan *)c);
        break;
    }
    case BT_BNEP_CMD_DISCONN_RSP:
        c->state = BT_BNEP_S_WAIT;
        if (b->on_close) b->on_close(b->close_ctx, (BtBnepChan *)c);
        break;
    case BT_BNEP_CMD_FILTER:
        if (n >= 1) c->filter_mode = p[0];
        if (n >= 7) memcpy(c->filter_group, p + 1, 6);
        break;
    default:
        break;
    }
}

void bt_bnep_rx(BtBnep *b, int cid, const uint8_t *p, size_t n) {
    if (!b || !p || n < 1) return;
    int idx = bnep_find(b, cid);
    if (idx < 0) return;
    uint8_t type = p[0];
    if (type == BT_BNEP_HDR_ETHERNET) {
        if (n < 1 + BT_BNEP_ETH_HDR_LEN) return;
        const uint8_t *frame = p + 1;
        b->chans[idx].ethertype = bt_rd16(frame + 12);
        if (b->on_frame) b->on_frame(b->frame_ctx, (BtBnepChan *)&b->chans[idx], frame, n - 1);
        return;
    }
    if (type == BT_BNEP_HDR_BNEP) {
        if (n < 1 + BT_BNEP_HDR_LEN) return;
        uint8_t cmd = p[1];
        bnep_handle_control(b, idx, cmd, p + 5, n - 5);
        return;
    }
}
