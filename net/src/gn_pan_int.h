/* Internals of the PAN interface: the wire formats, the endpoint table and
 * the helpers the IP and TCP halves share. */
#ifndef GN_PAN_INT_H
#define GN_PAN_INT_H

#include "gn_ring.h"
#include "os999_guestnet.h"

#include <stdint.h>
#include <string.h>

enum {
    GN_ETH_HDR_LEN = 14,
    GN_ETH_IPV4 = 0x0800,
    GN_ETH_ARP = 0x0806,
    GN_ARP_LEN = 28,
    GN_IPV4_HDR_LEN = 20,
    GN_PROTO_TCP = 6,
    GN_TCP_HDR_LEN = 20,
    GN_PAN_MTU = 1500,
    GN_PAN_MSS = 1460,
    GN_PAN_WINDOW = 32768,
    GN_PAN_TCP_MAX = 16,
    GN_PAN_BUF = 8192,
    GN_PAN_RTO_MS = 2000,
    GN_PAN_MAX_RETRY = 3
};

/* TCP flags. */
enum { GN_TCP_FIN = 0x01, GN_TCP_SYN = 0x02, GN_TCP_RST = 0x04, GN_TCP_PSH = 0x08, GN_TCP_ACK = 0x10 };

/* TCP endpoint states. */
enum {
    GN_TS_CLOSED = 0, GN_TS_LISTEN, GN_TS_SYN_SENT, GN_TS_SYN_RECEIVED,
    GN_TS_ESTABLISHED, GN_TS_FIN_WAIT_1, GN_TS_FIN_WAIT_2, GN_TS_CLOSE_WAIT,
    GN_TS_CLOSING, GN_TS_LAST_ACK, GN_TS_TIME_WAIT
};

typedef struct GnPanTcp GnPanTcp;

struct GnPanTcp {
    GnPan *pan;
    int used, state;
    uint32_t peer_ip, local_ip;
    uint16_t peer_port, local_port;
    uint32_t snd_una, snd_nxt, rcv_nxt, isn;
    GnRing tx;      /* queued by the application */
    GnRing unacked; /* sent, not yet acknowledged */
    GnRing rx;      /* received, not yet consumed */
    uint64_t rto_at;
    int retries;
    int in_flight;   /* SYN and FIN bits of the segment in flight */
    int peer_fin, fin_sent, pending_fin, reset, eof;
    int fin_next;    /* the state to enter once the queued FIN has gone out */
    int accepted[GN_PAN_TCP_MAX];
    int naccepted;
};

struct GnPan {
    GnPanLink link;
    uint32_t local_ip, netmask, peer_ip;
    uint8_t local_mac[6], peer_mac[6];
    int have_peer;
    uint16_t next_port;
    uint32_t next_isn;
    uint64_t now, fake_now;
    GnPanTcp tcp[GN_PAN_TCP_MAX];
};

/* ---- byte order ---- */
uint16_t gn_rd16(const uint8_t *p);
void gn_wr16(uint8_t *p, uint16_t v);
uint32_t gn_rd32(const uint8_t *p);
void gn_wr32(uint8_t *p, uint32_t v);

/* ---- the Internet checksum ---- */
uint32_t gn_ip_sum(const void *data, size_t len, uint32_t sum);
uint16_t gn_ip_fold(uint32_t sum);
uint16_t gn_ip_checksum(const void *data, size_t len, uint32_t sum);

/* ---- the two halves ---- */
int gn_pan_send_tcp(GnPan *p, uint32_t dst_ip, uint16_t sport, uint16_t dport, uint32_t seq,
                    uint32_t ack, uint8_t flags, const uint8_t *data, size_t len);
void gn_pan_tcp_input(GnPan *p, uint32_t src_ip, uint32_t dst_ip, const uint8_t *seg, size_t len);
int gn_pan_tick_endpoints(GnPan *p);
uint64_t gn_pan_clock_ms(GnPan *p);
/* Pin the clock so a test can drive the retransmit timer by hand. */
void gn_pan_set_clock(GnPan *p, uint64_t now_ms);

#endif
