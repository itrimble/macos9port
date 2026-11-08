/* Controller simulator.
 *
 * It answers HCI commands the way a real controller does and carries the
 * ACL link to a peer stack, so the whole L2CAP, RFCOMM and BNEP path can
 * be exercised headlessly with no radio.
 *
 * The peer stack is supplied by the caller: the simulator only owns the
 * link, so the same code under test runs on both sides. */
#include "bt_internal.h"

#include <stdlib.h>

#include "bt_hci_sim.h"

enum { BT_SIM_DEVS = 8, BT_SIM_LINKS = 8, BT_SIM_QUEUE = 64 };

typedef struct {
    uint8_t *p;
    size_t len;
} SimPacket;

typedef struct {
    int used;
    BtAddress addr;
    char name[32];
    uint32_t cod;
    int needs_pin;
    int paired;
} SimDev;

typedef struct {
    int used;
    uint16_t handle;
    BtAddress peer;
    int needs_pin;
    int encrypted;
} SimLink;

struct BtSim {
    SimPacket queue[BT_SIM_QUEUE];
    int nqueue;
    SimDev devs[BT_SIM_DEVS];
    int ndevs;
    SimLink links[BT_SIM_LINKS];
    int nlinks;
    uint16_t next_handle;
    BtAddress local;
    BtL2cap *peer_l2cap;
    BtHciTransport transport;
};

static void sim_push(BtSim *s, const uint8_t *p, size_t n) {
    if (s->nqueue >= BT_SIM_QUEUE) return;
    SimPacket *q = &s->queue[s->nqueue++];
    q->p = malloc(n);
    if (!q->p) { s->nqueue--; return; }
    memcpy(q->p, p, n);
    q->len = n;
}

static void sim_push_event(BtSim *s, uint8_t code, const uint8_t *params, size_t n) {
    uint8_t pkt[BT_HCI_BUF];
    size_t k = bt_hci_encode_event(code, params, n, pkt, sizeof pkt);
    if (!k) return;
    sim_push(s, pkt, k);
}

static void sim_command_complete(BtSim *s, uint16_t opcode, uint8_t status,
                                 const uint8_t *ret, size_t ret_len) {
    uint8_t p[260];
    size_t n = 0;
    p[n++] = 0x01; /* num_hci_command_packets */
    bt_wr16(p + n, opcode);
    n += 2;
    p[n++] = status;
    if (ret_len && ret) {
        if (n + ret_len > sizeof p) ret_len = sizeof p - n;
        memcpy(p + n, ret, ret_len);
        n += ret_len;
    }
    sim_push_event(s, BT_EV_COMMAND_COMPLETE, p, n);
}

static void sim_command_status(BtSim *s, uint16_t opcode, uint8_t status) {
    uint8_t p[4];
    p[0] = status;
    p[1] = 0x01;
    bt_wr16(p + 2, opcode);
    sim_push_event(s, BT_EV_COMMAND_STATUS, p, sizeof p);
}

static SimDev *sim_find_dev(BtSim *s, const BtAddress *addr) {
    for (int i = 0; i < s->ndevs; i++)
        if (bt_addr_cmp(&s->devs[i].addr, addr) == 0) return &s->devs[i];
    return NULL;
}

static SimLink *sim_find_link(BtSim *s, uint16_t handle) {
    for (int i = 0; i < BT_SIM_LINKS; i++)
        if (s->links[i].used && s->links[i].handle == handle) return &s->links[i];
    return NULL;
}

static void sim_connection_complete(BtSim *s, uint16_t handle, const BtAddress *peer) {
    uint8_t p[11];
    p[0] = BT_ST_SUCCESS;
    bt_wr16(p + 1, handle);
    memcpy(p + 3, peer->b, 6);
    p[9] = 0x00;  /* link type: SCO */
    p[10] = 0x00; /* encryption off */
    sim_push_event(s, BT_EV_CONNECTION_COMPLETE, p, sizeof p);
}

static void sim_do_inquiry(BtSim *s) {
    /* One inquiry result event carrying every known device. */
    int n = 0;
    uint8_t p[1 + 6 * BT_SIM_DEVS + 3 * BT_SIM_DEVS + 2 * BT_SIM_DEVS];
    size_t off = 0;
    p[off++] = 0x00; /* num responses, filled in below */
    for (int i = 0; i < s->ndevs && n < BT_SIM_DEVS; i++) {
        memcpy(p + off, s->devs[i].addr.b, 6);
        off += 6;
        n++;
    }
    for (int i = 0; i < n; i++) {
        p[off++] = (uint8_t)(s->devs[i].cod >> 16);
        p[off++] = (uint8_t)(s->devs[i].cod >> 8);
        p[off++] = (uint8_t)s->devs[i].cod;
    }
    for (int i = 0; i < n; i++) {
        bt_wr16(p + off, 0x0000);
        off += 2;
    }
    p[0] = (uint8_t)n;
    if (n) sim_push_event(s, BT_EV_INQUIRY_RESULT, p, off);
    uint8_t done[1] = {BT_ST_SUCCESS};
    sim_push_event(s, BT_EV_INQUIRY_COMPLETE, done, sizeof done);
}

