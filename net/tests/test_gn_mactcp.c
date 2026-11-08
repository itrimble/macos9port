/* MacTCP adapter over fake big-endian guest RAM and a loopback echo
 * server: create/open/send/receive/close/release, async completions and
 * ASR events, no-copy receive, passive open, UDP, DNR, error paths. */
#include "gn_test.h"
#include "gn_mactcp_int.h"

enum { RAM = 1 << 20, PB = 0x1000, PB2 = 0x1100, RCVA = 0x10000, RCVB = 0x14000, BUF = 0x20000,
       WDS = 0x30000, RDS = 0x30100, DATA = 0x31000, ASR = 0x400, COMP = 0x480 };
static uint8_t ram[RAM];
static GnMem mem = {ram, RAM};
static GnMacTcp *mt;

static void w(uint32_t a, int n, uint32_t v) { gm_wr(&mem, a, n, v); }
static uint32_t r(uint32_t a, int n) { return gm_rd(&mem, a, n); }
static int16_t res(uint32_t pb) { return (int16_t)r(pb + 16, 2); }
static void pb_new(uint32_t pb, int code, uint32_t stream) {
    memset(ram + pb, 0, 128);
    w(pb + 24, 2, 0xFFC0); /* ioCRefNum, unused by the adapter */
    w(pb + 26, 2, (uint32_t)code);
    w(pb + 28, 4, stream);
}

static GnGuestCall calls[64];
static int ncalls;
static void drain(void) {
    int k = gn_mactcp_poll(mt, &mem, gn_now(), calls + ncalls, 64 - ncalls);
    ncalls += k;
}
/* Issue a call and, for sync calls, poll until it is no longer pending. */
static int16_t call(uint32_t pb, int async) {
    gn_mactcp_control(mt, &mem, pb, async, gn_now());
    uint64_t end = gn_now() + 5000;
    while (!async && res(pb) == 1 && gn_now() < end) { drain(); usleep(1000); }
    return res(pb);
}
static int wait_done(uint32_t pb) {
    uint64_t end = gn_now() + 5000;
    while (res(pb) == 1 && gn_now() < end) { drain(); usleep(1000); }
    return res(pb);
}
static int find_call(int kind, uint32_t key, uint16_t event) {
    for (int i = 0; i < ncalls; i++)
        if (calls[i].kind == kind && (kind == 1 ? calls[i].pb == key : calls[i].stream == key && calls[i].event == event))
            return 1;
    return 0;
}
static uint32_t tcp_create(uint32_t rcv, uint32_t len) {
    pb_new(PB, GN_TCP_CREATE, 0);
    w(PB + 32, 4, rcv); w(PB + 36, 4, len); w(PB + 40, 4, ASR); w(PB + 44, 4, 0xABCD);
    return call(PB, 0) == 0 ? r(PB + 28, 4) : 0;
}
static int16_t active_open(uint32_t sp, uint32_t ip, uint16_t port) {
    pb_new(PB, GN_TCP_ACTIVE_OPEN, sp);
    w(PB + 35, 1, 5); w(PB + 36, 4, ip); w(PB + 40, 2, port);
    return call(PB, 0);
}
static void wds2(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    memcpy(ram + DATA, a, la);
    memcpy(ram + DATA + 0x100, b, lb);
    w(WDS, 2, (uint32_t)la); w(WDS + 2, 4, DATA);
    w(WDS + 6, 2, (uint32_t)lb); w(WDS + 8, 4, DATA + 0x100);
    w(WDS + 12, 2, 0);
}
static int16_t send_sync(uint32_t sp, const char *a, const char *b) {
    wds2(a, b);
    pb_new(PB, GN_TCP_SEND, sp);
    w(PB + 38, 4, WDS);
    return call(PB, 0);
}
static int rcv_all(uint32_t sp, char *out, int want) {
    int got = 0;
    for (int tries = 0; got < want && tries < 50; tries++) {
        pb_new(PB, GN_TCP_RCV, sp);
        w(PB + 32, 1, 2); w(PB + 36, 4, BUF); w(PB + 40, 2, 512);
        if (call(PB, 0)) break;
        int k = (int)r(PB + 40, 2);
        memcpy(out + got, ram + BUF, (size_t)k);
        got += k;
    }
    return got;
}

