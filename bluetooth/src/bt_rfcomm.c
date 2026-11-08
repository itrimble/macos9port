/* RFCOMM: serial port emulation over one L2CAP channel (PSM 0x0003).
 *
 * A Bluetooth RFCOMM mux is bound to a single L2CAP channel, so this
 * module owns one channel rather than a global table. The multiplexer
 * channel (DLCI 0) carries SABM/UA for the mux itself, each data channel
 * owns a DLCI of 2 * server_channel, and user data travels in INFO
 * frames. Flow control is credit based: the receiver grants the sender
 * permission to transmit a number of bytes.
 *
 * Addresses follow the direction of the frame: bit 0 is the extension
 * bit, bit 1 marks a server response, and bits 2..7 carry the DLCI. */
#include "bt_internal.h"

#include <stdlib.h>

enum { BT_RF_FRAME_MAX = 512, BT_RF_CREDIT0 = 128 };

struct BtRfcommChan {
    int used;
    int dlci;
    uint8_t server_channel;
    int state;
    int peer_credit;          /* bytes the peer still lets us send */
    uint8_t *rx; size_t rx_cap, rx_len, rx_head;
    uint8_t *tx; size_t tx_cap, tx_len, tx_head;
    BtRfcommOpenFn on_open;
    void *open_ctx;
};

struct BtRfcomm {
    BtL2cap *l2;
    int cid;                  /* L2CAP channel this mux runs on */
    int local_is_server;
    int mux;                  /* the mux channel is established */
    BtRfcommChan chans[BT_MAX_RFCOMM];
    struct {
        uint8_t server_channel;
        BtRfcommOpenFn on_open;
        void *ctx;
    } listens[BT_MAX_RFCOMM];
    int nlistens;
    BtRfcommDataFn on_data;
    void *data_ctx;
    BtRfcommCloseFn on_close;
    void *close_ctx;
    BtRfcommOpenFn on_open;
    void *open_ctx;
    uint8_t txbuf[BT_RF_FRAME_MAX + 8];
};

/* ---- helpers ---- */
static uint8_t rf_addr(int dlci, int local_is_server) {
    return (uint8_t)(((dlci & 0x3F) << 2) | (local_is_server ? 0x03 : 0x01));
}

static BtRfcommChan *chan_find(BtRfcomm *r, int dlci) {
    for (int i = 0; i < BT_MAX_RFCOMM; i++)
        if (r->chans[i].used && r->chans[i].dlci == dlci) return &r->chans[i];
    return NULL;
}

static BtRfcommChan *chan_alloc(BtRfcomm *r, int dlci, uint8_t server_channel) {
    for (int i = 0; i < BT_MAX_RFCOMM; i++) {
        BtRfcommChan *c = &r->chans[i];
        if (c->used) continue;
        memset(c, 0, sizeof *c);
        c->used = 1;
        c->dlci = dlci;
        c->server_channel = server_channel;
        c->state = BT_RFCOMM_WAIT_SABM;
        c->rx_cap = BT_MAX_FRAME * 4;
        c->rx = malloc(c->rx_cap);
        c->tx_cap = BT_MAX_FRAME * 4;
        c->tx = malloc(c->tx_cap);
        if (!c->rx || !c->tx) {
            free(c->rx);
            free(c->tx);
            memset(c, 0, sizeof *c);
            return NULL;
        }
        return c;
    }
    return NULL;
}

static void chan_free(BtRfcommChan *c) {
    free(c->rx);
    free(c->tx);
    memset(c, 0, sizeof *c);
}

static void rf_write(BtRfcomm *r, uint8_t addr, uint8_t ctrl, const uint8_t *info, size_t n) {
    if (n > BT_RF_FRAME_MAX) return;
    uint8_t *pkt = r->txbuf;
    pkt[0] = addr;
    pkt[1] = ctrl;
    bt_wr16(pkt + 2, (uint16_t)n);
    if (n) memcpy(pkt + 4, info, n);
    bt_l2cap_send(r->l2, r->cid, pkt, 4 + n);
}

