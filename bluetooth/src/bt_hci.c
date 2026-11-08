/* HCI engine: packet codec plus the command/event state machine that sits
 * on top of a controller transport.
 *
 * The engine never blocks a guest: bt_hci_pump() drains whatever the
 * controller has and returns. Commands are queued with a pending entry so
 * the caller can wait for the controller's Command Complete or Command
 * Status without blocking the stack. */
#include "bt_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ---- pending command table ---- */
enum { BT_PEND_NONE = 0, BT_PEND_DONE };

typedef struct {
    uint16_t opcode;
    int state;
    int status;
} BtPending;

/* Expected reply kind for an opcode: 1 = Command Complete, 2 = Command
 * Status, 0 = no reply expected. */
static int hci_reply_kind(uint16_t opcode) {
    switch (opcode) {
    case BT_OP_INQUIRY:
    case BT_OP_CREATE_CONNECTION:
    case BT_OP_DISCONNECT:
    case BT_OP_AUTH_REQUESTED:
    case BT_OP_SET_ENCRYPTION:
    case BT_OP_REMOTE_NAME_REQUEST:
        return 2;
    default:
        return 1;
    }
}

struct BtHci {
    BtHciTransport tr;
    BtHciLogFn log;
    void *log_ctx;
    BtHciSink sink;
    int have_sink;
    uint8_t rbuf[BT_HCI_BUF];
    size_t rlen;
    BtPending pending[BT_MAX_PENDING];
    int npending;
};

static void hci_log(const BtHci *h, const char *fmt, ...) {
    if (!h->log) return;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    h->log(h->log_ctx, line);
}

/* ---- packet codec ---- */
/* Every encoded packet starts with the HCI packet type indicator, so a
 * byte stream can carry commands, events and ACL data side by side. */
size_t bt_hci_encode_command(uint16_t opcode, const uint8_t *params, size_t n, uint8_t *out, size_t cap) {
    if (!out || cap < 4 + n || n > 255) return 0;
    out[0] = BT_PKT_COMMAND;
    out[1] = (uint8_t)(opcode >> 8);
    out[2] = (uint8_t)opcode;
    out[3] = (uint8_t)n;
    if (n) memcpy(out + 4, params, n);
    return 4 + n;
}

size_t bt_hci_encode_acl(uint16_t handle, uint8_t flags, const uint8_t *data, size_t n, uint8_t *out, size_t cap) {
    if (!out || cap < 6 + n) return 0;
    uint16_t hf = (uint16_t)((handle & 0x0FFF) | ((uint16_t)(flags & 0x0F) << 12));
    out[0] = BT_PKT_ACL;
    bt_wr16(out + 1, hf);
    bt_wr16(out + 3, (uint16_t)n);
    if (n) memcpy(out + 5, data, n);
    return 6 + n;
}

size_t bt_hci_encode_event(uint8_t code, const uint8_t *params, size_t n, uint8_t *out, size_t cap) {
    if (!out || cap < 3 + n || n > 255) return 0;
    out[0] = BT_PKT_EVENT;
    out[1] = code;
    out[2] = (uint8_t)n;
    if (n) memcpy(out + 3, params, n);
    return 3 + n;
}