static void test_tcp(uint16_t port) {
    pb_new(PB, GN_IPCTL_GETADDR, 0);
    CHECK(call(PB, 0) == 0 && r(PB + 28, 4) == 0x0A000202u && r(PB + 32, 4) == 0xFFFFFF00u);

    pb_new(PB, GN_TCP_CREATE, 0);
    w(PB + 32, 4, RCVA); w(PB + 36, 4, 100);
    CHECK(call(PB, 0) == MT_INVALID_LENGTH);
    w(PB + 32, 4, RAM - 16); w(PB + 36, 4, 8192);
    CHECK(call(PB, 0) == MT_INVALID_BUF_PTR);

    uint32_t sp = tcp_create(RCVA, 8192);
    CHECK(sp >= GN_STREAM_BASE);
    pb_new(PB, GN_TCP_SEND, sp);
    w(PB + 38, 4, WDS);
    CHECK(call(PB, 0) == MT_CONNECTION_DOESNT_EXIST);
    CHECK(active_open(sp, 0x7F000001u, port) == 0);
    CHECK(r(PB + 42, 4) == 0x0A000202u && r(PB + 46, 2) != 0);
    CHECK(active_open(sp, 0x7F000001u, port) == MT_CONNECTION_EXISTS);

    pb_new(PB, GN_TCP_STATUS, sp);
    CHECK(call(PB, 0) == 0 && r(PB + 52, 1) == 8 && r(PB + 38, 4) == 0x7F000001u && r(PB + 42, 2) == port);

    CHECK(send_sync(sp, "hello ", "world") == 0);
    char got[64] = {0};
    CHECK(rcv_all(sp, got, 11) == 11 && !memcmp(got, "hello world", 11));

    /* ASR: data arrives with no receive outstanding. */
    ncalls = 0;
    CHECK(send_sync(sp, "ab", "cd") == 0);
    uint64_t end = gn_now() + 3000;
    while (!find_call(2, sp, 4) && gn_now() < end) { drain(); usleep(1000); }
    CHECK(find_call(2, sp, 4));
    for (int i = 0; i < ncalls; i++)
        if (calls[i].kind == 2) CHECK(calls[i].proc == ASR && calls[i].user_data == 0xABCD);

    /* No-copy receive lends the stream's own area through an RDS. */
    pb_new(PB2, GN_TCP_NOCOPY_RCV, sp);
    w(PB2 + 12, 4, COMP); w(PB2 + 32, 1, 3); w(PB2 + 42, 4, RDS); w(PB2 + 46, 2, 4);
    ncalls = 0;
    gn_mactcp_control(mt, &mem, PB2, 1, gn_now());
    CHECK(wait_done(PB2) == 0);
    drain();
    CHECK(find_call(1, PB2, 0));
    uint32_t n = r(RDS, 2), p = r(RDS + 2, 4);
    CHECK(n >= 1 && n <= 4 && p == RCVA && r(RDS + 6, 2) == 0);
    CHECK(!memcmp(ram + RCVA, "abcd", n));
    /* While lent, another no-copy receive waits, then times out. */
    pb_new(PB2, GN_TCP_NOCOPY_RCV, sp);
    w(PB2 + 32, 1, 1); w(PB2 + 42, 4, RDS); w(PB2 + 46, 2, 4);
    CHECK(call(PB2, 0) == MT_COMMAND_TIMEOUT);
    pb_new(PB2, GN_TCP_RCV_BFR_RETURN, sp);
    w(PB2 + 42, 4, RDS);
    CHECK(call(PB2, 0) == 0);
    if (n < 4) { char rest[4]; CHECK(rcv_all(sp, rest, (int)(4 - n)) == (int)(4 - n)); }

    /* Async receive, then abort: the pending call completes as terminated. */
    pb_new(PB2, GN_TCP_RCV, sp);
    w(PB2 + 12, 4, COMP); w(PB2 + 36, 4, BUF); w(PB2 + 40, 2, 64);
    ncalls = 0;
    CHECK(gn_mactcp_control(mt, &mem, PB2, 1, gn_now()) == 1 && res(PB2) == 1);
    pb_new(PB, GN_TCP_ABORT, sp);
    CHECK(call(PB, 0) == 0);
    CHECK(res(PB2) == MT_CONNECTION_TERMINATED);
    drain();
    CHECK(find_call(1, PB2, 0));
    pb_new(PB, GN_TCP_STATUS, sp);
    CHECK(call(PB, 0) == MT_CONNECTION_DOESNT_EXIST && r(PB + 52, 1) == 0);

    /* Reopen, close gracefully, release returns the receive area. */
    CHECK(active_open(sp, 0x7F000001u, port) == 0);
    CHECK(send_sync(sp, "x", "y") == 0);
    pb_new(PB, GN_TCP_CLOSE, sp);
    w(PB + 32, 1, 5); w(PB + 34, 1, 0x80);
    CHECK(call(PB, 0) == 0);
    pb_new(PB, GN_TCP_SEND, sp);
    w(PB + 38, 4, WDS);
    CHECK(call(PB, 0) == MT_CONNECTION_CLOSING);
    pb_new(PB, GN_TCP_RELEASE, sp);
    CHECK(call(PB, 0) == 0 && r(PB + 32, 4) == RCVA && r(PB + 36, 4) == 8192);
    pb_new(PB, GN_TCP_STATUS, sp);
    CHECK(call(PB, 0) == MT_INVALID_STREAM);
    pb_new(PB, GN_TCP_STATUS, 0x12345678);
    CHECK(call(PB, 0) == MT_INVALID_STREAM);
    pb_new(PB, 99, sp);
    CHECK(call(PB, 0) != 0);
}