/* A UIH frame with P/F set carries a single credit byte, not a length. */
static void rf_write_credit(BtRfcomm *r, uint8_t addr, uint8_t credits) {
    uint8_t pkt[3];
    pkt[0] = addr;
    pkt[1] = BT_RFCOMM_UIH;   /* P/F and UIH both set */
    pkt[2] = credits;
    bt_l2cap_send(r->l2, r->cid, pkt, sizeof pkt);
}

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

/* ---- setup ---- */
static void rf_on_l2cap_data(void *ctx, BtL2capChan *chan, const uint8_t *p, size_t n) {
    BtRfcomm *r = ctx;
    (void)chan;
    bt_rfcomm_rx(r, p, n);
}

BtRfcomm *bt_rfcomm_new(BtL2cap *l, int cid, int local_is_server) {
    if (!l || cid < 0) return NULL;
    BtRfcomm *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->l2 = l;
    r->cid = cid;
    r->local_is_server = local_is_server;
    BtL2capChan *chan = bt_l2cap_chan(l, cid);
    if (chan) bt_l2cap_set_chan_data_fn(chan, rf_on_l2cap_data, r);
    return r;
}

void bt_rfcomm_free(BtRfcomm *r) {
    if (!r) return;
    for (int i = 0; i < BT_MAX_RFCOMM; i++)
        if (r->chans[i].used) chan_free(&r->chans[i]);
    free(r);
}

void bt_rfcomm_set_data_fn(BtRfcomm *r, BtRfcommDataFn fn, void *ctx) {
    if (!r) return;
    r->on_data = fn;
    r->data_ctx = ctx;
}

void bt_rfcomm_set_close_fn(BtRfcomm *r, BtRfcommCloseFn fn, void *ctx) {
    if (!r) return;
    r->on_close = fn;
    r->close_ctx = ctx;
}

void bt_rfcomm_set_open_fn(BtRfcomm *r, BtRfcommOpenFn fn, void *ctx) {
    if (!r) return;
    r->on_open = fn;
    r->open_ctx = ctx;
}

int bt_rfcomm_listen(BtRfcomm *r, uint8_t server_channel, BtRfcommOpenFn on_open, void *ctx) {
    if (!r || server_channel < 1 || server_channel > 30) return BT_EINVAL;
    for (int i = 0; i < r->nlistens; i++)
        if (r->listens[i].server_channel == server_channel) return BT_EBUSY;
    if (r->nlistens >= BT_MAX_RFCOMM) return BT_ELIMIT;
    r->listens[r->nlistens].server_channel = server_channel;
    r->listens[r->nlistens].on_open = on_open;
    r->listens[r->nlistens].ctx = ctx;
    r->nlistens++;
    return BT_OK;
}

/* ---- connect ---- */
int bt_rfcomm_connect(BtRfcomm *r, uint8_t server_channel, int *out_dlci) {
    if (!r || !out_dlci || server_channel < 1 || server_channel > 30) return BT_EINVAL;
    if (r->local_is_server) return BT_EINVAL; /* the server never dials out */
    int dlci = server_channel * 2;
    if (chan_find(r, dlci)) return BT_EBUSY;
    BtRfcommChan *c = chan_alloc(r, dlci, server_channel);
    if (!c) return BT_ENOMEM;
    c->on_open = r->on_open;
    c->open_ctx = r->open_ctx;
    *out_dlci = dlci;
    if (!r->mux) {
        /* Bring the multiplexer channel up first. */
        rf_write(r, rf_addr(0, 0), BT_RFCOMM_SABM, NULL, 0);
    } else {
        rf_write(r, rf_addr(dlci, 0), BT_RFCOMM_SABM, NULL, 0);
        c->state = BT_RFCOMM_WAIT_UA;
    }
    return BT_OK;
}

int bt_rfcomm_disconnect(BtRfcomm *r, int dlci) {
    if (!r) return BT_EINVAL;
    BtRfcommChan *c = chan_find(r, dlci);
    if (!c) return BT_ENOENT;
    rf_write(r, rf_addr(dlci, r->local_is_server), BT_RFCOMM_DISC, NULL, 0);
    c->state = BT_RFCOMM_WAIT_DISC;
    return BT_OK;
}