int bt_hci_decode(const uint8_t *pkt, size_t len, BtHciPacket *out) {
    if (!pkt || !out || len < 1) return BT_EINVAL;
    memset(out, 0, sizeof *out);
    switch (pkt[0]) {
    case BT_PKT_COMMAND: {
        if (len < 4) return BT_EINVAL;
        out->kind = BT_PKT_COMMAND;
        out->opcode = bt_rd16(pkt + 1);
        out->len = pkt[3];
        if (len < 4 + out->len) return BT_EINVAL;
        out->data = pkt + 4;
        return BT_OK;
    }
    case BT_PKT_ACL: {
        if (len < 6) return BT_EINVAL;
        uint16_t hf = bt_rd16(pkt + 1);
        out->kind = BT_PKT_ACL;
        out->handle = (uint16_t)(hf & 0x0FFF);
        out->flags = (uint8_t)((hf >> 12) & 0x0F);
        out->len = bt_rd16(pkt + 3);
        if (len < 6 + out->len) return BT_EINVAL;
        out->data = pkt + 5;
        return BT_OK;
    }
    case BT_PKT_SCO: {
        if (len < 5) return BT_EINVAL;
        uint16_t hf = bt_rd16(pkt + 1);
        out->kind = BT_PKT_SCO;
        out->handle = (uint16_t)(hf & 0x0FFF);
        out->flags = (uint8_t)((hf >> 12) & 0x0F);
        out->len = pkt[4];
        if (len < 5 + out->len) return BT_EINVAL;
        out->data = pkt + 5;
        return BT_OK;
    }
    case BT_PKT_EVENT: {
        if (len < 3) return BT_EINVAL;
        out->kind = BT_PKT_EVENT;
        out->code = pkt[1];
        out->len = pkt[2];
        if (len < 3 + out->len) return BT_EINVAL;
        out->data = pkt + 3;
        return BT_OK;
    }
    default:
        return BT_EINVAL;
    }
}

/* ---- engine ---- */
BtHci *bt_hci_new(const BtHciTransport *tr, BtHciLogFn log, void *log_ctx) {
    if (!tr || !tr->write || !tr->read) return NULL;
    BtHci *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->tr = *tr;
    h->log = log;
    h->log_ctx = log_ctx;
    return h;
}

void bt_hci_free(BtHci *h) {
    if (!h) return;
    if (h->tr.close) h->tr.close(h->tr.ctx);
    free(h);
}

void bt_hci_set_sink(BtHci *h, const BtHciSink *sink) {
    if (!h) return;
    if (sink) { h->sink = *sink; h->have_sink = 1; }
    else { memset(&h->sink, 0, sizeof h->sink); h->have_sink = 0; }
}

static int pending_add(BtHci *h, uint16_t opcode) {
    /* Reuse the slot of an unanswered command with the same opcode so a
     * controller that never replies cannot leak table entries. */
    for (int i = 0; i < BT_MAX_PENDING; i++) {
        if (h->pending[i].state == BT_PEND_NONE || h->pending[i].opcode == opcode) {
            h->pending[i].opcode = opcode;
            h->pending[i].state = 0;
            h->pending[i].status = BT_ST_SUCCESS;
            return BT_OK;
        }
    }
    if (h->npending >= BT_MAX_PENDING) return BT_ELIMIT;
    for (int i = 0; i < BT_MAX_PENDING; i++) {
        if (h->pending[i].state == BT_PEND_NONE) {
            h->pending[i].opcode = opcode;
            h->pending[i].state = 0;
            h->pending[i].status = BT_ST_SUCCESS;
            h->npending++;
            return BT_OK;
        }
    }
    return BT_ELIMIT;
}

static void pending_complete(BtHci *h, uint16_t opcode, int status) {
    for (int i = 0; i < BT_MAX_PENDING; i++) {
        BtPending *p = &h->pending[i];
        if (p->state != BT_PEND_NONE || p->opcode != opcode) continue;
        p->state = BT_PEND_DONE;
        p->status = status;
        h->npending--;
        return;
    }
}

static int pending_take(BtHci *h, uint16_t opcode) {
    for (int i = 0; i < BT_MAX_PENDING; i++) {
        BtPending *p = &h->pending[i];
        if (p->state != BT_PEND_DONE || p->opcode != opcode) continue;
        int st = p->status;
        p->state = BT_PEND_NONE;
        if (h->npending > 0) h->npending--;
        return st;
    }
    return -1;
}

/* Forget an outstanding command without a reply. */
static void pending_drop(BtHci *h, uint16_t opcode) {
    for (int i = 0; i < BT_MAX_PENDING; i++) {
        BtPending *p = &h->pending[i];
        if (p->state == BT_PEND_NONE || p->opcode != opcode) continue;
        p->state = BT_PEND_NONE;
        if (h->npending > 0) h->npending--;
        return;
    }
}