static void test_passive(void) {
    uint32_t a = tcp_create(RCVA, 8192), b = tcp_create(RCVB, 8192);
    CHECK(a && b);
    pb_new(PB2, GN_TCP_PASSIVE_OPEN, a);
    w(PB2 + 12, 4, COMP);
    ncalls = 0;
    CHECK(gn_mactcp_control(mt, &mem, PB2, 1, gn_now()) == 1);
    uint16_t lport = (uint16_t)r(PB2 + 46, 2);
    CHECK(lport != 0);
    pb_new(PB, GN_TCP_STATUS, a);
    CHECK(call(PB, 0) == 0 && r(PB + 52, 1) == 2);
    CHECK(active_open(b, 0x7F000001u, lport) == 0);
    CHECK(wait_done(PB2) == 0);
    CHECK(r(PB2 + 36, 4) == 0x7F000001u && r(PB2 + 40, 2) != 0);
    drain();
    CHECK(find_call(1, PB2, 0));
    CHECK(send_sync(b, "from ", "b") == 0);
    char got[16] = {0};
    CHECK(rcv_all(a, got, 6) == 6 && !memcmp(got, "from b", 6));
    /* Peer close reaches the other side as TCPClosing. */
    pb_new(PB, GN_TCP_CLOSE, b);
    CHECK(call(PB, 0) == 0);
    ncalls = 0;
    uint64_t end = gn_now() + 3000;
    while (!find_call(2, a, 1) && gn_now() < end) { drain(); usleep(1000); }
    CHECK(find_call(2, a, 1));
    pb_new(PB, GN_TCP_RCV, a);
    w(PB + 36, 4, BUF); w(PB + 40, 2, 16);
    CHECK(call(PB, 0) == MT_CONNECTION_CLOSING);
    pb_new(PB, GN_TCP_RELEASE, a);
    CHECK(call(PB, 0) == 0);
    pb_new(PB, GN_TCP_RELEASE, b);
    CHECK(call(PB, 0) == 0);
}

