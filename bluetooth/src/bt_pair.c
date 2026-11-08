/* Pairing state machine: PIN entry, authentication, encryption and
 * bonding.
 *
 * The controller drives the sequence. A link key request is answered
 * from the stored keys, a PIN request waits for the user, and a
 * successful encryption change writes the link key so the next
 * connection is silent. */
#include "bt_internal.h"

#include <stdlib.h>

enum { BT_PAIR_MAX = 8 };

typedef struct {
    int used;
    BtAddress peer;
    uint16_t handle;
    int state;
    uint8_t pin[16];
    int pin_len;
    uint8_t link_key[16];
    int have_key;
    int error;
} BtPairEntry;

struct BtPair {
    BtHci *hci;
    BtPairEntry entries[BT_PAIR_MAX];
};

BtPair *bt_pair_new(BtHci *hci) {
    if (!hci) return NULL;
    BtPair *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->hci = hci;
    return p;
}

void bt_pair_free(BtPair *p) {
    free(p);
}

static BtPairEntry *entry_find(BtPair *p, const BtAddress *peer) {
    for (int i = 0; i < BT_PAIR_MAX; i++)
        if (p->entries[i].used && bt_addr_cmp(&p->entries[i].peer, peer) == 0)
            return &p->entries[i];
    return NULL;
}

static BtPairEntry *entry_alloc(BtPair *p, const BtAddress *peer, uint16_t handle) {
    BtPairEntry *e = entry_find(p, peer);
    if (e) {
        /* A handle of zero means "keep the one we have". */
        if (handle) e->handle = handle;
        return e;
    }
    for (int i = 0; i < BT_PAIR_MAX; i++) {
        if (p->entries[i].used) continue;
        memset(&p->entries[i], 0, sizeof p->entries[i]);
        p->entries[i].used = 1;
        p->entries[i].peer = *peer;
        p->entries[i].handle = handle;
        p->entries[i].state = BT_PAIR_IDLE;
        return &p->entries[i];
    }
    return NULL;
}

int bt_pair_state(const BtPair *p, const BtAddress *peer, BtPairState *out) {
    if (!p || !peer || !out) return BT_EINVAL;
    BtPairEntry *e = entry_find((BtPair *)p, peer);
    memset(out, 0, sizeof *out);
    out->peer = *peer;
    if (!e) {
        out->state = BT_PAIR_IDLE;
        return BT_ENOENT;
    }
    out->state = e->state;
    out->pin_len = e->pin_len;
    if (e->pin_len) memcpy(out->pin, e->pin, (size_t)e->pin_len);
    if (e->have_key) memcpy(out->link_key, e->link_key, 16);
    out->authenticated = e->state == BT_PAIR_BONDING || e->state == BT_PAIR_PAIRED;
    out->encrypted = e->state == BT_PAIR_BONDING || e->state == BT_PAIR_PAIRED;
    out->bonded = e->state == BT_PAIR_PAIRED;
    out->error = e->error;
    return BT_OK;
}

int bt_pair_begin(BtPair *p, const BtAddress *peer, uint16_t handle) {
    if (!p || !peer) return BT_EINVAL;
    BtPairEntry *e = entry_alloc(p, peer, handle);
    if (!e) return BT_ELIMIT;
    e->state = BT_PAIR_WAIT_AUTH;
    e->error = BT_OK;
    return bt_hci_auth_requested(p->hci, handle);
}

int bt_pair_set_pin(BtPair *p, const BtAddress *peer, const uint8_t *pin, int len) {
    if (!p || !peer || len < 1 || len > 16) return BT_EINVAL;
    BtPairEntry *e = entry_find(p, peer);
    if (!e) return BT_ENOENT;
    if (e->state != BT_PAIR_WAIT_PIN) return BT_EBUSY;
    memcpy(e->pin, pin, (size_t)len);
    e->pin_len = len;
    return bt_hci_pin_reply(p->hci, (uint8_t)len, pin);
}

int bt_pair_cancel(BtPair *p, const BtAddress *peer) {
    if (!p || !peer) return BT_EINVAL;
    BtPairEntry *e = entry_find(p, peer);
    if (!e) return BT_ENOENT;
    e->state = BT_PAIR_IDLE;
    e->error = BT_ECLOSED;
    return BT_OK;
}

int bt_pair_has_key(const BtPair *p, const BtAddress *peer, uint8_t key[16]) {
    if (!p || !peer || !key) return BT_EINVAL;
    BtPairEntry *e = entry_find((BtPair *)p, peer);
    if (!e || !e->have_key) return BT_ENOENT;
    memcpy(key, e->link_key, 16);
    return BT_OK;
}

/* The controller told us a key exists for this peer. */
static void pair_store_key(BtPair *p, const BtAddress *peer, const uint8_t key[16]) {
    BtPairEntry *e = entry_alloc(p, peer, 0);
    if (!e) return;
    memcpy(e->link_key, key, 16);
    e->have_key = 1;
}

void bt_pair_on_event(BtPair *p, uint8_t code, const uint8_t *params, size_t len) {
    if (!p || !params) return;
    switch (code) {
    case BT_EV_PIN_CODE_REQUEST: {
        if (len < 6) return;
        BtAddress peer;
        memcpy(peer.b, params, 6);
        BtPairEntry *e = entry_alloc(p, &peer, 0);
        if (!e) return;
        e->state = BT_PAIR_WAIT_PIN;
        break;
    }
    case BT_EV_LINK_KEY_REQUEST: {
        if (len < 6) return;
        BtAddress peer;
        memcpy(peer.b, params, 6);
        BtPairEntry *e = entry_find(p, &peer);
        if (e && e->have_key) {
            bt_hci_write_link_key(p->hci, &peer, e->link_key);
        } else {
            bt_hci_read_link_key(p->hci, &peer);
        }
        break;
    }
    case BT_EV_LINK_KEY_NOTIFICATION: {
        if (len < 23) return;
        BtAddress peer;
        memcpy(peer.b, params, 6);
        pair_store_key(p, &peer, params + 6);
        break;
    }
    case BT_EV_AUTHENTICATION_COMPLETE: {
        if (len < 3) return;
        int status = params[0];
        uint16_t handle = bt_rd16(params + 1);
        BtPairEntry *e = NULL;
        for (int i = 0; i < BT_PAIR_MAX; i++)
            if (p->entries[i].used && p->entries[i].handle == handle) e = &p->entries[i];
        if (!e) return;
        if (status != BT_ST_SUCCESS) {
            e->state = BT_PAIR_FAILED;
            e->error = BT_EAUTH;
            return;
        }
        e->state = BT_PAIR_ENCRYPTING;
        bt_hci_set_encryption(p->hci, handle);
        break;
    }
    case BT_EV_ENCRYPTION_CHANGE: {
        if (len < 3) return;
        int status = params[0];
        uint16_t handle = bt_rd16(params + 1);
        BtPairEntry *e = NULL;
        for (int i = 0; i < BT_PAIR_MAX; i++)
            if (p->entries[i].used && p->entries[i].handle == handle) e = &p->entries[i];
        if (!e) return;
        if (status != BT_ST_SUCCESS) {
            e->state = BT_PAIR_FAILED;
            e->error = BT_EAUTH;
            return;
        }
        e->state = BT_PAIR_BONDING;
        /* Bond: store the key with the controller so the next link is
         * silent. */
        if (e->have_key) bt_hci_write_link_key(p->hci, &e->peer, e->link_key);
        e->state = BT_PAIR_PAIRED;
        break;
    }
    default:
        break;
    }
}