/* Write one command and register the reply we expect. */
static int hci_send(BtHci *h, uint16_t opcode, const uint8_t *params, size_t n) {
    uint8_t pkt[BT_HCI_BUF];
    if (hci_reply_kind(opcode)) {
        int rc = pending_add(h, opcode);
        if (rc != BT_OK) return rc;
    }
    size_t k = bt_hci_encode_command(opcode, params, n, pkt, sizeof pkt);
    if (!k) {
        hci_log(h, "command %04x too large", opcode);
        return BT_EINVAL;
    }
    long w = h->tr.write(h->tr.ctx, pkt, k);
    if (w < 0) {
        hci_log(h, "command %04x write failed", opcode);
        return BT_EIO;
    }
    return BT_OK;
}

/* Total length of the packet that starts at p, 0 when the header is not
 * complete yet and (size_t)-1 when the type byte is not valid. */
static size_t expected_len(const uint8_t *p, size_t n) {
    if (!n) return 0;
    switch (p[0]) {
    case BT_PKT_COMMAND: return n >= 4 ? (size_t)(4 + p[3]) : 0;
    case BT_PKT_ACL:    return n >= 6 ? (size_t)(6 + bt_rd16(p + 3)) : 0;
    case BT_PKT_SCO:    return n >= 5 ? (size_t)(5 + p[4]) : 0;
    case BT_PKT_EVENT:  return n >= 3 ? (size_t)(3 + p[2]) : 0;
    default:            return (size_t)-1;
    }
}

/* Pull one complete packet out of the transport. */
static int hci_read_packet(BtHci *h, uint8_t *out, size_t cap, size_t *out_len) {
    for (;;) {
        size_t need = expected_len(h->rbuf, h->rlen);
        if (need == (size_t)-1) {
            h->rlen = 0; /* drop the corrupt packet */
            return BT_EINVAL;
        }
        if (need && h->rlen >= need) {
            memcpy(out, h->rbuf, need);
            h->rlen -= need;
            if (h->rlen) memmove(h->rbuf, h->rbuf + need, h->rlen);
            *out_len = need;
            return BT_OK;
        }
        if (h->rlen >= cap) { h->rlen = 0; return BT_EINVAL; }
        long k = h->tr.read(h->tr.ctx, h->rbuf + h->rlen, cap - h->rlen);
        if (k < 0) return BT_EIO;
        if (k == 0) return BT_EAGAIN;
        h->rlen += (size_t)k;
        if (h->rlen > cap) { h->rlen = 0; return BT_EINVAL; }
    }
}

static void hci_dispatch(BtHci *h, const uint8_t *pkt, size_t len) {
    BtHciPacket p;
    if (bt_hci_decode(pkt, len, &p) != BT_OK) {
        hci_log(h, "malformed hci packet");
        return;
    }
    if (p.kind == BT_PKT_ACL) {
        if (h->have_sink && h->sink.on_acl)
            h->sink.on_acl(h->sink.ctx, p.handle, p.flags, p.data, p.len);
        return;
    }
    if (p.kind != BT_PKT_EVENT) {
        hci_log(h, "unexpected hci packet type %d", p.kind);
        return;
    }
    if (p.code == BT_EV_COMMAND_COMPLETE && p.len >= 4)
        pending_complete(h, bt_rd16(p.data + 1), p.data[3]);
    else if (p.code == BT_EV_COMMAND_STATUS && p.len >= 4)
        pending_complete(h, bt_rd16(p.data + 2), p.data[0]);
    if (h->have_sink && h->sink.on_event) h->sink.on_event(h->sink.ctx, p.code, p.data, p.len);
}

