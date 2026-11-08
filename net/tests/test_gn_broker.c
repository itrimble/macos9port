/* Broker over loopback: IPv4/IPv6 TCP echo, peer close, refusal, policy
 * denial, bounded buffers, connection limit, listen/accept, UDP, resolver. */
#include "gn_test.h"

static int wait_state(GnBroker *b, int id, int want, unsigned ms) {
    uint64_t end = gn_now() + ms;
    GnInfo in;
    while (gn_now() < end) {
        gn_pump(b, 10);
        if (gn_info(b, id, &in) == GN_OK && (in.state == want || in.state == GN_S_FAILED)) return in.state;
    }
    return gn_info(b, id, &in) == GN_OK ? in.state : -1;
}

static long recv_all(GnBroker *b, int id, char *buf, size_t want, unsigned ms) {
    size_t got = 0;
    uint64_t end = gn_now() + ms;
    while (got < want && gn_now() < end) {
        gn_pump(b, 10);
        long k = gn_recv(b, id, buf + got, want - got);
        if (k < 0) return got ? (long)got : k;
        got += (size_t)k;
    }
    return (long)got;
}

static void echo_round(int family) {
    GnEcho e;
    CHECK(gn_echo_start(&e, family, 0) == 0);
    GnPolicy p = gn_open_policy();
    GnBroker *b = gn_broker_new(&p, "Fetch", NULL, NULL, NULL);
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    socklen_t len;
    if (family == AF_INET) { gn_sin((struct sockaddr_in *)&ss, "127.0.0.1", e.port); len = sizeof(struct sockaddr_in); }
    else {
        struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
        a->sin6_family = AF_INET6; a->sin6_addr = in6addr_loopback; a->sin6_port = htons(e.port);
        len = sizeof *a;
    }
    int id = -1;
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&ss, len, NULL, &id) == GN_OK);
    CHECK(wait_state(b, id, GN_S_OPEN, 2000) == GN_S_OPEN);
    static char out[200000], in[200000];
    for (size_t i = 0; i < sizeof out; i++) out[i] = (char)(i * 31 + 7);
    size_t sent = 0, got = 0;
    uint64_t end = gn_now() + 5000;
    while (got < sizeof out && gn_now() < end) { /* more than both caps: exercises back-pressure */
        if (sent < sizeof out) { long k = gn_send(b, id, out + sent, sizeof out - sent); if (k > 0) sent += (size_t)k; }
        gn_pump(b, 5);
        long k = gn_recv(b, id, in + got, sizeof in - got);
        if (k > 0) got += (size_t)k;
    }
    CHECK(got == sizeof out && !memcmp(in, out, sizeof out));
    GnInfo info;
    CHECK(gn_info(b, id, &info) == GN_OK && info.plaintext == 1 && info.kind == 1);
    CHECK(gn_shutdown(b, id) == GN_OK);
    CHECK(wait_state(b, id, GN_S_PEER_CLOSED, 2000) >= GN_S_PEER_CLOSED);
    CHECK(gn_release(b, id) == GN_OK);
    CHECK(gn_info(b, id, &info) != GN_OK || info.kind == 0);
    gn_broker_free(b);
    gn_echo_stop(&e);
}

static void test_failures(void) {
    GnPolicy p = gn_open_policy();
    GnLimits lim = {.max_conns = 2, .send_cap = 1024, .recv_cap = 1024, .connect_timeout_ms = 300};
    GnBroker *b = gn_broker_new(&p, "Fetch", &lim, NULL, NULL);
    /* Refused: bind a port, close it, connect to it. */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    gn_sin(&a, "127.0.0.1", 0);
    bind(s, (struct sockaddr *)&a, sizeof a);
    socklen_t al = sizeof a;
    getsockname(s, (struct sockaddr *)&a, &al);
    close(s);
    int id = -1;
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id) == GN_OK);
    CHECK(wait_state(b, id, GN_S_OPEN, 2000) == GN_S_FAILED);
    GnInfo info;
    gn_info(b, id, &info);
    CHECK(info.error == GN_EIO || info.error == GN_ETIMEOUT);
    CHECK(gn_send(b, id, "x", 1) < 0);
    gn_release(b, id);

    /* Bounded send buffer: an unconnected-yet stream accepts at most send_cap. */
    GnEcho e;
    gn_echo_start(&e, AF_INET, 0);
    gn_sin(&a, "127.0.0.1", e.port);
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id) == GN_OK);
    char big[4096] = {0};
    long k = gn_send(b, id, big, sizeof big);
    CHECK(k > 0 && k <= 1024);
    CHECK(gn_send(b, id, big, sizeof big) == 0 || gn_send(b, id, big, sizeof big) == GN_EAGAIN);
    int id2 = -1, id3 = -1;
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id2) == GN_OK);
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id3) == GN_ELIMIT);
    CHECK(gn_abort(b, id2) == GN_OK);
    gn_release(b, id2);
    gn_release(b, id);
    gn_echo_stop(&e);

    /* Peer closes after one read: we see PEER_CLOSED and recv reports EOF. */
    gn_echo_start(&e, AF_INET, 1);
    gn_sin(&a, "127.0.0.1", e.port);
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id) == GN_OK);
    CHECK(wait_state(b, id, GN_S_OPEN, 2000) == GN_S_OPEN);
    gn_send(b, id, "hello", 5);
    char buf[16];
    CHECK(recv_all(b, id, buf, 5, 2000) == 5 && !memcmp(buf, "hello", 5));
    CHECK(wait_state(b, id, GN_S_PEER_CLOSED, 2000) == GN_S_PEER_CLOSED);
    CHECK(gn_recv(b, id, buf, sizeof buf) == GN_ECLOSED);
    gn_release(b, id);
    gn_echo_stop(&e);

    /* Invalid ids never crash. */
    CHECK(gn_send(b, 999, "x", 1) == GN_EINVAL);
    CHECK(gn_recv(b, -1, buf, 1) == GN_EINVAL);
    CHECK(gn_release(b, 12345) == GN_EINVAL);
    gn_broker_free(b);
}

