#ifndef BT_TEST_H
#define BT_TEST_H

#include "os999_bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bt_checks, bt_failures;
#define CHECK(c)                                                                                       \
    do {                                                                                               \
        bt_checks++;                                                                                   \
        if (!(c)) { bt_failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); }      \
    } while (0)

static inline int bt_done(const char *name) {
    printf("%s: %d checks, %d failures\n", name, bt_checks, bt_failures);
    return bt_failures != 0;
}

/* A controller transport that records what the host writes and hands back
 * whatever the test queues for it. */
enum { BT_FAKE_MAX = 8, BT_FAKE_BUF = 4096 };

typedef struct {
    uint8_t out[BT_FAKE_MAX][BT_FAKE_BUF];
    size_t outlen[BT_FAKE_MAX];
    int nout;
    uint8_t in[BT_FAKE_MAX][BT_FAKE_BUF];
    size_t inlen[BT_FAKE_MAX];
    int nin;
    int fail_writes;
} BtFakeTr;

static inline void bt_fake_init(BtFakeTr *t) {
    memset(t, 0, sizeof *t);
}

static inline long bt_fake_write(void *ctx, const uint8_t *pkt, size_t len) {
    BtFakeTr *t = ctx;
    if (t->fail_writes) return -1;
    if (t->nout >= BT_FAKE_MAX || len > BT_FAKE_BUF) return -1;
    memcpy(t->out[t->nout], pkt, len);
    t->outlen[t->nout] = len;
    t->nout++;
    return (long)len;
}

static inline long bt_fake_read(void *ctx, uint8_t *buf, size_t cap) {
    BtFakeTr *t = ctx;
    if (t->nin <= 0) return 0;
    if (t->inlen[0] > cap) return -1;
    memcpy(buf, t->in[0], t->inlen[0]);
    size_t n = t->inlen[0];
    t->nin--;
    for (int i = 0; i < t->nin; i++) {
        memcpy(t->in[i], t->in[i + 1], t->inlen[i + 1]);
        t->inlen[i] = t->inlen[i + 1];
    }
    return (long)n;
}

static inline void bt_fake_close(void *ctx) { (void)ctx; }

static inline BtHciTransport bt_fake_transport(BtFakeTr *t) {
    BtHciTransport tr;
    tr.write = bt_fake_write;
    tr.read = bt_fake_read;
    tr.close = bt_fake_close;
    tr.ctx = t;
    return tr;
}

/* Queue an event for the host to read. */
static inline void bt_fake_push_event(BtFakeTr *t, uint8_t code, const uint8_t *params, size_t n) {
    if (t->nin >= BT_FAKE_MAX) return;
    uint8_t pkt[BT_FAKE_BUF];
    size_t k = bt_hci_encode_event(code, params, n, pkt, sizeof pkt);
    if (!k) return;
    memcpy(t->in[t->nin], pkt, k);
    t->inlen[t->nin] = k;
    t->nin++;
}

/* Queue an ACL packet for the host to read. */
static inline void bt_fake_push_acl(BtFakeTr *t, uint16_t handle, uint8_t flags,
                                    const uint8_t *data, size_t n) {
    if (t->nin >= BT_FAKE_MAX) return;
    uint8_t pkt[BT_FAKE_BUF];
    size_t k = bt_hci_encode_acl(handle, flags, data, n, pkt, sizeof pkt);
    if (!k) return;
    memcpy(t->in[t->nin], pkt, k);
    t->inlen[t->nin] = k;
    t->nin++;
}

/* Decoded view of the last command the host wrote. */
typedef struct {
    uint16_t opcode;
    const uint8_t *params;
    size_t len;
} BtLastCommand;

static inline int bt_fake_last_command(BtFakeTr *t, BtLastCommand *out) {
    if (t->nout <= 0) return 0;
    BtHciPacket p;
    if (bt_hci_decode(t->out[t->nout - 1], t->outlen[t->nout - 1], &p) != BT_OK) return 0;
    if (p.kind != BT_PKT_COMMAND) return 0;
    out->opcode = p.opcode;
    out->params = p.data;
    out->len = p.len;
    return 1;
}

/* Two L2CAP stacks wired together through a queue, so a round trip is
 * explicit instead of re-entrant. */
enum { BT_WIRE_MAX = 8, BT_WIRE_BUF = 4096 };