int bt_rfcomm_info(BtRfcomm *r, int dlci, BtRfcommInfo *out) {
    if (!r || !out) return BT_EINVAL;
    BtRfcommChan *c = chan_find(r, dlci);
    if (!c) return BT_ENOENT;
    memset(out, 0, sizeof *out);
    out->state = c->state;
    out->dlci = c->dlci;
    out->server_channel = c->server_channel;
    out->credit = c->peer_credit;
    out->unread = c->rx_len;
    return BT_OK;
}

/* ---- data ---- */
static void rf_pump(BtRfcomm *r);

int bt_rfcomm_send(BtRfcomm *r, int dlci, const void *p, size_t n) {
    if (!r || (!p && n)) return BT_EINVAL;
    BtRfcommChan *c = chan_find(r, dlci);
    if (!c) return BT_ENOENT;
    if (c->state != BT_RFCOMM_OPEN) return BT_ECLOSED;
    if (n > c->tx_cap - c->tx_len) return BT_ELIMIT;
    fifo_push(c->tx, c->tx_cap, &c->tx_head, &c->tx_len, p, n);
    rf_pump(r);
    return (int)n;
}

long bt_rfcomm_recv(BtRfcomm *r, int dlci, void *p, size_t cap) {
    if (!r || !p) return BT_EINVAL;
    BtRfcommChan *c = chan_find(r, dlci);
    if (!c) return BT_ENOENT;
    size_t n = fifo_pop(c->rx, c->rx_cap, &c->rx_head, &c->rx_len, p, cap);
    /* Reading frees buffer space, so give the peer the credit back. The
     * credit travels in one byte, so a large read is split. */
    size_t left = n;
    while (left) {
        uint8_t chunk = left > 255 ? 255 : (uint8_t)left;
        rf_write_credit(r, rf_addr(dlci, r->local_is_server), chunk);
        left -= chunk;
    }
    return (long)n;
}

int bt_rfcomm_grant_credit(BtRfcomm *r, int dlci, uint8_t credits) {
    if (!r) return BT_EINVAL;
    BtRfcommChan *c = chan_find(r, dlci);
    if (!c) return BT_ENOENT;
    rf_write_credit(r, rf_addr(dlci, r->local_is_server), credits);
    return BT_OK;
}

/* Move queued bytes onto the wire while the peer has credit. */
static void rf_pump(BtRfcomm *r) {
    for (int i = 0; i < BT_MAX_RFCOMM; i++) {
        BtRfcommChan *c = &r->chans[i];
        if (!c->used || c->state != BT_RFCOMM_OPEN) continue;
        while (c->tx_len && c->peer_credit > 0) {
            size_t n = c->tx_len < BT_RF_FRAME_MAX ? c->tx_len : BT_RF_FRAME_MAX;
            if ((int)n > c->peer_credit) n = (size_t)c->peer_credit;
            uint8_t buf[BT_RF_FRAME_MAX];
            fifo_pop(c->tx, c->tx_cap, &c->tx_head, &c->tx_len, buf, n);
            rf_write(r, rf_addr(c->dlci, r->local_is_server), 0x00, buf, n);
            c->peer_credit -= (int)n;
        }
    }
}