static void sim_command(BtSim *s, uint16_t opcode, const uint8_t *p, size_t n) {
    switch (opcode) {
    case BT_OP_RESET:
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        break;
    case BT_OP_READ_BD_ADDR:
        sim_command_complete(s, opcode, BT_ST_SUCCESS, s->local.b, 6);
        break;
    case BT_OP_WRITE_LOCAL_NAME:
    case BT_OP_WRITE_SCAN_ENABLE:
    case BT_OP_WRITE_CLASS_OF_DEVICE:
    case BT_OP_SET_EVENT_MASK:
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        break;
    case BT_OP_INQUIRY:
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        sim_do_inquiry(s);
        break;
    case BT_OP_INQUIRY_CANCEL: {
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        uint8_t done[1] = {BT_ST_SUCCESS};
        sim_push_event(s, BT_EV_INQUIRY_COMPLETE, done, sizeof done);
        break;
    }
    case BT_OP_CREATE_CONNECTION: {
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        if (n < 6) break;
        BtAddress peer;
        memcpy(peer.b, p, 6);
        SimDev *dev = sim_find_dev(s, &peer);
        if (!dev) {
            uint8_t fail[11];
            fail[0] = BT_ST_CONNECTION_TIMEOUT;
            bt_wr16(fail + 1, 0);
            memcpy(fail + 3, peer.b, 6);
            fail[9] = 0;
            fail[10] = 0;
            sim_push_event(s, BT_EV_CONNECTION_COMPLETE, fail, sizeof fail);
            break;
        }
        uint16_t handle = s->next_handle++;
        if (s->nlinks < BT_SIM_LINKS) {
            SimLink *ln = &s->links[s->nlinks++];
            ln->used = 1;
            ln->handle = handle;
            ln->peer = peer;
            ln->needs_pin = dev->needs_pin;
        }
        sim_connection_complete(s, handle, &peer);
        if (s->peer_l2cap) bt_l2cap_link_up(s->peer_l2cap, handle, &peer);
        break;
    }
    case BT_OP_ACCEPT_CONNECTION: {
        if (n < 6) { sim_command_complete(s, opcode, BT_ST_INVALID_PARAMS, NULL, 0); break; }
        BtAddress peer;
        memcpy(peer.b, p, 6);
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        uint16_t handle = s->next_handle++;
        if (s->nlinks < BT_SIM_LINKS) {
            SimLink *ln = &s->links[s->nlinks++];
            ln->used = 1;
            ln->handle = handle;
            ln->peer = peer;
        }
        sim_connection_complete(s, handle, &peer);
        if (s->peer_l2cap) bt_l2cap_link_up(s->peer_l2cap, handle, &peer);
        break;
    }
    case BT_OP_DISCONNECT: {
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        if (n < 2) break;
        uint16_t handle = bt_rd16(p);
        SimLink *ln = sim_find_link(s, handle);
        uint8_t ev[4];
        ev[0] = BT_ST_SUCCESS;
        bt_wr16(ev + 1, handle);
        ev[3] = 0x13; /* remote user terminated */
        sim_push_event(s, BT_EV_DISCONNECTION_COMPLETE, ev, sizeof ev);
        if (ln) {
            if (s->peer_l2cap) bt_l2cap_link_down(s->peer_l2cap, handle);
            ln->used = 0;
        }
        break;
    }
    case BT_OP_AUTH_REQUESTED: {
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        if (n < 2) break;
        uint16_t handle = bt_rd16(p);
        SimLink *ln = sim_find_link(s, handle);
        if (!ln) break;
        if (ln->needs_pin) {
            uint8_t ev[6];
            memcpy(ev, ln->peer.b, 6);
            sim_push_event(s, BT_EV_PIN_CODE_REQUEST, ev, sizeof ev);
            break;
        }
        uint8_t ev[3];
        ev[0] = BT_ST_SUCCESS;
        bt_wr16(ev + 1, handle);
        sim_push_event(s, BT_EV_AUTHENTICATION_COMPLETE, ev, sizeof ev);
        break;
    }
    case BT_OP_PIN_REPLY: {
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        for (int i = 0; i < s->nlinks; i++) {
            SimLink *ln = &s->links[i];
            if (!ln->used) continue;
            uint8_t ev[3];
            ev[0] = BT_ST_SUCCESS;
            bt_wr16(ev + 1, ln->handle);
            sim_push_event(s, BT_EV_AUTHENTICATION_COMPLETE, ev, sizeof ev);
            break;
        }
        break;
    }
    case BT_OP_SET_ENCRYPTION: {
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        if (n < 2) break;
        uint16_t handle = bt_rd16(p);
        SimLink *ln = sim_find_link(s, handle);
        if (ln) ln->encrypted = 1;
        uint8_t ev[4];
        ev[0] = BT_ST_SUCCESS;
        bt_wr16(ev + 1, handle);
        ev[3] = 0x01; /* encryption enabled */
        sim_push_event(s, BT_EV_ENCRYPTION_CHANGE, ev, sizeof ev);
        break;
    }
    case BT_OP_REMOTE_NAME_REQUEST: {
        sim_command_status(s, opcode, BT_ST_SUCCESS);
        if (n < 6) break;
        BtAddress peer;
        memcpy(peer.b, p, 6);
        SimDev *dev = sim_find_dev(s, &peer);
        uint8_t ev[255];
        memset(ev, 0, sizeof ev);
        ev[0] = BT_ST_SUCCESS;
        memcpy(ev + 1, peer.b, 6);
        size_t name_len = dev ? strlen(dev->name) : 0;
        ev[7] = (uint8_t)name_len;
        if (name_len) memcpy(ev + 8, dev->name, name_len);
        sim_push_event(s, BT_EV_REMOTE_NAME_COMPLETE, ev, 8 + name_len);
        break;
    }
    case BT_OP_WRITE_LINK_KEY:
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        break;
    case BT_OP_LINK_KEY_NEG_REPLY:
        sim_command_complete(s, opcode, BT_ST_SUCCESS, NULL, 0);
        break;
    default:
        sim_command_complete(s, opcode, BT_ST_UNKNOWN_COMMAND, NULL, 0);
        break;
    }
}

