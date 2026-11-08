/* Shared internals for the OS999 Bluetooth stack. Not a public header. */
#ifndef BT_INTERNAL_H
#define BT_INTERNAL_H

#include "os999_bluetooth.h"

#include <string.h>

/* Bounded sizes. The stack never allocates without a cap so a hostile
 * peer cannot make the desktop grow. */
enum {
    BT_MAX_CHANS = 32,      /* L2CAP channels per stack */
    BT_MAX_LINKS = 16,      /* baseband links per stack */
    BT_MAX_RFCOMM = 16,     /* RFCOMM DLCIs */
    BT_MAX_BNEP = 8,        /* BNEP channels */
    BT_MAX_PAN = 8,         /* PAN sockets */
    BT_MAX_DEVICES = 64,    /* entries in the device database */
    BT_MAX_PENDING = 32,    /* outstanding HCI commands */
    BT_HCI_BUF = 4096,      /* largest HCI packet we accept */
    BT_MAX_SDU = 4096,      /* largest reassembled L2CAP payload */
    BT_MAX_FRAME = 4096     /* largest PAN frame */
};

uint64_t bt_now_ms(void);

/* ---- byte helpers ---- */
static inline uint16_t bt_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void bt_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void bt_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline uint32_t bt_rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* ---- HCI opcodes ---- */
enum {
    BT_OP_INQUIRY = 0x0401,
    BT_OP_INQUIRY_CANCEL = 0x0402,
    BT_OP_CREATE_CONNECTION = 0x0405,
    BT_OP_DISCONNECT = 0x0406,
    BT_OP_ACCEPT_CONNECTION = 0x0409,
    BT_OP_WRITE_LINK_KEY = 0x040B,
    BT_OP_LINK_KEY_NEG_REPLY = 0x040C,
    BT_OP_PIN_REPLY = 0x040D,
    BT_OP_PIN_NEG_REPLY = 0x040E,
    BT_OP_AUTH_REQUESTED = 0x0411,
    BT_OP_SET_ENCRYPTION = 0x0413,
    BT_OP_REMOTE_NAME_REQUEST = 0x0417,
    BT_OP_SET_EVENT_MASK = 0x0C01,
    BT_OP_RESET = 0x0C03,
    BT_OP_WRITE_LOCAL_NAME = 0x0C13,
    BT_OP_WRITE_SCAN_ENABLE = 0x0C1A,
    BT_OP_WRITE_CLASS_OF_DEVICE = 0x0C24,
    BT_OP_READ_BD_ADDR = 0x1009,
    BT_OP_READ_BUFFER_SIZE = 0x1005
};

/* ---- HCI event codes ---- */
enum {
    BT_EV_INQUIRY_COMPLETE = 0x01,
    BT_EV_INQUIRY_RESULT = 0x02,
    BT_EV_CONNECTION_COMPLETE = 0x03,
    BT_EV_CONNECTION_REQUEST = 0x04,
    BT_EV_DISCONNECTION_COMPLETE = 0x05,
    BT_EV_AUTHENTICATION_COMPLETE = 0x06,
    BT_EV_REMOTE_NAME_COMPLETE = 0x07,
    BT_EV_ENCRYPTION_CHANGE = 0x08,
    BT_EV_COMMAND_COMPLETE = 0x0C,
    BT_EV_COMMAND_STATUS = 0x0D,
    BT_EV_PIN_CODE_REQUEST = 0x12,
    BT_EV_LINK_KEY_REQUEST = 0x16,
    BT_EV_LINK_KEY_NOTIFICATION = 0x17,
    BT_EV_INQUIRY_RESULT_RSSI = 0x1C,
    BT_EV_LE_META = 0x3E
};

/* ---- HCI status codes ---- */
enum {
    BT_ST_SUCCESS = 0x00,
    BT_ST_UNKNOWN_COMMAND = 0x01,
    BT_ST_AUTH_FAILURE = 0x04,
    BT_ST_PIN_OR_KEY_MISSING = 0x05,
    BT_ST_CONNECTION_TIMEOUT = 0x08,
    BT_ST_COMMAND_DISALLOWED = 0x0C,
    BT_ST_INVALID_PARAMS = 0x12,
    BT_ST_REMOTE_USER_TERMINATED = 0x13,
    BT_ST_LOCAL_HOST_TERMINATED = 0x16,
    BT_ST_UNSUPPORTED_REMOTE_FEATURE = 0x1A,
    BT_ST_CONNECTION_FAILED = 0x3E
};

/* ---- L2CAP signalling ---- */
enum {
    BT_SIG_CMD_REJECT = 0x01,
    BT_SIG_CONN_REQ = 0x02,
    BT_SIG_CONN_RSP = 0x03,
    BT_SIG_CONFIG_REQ = 0x04,
    BT_SIG_CONFIG_RSP = 0x05,
    BT_SIG_DISCONN_REQ = 0x06,
    BT_SIG_DISCONN_RSP = 0x07,
    BT_SIG_ECHO_REQ = 0x08,
    BT_SIG_ECHO_RSP = 0x09
};

enum {
    BT_SIG_RESULT_OK = 0x0000,
    BT_SIG_RESULT_REFUSED = 0x0001,
    BT_SIG_RESULT_PSM_NOT_SUPPORTED = 0x0002,
    BT_SIG_RESULT_SECURITY_BLOCK = 0x0003,
    BT_SIG_RESULT_NO_RESOURCES = 0x0004
};

enum {
    BT_L2CAP_CID_SIG = 0x0001,
    BT_L2CAP_CID_FIRST = 0x0040,
    BT_L2CAP_MTU_DEFAULT = 2048,
    BT_L2CAP_MTU_MIN = 48
};

/* ACL packet boundary flags, stored in the top nibble of the handle word. */
enum { BT_ACL_PB_CONT = 0x01, BT_ACL_PB_FIRST = 0x02 };

/* ---- RFCOMM ---- */
/* The control octet is P/F (0x80) | UIH (0x40) | type (0x3F). SABM and
 * DISC are commands with P/F clear; UA and DM are their final
 * responses with P/F set. */
enum {
    BT_RFCOMM_SABM = 0x3F,
    BT_RFCOMM_UA = 0x83,
    BT_RFCOMM_DISC = 0x43,
    BT_RFCOMM_DM = 0x8F,
    BT_RFCOMM_UIH = 0xEF
};
enum {
    BT_RFCOMM_T_INFO = 0x00,
    BT_RFCOMM_T_UA = 0x03,
    BT_RFCOMM_T_DM = 0x0F,
    BT_RFCOMM_T_SABM = 0x3F,
    BT_RFCOMM_T_DISC = 0x43
};
enum { BT_RFCOMM_CTRL_DLCI = 0, BT_RFCOMM_DLCI_FIRST = 2, BT_RFCOMM_MAX_DLCI = 63 };

/* ---- BNEP ---- */


/* Small helper: copy with truncation, always NUL terminated. */
static inline void bt_strlcpy(char *dst, const char *src, size_t cap) {
    if (!cap) return;
    size_t n = 0;
    if (src) while (src[n] && n + 1 < cap) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

/* True when the MAC address is a multicast or broadcast address. */
int bt_mac_is_group(const uint8_t mac[6]);

#endif
