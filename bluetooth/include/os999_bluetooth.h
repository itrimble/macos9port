/* OS999 Bluetooth stack: HCI, L2CAP, RFCOMM and BNEP.
 *
 * Classic programs never touch a Bluetooth controller. Their transport
 * calls are translated by this stack into HCI commands and ACL packets;
 * the controller layer owns the link, applies the pairing policy, keeps
 * every buffer bounded and never blocks a guest.
 *
 * The layers are independent so a guest can reach them through the same
 * socket-shaped API the Open Transport equivalent in net/ uses:
 *
 *   bt_hci      raw controller packets (command, event, ACL)
 *   bt_l2cap    logical channels over an ACL link: signalling, MTU, CID
 *   bt_rfcomm   serial ports (DLCI, framing, credit flow control)
 *   bt_bnep     Ethernet encapsulation and multicast filters
 *   bt_pan      a link-layer socket over BNEP for the guest network stack
 *
 * C11 + POSIX only; no SDL and no windowing. */
#ifndef OS999_BLUETOOTH_H
#define OS999_BLUETOOTH_H

#include <stddef.h>
#include <stdint.h>

enum { BT_OK = 0, BT_EINVAL = -1, BT_EDENIED = -2, BT_ELIMIT = -3, BT_EIO = -4,
       BT_EAGAIN = -5, BT_ECLOSED = -6, BT_ENOENT = -7, BT_ETIMEOUT = -8,
       BT_ENOMEM = -9, BT_EBUSY = -10, BT_EAUTH = -11, BT_EUNSUP = -12 };
const char *bt_status_text(int code);

/* ---- addresses ---- */
/* A Bluetooth device address, most significant octet first. */
typedef struct { uint8_t b[6]; } BtAddress;

/* "AA:BB:CC:DD:EE:FF" (case insensitive, "-" and "." also accepted). */
int bt_addr_parse(const char *text, BtAddress *out);
int bt_addr_format(const BtAddress *addr, char *out, size_t cap);
int bt_addr_cmp(const BtAddress *a, const BtAddress *b);
int bt_addr_is_zero(const BtAddress *addr);

/* ---- device database ---- */
typedef struct {
    BtAddress addr;
    char name[64];
    uint32_t class_of_device;   /* 24 bit Class of Device */
    int rssi;                   /* dBm, 0 when unknown */
    int paired, bonded, connected, legacy;
    uint64_t last_seen_ms;
} BtDeviceInfo;

typedef struct BtDeviceDb BtDeviceDb;
BtDeviceDb *bt_db_new(void);
void bt_db_free(BtDeviceDb *db);
/* Insert or update by address; keeps the name when the new record has none. */
int bt_db_upsert(BtDeviceDb *db, const BtDeviceInfo *info);
const BtDeviceInfo *bt_db_find(const BtDeviceDb *db, const BtAddress *addr);
int bt_db_count(const BtDeviceDb *db);
const BtDeviceInfo *bt_db_at(const BtDeviceDb *db, int index);
int bt_db_remove(BtDeviceDb *db, const BtAddress *addr);

/* ---- HCI packets ---- */
enum { BT_PKT_COMMAND = 1, BT_PKT_ACL = 2, BT_PKT_SCO = 3, BT_PKT_EVENT = 4 };

typedef struct {
    int kind;            /* BT_PKT_* */
    uint16_t opcode;     /* command */
    uint16_t handle;     /* ACL */
    uint8_t flags;       /* ACL packet boundary and direction */
    uint8_t code;        /* event */
    const uint8_t *data; /* payload, not owned */
    size_t len;
} BtHciPacket;

size_t bt_hci_encode_command(uint16_t opcode, const uint8_t *params, size_t n, uint8_t *out, size_t cap);
size_t bt_hci_encode_acl(uint16_t handle, uint8_t flags, const uint8_t *data, size_t n, uint8_t *out, size_t cap);
size_t bt_hci_encode_event(uint8_t code, const uint8_t *params, size_t n, uint8_t *out, size_t cap);
/* Split one raw packet. Returns 0 on success, BT_EINVAL when the packet is
 * truncated or carries an unknown type. */
int bt_hci_decode(const uint8_t *pkt, size_t len, BtHciPacket *out);

