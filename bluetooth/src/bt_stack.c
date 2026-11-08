/* Top level stack: owns the controller engine, the device database, the
 * L2CAP layer and the pairing machine, and routes between them. */
#include "bt_internal.h"

#include <stdlib.h>

enum { BT_STACK_LINKS = 8 };

typedef struct {
    int used;
    uint16_t handle;
    BtAddress peer;
} BtStackLink;

struct BtStack {
    BtHci *hci;
    BtL2cap *l2cap;
    BtPair *pair;
    BtDeviceDb *db;
    BtStackLink links[BT_STACK_LINKS];
    BtHciLogFn log;
    void *log_ctx;
};

static void stack_on_event(void *ctx, uint8_t code, const uint8_t *params, size_t len) {
    BtStack *s = ctx;
    switch (code) {
    case BT_EV_INQUIRY_RESULT:
    case BT_EV_INQUIRY_RESULT_RSSI: {
        if (len < 1) break;
        int n = params[0];
        /* The controller sends one array per field: addresses, then
         * class of device, then clock offset, then (with RSSI) the
         * signal strength. */
        size_t need = (size_t)n * 11;
        if (len < 1 + need) break;
        const uint8_t *addrs = params + 1;
        const uint8_t *cods = addrs + (size_t)n * 6;
        const uint8_t *rssi = cods + (size_t)n * 3 + (size_t)n * 2;
        int has_rssi = code == BT_EV_INQUIRY_RESULT_RSSI && len >= 1 + (size_t)n * 12;
        for (int i = 0; i < n; i++) {
            BtDeviceInfo info;
            memset(&info, 0, sizeof info);
            memcpy(info.addr.b, addrs + (size_t)i * 6, 6);
            info.class_of_device = ((uint32_t)cods[i * 3] << 16) | ((uint32_t)cods[i * 3 + 1] << 8) |
                                   cods[i * 3 + 2];
            if (has_rssi) {
                int r = (int)rssi[i];
                info.rssi = r > 127 ? r - 256 : r;
            }
            info.last_seen_ms = bt_now_ms();
            bt_db_upsert(s->db, &info);
        }
        break;
    }
    case BT_EV_REMOTE_NAME_COMPLETE: {
        if (len < 8) break;
        BtDeviceInfo info;
        memset(&info, 0, sizeof info);
        memcpy(info.addr.b, params + 1, 6);
        size_t name_len = params[7];
        if (name_len > sizeof info.name - 1) name_len = sizeof info.name - 1;
        memcpy(info.name, params + 8, name_len);
        info.name[name_len] = 0;
        info.last_seen_ms = bt_now_ms();
        bt_db_upsert(s->db, &info);
        break;
    }
    case BT_EV_CONNECTION_COMPLETE: {
        if (len < 9) break;
        if (params[0] != BT_ST_SUCCESS) break;
        uint16_t handle = bt_rd16(params + 1);
        BtAddress peer;
        memcpy(peer.b, params + 3, 6);
        for (int i = 0; i < BT_STACK_LINKS; i++) {
            if (s->links[i].used && s->links[i].handle == handle) continue;
            if (s->links[i].used) continue;
            s->links[i].used = 1;
            s->links[i].handle = handle;
            s->links[i].peer = peer;
            break;
        }
        BtDeviceInfo info;
        memset(&info, 0, sizeof info);
        info.addr = peer;
        info.connected = 1;
        info.last_seen_ms = bt_now_ms();
        bt_db_upsert(s->db, &info);
        bt_l2cap_link_up(s->l2cap, handle, &peer);
        break;
    }
    case BT_EV_DISCONNECTION_COMPLETE: {
        if (len < 3) break;
        uint16_t handle = bt_rd16(params + 1);
        for (int i = 0; i < BT_STACK_LINKS; i++) {
            if (!s->links[i].used || s->links[i].handle != handle) continue;
            BtDeviceInfo info;
            memset(&info, 0, sizeof info);
            info.addr = s->links[i].peer;
            info.last_seen_ms = bt_now_ms();
            bt_db_upsert(s->db, &info);
            s->links[i].used = 0;
        }
        bt_l2cap_link_down(s->l2cap, handle);
        break;
    }
    default:
        break;
    }
    bt_pair_on_event(s->pair, code, params, len);
}

static void stack_on_acl(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    BtStack *s = ctx;
    bt_l2cap_rx(s->l2cap, handle, flags, data, len);
}

static long stack_acl_send(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    BtHci *hci = ctx;
    if (bt_hci_send_acl_flags(hci, handle, flags, data, len) != BT_OK) return -1;
    return (long)len;
}

BtStack *bt_stack_new(const BtHciTransport *tr, BtHciLogFn log, void *log_ctx) {
    if (!tr) return NULL;
    BtStack *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->log = log;
    s->log_ctx = log_ctx;
    s->db = bt_db_new();
    s->l2cap = bt_l2cap_new();
    s->hci = bt_hci_new(tr, log, log_ctx);
    if (!s->db || !s->l2cap || !s->hci) {
        bt_stack_free(s);
        return NULL;
    }
    s->pair = bt_pair_new(s->hci);
    if (!s->pair) {
        bt_stack_free(s);
        return NULL;
    }
    BtHciSink sink;
    sink.on_event = stack_on_event;
    sink.on_acl = stack_on_acl;
    sink.ctx = s;
    bt_hci_set_sink(s->hci, &sink);
    bt_l2cap_set_acl_sender(s->l2cap, stack_acl_send, s->hci);
    return s;
}

void bt_stack_free(BtStack *s) {
    if (!s) return;
    bt_pair_free(s->pair);
    bt_hci_free(s->hci);
    bt_l2cap_free(s->l2cap);
    bt_db_free(s->db);
    free(s);
}

BtDeviceDb *bt_stack_devices(BtStack *s) { return s ? s->db : NULL; }
BtHci *bt_stack_hci(BtStack *s) { return s ? s->hci : NULL; }
BtL2cap *bt_stack_l2cap(BtStack *s) { return s ? s->l2cap : NULL; }
BtPair *bt_stack_pair(BtStack *s) { return s ? s->pair : NULL; }

int bt_stack_discover(BtStack *s, uint8_t length_units, int8_t timeout) {
    if (!s) return BT_EINVAL;
    int rc = bt_hci_set_scan_enable(s->hci, 1);
    if (rc != BT_OK) return rc;
    return bt_hci_inquiry(s->hci, length_units, timeout, 0xFFFFFFFFu);
}

int bt_stack_connect(BtStack *s, const BtAddress *peer, uint16_t packet_type) {
    if (!s || !peer) return BT_EINVAL;
    return bt_hci_create_connection(s->hci, peer, packet_type);
}

int bt_stack_disconnect(BtStack *s, uint16_t handle) {
    if (!s) return BT_EINVAL;
    return bt_hci_disconnect(s->hci, handle, 0x13);
}

int bt_stack_handle_for(BtStack *s, const BtAddress *peer, uint16_t *out) {
    if (!s || !peer || !out) return BT_EINVAL;
    for (int i = 0; i < BT_STACK_LINKS; i++)
        if (s->links[i].used && bt_addr_cmp(&s->links[i].peer, peer) == 0) {
            *out = s->links[i].handle;
            return BT_OK;
        }
    return BT_ENOENT;
}

const BtDeviceInfo *bt_stack_device(BtStack *s, const BtAddress *addr) {
    if (!s || !addr) return NULL;
    return bt_db_find(s->db, addr);
}

int bt_stack_pump(BtStack *s, int timeout_ms) {
    if (!s) return BT_EINVAL;
    return bt_hci_pump(s->hci, timeout_ms);
}
