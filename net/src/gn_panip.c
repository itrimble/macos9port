/* PAN interface: Ethernet framing, ARP and IPv4 over a link that carries
 * frames. The link belongs to the transport (a Bluetooth BNEP channel in
 * practice); this file owns the address resolution and the IP header and
 * hands TCP segments to the endpoint table in gn_pantcp.c. */
#include "gn_pan_int.h"

#include <stdlib.h>
#include <time.h>

/* ---- byte order ---- */

uint16_t gn_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

void gn_wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

uint32_t gn_rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void gn_wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ---- the Internet checksum ---- */

uint32_t gn_ip_sum(const void *data, size_t len, uint32_t sum) {
    const uint8_t *p = data;
    while (len > 1) {
        sum += (uint32_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)p[0] << 8; /* the odd byte is padded low */
    return sum;
}

uint16_t gn_ip_fold(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t gn_ip_checksum(const void *data, size_t len, uint32_t sum) {
    return gn_ip_fold(gn_ip_sum(data, len, sum));
}

/* ---- the interface ---- */

uint64_t gn_pan_clock_ms(GnPan *p) {
    if (p && p->fake_now) return p->fake_now;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void gn_pan_set_clock(GnPan *p, uint64_t now_ms) {
    if (!p) return;
    p->fake_now = now_ms;
    if (now_ms) p->now = now_ms;
}

GnPan *gn_pan_new(const GnPanLink *link, uint32_t local_ip, uint32_t netmask, const uint8_t mac[6]) {
    if (!link || !link->tx || !mac) return NULL;
    GnPan *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->link = *link;
    p->local_ip = local_ip;
    p->netmask = netmask;
    memcpy(p->local_mac, mac, 6);
    p->next_port = 49152;
    p->next_isn = 0x10000000u;
    p->now = gn_pan_clock_ms(p);
    return p;
}

void gn_pan_free(GnPan *p) {
    if (!p) return;
    for (int i = 0; i < GN_PAN_TCP_MAX; i++)
        if (p->tcp[i].used) gn_pan_tcp_free(&p->tcp[i]);
    free(p);
}

int gn_pan_set_peer(GnPan *p, uint32_t peer_ip, const uint8_t mac[6]) {
    if (!p || !mac) return GN_EINVAL;
    p->peer_ip = peer_ip;
    memcpy(p->peer_mac, mac, 6);
    p->have_peer = 1;
    return GN_OK;
}

int gn_pan_owns(GnPan *p, uint32_t ip) {
    if (!p) return 0;
    return (ip & p->netmask) == (p->local_ip & p->netmask);
}

uint32_t gn_pan_local_ip(GnPan *p) { return p ? p->local_ip : 0; }

uint32_t gn_pan_netmask(GnPan *p) { return p ? p->netmask : 0; }

/* ---- the wire ---- */

/* Hand one Ethernet frame to the link. */
static int pan_tx(GnPan *p, const uint8_t dst[6], uint16_t type, const uint8_t *payload, size_t len) {
    uint8_t frame[GN_ETH_HDR_LEN + GN_PAN_MTU];
    if (len + GN_ETH_HDR_LEN > sizeof frame) return GN_ELIMIT;
    memcpy(frame, dst, 6);
    memcpy(frame + 6, p->local_mac, 6);
    gn_wr16(frame + 12, type);
    memcpy(frame + GN_ETH_HDR_LEN, payload, len);
    int rc = p->link.tx(p->link.ctx, frame, GN_ETH_HDR_LEN + len);
    return rc == 0 ? GN_OK : GN_EIO;
}

/* Answer an address request and learn the peer from its answer. */
static void pan_on_arp(GnPan *p, const uint8_t *a, size_t len) {
    if (len < GN_ARP_LEN) return;
    if (gn_rd16(a) != 1 || gn_rd16(a + 2) != GN_ETH_IPV4 || a[4] != 6 || a[5] != 4) return;
    uint16_t op = gn_rd16(a + 6);
    uint32_t spa = gn_rd32(a + 14), tpa = gn_rd32(a + 24);
    if (op == 1) {
        if (tpa != p->local_ip) return; /* somebody else is asked */
        uint8_t rep[GN_ARP_LEN];
        gn_wr16(rep, 1);
        gn_wr16(rep + 2, GN_ETH_IPV4);
        rep[4] = 6;
        rep[5] = 4;
        gn_wr16(rep + 6, 2); /* reply */
        memcpy(rep + 8, p->local_mac, 6);
        gn_wr32(rep + 14, p->local_ip);
        memcpy(rep + 18, a + 8, 6); /* the requester's hardware address */
        gn_wr32(rep + 24, spa);
        pan_tx(p, a + 8, GN_ETH_ARP, rep, sizeof rep);
        return;
    }
    if (op == 2 && (!p->have_peer || p->peer_ip == spa)) {
        memcpy(p->peer_mac, a + 8, 6);
        p->have_peer = 1;
        if (!p->peer_ip) p->peer_ip = spa;
    }
}

/* Check the IPv4 header and hand the TCP segment on. */
static void pan_on_ipv4(GnPan *p, const uint8_t *ip, size_t len) {
    if (len < GN_IPV4_HDR_LEN || (ip[0] >> 4) != 4) return;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    if (ihl < GN_IPV4_HDR_LEN || ihl > len) return;
    uint16_t total = gn_rd16(ip + 2);
    if (total < ihl || total > len) total = (uint16_t)len;
    if (gn_ip_checksum(ip, ihl, 0) != 0) return; /* corrupt */
    uint32_t src = gn_rd32(ip + 12), dst = gn_rd32(ip + 16);
    if (dst != p->local_ip) return;
    if (ip[9] != GN_PROTO_TCP) return;
    const uint8_t *seg = ip + ihl;
    size_t seglen = total - ihl;
    if (seglen < GN_TCP_HDR_LEN) return;
    uint8_t ph[12];
    gn_wr32(ph, src);
    gn_wr32(ph + 4, dst);
    ph[8] = 0;
    ph[9] = GN_PROTO_TCP;
    gn_wr16(ph + 10, (uint16_t)seglen);
    uint32_t sum = gn_ip_sum(ph, sizeof ph, 0);
    sum = gn_ip_sum(seg, seglen, sum);
    if (gn_ip_fold(sum) != 0) return; /* corrupt */
    gn_pan_tcp_input(p, src, dst, seg, seglen);
}

int gn_pan_rx(GnPan *p, const void *frame, size_t len) {
    if (!p || (!frame && len)) return GN_EINVAL;
    if (len < GN_ETH_HDR_LEN) return GN_EINVAL;
    const uint8_t *f = frame;
    uint16_t type = gn_rd16(f + 12);
    if (type == GN_ETH_ARP) pan_on_arp(p, f + GN_ETH_HDR_LEN, len - GN_ETH_HDR_LEN);
    else if (type == GN_ETH_IPV4) pan_on_ipv4(p, f + GN_ETH_HDR_LEN, len - GN_ETH_HDR_LEN);
    return GN_OK;
}

/* Build one IPv4 datagram carrying a TCP segment and send it. */
int gn_pan_send_tcp(GnPan *p, uint32_t dst_ip, uint16_t sport, uint16_t dport, uint32_t seq,
                    uint32_t ack, uint8_t flags, const uint8_t *data, size_t len) {
    if (!p || !p->have_peer) return GN_EIO;
    if (len + GN_TCP_HDR_LEN > GN_PAN_MTU) return GN_ELIMIT;
    /* One buffer holds the datagram: the IP header then the TCP segment. */
    uint8_t dgram[GN_IPV4_HDR_LEN + GN_TCP_HDR_LEN + GN_PAN_MTU];
    uint8_t *ip = dgram, *seg = dgram + GN_IPV4_HDR_LEN;
    gn_wr16(seg, sport);
    gn_wr16(seg + 2, dport);
    gn_wr32(seg + 4, seq);
    gn_wr32(seg + 8, ack);
    seg[12] = (uint8_t)((GN_TCP_HDR_LEN / 4) << 4);
    seg[13] = flags;
    gn_wr16(seg + 14, GN_PAN_WINDOW);
    gn_wr16(seg + 16, 0); /* the checksum is filled in below */
    gn_wr16(seg + 18, 0);
    if (len) memcpy(seg + GN_TCP_HDR_LEN, data, len);
    size_t seglen = GN_TCP_HDR_LEN + len;

    uint8_t ph[12];
    gn_wr32(ph, p->local_ip);
    gn_wr32(ph + 4, dst_ip);
    ph[8] = 0;
    ph[9] = GN_PROTO_TCP;
    gn_wr16(ph + 10, (uint16_t)seglen);
    uint32_t sum = gn_ip_sum(ph, sizeof ph, 0);
    sum = gn_ip_sum(seg, seglen, sum);
    gn_wr16(seg + 16, gn_ip_fold(sum));

    ip[0] = 0x45;
    ip[1] = 0;
    gn_wr16(ip + 2, (uint16_t)(GN_IPV4_HDR_LEN + seglen));
    gn_wr16(ip + 4, 0);      /* identification */
    gn_wr16(ip + 6, 0x4000); /* do not fragment */
    ip[8] = 64;              /* time to live */
    ip[9] = GN_PROTO_TCP;
    gn_wr16(ip + 10, 0);
    gn_wr32(ip + 12, p->local_ip);
    gn_wr32(ip + 16, dst_ip);
    gn_wr16(ip + 10, gn_ip_checksum(ip, GN_IPV4_HDR_LEN, 0));

    return pan_tx(p, p->peer_mac, GN_ETH_IPV4, dgram, GN_IPV4_HDR_LEN + seglen);
}

int gn_pan_tick(GnPan *p) {
    if (!p) return GN_EINVAL;
    p->now = gn_pan_clock_ms(p);
    return gn_pan_tick_endpoints(p);
}