/* ---- HCI transport ---- */
/* The controller link. write() hands one complete packet to the
 * controller; read() pulls one complete packet back. Both return the
 * byte count, 0 for "nothing yet" and a negative value on failure. */
typedef struct BtHciTransport {
    long (*write)(void *ctx, const uint8_t *pkt, size_t len);
    long (*read)(void *ctx, uint8_t *buf, size_t cap);
    void (*close)(void *ctx);
    void *ctx;
} BtHciTransport;

/* ---- HCI engine ---- */
typedef struct BtHci BtHci;
typedef void (*BtHciLogFn)(void *ctx, const char *line);

/* Events and inbound ACL packets leave the engine through this sink.
 * flags carries the ACL packet boundary bits (BT_ACL_PB_FIRST when the
 * payload starts an L2CAP PDU). */
typedef struct {
    void (*on_event)(void *ctx, uint8_t code, const uint8_t *params, size_t len);
    void (*on_acl)(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len);
    void *ctx;
} BtHciSink;

BtHci *bt_hci_new(const BtHciTransport *tr, BtHciLogFn log, void *log_ctx);
void bt_hci_free(BtHci *h);
void bt_hci_set_sink(BtHci *h, const BtHciSink *sink);
/* Read whatever the controller has and dispatch it. Returns the number of
 * packets processed, or a negative BT_* code. */
int bt_hci_pump(BtHci *h, int timeout_ms);
/* Pump until the controller answers one opcode. Returns the HCI status,
 * or BT_ETIMEOUT when it does not arrive in time. */
int bt_hci_wait(BtHci *h, uint16_t opcode, int timeout_ms);

int bt_hci_reset(BtHci *h);
int bt_hci_read_bd_addr(BtHci *h);
int bt_hci_write_local_name(BtHci *h, const char *name);
int bt_hci_set_scan_enable(BtHci *h, int on);
/* Standard inquiry (LAP 0x9E8B33), length in 1.28 s units. */
int bt_hci_inquiry(BtHci *h, uint8_t length_units, int8_t timeout, uint32_t class_mask);
int bt_hci_cancel_inquiry(BtHci *h);
int bt_hci_create_connection(BtHci *h, const BtAddress *peer, uint16_t packet_type);
int bt_hci_accept_connection(BtHci *h, const BtAddress *peer, uint8_t role);
int bt_hci_disconnect(BtHci *h, uint16_t handle, uint8_t reason);
int bt_hci_pin_reply(BtHci *h, uint8_t pin_len, const uint8_t *pin);
int bt_hci_auth_requested(BtHci *h, uint16_t handle);
int bt_hci_set_encryption(BtHci *h, uint16_t handle);
int bt_hci_remote_name_request(BtHci *h, const BtAddress *peer, uint16_t timeout);
int bt_hci_write_link_key(BtHci *h, const BtAddress *peer, const uint8_t key[16]);
int bt_hci_read_link_key(BtHci *h, const BtAddress *peer);
int bt_hci_send_acl(BtHci *h, uint16_t handle, const uint8_t *data, size_t len);
/* Same, with explicit ACL packet boundary flags (BT_ACL_PB_FIRST or
 * BT_ACL_PB_CONT) for segmented payloads. */
int bt_hci_send_acl_flags(BtHci *h, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len);

/* ---- pairing ---- */
enum { BT_PAIR_IDLE = 0, BT_PAIR_WAIT_PIN, BT_PAIR_WAIT_AUTH, BT_PAIR_ENCRYPTING,
       BT_PAIR_BONDING, BT_PAIR_PAIRED, BT_PAIR_FAILED };

typedef struct {
    int state;
    BtAddress peer;
    uint8_t pin[16];
    int pin_len;
    uint8_t link_key[16];
    int authenticated, encrypted, bonded;
    int error;                /* BT_* */
} BtPairState;

typedef struct BtPair BtPair;
BtPair *bt_pair_new(BtHci *hci);
void bt_pair_free(BtPair *p);
/* Start authenticating a link. */
int bt_pair_begin(BtPair *p, const BtAddress *peer, uint16_t handle);
/* Answer a PIN request. */
int bt_pair_set_pin(BtPair *p, const BtAddress *peer, const uint8_t *pin, int len);
int bt_pair_cancel(BtPair *p, const BtAddress *peer);
int bt_pair_state(const BtPair *p, const BtAddress *peer, BtPairState *out);
int bt_pair_has_key(const BtPair *p, const BtAddress *peer, uint8_t key[16]);
/* Feed controller events to the machine. */
void bt_pair_on_event(BtPair *p, uint8_t code, const uint8_t *params, size_t len);