/* ---- receive ---- */
static void rf_handle(BtRfcomm *r, uint8_t addr, uint8_t ctrl, const uint8_t *info,
                       size_t info_len, int credit_frame, uint8_t credit) {
    int dlci = (addr >> 2) & 0x3F;
    int is_uih = (ctrl & 0x40) != 0;
    int type = ctrl & 0x3F;

    if (credit_frame) {
        BtRfcommChan *c = chan_find(r, dlci);
        if (c) c->peer_credit += credit;
        rf_pump(r);
        return;
    }
    if (is_uih) {
        /* A UIH frame with P/F clear is a multiplexer control frame.
         * The data channels work with the default parameters, so it
         * is accepted and ignored. */
        return;
    }

    switch (type) {
    case BT_RFCOMM_T_SABM: {
        if (dlci == 0) {
            r->mux = 1;
            rf_write(r, rf_addr(0, r->local_is_server), BT_RFCOMM_UA, NULL, 0);
            return;
        }
        uint8_t sc = (uint8_t)(dlci / 2);
        BtRfcommOpenFn on_open = NULL;
        void *ctx = NULL;
        int found = 0;
        for (int i = 0; i < r->nlistens; i++) {
            if (r->listens[i].server_channel != sc) continue;
            on_open = r->listens[i].on_open;
            ctx = r->listens[i].ctx;
            found = 1;
            break;
        }
        if (!found) {
            rf_write(r, rf_addr(dlci, r->local_is_server), BT_RFCOMM_DM, NULL, 0);
            return;
        }
        BtRfcommChan *c = chan_find(r, dlci);
        if (!c) c = chan_alloc(r, dlci, sc);
        if (!c) {
            rf_write(r, rf_addr(dlci, r->local_is_server), BT_RFCOMM_DM, NULL, 0);
            return;
        }
        c->server_channel = sc;
        c->on_open = on_open;
        c->open_ctx = ctx;
        rf_write(r, rf_addr(dlci, r->local_is_server), BT_RFCOMM_UA, NULL, 0);
        c->state = BT_RFCOMM_OPEN;
        rf_write_credit(r, rf_addr(dlci, r->local_is_server), BT_RF_CREDIT0);
        if (c->on_open) c->on_open(c->open_ctx, (BtRfcommChan *)c);
        break;
    }
    case BT_RFCOMM_T_UA: {
        BtRfcommChan *c = chan_find(r, dlci);
        if (dlci == 0) {
            r->mux = 1;
            for (int i = 0; i < BT_MAX_RFCOMM; i++) {
                BtRfcommChan *w = &r->chans[i];
                if (!w->used || w->state != BT_RFCOMM_WAIT_SABM) continue;
                rf_write(r, rf_addr(w->dlci, r->local_is_server), BT_RFCOMM_SABM, NULL, 0);
                w->state = BT_RFCOMM_WAIT_UA;
            }
            return;
        }
        if (!c) return;
        c->state = BT_RFCOMM_OPEN;
        rf_write_credit(r, rf_addr(dlci, r->local_is_server), BT_RF_CREDIT0);
        if (c->on_open) c->on_open(c->open_ctx, (BtRfcommChan *)c);
        rf_pump(r);
        break;
    }
    case BT_RFCOMM_T_DISC: {
        BtRfcommChan *c = chan_find(r, dlci);
        rf_write(r, rf_addr(dlci, r->local_is_server), BT_RFCOMM_DM, NULL, 0);
        if (c) {
            c->state = BT_RFCOMM_CLOSED;
            if (r->on_close) r->on_close(r->close_ctx, (BtRfcommChan *)c);
        }
        break;
    }
    case BT_RFCOMM_T_DM: {
        BtRfcommChan *c = chan_find(r, dlci);
        if (!c) return;
        c->state = BT_RFCOMM_ERROR;
        if (r->on_close) r->on_close(r->close_ctx, (BtRfcommChan *)c);
        break;
    }
    case BT_RFCOMM_T_INFO: { /* user data */
        BtRfcommChan *c = chan_find(r, dlci);
        if (!c) return;
        fifo_push(c->rx, c->rx_cap, &c->rx_head, &c->rx_len, info, info_len);
        if (r->on_data) r->on_data(r->data_ctx, (BtRfcommChan *)c, info, info_len);
        break;
    }
    default:
        /* UIH multiplexer frames are accepted and ignored: the data
         * channels work with the default parameters. */
        break;
    }
}

void bt_rfcomm_rx(BtRfcomm *r, const uint8_t *p, size_t n) {
    if (!r || !p || n < 2) return;
    uint8_t addr = p[0];
    uint8_t ctrl = p[1];
    int is_uih = (ctrl & 0x40) != 0;
    int pf = (ctrl & 0x80) != 0;

    if (is_uih && pf) {
        if (n < 3) return;
        rf_handle(r, addr, ctrl, NULL, 0, 1, p[2]);
        return;
    }
    if (n < 4) return;
    uint16_t info_len = bt_rd16(p + 2);
    if (n < 4 + info_len) return;
    rf_handle(r, addr, ctrl, p + 4, info_len, 0, 0);
}