static void test_udp(void) {
    uint32_t sp[2];
    uint16_t port[2];
    for (int i = 0; i < 2; i++) {
        pb_new(PB, GN_UDP_CREATE, 0);
        w(PB + 32, 4, i ? RCVB : RCVA); w(PB + 36, 4, 4096);
        CHECK(call(PB, 0) == 0);
        sp[i] = r(PB + 28, 4);
        port[i] = (uint16_t)r(PB + 44, 2);
        CHECK(port[i] != 0);
    }
    pb_new(PB, GN_UDP_CREATE, 0);
    w(PB + 32, 4, BUF); w(PB + 36, 4, 512);
    CHECK(call(PB, 0) == MT_INVALID_LENGTH);
    pb_new(PB, GN_UDP_MTU, 0);
    w(PB + 28, 4, sp[0]);
    CHECK(call(PB, 0) == 0 && r(PB + 32, 2) == 1472);

    wds2("dgram-", "one");
    pb_new(PB, GN_UDP_WRITE, sp[0]);
    w(PB + 34, 4, 0x7F000001u); w(PB + 38, 2, port[1]); w(PB + 40, 4, WDS);
    CHECK(call(PB, 0) == 0);
    pb_new(PB, GN_UDP_READ, sp[1]);
    w(PB + 32, 2, 2);
    CHECK(call(PB, 0) == 0);
    CHECK(r(PB + 34, 4) == 0x7F000001u && r(PB + 38, 2) == port[0]);
    CHECK(r(PB + 40, 4) == RCVB && r(PB + 44, 2) == 9 && !memcmp(ram + RCVB, "dgram-one", 9));
    pb_new(PB, GN_UDP_BFR_RETURN, sp[1]);
    w(PB + 40, 4, RCVB);
    CHECK(call(PB, 0) == 0);
    /* Nothing more arrives: a 1 s read times out. */
    pb_new(PB, GN_UDP_READ, sp[1]);
    w(PB + 32, 2, 1);
    CHECK(call(PB, 0) == MT_COMMAND_TIMEOUT);
    for (int i = 0; i < 2; i++) {
        pb_new(PB, GN_UDP_RELEASE, sp[i]);
        CHECK(call(PB, 0) == 0 && r(PB + 32, 4) == (uint32_t)(i ? RCVB : RCVA));
    }
    pb_new(PB, GN_UDP_MTU, sp[0]);
    CHECK(call(PB, 0) == MT_INVALID_STREAM);
}

static void test_dnr(void) {
    uint32_t ip = 0;
    CHECK(gn_mactcp_str_to_addr(mt, "10.0.0.1", &ip, 0) == 0 && ip == 0x0A000001u);
    CHECK(gn_mactcp_str_to_addr(mt, "localhost", &ip, 3000) == 0 && ip == 0x7F000001u);
    CHECK(gn_mactcp_str_to_addr(mt, "localhost", &ip, 0) == 0); /* cached */
    CHECK(gn_mactcp_str_to_addr(mt, "no such!name", &ip, 100) == MT_NAME_SYNTAX);
    CHECK(gn_mactcp_str_to_addr(mt, NULL, &ip, 100) == MT_PARAM_ERR);
}

static void test_denied(uint16_t port) {
    GnPolicy p;
    gn_policy_init(&p);
    gn_policy_parse(&p, "allow out tcp * *\nlocal on\n"); /* no legacy profile */
    GnBroker *b = gn_broker_new(&p, "Fetch", NULL, NULL, NULL);
    GnMacTcp *saved = mt;
    mt = gn_mactcp_new(b, NULL, 0x0A000202u);
    uint32_t sp = tcp_create(RCVA, 8192);
    CHECK(sp && active_open(sp, 0x7F000001u, port) == MT_OPEN_FAILED);
    gn_mactcp_free(mt);
    gn_broker_free(b);
    mt = saved;
}

int main(void) {
    GnEcho e;
    CHECK(gn_echo_start(&e, AF_INET, 0) == 0);
    GnPolicy p = gn_open_policy();
    GnBroker *b = gn_broker_new(&p, "Fetch", NULL, NULL, NULL);
    GnResolver *res_ = gn_resolver_new();
    mt = gn_mactcp_new(b, res_, 0x0A000202u);
    test_tcp(e.port);
    test_passive();
    test_udp();
    test_dnr();
    test_denied(e.port);
    gn_mactcp_free(mt);
    gn_resolver_free(res_);
    gn_broker_free(b);
    gn_echo_stop(&e);
    usleep(200000);
    return gn_done("test_gn_mactcp");
}
