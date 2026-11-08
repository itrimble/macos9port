/* The guest network stack over a Bluetooth personal area network: a guest
 * MacTCP stream crosses BNEP, L2CAP and the controller simulator, and the
 * bytes come back the other way. */
#include "bt_test.h"
#include "bt_hci_sim.h"
#include "os999_bt_panif.h"

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define HOST_IP 0x0A000101u /* 10.0.1.1 */
#define PEER_IP 0x0A000102u /* 10.0.1.2 */
#define PEER_PORT 8080
#define GN_STREAM_BASE 0x4E540000u

enum { RAM = 1 << 20, PB = 0x1000, RCVA = 0x10000, BUF = 0x20000, WDS = 0x30000, DATA = 0x31000 };
static uint8_t ram[RAM];
static GnMem mem = {ram, RAM};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Big-endian guest RAM. */
static void w(uint32_t a, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) {
        ram[a + (uint32_t)i] = (uint8_t)v;
        v >>= 8;
    }
}
static uint32_t r(uint32_t a, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = v << 8 | ram[a + (uint32_t)i];
    return v;
}
static int16_t res(uint32_t pb) { return (int16_t)r(pb + 16, 2); }
static void pb_new(uint32_t pb, int code, uint32_t stream) {
    memset(ram + pb, 0, 128);
    w(pb + 24, 2, 0xFFC0);
    w(pb + 26, 2, (uint32_t)code);
    w(pb + 28, 4, stream);
}

/* ---- the far end of the personal area network ---- */

typedef struct {
    BtSim *sim;
    BtL2cap *peer_l2cap;
    BtStack *host;
    BtBnep *peer_bnep;
    BtPan *peer_pan;
    BtPanIf *peer_if;
    GnPanTcp *peer_srv;
    GnPanTcp *peer_conn;
    BtAddress host_addr, peer_addr;
    uint8_t host_mac[6], peer_mac[6];
    int bnep_opened;
} Peer;

static void peer_on_bnep_open(void *ctx, BtBnepChan *chan) {
    Peer *p = ctx;
    (void)chan;
    p->bnep_opened++;
}

static void peer_setup(Peer *p) {
    memset(p, 0, sizeof *p);
    bt_addr_parse("00:00:00:00:00:07", &p->peer_addr);
    bt_addr_parse("00:11:22:33:44:55", &p->host_addr);
    p->sim = bt_sim_new();
    p->peer_l2cap = bt_l2cap_new();
    bt_l2cap_set_acl_sender(p->peer_l2cap, (BtAclSendFn)bt_sim_send_acl, p->sim);
    bt_sim_set_peer_l2cap(p->sim, p->peer_l2cap);
    /* Each end uses its own device address on the link. */
    memcpy(p->peer_mac, p->peer_addr.b, 6);
    memcpy(p->host_mac, p->host_addr.b, 6);

    p->peer_bnep = bt_bnep_new(p->peer_l2cap);
    bt_bnep_listen(p->peer_bnep, peer_on_bnep_open, p);
}

static void peer_free(Peer *p) {
    if (p->peer_conn) gn_pan_tcp_free(p->peer_conn);
    if (p->peer_srv) gn_pan_tcp_free(p->peer_srv);
    if (p->peer_if) bt_panif_free(p->peer_if);
    if (p->peer_pan) bt_pan_free(p->peer_pan);
    bt_bnep_free(p->peer_bnep);
    bt_l2cap_free(p->peer_l2cap);
    bt_stack_free(p->host);
    bt_sim_free(p->sim);
}

/* ---- the guest ---- */

typedef struct {
    Peer peer;
    BtBnep *host_bnep;
    BtPan *host_pan;
    BtPanIf *host_if;
    GnBroker *broker;
    GnMacTcp *mt;
    int pan_id, host_cid;
    uint16_t handle;
} Host;

static GnGuestCall calls[256];
static int ncalls;

static void host_pump(Host *h) {
    bt_stack_pump(h->peer.host, 0);
    if (h->host_if) bt_panif_pump(h->host_if);
    if (h->peer.peer_if) bt_panif_pump(h->peer.peer_if);
    gn_pump(h->broker, 0);
    if (h->mt) {
        int cap = 256 - ncalls;
        if (cap > 0) ncalls += gn_mactcp_poll(h->mt, &mem, now_ms(), calls + ncalls, cap);
    }
}

