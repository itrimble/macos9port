/* L2CAP internals shared by the data plane (bt_l2cap.c) and the
 * signalling plane (bt_l2cap_sig.c). Not a public header. */
#ifndef BT_L2CAP_INT_H
#define BT_L2CAP_INT_H

#include "bt_internal.h"

struct BtL2capChan {
    int used;
    int cid;              /* local CID */
    int remote_cid;       /* peer CID, used for outbound packets */
    uint16_t psm;
    int state;
    uint16_t mtu, peer_mtu;
    uint16_t handle;
    BtAddress peer;
    /* reassembly scratch */
    uint8_t *rx;
    size_t rx_cap, rx_len, rx_expect;
    int rx_overflow;
    /* queue of complete SDUs, each stored as a 2 byte length then data */
    uint8_t *q;
    size_t q_cap, q_len, q_head;
    BtL2capOpenFn on_open;
    void *open_ctx;
    BtL2capCloseFn chan_close;
    void *chan_close_ctx;
    BtL2capDataFn data_fn;
    void *data_ctx;
    /* Set when the operating system opened the channel for us. */
    BtL2capPipe pipe;
    int adopted;
};

typedef struct {
    int used;
    uint16_t handle;
    BtAddress peer;
    int rx_cid;           /* channel being reassembled, -1 when idle */
} BtLinkEntry;

typedef struct {
    uint16_t psm;
    BtL2capOpenFn on_open;
    void *ctx;
} BtListener;

struct BtL2cap {
    BtAclSendFn send;
    void *send_ctx;
    BtL2capChan chans[BT_MAX_CHANS];
    BtLinkEntry links[BT_MAX_LINKS];
    BtListener listens[BT_MAX_CHANS];
    int nlistens;
    uint8_t lid;
    uint8_t *tx;
    BtL2capCloseFn on_close;
    void *close_ctx;
};

/* Data plane helpers (bt_l2cap.c). */
BtL2capChan *bt_l2cap_chan_find(BtL2cap *l, int cid);
BtL2capChan *bt_l2cap_chan_find_link(BtL2cap *l, uint16_t handle, int cid);
BtL2capChan *bt_l2cap_chan_alloc(BtL2cap *l, uint16_t handle, int cid, uint16_t psm);
void bt_l2cap_chan_release(BtL2capChan *c);
void bt_l2cap_chan_close(BtL2cap *l, BtL2capChan *c);
const BtAddress *bt_l2cap_link_peer(BtL2cap *l, uint16_t handle);
int bt_l2cap_link_rx_cid(BtL2cap *l, uint16_t handle);
void bt_l2cap_link_set_rx_cid(BtL2cap *l, uint16_t handle, int cid);
void bt_l2cap_queue_push(BtL2capChan *c, const uint8_t *p, size_t n);
size_t bt_l2cap_queue_pop(BtL2capChan *c, uint8_t *p, size_t cap);
uint8_t bt_l2cap_next_lid(BtL2cap *l);

/* Signalling plane (bt_l2cap_sig.c). */
int bt_l2cap_sig_send(BtL2cap *l, uint16_t handle, uint8_t code, uint8_t lid,
                      const uint8_t *data, size_t n);
void bt_l2cap_handle_sig(BtL2cap *l, uint16_t handle, const uint8_t *p, size_t len);

#endif