/* ---- L2CAP ---- */
enum { BT_PSM_SDP = 0x0001, BT_PSM_RFCOMM = 0x0003, BT_PSM_BNEP = 0x000F };

enum { BT_L2CAP_CLOSED = 0, BT_L2CAP_WAIT_CONNECT_RSP, BT_L2CAP_WAIT_CONFIG_RSP,
       BT_L2CAP_OPEN, BT_L2CAP_WAIT_DISCONNECT_RSP };

typedef struct BtL2cap BtL2cap;
typedef struct BtL2capChan BtL2capChan;

typedef struct {
    int state;
    uint16_t cid, remote_cid, psm, mtu, peer_mtu;
    BtAddress peer;
} BtL2capInfo;

/* Sends one ACL payload on a handle. flags carries the packet boundary
 * bits. Returns bytes accepted or <0. */
typedef long (*BtAclSendFn)(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len);

/* Called when a peer opens a channel on a listened PSM. */
typedef void (*BtL2capOpenFn)(void *ctx, BtL2capChan *chan);
typedef void (*BtL2capCloseFn)(void *ctx, BtL2capChan *chan);
/* Called with every complete payload that arrives on a channel. */
typedef void (*BtL2capDataFn)(void *ctx, BtL2capChan *chan, const uint8_t *p, size_t n);

BtL2cap *bt_l2cap_new(void);
void bt_l2cap_free(BtL2cap *l);
void bt_l2cap_set_acl_sender(BtL2cap *l, BtAclSendFn fn, void *ctx);
/* Announce a new baseband link. cid space is per link. */
void bt_l2cap_link_up(BtL2cap *l, uint16_t handle, const BtAddress *peer);
void bt_l2cap_link_down(BtL2cap *l, uint16_t handle);
int bt_l2cap_listen(BtL2cap *l, uint16_t psm, BtL2capOpenFn on_open, void *ctx);
/* Starts an outgoing channel. on_open fires once the peer configured the
 * channel. out_cid receives the local CID. */
int bt_l2cap_connect(BtL2cap *l, uint16_t handle, const BtAddress *peer, uint16_t psm,
                     uint16_t mtu, BtL2capOpenFn on_open, void *ctx, int *out_cid);
int bt_l2cap_send(BtL2cap *l, int cid, const void *p, size_t n);
/* 0 when nothing is buffered, BT_ECLOSED once the peer is gone. */
long bt_l2cap_recv(BtL2cap *l, int cid, void *p, size_t cap);
int bt_l2cap_disconnect(BtL2cap *l, int cid);
int bt_l2cap_info(BtL2cap *l, int cid, BtL2capInfo *out);
void bt_l2cap_set_close_fn(BtL2cap *l, BtL2capCloseFn fn, void *ctx);
/* Per channel close callback; it overrides the stack wide one for that
 * channel. The channel pointer is only valid inside the callback. */
void bt_l2cap_set_chan_close_fn(BtL2capChan *chan, BtL2capCloseFn fn, void *ctx);
/* Per channel payload callback. The payload is also queued for
 * bt_l2cap_recv, so a consumer that uses the callback must not poll the
 * same channel. */
void bt_l2cap_set_chan_data_fn(BtL2capChan *chan, BtL2capDataFn fn, void *ctx);
/* The channel with this local CID, or NULL. */
BtL2capChan *bt_l2cap_chan(BtL2cap *l, int cid);

/* A byte pipe to a channel the operating system opened for us. The
 * system already did the L2CAP framing, so the payload is exchanged
 * without a header. */
typedef struct {
    long (*write)(void *ctx, const void *p, size_t n);
    long (*read)(void *ctx, void *p, size_t cap);
    void *ctx;
} BtL2capPipe;

/* Adopt a channel the operating system opened and framed. The channel
 * starts open. */
int bt_l2cap_adopt(BtL2cap *l, uint16_t handle, const BtAddress *peer, uint16_t psm,
                   uint16_t mtu, const BtL2capPipe *pipe, int *out_cid);