/* The far end answers whatever the guest sends. */
static void peer_serve(Peer *p) {
    if (!p->peer_conn && p->peer_srv) {
        GnPanTcp *c = NULL;
        if (gn_pan_tcp_accept(p->peer_srv, &c) == GN_OK) p->peer_conn = c;
    }
    if (!p->peer_conn) return;
    char buf[256];
    long k = gn_pan_tcp_recv(p->peer_conn, buf, sizeof buf);
    if (k > 0) gn_pan_tcp_send(p->peer_conn, buf, (size_t)k);
}

static void host_setup(Host *h) {
    memset(h, 0, sizeof *h);
    peer_setup(&h->peer);

    h->peer.host = bt_stack_new(bt_sim_transport(h->peer.sim), NULL, NULL);
    bt_sim_add_device(h->peer.sim, &h->peer.peer_addr, "Peer", 0x00010C, 0);
    CHECK(bt_stack_connect(h->peer.host, &h->peer.peer_addr, 0xCC18) == BT_OK);
    bt_stack_pump(h->peer.host, 0);
    CHECK(bt_stack_handle_for(h->peer.host, &h->peer.peer_addr, &h->handle) == BT_OK);

    /* The host opens the network link to the peer. */
    h->host_bnep = bt_bnep_new(bt_stack_l2cap(h->peer.host));
    CHECK(bt_bnep_connect(h->host_bnep, h->handle, &h->peer.peer_addr, &h->host_cid) == BT_OK);
    BtBnepInfo binfo;
    memset(&binfo, 0, sizeof binfo);
    for (int i = 0; i < 20; i++) {
        bt_stack_pump(h->peer.host, 0);
        if (bt_bnep_info(h->host_bnep, h->host_cid, &binfo) == BT_OK && binfo.up) break;
    }
    CHECK(binfo.up == 1);
    CHECK(h->peer.bnep_opened == 1);

    /* Both ends use their own address on the link. */
    h->host_pan = bt_pan_new(h->host_bnep);
    CHECK(h->host_pan != NULL);
    bt_pan_set_local_mac(h->host_pan, h->peer.host_mac);
    CHECK(bt_pan_open(h->host_pan, h->handle, &h->peer.peer_addr, h->peer.peer_mac, &h->pan_id) ==
          BT_OK);

    h->peer.peer_pan = bt_pan_new(h->peer.peer_bnep);
    CHECK(h->peer.peer_pan != NULL);
    bt_pan_set_local_mac(h->peer.peer_pan, h->peer.peer_mac);
    int peer_pan_id = 0;
    CHECK(bt_pan_open(h->peer.peer_pan, h->handle, &h->peer.host_addr, h->peer.host_mac,
                      &peer_pan_id) == BT_OK);

    /* The guest IP stack sits on the link, and so does the far end. */
    h->host_if = bt_panif_new(h->host_pan, h->pan_id, HOST_IP, 0xFFFFFF00u, NULL);
    CHECK(h->host_if != NULL);
    CHECK(bt_panif_set_peer(h->host_if, PEER_IP, h->peer.peer_mac) == GN_OK);

    h->peer.peer_if = bt_panif_new(h->peer.peer_pan, peer_pan_id, PEER_IP, 0xFFFFFF00u, NULL);
    CHECK(h->peer.peer_if != NULL);
    CHECK(bt_panif_set_peer(h->peer.peer_if, HOST_IP, h->peer.host_mac) == GN_OK);

    h->peer.peer_srv = gn_pan_tcp_new(bt_panif_pan(h->peer.peer_if));
    CHECK(h->peer.peer_srv != NULL);
    CHECK(gn_pan_tcp_listen(h->peer.peer_srv, PEER_PORT) == GN_OK);

    GnPolicy policy;
    gn_policy_init(&policy);
    gn_policy_parse(&policy, "legacy on\nlocal on\nallow both any * *\n");
    h->broker = gn_broker_new(&policy, "Fetch", NULL, NULL, NULL);
    CHECK(gn_broker_set_pan(h->broker, bt_panif_pan(h->host_if)) == GN_OK);
    h->mt = gn_mactcp_new(h->broker, NULL, 0);
    CHECK(h->mt != NULL);
}