typedef struct {
    uint8_t buf[BT_WIRE_MAX][BT_WIRE_BUF];
    size_t len[BT_WIRE_MAX];
    uint8_t flags[BT_WIRE_MAX];
    int n;
} BtWireQ;

typedef struct {
    BtL2cap *a, *b;
    BtWireQ to_a, to_b;
    uint16_t handle;
    int opened_a, opened_b, closed_a, closed_b;
    int cid_a, cid_b;
} BtWire;

static inline void bt_wire_push(BtWireQ *q, uint8_t flags, const uint8_t *d, size_t n) {
    if (q->n >= BT_WIRE_MAX || n > BT_WIRE_BUF) return;
    memcpy(q->buf[q->n], d, n);
    q->len[q->n] = n;
    q->flags[q->n] = flags;
    q->n++;
}

static inline long bt_wire_send_a(void *ctx, uint16_t handle, uint8_t flags,
                                  const uint8_t *d, size_t n) {
    BtWire *w = ctx;
    (void)handle;
    bt_wire_push(&w->to_b, flags, d, n);
    return (long)n;
}

static inline long bt_wire_send_b(void *ctx, uint16_t handle, uint8_t flags,
                                  const uint8_t *d, size_t n) {
    BtWire *w = ctx;
    (void)handle;
    bt_wire_push(&w->to_a, flags, d, n);
    return (long)n;
}

static inline void bt_wire_on_open_a(void *ctx, BtL2capChan *ch) {
    BtWire *w = ctx;
    w->opened_a++;
    w->cid_a = bt_l2cap_chan_cid(ch);
}

static inline void bt_wire_on_open_b(void *ctx, BtL2capChan *ch) {
    BtWire *w = ctx;
    w->opened_b++;
    w->cid_b = bt_l2cap_chan_cid(ch);
}

static inline void bt_wire_on_close(void *ctx, BtL2capChan *ch) {
    BtWire *w = ctx;
    (void)ch;
    w->closed_a++;
}

/* Deliver one queued packet in each direction. */
static inline void bt_wire_step(BtWire *w) {
    if (w->to_a.n) {
        uint8_t flags = w->to_a.flags[0];
        size_t n = w->to_a.len[0];
        uint8_t tmp[BT_WIRE_BUF];
        memcpy(tmp, w->to_a.buf[0], n);
        w->to_a.n--;
        for (int i = 0; i < w->to_a.n; i++) {
            memcpy(w->to_a.buf[i], w->to_a.buf[i + 1], w->to_a.len[i + 1]);
            w->to_a.len[i] = w->to_a.len[i + 1];
            w->to_a.flags[i] = w->to_a.flags[i + 1];
        }
        bt_l2cap_rx(w->a, w->handle, flags, tmp, n);
    }
    if (w->to_b.n) {
        uint8_t flags = w->to_b.flags[0];
        size_t n = w->to_b.len[0];
        uint8_t tmp[BT_WIRE_BUF];
        memcpy(tmp, w->to_b.buf[0], n);
        w->to_b.n--;
        for (int i = 0; i < w->to_b.n; i++) {
            memcpy(w->to_b.buf[i], w->to_b.buf[i + 1], w->to_b.len[i + 1]);
            w->to_b.len[i] = w->to_b.len[i + 1];
            w->to_b.flags[i] = w->to_b.flags[i + 1];
        }
        bt_l2cap_rx(w->b, w->handle, flags, tmp, n);
    }
}

static inline void bt_wire_run(BtWire *w, int rounds) {
    for (int i = 0; i < rounds; i++) bt_wire_step(w);
}

static inline void bt_wire_init(BtWire *w) {
    memset(w, 0, sizeof *w);
    w->handle = 0x0007;
    w->cid_a = -1;
    w->cid_b = -1;
    w->a = bt_l2cap_new();
    w->b = bt_l2cap_new();
    bt_l2cap_set_acl_sender(w->a, bt_wire_send_a, w);
    bt_l2cap_set_acl_sender(w->b, bt_wire_send_b, w);
    BtAddress peer;
    bt_addr_parse("00:01:02:03:04:05", &peer);
    bt_l2cap_link_up(w->a, w->handle, &peer);
    bt_l2cap_link_up(w->b, w->handle, &peer);
}

static inline void bt_wire_free(BtWire *w) {
    bt_l2cap_free(w->a);
    bt_l2cap_free(w->b);
}

#endif