int bt_hci_pump(BtHci *h, int timeout_ms) {
    if (!h) return BT_EINVAL;
    int count = 0;
    uint64_t deadline = bt_now_ms() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
    for (;;) {
        uint8_t pkt[BT_HCI_BUF];
        size_t len = 0;
        int rc = hci_read_packet(h, pkt, sizeof pkt, &len);
        if (rc == BT_EAGAIN) {
            if (count > 0) break;
            if (timeout_ms <= 0) break;
            if (bt_now_ms() >= deadline) break;
            struct timespec ts = {0, 200000L};
            nanosleep(&ts, NULL);
            continue;
        }
        if (rc == BT_EINVAL) {
            hci_log(h, "dropped malformed hci packet");
            continue;
        }
        if (rc != BT_OK) return rc;
        hci_dispatch(h, pkt, len);
        count++;
        if (timeout_ms <= 0) continue;
        if (bt_now_ms() >= deadline) break;
    }
    return count;
}

/* Wait for the controller's reply to one opcode. Returns the HCI status. */
int bt_hci_wait(BtHci *h, uint16_t opcode, int timeout_ms) {
    if (!h) return BT_EINVAL;
    uint64_t deadline = bt_now_ms() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
    for (;;) {
        int st = pending_take(h, opcode);
        if (st >= 0) return st;
        int n = bt_hci_pump(h, timeout_ms > 0 ? 5 : 0);
        if (n < 0) return n;
        if (timeout_ms <= 0) break;
        if (bt_now_ms() >= deadline) break;
    }
    pending_drop(h, opcode);
    return BT_ETIMEOUT;
}

/* ---- commands ---- */
int bt_hci_reset(BtHci *h) {
    if (!h) return BT_EINVAL;
    return hci_send(h, BT_OP_RESET, NULL, 0);
}

int bt_hci_read_bd_addr(BtHci *h) {
    if (!h) return BT_EINVAL;
    return hci_send(h, BT_OP_READ_BD_ADDR, NULL, 0);
}

int bt_hci_write_local_name(BtHci *h, const char *name) {
    if (!h || !name) return BT_EINVAL;
    uint8_t p[248];
    memset(p, 0, sizeof p);
    bt_strlcpy((char *)p, name, sizeof p);
    return hci_send(h, BT_OP_WRITE_LOCAL_NAME, p, sizeof p);
}

int bt_hci_set_scan_enable(BtHci *h, int on) {
    if (!h) return BT_EINVAL;
    uint8_t p[2] = {0x01, on ? 0x01 : 0x00};
    return hci_send(h, BT_OP_WRITE_SCAN_ENABLE, p, sizeof p);
}

int bt_hci_inquiry(BtHci *h, uint8_t length_units, int8_t timeout, uint32_t class_mask) {
    if (!h) return BT_EINVAL;
    uint8_t p[8];
    p[0] = 0x9E; p[1] = 0x8B; p[2] = 0x33; /* general inquiry LAP */
    p[3] = length_units ? length_units : 8;
    p[4] = 0;                                     /* unlimited responses */
    p[5] = (uint8_t)timeout;
    bt_wr16(p + 6, (uint16_t)class_mask);
    return hci_send(h, BT_OP_INQUIRY, p, sizeof p);
}

int bt_hci_cancel_inquiry(BtHci *h) {
    if (!h) return BT_EINVAL;
    return hci_send(h, BT_OP_INQUIRY_CANCEL, NULL, 0);
}

int bt_hci_create_connection(BtHci *h, const BtAddress *peer, uint16_t packet_type) {
    if (!h || !peer) return BT_EINVAL;
    uint8_t p[14];
    memcpy(p, peer->b, 6);
    bt_wr16(p + 6, packet_type ? packet_type : 0xCC18); /* DM1/DH1/DM3/DH3/DM5/DH5 */
    p[8] = 0x01;                        /* page scan repetition mode R1 */
    p[9] = 0x00;                        /* reserved */
    bt_wr16(p + 10, 0x0000);            /* clock offset */
    p[12] = 0x01;                       /* allow role switch */
    p[13] = 0x00;                       /* reserved */
    return hci_send(h, BT_OP_CREATE_CONNECTION, p, sizeof p);
}

