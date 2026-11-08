/* Small shared helpers: status text, clock and address handling. */
#include "bt_internal.h"

#include <stdio.h>
#include <time.h>

uint64_t bt_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

const char *bt_status_text(int code) {
    switch (code) {
    case BT_OK: return "ok";
    case BT_EINVAL: return "invalid argument";
    case BT_EDENIED: return "denied";
    case BT_ELIMIT: return "limit reached";
    case BT_EIO: return "i/o error";
    case BT_EAGAIN: return "try again";
    case BT_ECLOSED: return "closed";
    case BT_ENOENT: return "not found";
    case BT_ETIMEOUT: return "timed out";
    case BT_ENOMEM: return "out of memory";
    case BT_EBUSY: return "busy";
    case BT_EAUTH: return "authentication failed";
    case BT_EUNSUP: return "unsupported";
    default: return "unknown";
    }
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Accepts "AA:BB:CC:DD:EE:FF" with ':', '-' or '.' separators. The
 * separators are required so a malformed address cannot be read as a
 * different device. */
int bt_addr_parse(const char *text, BtAddress *out) {
    if (!text || !out) return BT_EINVAL;
    int n = 0, hi = -1;
    for (const char *p = text;; p++) {
        int v = hexval((unsigned char)*p);
        if (v >= 0) {
            if (hi < 0) {
                hi = v;
                continue;
            }
            if (n >= 6) return BT_EINVAL;
            out->b[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
            /* Another octet must be separated from this one. */
            if (n < 6 && !(p[1] == ':' || p[1] == '-' || p[1] == '.')) return BT_EINVAL;
            continue;
        }
        if (*p == ':' || *p == '-' || *p == '.') {
            if (hi >= 0) return BT_EINVAL; /* dangling nibble */
            continue;
        }
        break;
    }
    if (hi >= 0) return BT_EINVAL;
    if (n != 6) return BT_EINVAL;
    return BT_OK;
}

int bt_addr_format(const BtAddress *addr, char *out, size_t cap) {
    if (!addr || !out || cap < 18) return BT_EINVAL;
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", addr->b[0], addr->b[1], addr->b[2],
             addr->b[3], addr->b[4], addr->b[5]);
    return BT_OK;
}

int bt_addr_cmp(const BtAddress *a, const BtAddress *b) {
    if (!a || !b) return 1;
    return memcmp(a->b, b->b, sizeof a->b);
}

int bt_addr_is_zero(const BtAddress *addr) {
    if (!addr) return 1;
    for (int i = 0; i < 6; i++)
        if (addr->b[i]) return 0;
    return 1;
}

int bt_mac_is_group(const uint8_t mac[6]) {
    if (!mac) return 0;
    return (mac[0] & 0x01) != 0;
}
