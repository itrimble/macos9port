/* BNEP: Ethernet encapsulation and PAN socket routing. */
#include "bt_test.h"

static const uint8_t peer_mac[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
static const uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

static int server_opened;
static uint8_t got_frame[256];
static size_t got_len;

static void on_open(void *ctx, BtBnepChan *ch) {
    (void)ctx; (void)ch;
    server_opened++;
}

static void on_frame(void *ctx, BtBnepChan *ch, const uint8_t *frame, size_t len) {
    (void)ctx; (void)ch;
    if (len > sizeof got_frame) len = sizeof got_frame;
    memcpy(got_frame, frame, len);
    got_len = len;
}

/* Bring up a BNEP link over L2CAP and finish the connection handshake. */
static int setup_bnep(BtWire *w, BtBnep **client, BtBnep **server) {
    bt_wire_init(w);
    *server = bt_bnep_new(w->b);
    if (!*server) return -1;
    if (bt_bnep_listen(*server, on_open, NULL) != BT_OK) return -1;
    *client = bt_bnep_new(w->a);
    if (!*client) return -1;
    int cid = -1;
    if (bt_bnep_connect(*client, w->handle, NULL, &cid) != BT_OK) return -1;
    bt_wire_run(w, 16);
    return cid;
}

static void test_bnep_connects_and_carries_frames(void) {
    BtWire w;
    BtBnep *client = NULL, *server = NULL;
    int cid = setup_bnep(&w, &client, &server);
    CHECK(cid >= 0);
    CHECK(server_opened == 1);

    BtBnepInfo info;
    CHECK(bt_bnep_info(client, cid, &info) == BT_OK);
    CHECK(info.up == 1);
    CHECK(memcmp(info.dest_mac, peer_mac, 6) == 0);

    bt_bnep_set_frame_fn(server, on_frame, NULL);

    /* An Ethernet frame with an IPv4 payload. */
    uint8_t payload[8] = {0x45, 0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00};
    CHECK(bt_bnep_send(client, cid, peer_mac, our_mac, BT_BNEP_ETHERTYPE_IPV4,
                        payload, sizeof payload) == BT_OK);
    bt_wire_run(&w, 6);

    CHECK(got_len == 14 + sizeof payload);
    CHECK(memcmp(got_frame, peer_mac, 6) == 0);
    CHECK(memcmp(got_frame + 6, our_mac, 6) == 0);
    CHECK(got_frame[12] == 0x08 && got_frame[13] == 0x00);
    CHECK(memcmp(got_frame + 14, payload, sizeof payload) == 0);

    bt_bnep_free(client);
    bt_bnep_free(server);
    bt_wire_free(&w);
}

static void test_multicast_filter(void) {
    BtWire w;
    BtBnep *client = NULL, *server = NULL;
    int cid = setup_bnep(&w, &client, &server);
    CHECK(cid >= 0);

    uint8_t group[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    CHECK(bt_bnep_set_filter(client, cid, BT_BNEP_FILTER_ALL, group) == BT_OK);
    bt_wire_run(&w, 6);
    CHECK(bt_bnep_set_filter(client, 999, BT_BNEP_FILTER_ALL, group) == BT_ENOENT);

    bt_bnep_free(client);
    bt_bnep_free(server);
    bt_wire_free(&w);
}

static void test_pan_socket_routes_frames(void) {
    BtWire w;
    BtBnep *client = NULL, *server = NULL;
    int cid = setup_bnep(&w, &client, &server);
    CHECK(cid >= 0);

    BtPan *pan = bt_pan_new(client);
    CHECK(pan != NULL);
    int id = -1;
    /* The BNEP channel to this peer is reused, so no second link opens. */
    CHECK(bt_pan_open(pan, w.handle, NULL, peer_mac, &id) == BT_OK);
    CHECK(id >= 0);

    BtPanInfo info;
    CHECK(bt_pan_info(pan, id, &info) == BT_OK);
    CHECK(memcmp(info.peer_mac, peer_mac, 6) == 0);

    /* The peer sends a frame; the PAN routes it by source MAC. */
    bt_bnep_set_frame_fn(server, on_frame, NULL);
    uint8_t payload[4] = {1, 2, 3, 4};
    CHECK(bt_bnep_send(server, cid, our_mac, peer_mac, BT_BNEP_ETHERTYPE_ARP,
                       payload, sizeof payload) == BT_OK);
    bt_wire_run(&w, 6);

    uint8_t frame[128];
    long n = bt_pan_recv(pan, id, frame, sizeof frame);
    CHECK(n == 14 + 4);
    CHECK(memcmp(frame + 6, peer_mac, 6) == 0);

    /* A frame from an unknown peer is not routed anywhere. */
    uint8_t other[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    CHECK(bt_pan_route(pan, other, frame, 18) == BT_ENOENT);

    /* Sending through the socket reaches the peer. */
    bt_bnep_set_frame_fn(server, on_frame, NULL);
    memset(got_frame, 0, sizeof got_frame);
    got_len = 0;
    CHECK(bt_pan_send(pan, id, frame, 18) == 18);
    bt_wire_run(&w, 6);
    CHECK(got_len == 18);

    CHECK(bt_pan_close(pan, id) == BT_OK);
    CHECK(bt_pan_info(pan, id, &info) == BT_ENOENT);

    bt_pan_free(pan);
    bt_bnep_free(client);
    bt_bnep_free(server);
    bt_wire_free(&w);
}

static void test_pan_rejects_bad_input(void) {
    BtWire w;
    BtBnep *client = NULL, *server = NULL;
    setup_bnep(&w, &client, &server);
    BtPan *pan = bt_pan_new(client);
    int id = -1;
    CHECK(bt_pan_open(pan, w.handle, NULL, NULL, &id) == BT_EINVAL);
    CHECK(bt_pan_send(pan, 0, "x", 1) == BT_ENOENT);
    CHECK(bt_pan_recv(pan, 0, (char[8]){0}, 8) == BT_ENOENT);
    CHECK(bt_pan_info(pan, 0, NULL) == BT_EINVAL);
    CHECK(bt_pan_close(pan, 0) == BT_ENOENT);
    /* A frame shorter than an Ethernet header is refused. */
    int pid = -1;
    CHECK(bt_pan_open(pan, w.handle, NULL, peer_mac, &pid) == BT_OK);
    CHECK(bt_pan_send(pan, pid, "short", 5) == BT_EINVAL);
    bt_pan_free(pan);
    bt_bnep_free(client);
    bt_bnep_free(server);
    bt_wire_free(&w);
}

int main(void) {
    test_bnep_connects_and_carries_frames();
    test_multicast_filter();
    test_pan_socket_routes_frames();
    test_pan_rejects_bad_input();
    return bt_done("test_bt_bnep");
}