/* ---- transport ---- */
static long sim_write(void *ctx, const uint8_t *pkt, size_t len) {
    BtSim *s = ctx;
    BtHciPacket p;
    if (bt_hci_decode(pkt, len, &p) != BT_OK) return -1;
    if (p.kind == BT_PKT_COMMAND) {
        sim_command(s, p.opcode, p.data, p.len);
        return (long)len;
    }
    if (p.kind == BT_PKT_ACL) {
        /* The controller carries the payload to the peer stack. */
        if (s->peer_l2cap) bt_l2cap_rx(s->peer_l2cap, p.handle, p.flags, p.data, p.len);
        return (long)len;
    }
    return (long)len;
}

static long sim_read(void *ctx, uint8_t *buf, size_t cap) {
    BtSim *s = ctx;
    if (s->nqueue <= 0) return 0;
    SimPacket *q = &s->queue[0];
    if (q->len > cap) return -1;
    memcpy(buf, q->p, q->len);
    size_t n = q->len;
    free(q->p);
    s->nqueue--;
    for (int i = 0; i < s->nqueue; i++) s->queue[i] = s->queue[i + 1];
    return (long)n;
}

static void sim_close(void *ctx) {
    (void)ctx;
}

/* ---- public ---- */
BtSim *bt_sim_new(void) {
    BtSim *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    static const uint8_t local[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(s->local.b, local, 6);
    s->next_handle = 0x0001;
    s->transport.write = sim_write;
    s->transport.read = sim_read;
    s->transport.close = sim_close;
    s->transport.ctx = s;
    return s;
}

void bt_sim_free(BtSim *s) {
    if (!s) return;
    for (int i = 0; i < s->nqueue; i++) free(s->queue[i].p);
    free(s);
}

const BtHciTransport *bt_sim_transport(BtSim *s) {
    return s ? &s->transport : NULL;
}

void bt_sim_set_peer_l2cap(BtSim *s, BtL2cap *peer) {
    if (!s) return;
    s->peer_l2cap = peer;
}

int bt_sim_add_device(BtSim *s, const BtAddress *addr, const char *name, uint32_t cod,
                      int needs_pin) {
    if (!s || !addr) return BT_EINVAL;
    if (s->ndevs >= BT_SIM_DEVS) return BT_ELIMIT;
    SimDev *d = &s->devs[s->ndevs++];
    memset(d, 0, sizeof *d);
    d->addr = *addr;
    bt_strlcpy(d->name, name ? name : "", sizeof d->name);
    d->cod = cod;
    d->needs_pin = needs_pin;
    return BT_OK;
}

const BtAddress *bt_sim_local_addr(BtSim *s) {
    return s ? &s->local : NULL;
}

int bt_sim_link_count(BtSim *s) {
    if (!s) return 0;
    int n = 0;
    for (int i = 0; i < s->nlinks; i++)
        if (s->links[i].used) n++;
    return n;
}

/* Peer side ACL sender: wrap the payload in an HCI ACL packet and hand
 * it to the host. */
long bt_sim_send_acl(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    BtSim *s = ctx;
    uint8_t pkt[BT_HCI_BUF];
    size_t k = bt_hci_encode_acl(handle, flags, data, len, pkt, sizeof pkt);
    if (!k) return -1;
    sim_push(s, pkt, k);
    return (long)len;
}