/* Feed one payload that arrived on an adopted channel. */
void bt_l2cap_pipe_rx(BtL2cap *l, int cid, const uint8_t *p, size_t n);
/* Feed one inbound ACL payload. */
void bt_l2cap_rx(BtL2cap *l, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len);
int bt_l2cap_chan_cid(const BtL2capChan *chan);
int bt_l2cap_chan_peer(const BtL2capChan *chan, BtAddress *out);

/* ---- RFCOMM ---- */
enum { BT_RFCOMM_CLOSED = 0, BT_RFCOMM_WAIT_SABM, BT_RFCOMM_WAIT_UA, BT_RFCOMM_OPEN,
       BT_RFCOMM_WAIT_DISC, BT_RFCOMM_ERROR };

typedef struct BtRfcomm BtRfcomm;
typedef struct BtRfcommChan BtRfcommChan;

typedef struct {
    int state;
    int dlci;
    uint8_t server_channel;
    int credit;          /* credits the peer granted us */
    size_t unread;
} BtRfcommInfo;

typedef void (*BtRfcommOpenFn)(void *ctx, BtRfcommChan *chan);
typedef void (*BtRfcommDataFn)(void *ctx, BtRfcommChan *chan, const uint8_t *p, size_t n);
typedef void (*BtRfcommCloseFn)(void *ctx, BtRfcommChan *chan);

BtRfcomm *bt_rfcomm_new(BtL2cap *l, int cid, int local_is_server);
void bt_rfcomm_free(BtRfcomm *r);
/* Server side: wait for SABM on a server channel (1..30). */
int bt_rfcomm_listen(BtRfcomm *r, uint8_t server_channel, BtRfcommOpenFn on_open, void *ctx);
/* Client side: open a server channel. out_dlci receives the data DLCI. */
int bt_rfcomm_connect(BtRfcomm *r, uint8_t server_channel, int *out_dlci);
int bt_rfcomm_send(BtRfcomm *r, int dlci, const void *p, size_t n);
long bt_rfcomm_recv(BtRfcomm *r, int dlci, void *p, size_t cap);
int bt_rfcomm_grant_credit(BtRfcomm *r, int dlci, uint8_t credits);
int bt_rfcomm_disconnect(BtRfcomm *r, int dlci);
int bt_rfcomm_info(BtRfcomm *r, int dlci, BtRfcommInfo *out);
void bt_rfcomm_set_data_fn(BtRfcomm *r, BtRfcommDataFn fn, void *ctx);
void bt_rfcomm_set_close_fn(BtRfcomm *r, BtRfcommCloseFn fn, void *ctx);
/* Called for a channel this side opened with bt_rfcomm_connect. */
void bt_rfcomm_set_open_fn(BtRfcomm *r, BtRfcommOpenFn fn, void *ctx);
/* Feed one inbound L2CAP payload. */
void bt_rfcomm_rx(BtRfcomm *r, const uint8_t *p, size_t n);

/* ---- BNEP ---- */
enum { BT_BNEP_ETHERTYPE_IPV4 = 0x0800, BT_BNEP_ETHERTYPE_ARP = 0x0806,
       BT_BNEP_ETHERTYPE_IPV6 = 0x86DD };

/* BNEP header types carried in the first octet. */
enum { BT_BNEP_HDR_ETHERNET = 0x00, BT_BNEP_HDR_BNEP = 0x01 };

/* BNEP control message types. */
enum { BT_BNEP_CMD_FILTER = 0x00, BT_BNEP_CMD_CONN_REQ = 0x01, BT_BNEP_CMD_CONN_RSP = 0x02,
       BT_BNEP_CMD_DISCONN_REQ = 0x03, BT_BNEP_CMD_DISCONN_RSP = 0x04 };

/* Multicast filter modes carried in the filter message. */
enum { BT_BNEP_FILTER_ALL = 0, BT_BNEP_FILTER_NONE = 1 };

typedef struct BtBnep BtBnep;
typedef struct BtBnepChan BtBnepChan;

typedef struct {
    int up;
    int state;
    uint8_t dest_mac[6], src_mac[6];
    uint16_t ethertype;
} BtBnepInfo;

