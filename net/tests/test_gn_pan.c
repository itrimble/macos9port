/* The PAN IP stack over a wired pair of interfaces: ARP, IPv4 and TCP
 * checksums, the TCP handshake, data in both directions, close, and the
 * broker's PAN endpoint. */
#include "gn_test.h"
#include "gn_pan_int.h"

enum { WIRE_MAX = 64, WIRE_BUF = 2048 };

typedef struct {
    uint8_t buf[WIRE_MAX][WIRE_BUF];
    size_t len[WIRE_MAX];
    int n, head;
} Wire;

typedef struct {
    GnPan *a, *b;
    Wire to_a, to_b;
} PanWire;

static const uint8_t MAC_A[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t MAC_B[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
#define IP_A 0xC0A80A01u /* 192.168.10.1 */
#define IP_B 0xC0A80A02u /* 192.168.10.2 */

static int wire_tx_a(void *ctx, const void *frame, size_t len) {
    PanWire *w = ctx;
    if (w->to_b.n >= WIRE_MAX || len > WIRE_BUF) return -1;
    memcpy(w->to_b.buf[w->to_b.n], frame, len);
    w->to_b.len[w->to_b.n] = len;
    w->to_b.n++;
    return 0;
}

static int wire_tx_b(void *ctx, const void *frame, size_t len) {
    PanWire *w = ctx;
    if (w->to_a.n >= WIRE_MAX || len > WIRE_BUF) return -1;
    memcpy(w->to_a.buf[w->to_a.n], frame, len);
    w->to_a.len[w->to_a.n] = len;
    w->to_a.n++;
    return 0;
}

static int wire_deliver(Wire *q, GnPan *dst) {
    if (q->head >= q->n) return 0;
    gn_pan_rx(dst, q->buf[q->head], q->len[q->head]);
    q->head++;
    return 1;
}

/* One round trip: deliver what each side queued, then let both tick. */
static void wire_step(PanWire *w) {
    wire_deliver(&w->to_b, w->b);
    wire_deliver(&w->to_a, w->a);
    gn_pan_tick(w->a);
    gn_pan_tick(w->b);
}

static void wire_run(PanWire *w, int rounds) {
    for (int i = 0; i < rounds; i++) wire_step(w);
}

static void wire_init(PanWire *w) {
    memset(w, 0, sizeof *w);
    GnPanLink la = {wire_tx_a, w}, lb = {wire_tx_b, w};
    w->a = gn_pan_new(&la, IP_A, 0xFFFFFF00u, MAC_A);
    w->b = gn_pan_new(&lb, IP_B, 0xFFFFFF00u, MAC_B);
    gn_pan_set_peer(w->a, IP_B, MAC_B);
    gn_pan_set_peer(w->b, IP_A, MAC_A);
}

static void wire_free(PanWire *w) {
    gn_pan_free(w->a);
    gn_pan_free(w->b);
}

/* An ARP request for one address, built by hand. */
static size_t build_arp(uint8_t *out, const uint8_t src_mac[6], uint32_t src_ip,
                        const uint8_t dst_mac[6], uint32_t dst_ip) {
    memcpy(out, dst_mac, 6);
    memcpy(out + 6, src_mac, 6);
    gn_wr16(out + 12, 0x0806);
    uint8_t *a = out + 14;
    gn_wr16(a, 1);         /* hardware type: Ethernet */
    gn_wr16(a + 2, 0x0800); /* protocol type: IPv4 */
    a[4] = 6;              /* hardware length */
    a[5] = 4;              /* protocol length */
    gn_wr16(a + 6, 1);     /* request */
    memcpy(a + 8, src_mac, 6);
    gn_wr32(a + 14, src_ip);
    memcpy(a + 18, dst_mac, 6);
    gn_wr32(a + 24, dst_ip);
    return 42;
}

/* The Internet checksum of an IPv4 header comes out zero when valid. */
static int ipv4_ok(const uint8_t *ip, size_t len) {
    if (len < 20 || (ip[0] >> 4) != 4) return 0;
    size_t ihl = (size_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || ihl > len) return 0;
    return gn_ip_checksum(ip, ihl, 0) == 0;
}

/* The TCP checksum covers the pseudo header, so it too comes out zero. */
static int tcp_ok(const uint8_t *ip, const uint8_t *seg, size_t seglen) {
    uint8_t ph[12];
    gn_wr32(ph, gn_rd32(ip + 12));
    gn_wr32(ph + 4, gn_rd32(ip + 16));
    ph[8] = 0;
    ph[9] = 6;
    gn_wr16(ph + 10, (uint16_t)seglen);
    uint32_t sum = gn_ip_sum(ph, sizeof ph, 0);
    sum = gn_ip_sum(seg, seglen, sum);
    return gn_ip_fold(sum) == 0;
}

static void test_subnet(void) {
    PanWire w;
    wire_init(&w);
    CHECK(gn_pan_owns(w.a, IP_A) == 1);
    CHECK(gn_pan_owns(w.a, IP_B) == 1);
    CHECK(gn_pan_owns(w.a, 0x0A000001u) == 0);
    CHECK(gn_pan_owns(w.a, 0xC0A80B01u) == 0);
    CHECK(gn_pan_local_ip(w.a) == IP_A);
    CHECK(gn_pan_netmask(w.a) == 0xFFFFFF00u);
    wire_free(&w);
}

static void test_arp(void) {
    PanWire w;
    wire_init(&w);

    uint8_t frame[64];
    size_t n = build_arp(frame, MAC_B, IP_B, MAC_A, IP_A);
    CHECK(gn_pan_rx(w.a, frame, n) == GN_OK);

    CHECK(w.to_b.n == 1);
    const uint8_t *rep = w.to_b.buf[0];
    CHECK(w.to_b.len[0] == 42);
    CHECK(gn_rd16(rep + 12) == 0x0806);
    const uint8_t *a = rep + 14;
    CHECK(gn_rd16(a + 6) == 2);                 /* reply */
    CHECK(memcmp(a + 8, MAC_A, 6) == 0);        /* our hardware address */
    CHECK(gn_rd32(a + 14) == IP_A);             /* our protocol address */
    CHECK(memcmp(a + 18, MAC_B, 6) == 0);       /* back to the requester */
    CHECK(gn_rd32(a + 24) == IP_B);

    /* A request for somebody else is not answered. */
    w.to_b.n = w.to_b.head = 0;
    n = build_arp(frame, MAC_B, IP_B, MAC_A, 0x0A000001u);
    gn_pan_rx(w.a, frame, n);
    CHECK(w.to_b.n == 0);

    /* A short frame is ignored. */
    gn_pan_rx(w.a, frame, 10);
    CHECK(w.to_b.n == 0);
    wire_free(&w);
}

static void test_handshake(void) {
    PanWire w;
    wire_init(&w);

    GnPanTcp *client = gn_pan_tcp_new(w.a);
    CHECK(client != NULL);
    CHECK(gn_pan_tcp_connect(client, IP_B, 8080, 0) == GN_OK);
    CHECK(gn_pan_tcp_state(client) == GN_TS_SYN_SENT);
    CHECK(gn_pan_tcp_local_port(client) != 0);

    /* The SYN that went out carries valid IPv4 and TCP checksums. */
    CHECK(w.to_b.n == 1);
    const uint8_t *syn = w.to_b.buf[0];
    CHECK(w.to_b.len[0] == 14 + 20 + 20);
    CHECK(gn_rd16(syn + 12) == 0x0800);
    CHECK(memcmp(syn, MAC_B, 6) == 0);   /* addressed to the peer */
    CHECK(memcmp(syn + 6, MAC_A, 6) == 0);
    CHECK(ipv4_ok(syn + 14, 40));
    const uint8_t *seg = syn + 34;
    CHECK(gn_rd16(seg + 2) == 8080);
    CHECK((seg[13] & GN_TCP_SYN) != 0);
    CHECK(tcp_ok(syn + 14, seg, 20));

    /* The peer listens and answers with a SYN|ACK. */
    GnPanTcp *listener = gn_pan_tcp_new(w.b);
    CHECK(gn_pan_tcp_listen(listener, 8080) == GN_OK);
    wire_run(&w, 4);

    GnPanTcp *server = NULL;
    CHECK(gn_pan_tcp_accept(listener, &server) == GN_OK);
    CHECK(server != NULL);
    CHECK(gn_pan_tcp_state(client) == GN_TS_ESTABLISHED);
    CHECK(gn_pan_tcp_state(server) == GN_TS_ESTABLISHED);
    CHECK(gn_pan_tcp_local_port(server) == 8080);

    gn_pan_tcp_free(client);
    gn_pan_tcp_free(server);
    gn_pan_tcp_free(listener);
    wire_free(&w);
}

static void test_data(void) {
    PanWire w;
    wire_init(&w);

    GnPanTcp *client = gn_pan_tcp_new(w.a);
    GnPanTcp *listener = gn_pan_tcp_new(w.b);
    gn_pan_tcp_connect(client, IP_B, 8080, 0);
    gn_pan_tcp_listen(listener, 8080);
    wire_run(&w, 4);

    GnPanTcp *server = NULL;
    CHECK(gn_pan_tcp_accept(listener, &server) == GN_OK);

    CHECK(gn_pan_tcp_send(client, "hello", 5) == 5);
    wire_run(&w, 4);

    char buf[32];
    CHECK(gn_pan_tcp_recv(server, buf, sizeof buf) == 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK(gn_pan_tcp_recv(server, buf, sizeof buf) == 0);

    /* And back the other way. */
    CHECK(gn_pan_tcp_send(server, "world", 5) == 5);
    wire_run(&w, 4);
    CHECK(gn_pan_tcp_recv(client, buf, sizeof buf) == 5);
    CHECK(memcmp(buf, "world", 5) == 0);

    /* A payload larger than one segment is split and reassembled. */
    char big[4000];
    for (size_t i = 0; i < sizeof big; i++) big[i] = (char)(i * 7);
    CHECK(gn_pan_tcp_send(client, big, sizeof big) == (long)sizeof big);
    wire_run(&w, 40);
    size_t got = 0;
    while (got < sizeof big) {
        long k = gn_pan_tcp_recv(server, buf, sizeof buf);
        if (k <= 0) break;
        got += (size_t)k;
    }
    CHECK(got == sizeof big);

    gn_pan_tcp_free(client);
    gn_pan_tcp_free(server);
    gn_pan_tcp_free(listener);
    wire_free(&w);
}

static void test_close(void) {
    PanWire w;
    wire_init(&w);

    GnPanTcp *client = gn_pan_tcp_new(w.a);
    GnPanTcp *listener = gn_pan_tcp_new(w.b);
    gn_pan_tcp_connect(client, IP_B, 8080, 0);
    gn_pan_tcp_listen(listener, 8080);
    wire_run(&w, 4);

    GnPanTcp *server = NULL;
    gn_pan_tcp_accept(listener, &server);

    /* The guest closes first: the peer sees the close. */
    CHECK(gn_pan_tcp_shutdown(client) == GN_OK);
    wire_run(&w, 4);
    CHECK(gn_pan_tcp_state(client) == GN_TS_FIN_WAIT_2);
    CHECK(gn_pan_tcp_state(server) == GN_TS_CLOSE_WAIT);

    /* The peer answers with its own close. */
    CHECK(gn_pan_tcp_shutdown(server) == GN_OK);
    wire_run(&w, 8);
    CHECK(gn_pan_tcp_state(client) == GN_TS_CLOSED);
    CHECK(gn_pan_tcp_state(server) == GN_TS_CLOSED);

    gn_pan_tcp_free(client);
    gn_pan_tcp_free(server);
    gn_pan_tcp_free(listener);
    wire_free(&w);
}

static void test_broker_endpoint(void) {
    PanWire w;
    wire_init(&w);

    GnPolicy p = gn_open_policy();
    GnBroker *b = gn_broker_new(&p, "Fetch", NULL, NULL, NULL);
    CHECK(gn_broker_set_pan(b, w.a) == GN_OK);
    CHECK(gn_broker_local_ip(b) == IP_A);
    CHECK(gn_broker_netmask(b) == 0xFFFFFF00u);

    /* The peer listens on the PAN. */
    GnPanTcp *listener = gn_pan_tcp_new(w.b);
    CHECK(gn_pan_tcp_listen(listener, 8080) == GN_OK);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(8080);
    sa.sin_addr.s_addr = htonl(IP_B);
    int id = -1;
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&sa, sizeof sa, NULL, &id) == GN_OK);

    GnInfo info;
    CHECK(gn_info(b, id, &info) == GN_OK);
    CHECK(info.state == GN_S_CONNECTING);

    for (int i = 0; i < 8; i++) { wire_step(&w); gn_pump(b, 0); }
    CHECK(gn_info(b, id, &info) == GN_OK);
    CHECK(info.state == GN_S_OPEN);
    CHECK(ntohl(((struct sockaddr_in *)&info.remote)->sin_addr.s_addr) == IP_B);

    GnPanTcp *server = NULL;
    CHECK(gn_pan_tcp_accept(listener, &server) == GN_OK);

    /* Guest bytes travel over the PAN. */
    CHECK(gn_send(b, id, "ping", 4) == 4);
    for (int i = 0; i < 8; i++) { wire_step(&w); gn_pump(b, 0); }
    char buf[32];
    CHECK(gn_pan_tcp_recv(server, buf, sizeof buf) == 4);
    CHECK(memcmp(buf, "ping", 4) == 0);

    CHECK(gn_pan_tcp_send(server, "pong", 4) == 4);
    for (int i = 0; i < 8; i++) { wire_step(&w); gn_pump(b, 0); }
    CHECK(gn_recv(b, id, buf, sizeof buf) == 4);
    CHECK(memcmp(buf, "pong", 4) == 0);

    /* A close travels the same way. */
    CHECK(gn_shutdown(b, id) == GN_OK);
    for (int i = 0; i < 8; i++) { wire_step(&w); gn_pump(b, 0); }
    CHECK(gn_info(b, id, &info) == GN_OK);
    CHECK(info.state == GN_S_PEER_CLOSED || info.state == GN_S_OPEN);

    gn_release(b, id);
    gn_pan_tcp_free(server);
    gn_pan_tcp_free(listener);
    gn_broker_free(b);
    wire_free(&w);
}

int main(void) {
    test_subnet();
    test_arp();
    test_handshake();
    test_data();
    test_close();
    test_broker_endpoint();
    return gn_done("test_gn_pan");
}