static void host_free(Host *h) {
    gn_mactcp_free(h->mt);
    gn_broker_free(h->broker);
    bt_panif_free(h->host_if);
    bt_pan_free(h->host_pan);
    bt_bnep_free(h->host_bnep);
    peer_free(&h->peer);
}

/* ---- the test ---- */

static void test_guest_tcp_over_the_pan(void) {
    Host h;
    host_setup(&h);

    /* The guest asks for its address: it is the address on the PAN. */
    pb_new(PB, GN_IPCTL_GETADDR, 0);
    gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());
    CHECK(res(PB) == 0);
    CHECK(r(PB + 28, 4) == HOST_IP);
    CHECK(r(PB + 32, 4) == 0xFFFFFF00u);

    /* The guest creates a stream and opens a connection to the peer. */
    pb_new(PB, GN_TCP_CREATE, 0);
    w(PB + 32, 4, RCVA);
    w(PB + 36, 4, 8192);
    w(PB + 40, 4, 0x400);
    w(PB + 44, 4, 0xABCD);
    gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());
    CHECK(res(PB) == 0);
    uint32_t sp = r(PB + 28, 4);
    CHECK(sp >= GN_STREAM_BASE);

    pb_new(PB, GN_TCP_ACTIVE_OPEN, sp);
    w(PB + 35, 1, 5);
    w(PB + 36, 4, PEER_IP);
    w(PB + 40, 2, PEER_PORT);
    gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());

    uint64_t end = now_ms() + 5000;
    while (res(PB) == 1 && now_ms() < end) {
        host_pump(&h);
        peer_serve(&h.peer);
        usleep(1000);
    }
    CHECK(res(PB) == 0);
    CHECK(r(PB + 42, 4) == HOST_IP);
    CHECK(r(PB + 46, 2) != 0);

    /* The far end accepted the connection. */
    CHECK(h.peer.peer_conn != NULL);

    /* The guest sends and the bytes come back. */
    memcpy(ram + DATA, "ping over bluetooth", 19);
    w(WDS, 2, 19);
    w(WDS + 2, 4, DATA);
    w(WDS + 6, 2, 0);
    pb_new(PB, GN_TCP_SEND, sp);
    w(PB + 38, 4, WDS);
    gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());
    end = now_ms() + 5000;
    while (res(PB) == 1 && now_ms() < end) {
        host_pump(&h);
        peer_serve(&h.peer);
        usleep(1000);
    }
    CHECK(res(PB) == 0);

    char got[64] = {0};
    int total = 0;
    for (int tries = 0; tries < 200 && total < 19; tries++) {
        pb_new(PB, GN_TCP_RCV, sp);
        w(PB + 32, 1, 2);
        w(PB + 36, 4, BUF);
        w(PB + 40, 2, 64);
        gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());
        end = now_ms() + 5000;
        while (res(PB) == 1 && now_ms() < end) {
            host_pump(&h);
            peer_serve(&h.peer);
            usleep(1000);
        }
        if (res(PB) != 0) break;
        int k = (int)r(PB + 40, 2);
        memcpy(got + total, ram + BUF, (size_t)k);
        total += k;
    }
    CHECK(total == 19);
    CHECK(memcmp(got, "ping over bluetooth", 19) == 0);

    /* The guest closes and the far end sees it. */
    pb_new(PB, GN_TCP_CLOSE, sp);
    w(PB + 32, 1, 5);
    w(PB + 34, 1, 0x80);
    gn_mactcp_control(h.mt, &mem, PB, 0, now_ms());
    end = now_ms() + 5000;
    while (res(PB) == 1 && now_ms() < end) {
        host_pump(&h);
        peer_serve(&h.peer);
        usleep(1000);
    }
    CHECK(res(PB) == 0);

    host_free(&h);
}

int main(void) {
    test_guest_tcp_over_the_pan();
    return bt_done("test_bt_pan_guest");
}