typedef void (*BtBnepOpenFn)(void *ctx, BtBnepChan *chan);
typedef void (*BtBnepFrameFn)(void *ctx, BtBnepChan *chan, const uint8_t *frame, size_t len);
typedef void (*BtBnepCloseFn)(void *ctx, BtBnepChan *chan);

BtBnep *bt_bnep_new(BtL2cap *l);
void bt_bnep_free(BtBnep *b);
int bt_bnep_listen(BtBnep *b, BtBnepOpenFn on_open, void *ctx);
int bt_bnep_connect(BtBnep *b, uint16_t handle, const BtAddress *peer, int *out_cid);
/* Send one Ethernet frame (destination and source MAC plus payload). */
int bt_bnep_send(BtBnep *b, int cid, const uint8_t dst[6], const uint8_t src[6],
                 uint16_t ethertype, const void *payload, size_t len);
/* Multicast filter: 0 = all, 1 = none, otherwise a group address. */
int bt_bnep_set_filter(BtBnep *b, int cid, int mode, const uint8_t group[6]);
int bt_bnep_info(BtBnep *b, int cid, BtBnepInfo *out);
/* The BNEP channel that talks to this peer MAC, or BT_ENOENT. */
int bt_bnep_find_by_mac(BtBnep *b, const uint8_t mac[6], int *out_cid);
void bt_bnep_set_frame_fn(BtBnep *b, BtBnepFrameFn fn, void *ctx);
void bt_bnep_set_close_fn(BtBnep *b, BtBnepCloseFn fn, void *ctx);
/* Feed one inbound L2CAP payload. */
void bt_bnep_rx(BtBnep *b, int cid, const uint8_t *p, size_t n);

/* ---- PAN socket ---- */
/* A link-layer socket over BNEP. It mirrors the broker API in net/ so the
 * guest network stack can adopt a Bluetooth PAN the same way it adopts a
 * TCP or UDP connection. */
typedef struct BtPan BtPan;

typedef struct {
    int state;
    int up;
    uint8_t peer_mac[6];
    size_t unread, unsent;
    uint64_t bytes_in, bytes_out;
} BtPanInfo;

enum { BT_PAN_CLOSED = 0, BT_PAN_CONNECTING, BT_PAN_UP, BT_PAN_DOWN };

BtPan *bt_pan_new(BtBnep *b);
void bt_pan_free(BtPan *p);
/* Open a PAN link to a peer MAC address. The Bluetooth address is
 * needed to reach the peer over L2CAP. */
int bt_pan_open(BtPan *p, uint16_t handle, const BtAddress *peer, const uint8_t peer_mac[6],
                int *out_id);
int bt_pan_set_local_mac(BtPan *p, const uint8_t mac[6]);
int bt_pan_send(BtPan *p, int id, const void *frame, size_t len);
long bt_pan_recv(BtPan *p, int id, void *frame, size_t cap);
int bt_pan_info(BtPan *p, int id, BtPanInfo *out);
int bt_pan_close(BtPan *p, int id);
/* Route an inbound frame to the socket that owns its source MAC. */
int bt_pan_route(BtPan *p, const uint8_t src_mac[6], const uint8_t *frame, size_t len);

/* ---- top level stack ---- */
typedef struct BtStack BtStack;
/* Build the whole stack on a controller transport. */
BtStack *bt_stack_new(const BtHciTransport *tr, BtHciLogFn log, void *log_ctx);
void bt_stack_free(BtStack *s);
BtDeviceDb *bt_stack_devices(BtStack *s);
BtHci *bt_stack_hci(BtStack *s);
BtL2cap *bt_stack_l2cap(BtStack *s);
BtPair *bt_stack_pair(BtStack *s);
/* Run a standard inquiry and collect the devices that answer. */
int bt_stack_discover(BtStack *s, uint8_t length_units, int8_t timeout);
int bt_stack_connect(BtStack *s, const BtAddress *peer, uint16_t packet_type);
int bt_stack_disconnect(BtStack *s, uint16_t handle);
/* The baseband handle of a connected peer. */
int bt_stack_handle_for(BtStack *s, const BtAddress *peer, uint16_t *out);
const BtDeviceInfo *bt_stack_device(BtStack *s, const BtAddress *addr);
int bt_stack_pump(BtStack *s, int timeout_ms);

#endif