int bt_hci_accept_connection(BtHci *h, const BtAddress *peer, uint8_t role) {
    if (!h || !peer) return BT_EINVAL;
    uint8_t p[7];
    memcpy(p, peer->b, 6);
    p[6] = role ? 0x01 : 0x00;          /* 0 = master, 1 = slave */
    return hci_send(h, BT_OP_ACCEPT_CONNECTION, p, sizeof p);
}

int bt_hci_disconnect(BtHci *h, uint16_t handle, uint8_t reason) {
    if (!h) return BT_EINVAL;
    uint8_t p[3];
    bt_wr16(p, handle);
    p[2] = reason;
    return hci_send(h, BT_OP_DISCONNECT, p, sizeof p);
}

int bt_hci_pin_reply(BtHci *h, uint8_t pin_len, const uint8_t *pin) {
    if (!h || (pin_len && !pin) || pin_len > 16) return BT_EINVAL;
    uint8_t p[17];
    memset(p, 0, sizeof p);
    p[0] = pin_len;
    if (pin_len) memcpy(p + 1, pin, pin_len);
    return hci_send(h, BT_OP_PIN_REPLY, p, sizeof p);
}

int bt_hci_auth_requested(BtHci *h, uint16_t handle) {
    if (!h) return BT_EINVAL;
    uint8_t p[2];
    bt_wr16(p, handle);
    return hci_send(h, BT_OP_AUTH_REQUESTED, p, sizeof p);
}

int bt_hci_set_encryption(BtHci *h, uint16_t handle) {
    if (!h) return BT_EINVAL;
    uint8_t p[3];
    bt_wr16(p, handle);
    p[2] = 0x01; /* encryption on */
    return hci_send(h, BT_OP_SET_ENCRYPTION, p, sizeof p);
}

int bt_hci_remote_name_request(BtHci *h, const BtAddress *peer, uint16_t timeout) {
    if (!h || !peer) return BT_EINVAL;
    uint8_t p[12];
    memcpy(p, peer->b, 6);
    p[6] = 0x01;             /* page scan repetition mode R1 */
    p[7] = 0x00;             /* reserved */
    bt_wr16(p + 8, 0x0000);  /* clock offset */
    bt_wr16(p + 10, timeout);
    return hci_send(h, BT_OP_REMOTE_NAME_REQUEST, p, sizeof p);
}

int bt_hci_write_link_key(BtHci *h, const BtAddress *peer, const uint8_t key[16]) {
    if (!h || !peer || !key) return BT_EINVAL;
    uint8_t p[23];
    memcpy(p, peer->b, 6);
    memcpy(p + 6, key, 16);
    p[22] = 0x04; /* combination key from PIN */
    return hci_send(h, BT_OP_WRITE_LINK_KEY, p, sizeof p);
}

int bt_hci_read_link_key(BtHci *h, const BtAddress *peer) {
    if (!h || !peer) return BT_EINVAL;
    uint8_t p[6];
    memcpy(p, peer->b, 6);
    return hci_send(h, BT_OP_LINK_KEY_NEG_REPLY, p, sizeof p);
}

int bt_hci_send_acl(BtHci *h, uint16_t handle, const uint8_t *data, size_t len) {
    return bt_hci_send_acl_flags(h, handle, BT_ACL_PB_FIRST, data, len);
}

int bt_hci_send_acl_flags(BtHci *h, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    if (!h || (!data && len)) return BT_EINVAL;
    uint8_t pkt[BT_HCI_BUF];
    size_t k = bt_hci_encode_acl(handle, flags, data, len, pkt, sizeof pkt);
    if (!k) return BT_ELIMIT;
    long w = h->tr.write(h->tr.ctx, pkt, k);
    return w < 0 ? BT_EIO : BT_OK;
}
