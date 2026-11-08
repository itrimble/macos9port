/* BNEP setup/control framing follows Bluetooth SIG BNEP 1.0.
 * General Ethernet data only; optional filters return unsupported.
 * Disconnect belongs to L2CAP, not a private BNEP opcode. */
#include "bt_internal.h"

#include <stdlib.h>

enum { BT_BNEP_ETH_HDR_LEN = 14, BT_BNEP_HDR_LEN = 2 };

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

static int bnep_send_raw(BtBnep *b, int idx, const uint8_t *p, size_t n) {
    BtBnepChan *c = &b->chans[idx];
    if (n > sizeof b->txbuf) return BT_ELIMIT;
    if (p != b->txbuf) memcpy(b->txbuf, p, n);
    return bt_l2cap_send(b->l2, c->cid, b->txbuf, n);
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
static int bnep_send_cmd(BtBnep *b, int idx, uint8_t cmd, const uint8_t *payload, size_t n) {
    uint8_t pkt[BT_MAX_FRAME];
    if (n > sizeof pkt - BT_BNEP_HDR_LEN) return BT_ELIMIT;
    pkt[0] = BT_BNEP_HDR_BNEP;
    pkt[1] = cmd;
    if (n) memcpy(pkt + 2, payload, n);
    return bnep_send_raw(b, idx, pkt, 2 + n);
}

static void bnep_on_l2cap_data(void *ctx, BtL2capChan *chan, const uint8_t *p, size_t n) {
    BtBnep *b = ctx;
    bt_bnep_rx(b, bt_l2cap_chan_cid(chan), p, n);
}

static void bnep_on_l2cap_close(void *ctx, BtL2capChan *chan) {
    BtBnep *b = ctx;
    int cid = bt_l2cap_chan_cid(chan), idx = bnep_find(b, cid);
    if (idx < 0) return;
    b->chans[idx].state = BT_BNEP_S_WAIT;
    b->chans[idx].used = 0;
    for (int i = 0; i < BT_MAX_BNEP; i++)
        if (b->outgoing[i] == cid) b->outgoing[i] = -1;
    /* The notification may release the protocol owner. */
    if (b->on_close) b->on_close(b->close_ctx, &b->chans[idx]);
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
    bt_l2cap_set_chan_close_fn(chan, bnep_on_l2cap_close, b);
    if (client) {
        b->chans[idx].state = BT_BNEP_S_REQ;
        const uint8_t roles[] = {2, 0x11, 0x16, 0x11, 0x15};
        if (bnep_send_cmd(b, idx, BT_BNEP_CMD_CONN_REQ, roles, sizeof roles) != BT_OK)
            b->chans[idx].state = BT_BNEP_S_WAIT;
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
    if (!b) return;
    bt_l2cap_detach_context(b->l2, b);
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

int bt_bnep_adopt(BtBnep *b, int cid, const BtAddress *peer) {
    if (!b || !peer) return BT_EINVAL;
    if (bnep_find(b, cid) >= 0) return BT_EBUSY;
    BtL2capInfo info;
    if (bt_l2cap_info(b->l2, cid, &info) != BT_OK) return BT_ENOENT;
    if (info.psm != BT_PSM_BNEP || info.state != BT_L2CAP_OPEN) return BT_EINVAL;
    int slot = -1;
    for (int i = 0; i < BT_MAX_BNEP; i++)
        if (b->outgoing[i] < 0) { slot = i; break; }
    if (slot < 0) return BT_ELIMIT;
    b->outgoing[slot] = cid;
    bnep_on_l2cap_open(b, bt_l2cap_chan(b->l2, cid));
    if (bnep_find(b, cid) < 0) { b->outgoing[slot] = -1; return BT_ELIMIT; }
    int state = b->chans[bnep_find(b, cid)].state;
    return state == BT_BNEP_S_REQ || state == BT_BNEP_S_CONNECTED ? BT_OK : BT_EIO;
}

int bt_bnep_send(BtBnep *b, int cid, const uint8_t dst[6], const uint8_t src[6],
                 uint16_t ethertype, const void *payload, size_t len) {
    if (!b || !dst || !src || (len && !payload)) return BT_EINVAL;
    int idx = bnep_find(b, cid);
    if (idx < 0) return BT_ENOENT;
    if (b->chans[idx].state != BT_BNEP_S_CONNECTED) return BT_ECLOSED;
    if (len > sizeof b->txbuf - BT_BNEP_ETH_HDR_LEN - 1) return BT_ELIMIT;
    uint8_t *pkt = b->txbuf;
    pkt[0] = BT_BNEP_HDR_ETHERNET;
    memcpy(pkt + 1, dst, 6);
    memcpy(pkt + 7, src, 6);
    bt_wr16(pkt + 13, ethertype);
    if (len) memcpy(pkt + BT_BNEP_ETH_HDR_LEN + 1, payload, len);
    int rc = bnep_send_raw(b, idx, pkt, BT_BNEP_ETH_HDR_LEN + 1 + len);
    if (rc != BT_OK) return rc;
    b->chans[idx].ethertype = ethertype;
    return BT_OK;
}

int bt_bnep_set_filter(BtBnep *b, int cid, int mode, const uint8_t group[6]) {
    if (!b) return BT_EINVAL;
    int idx = bnep_find(b, cid);
    if (idx < 0) return BT_ENOENT;
    if (mode != BT_BNEP_FILTER_ALL && mode != BT_BNEP_FILTER_NONE) return BT_EINVAL;
    uint8_t payload[14] = {0};
    size_t n = 2;
    if (mode == BT_BNEP_FILTER_ALL) {
        payload[1] = 12;
        if (group) { memcpy(payload + 2, group, 6); memcpy(payload + 8, group, 6); }
        else memset(payload + 8, 0xff, 6);
        n = sizeof payload;
    }
    return bnep_send_cmd(b, idx, BT_BNEP_CMD_FILTER, payload, n);
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

/* Decode only Bluetooth base UUIDs; reject other services without narrowing. */
static int service_uuid(const uint8_t *p, unsigned n) {
    static const uint8_t base[12] = {0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    if (n == 2) return (p[0] << 8) | p[1];
    if (p[0] || p[1]) return -1;
    if (n == 16 && memcmp(p + 4, base, sizeof base)) return -1;
    return (p[2] << 8) | p[3];
}

static void control_status(BtBnep *b, int idx, uint8_t cmd, uint16_t status) {
    uint8_t p[] = {(uint8_t)(status >> 8), (uint8_t)status};
    bnep_send_cmd(b, idx, cmd, p, sizeof p);
}

static void bnep_handle_control(BtBnep *b, int idx, uint8_t cmd, const uint8_t *p, size_t n) {
    BtBnepChan *c = &b->chans[idx];
    if (cmd == BT_BNEP_CMD_CONN_REQ) {
        if (n < 1) return;
        unsigned width = p[0];
        if (width != 2 && width != 4 && width != 16) {
            control_status(b, idx, 2, 3); return;
        }
        if (n != 1 + 2 * width) return;
        int dst = service_uuid(p + 1, width), src = service_uuid(p + 1 + width, width);
        uint16_t status = 0;
        if (dst != 0x1115 && dst != 0x1116 && dst != 0x1117) status = 1;
        else if (src != 0x1115 && src != 0x1116 && src != 0x1117) status = 2;
        else if (src != 0x1115 && dst != 0x1115) status = 4;
        control_status(b, idx, 2, status);
        if (!status) c->state = BT_BNEP_S_CONNECTED;
    } else if (cmd == BT_BNEP_CMD_CONN_RSP) {
        if (n != 2 || c->state != BT_BNEP_S_REQ) return;
        c->state = (p[0] == 0 && p[1] == 0) ? BT_BNEP_S_CONNECTED : BT_BNEP_S_WAIT;
    } else if (cmd == BT_BNEP_CMD_NET_FILTER || cmd == BT_BNEP_CMD_FILTER) {
        if (n < 2) return;
        size_t len = ((size_t)p[0] << 8) | p[1];
        if (len != n - 2) return;
        /* Optional filtering is explicitly unsupported, never silently accepted. */
        control_status(b, idx, (uint8_t)(cmd + 1), 1);
    } else if (cmd != 0 && cmd != 4 && cmd != 6) {
        bnep_send_cmd(b, idx, 0, &cmd, 1);
    }
}

void bt_bnep_rx(BtBnep *b, int cid, const uint8_t *p, size_t n) {
    if (!b || !p || n < 1 || n > BT_MAX_FRAME) return;
    int idx = bnep_find(b, cid);
    if (idx < 0) return;
    /* Extensions and compressed forms require negotiated local MAC knowledge.
       Reject them safely until supported; never parse extension bytes as data. */
    if (p[0] == BT_BNEP_HDR_ETHERNET) {
        if (n < 15 || b->chans[idx].state != BT_BNEP_S_CONNECTED) return;
        b->chans[idx].ethertype = bt_rd16(p + 13);
        if (b->on_frame) b->on_frame(b->frame_ctx, &b->chans[idx], p + 1, n - 1);
    } else if (p[0] == BT_BNEP_HDR_BNEP && n >= 2) {
        bnep_handle_control(b, idx, p[1], p + 2, n - 2);
    }
}