static void test_policy_gate(void) {
    GnPolicy p;
    gn_policy_init(&p);
    gn_policy_parse(&p, "legacy on\nallow out tcp * 80\n"); /* no local on */
    GnBroker *b = gn_broker_new(&p, "Fetch", NULL, NULL, NULL);
    struct sockaddr_in a;
    gn_sin(&a, "127.0.0.1", 80);
    int id = -1;
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &id) == GN_EDENIED);
    CHECK(gn_tcp_listen(b, AF_INET, 0, &id) == GN_EDENIED);
    CHECK(gn_udp_open(b, AF_INET, 0, &id) == GN_OK); /* opening is fine; traffic is checked */
    CHECK(gn_udp_sendto(b, id, (struct sockaddr *)&a, sizeof a, "x", 1) == GN_EDENIED);
    gn_broker_free(b);
}

static void test_listen_udp(void) {
    GnPolicy p = gn_open_policy();
    GnBroker *b = gn_broker_new(&p, "Server", NULL, NULL, NULL);
    int l = -1, c = -1, s = -1;
    CHECK(gn_tcp_listen(b, AF_INET, 0, &l) == GN_OK);
    GnInfo info;
    gn_info(b, l, &info);
    CHECK(info.kind == 2 && info.state == GN_S_LISTENING);
    struct sockaddr_in *la = (struct sockaddr_in *)&info.local;
    CHECK(la->sin_addr.s_addr == htonl(INADDR_LOOPBACK)); /* loopback unless lan-listen */
    CHECK(gn_tcp_accept(b, l, &s) == GN_EAGAIN);
    struct sockaddr_in a;
    gn_sin(&a, "127.0.0.1", ntohs(la->sin_port));
    CHECK(gn_tcp_connect(b, (struct sockaddr *)&a, sizeof a, NULL, &c) == GN_OK);
    uint64_t end = gn_now() + 2000;
    int r = GN_EAGAIN;
    while (r == GN_EAGAIN && gn_now() < end) { gn_pump(b, 10); r = gn_tcp_accept(b, l, &s); }
    CHECK(r == GN_OK);
    CHECK(wait_state(b, c, GN_S_OPEN, 2000) == GN_S_OPEN);
    gn_send(b, c, "ping", 4);
    char buf[8];
    CHECK(recv_all(b, s, buf, 4, 2000) == 4 && !memcmp(buf, "ping", 4));

    int u1 = -1, u2 = -1;
    CHECK(gn_udp_open(b, AF_INET, 0, &u1) == GN_OK && gn_udp_open(b, AF_INET, 0, &u2) == GN_OK);
    gn_info(b, u2, &info);
    gn_sin(&a, "127.0.0.1", ntohs(((struct sockaddr_in *)&info.local)->sin_port));
    CHECK(gn_udp_sendto(b, u1, (struct sockaddr *)&a, sizeof a, "datagram", 8) == 8);
    struct sockaddr_storage from;
    long k = 0;
    end = gn_now() + 2000;
    while (k == 0 && gn_now() < end) { gn_pump(b, 10); k = gn_udp_recvfrom(b, u2, buf, sizeof buf, &from); }
    CHECK(k == 8 && !memcmp(buf, "datagram", 8));
    gn_broker_free(b); /* frees every live slot */
}

static void test_resolver(void) {
    GnResolver *r = gn_resolver_new();
    int id = -1, n = 0;
    struct sockaddr_storage addrs[4];
    CHECK(gn_resolve_start(r, "localhost", AF_INET, &id) == GN_OK);
    int st = GN_EAGAIN;
    uint64_t end = gn_now() + 3000;
    while (st == GN_EAGAIN && gn_now() < end) { usleep(2000); st = gn_resolve_poll(r, id, addrs, 4, &n); }
    CHECK(st == GN_OK && n >= 1 && addrs[0].ss_family == AF_INET);
    CHECK(gn_resolve_start(r, "bad name!", AF_INET, &id) == GN_EINVAL);
    CHECK(gn_resolve_start(r, "", AF_INET, &id) == GN_EINVAL);
    char longname[300];
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;
    CHECK(gn_resolve_start(r, longname, AF_INET, &id) == GN_EINVAL);
    /* Cancel and free with lookups pending must not block or leak. */
    for (int i = 0; i < 4; i++) {
        int j;
        if (gn_resolve_start(r, "localhost", AF_UNSPEC, &j) == GN_OK && (i & 1)) gn_resolve_cancel(r, j);
    }
    gn_resolver_free(r);
}

int main(void) {
    echo_round(AF_INET);
    int s6 = socket(AF_INET6, SOCK_STREAM, 0);
    struct sockaddr_in6 a6 = {.sin6_family = AF_INET6, .sin6_addr = IN6ADDR_LOOPBACK_INIT};
    int have6 = s6 >= 0 && bind(s6, (struct sockaddr *)&a6, sizeof a6) == 0;
    if (s6 >= 0) close(s6);
    if (have6) echo_round(AF_INET6);
    else printf("test_gn_broker: IPv6 loopback unavailable, skipped\n");
    test_failures();
    test_policy_gate();
    test_listen_udp();
    test_resolver();
    usleep(200000); /* let detached lookups finish before LeakSanitizer runs */
    return gn_done("test_gn_broker");
}
