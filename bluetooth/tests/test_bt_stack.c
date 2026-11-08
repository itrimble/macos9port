/* End to end: discovery, connection, pairing, RFCOMM and BNEP over a
 * simulated controller. */
#include "bt_test.h"
#include "bt_hci_sim.h"

typedef struct {
    BtSim *sim;
    BtL2cap *peer_l2cap;
    BtStack *host;
    BtRfcomm *peer_rfcomm;
    BtBnep *peer_bnep;
    int rfcomm_opened, bnep_opened;
    uint8_t rfcomm_rx[256];
    size_t rfcomm_rx_len;
    uint8_t bnep_rx[256];
    size_t bnep_rx_len;
} Peer;

static void peer_on_rfcomm_data(void *ctx, BtRfcommChan *ch, const uint8_t *p, size_t n) {
    Peer *peer = ctx;
    (void)ch;
    if (n > sizeof peer->rfcomm_rx) n = sizeof peer->rfcomm_rx;
    memcpy(peer->rfcomm_rx, p, n);
    peer->rfcomm_rx_len = n;
}

static void peer_on_rfcomm_open(void *ctx, BtRfcommChan *ch) {
    Peer *peer = ctx;
    (void)ch;
    peer->rfcomm_opened++;
    bt_rfcomm_set_data_fn(peer->peer_rfcomm, peer_on_rfcomm_data, peer);
}

static void peer_on_rfcomm_l2cap(void *ctx, BtL2capChan *chan) {
    Peer *peer = ctx;
    int cid = bt_l2cap_chan_cid(chan);
    peer->peer_rfcomm = bt_rfcomm_new(peer->peer_l2cap, cid, 1);
    bt_rfcomm_listen(peer->peer_rfcomm, 1, peer_on_rfcomm_open, peer);
}

static void peer_on_bnep_open(void *ctx, BtBnepChan *chan) {
    Peer *peer = ctx;
    (void)chan;
    peer->bnep_opened++;
}

static void peer_on_bnep_frame(void *ctx, BtBnepChan *chan, const uint8_t *frame, size_t n) {
    Peer *peer = ctx;
    (void)chan;
    if (n > sizeof peer->bnep_rx) n = sizeof peer->bnep_rx;
    memcpy(peer->bnep_rx, frame, n);
    peer->bnep_rx_len = n;
}

static void peer_setup(Peer *peer) {
    memset(peer, 0, sizeof *peer);
    peer->sim = bt_sim_new();
    peer->peer_l2cap = bt_l2cap_new();
    bt_l2cap_set_acl_sender(peer->peer_l2cap, (BtAclSendFn)bt_sim_send_acl, peer->sim);
    bt_sim_set_peer_l2cap(peer->sim, peer->peer_l2cap);
    /* The peer accepts RFCOMM on the usual server channel. */
    bt_l2cap_listen(peer->peer_l2cap, BT_PSM_RFCOMM, peer_on_rfcomm_l2cap, peer);
    /* BNEP brings its own L2CAP listener. */
    peer->peer_bnep = bt_bnep_new(peer->peer_l2cap);
    bt_bnep_listen(peer->peer_bnep, peer_on_bnep_open, peer);
    bt_bnep_set_frame_fn(peer->peer_bnep, peer_on_bnep_frame, peer);
}

static void peer_free(Peer *peer) {
    bt_rfcomm_free(peer->peer_rfcomm);
    bt_bnep_free(peer->peer_bnep);
    bt_l2cap_free(peer->peer_l2cap);
    bt_stack_free(peer->host);
    bt_sim_free(peer->sim);
}

static void test_discovery_finds_devices(void) {
    Peer peer;
    peer_setup(&peer);

    BtAddress a, b;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_addr_parse("00:00:00:00:00:08", &b);
    CHECK(bt_sim_add_device(peer.sim, &a, "Peer", 0x00010C, 1) == BT_OK);
    CHECK(bt_sim_add_device(peer.sim, &b, "Other", 0x000104, 0) == BT_OK);

    peer.host = bt_stack_new(bt_sim_transport(peer.sim), NULL, NULL);
    CHECK(peer.host != NULL);

    CHECK(bt_stack_discover(peer.host, 8, 5) == BT_OK);
    bt_stack_pump(peer.host, 0);

    BtDeviceDb *db = bt_stack_devices(peer.host);
    CHECK(bt_db_count(db) == 2);
    const BtDeviceInfo *info = bt_stack_device(peer.host, &a);
    CHECK(info != NULL);
    CHECK(info->class_of_device == 0x00010C);

    peer_free(&peer);
}

static void test_connect_and_pair(void) {
    Peer peer;
    peer_setup(&peer);

    BtAddress a;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_sim_add_device(peer.sim, &a, "Peer", 0x00010C, 1);

    peer.host = bt_stack_new(bt_sim_transport(peer.sim), NULL, NULL);
    CHECK(peer.host != NULL);

    CHECK(bt_stack_connect(peer.host, &a, 0xCC18) == BT_OK);
    bt_stack_pump(peer.host, 0);

    uint16_t handle = 0;
    CHECK(bt_stack_handle_for(peer.host, &a, &handle) == BT_OK);
    CHECK(handle != 0);
    CHECK(bt_sim_link_count(peer.sim) == 1);

    /* The device asks for a PIN. */
    BtPair *pair = bt_stack_pair(peer.host);
    CHECK(bt_pair_begin(pair, &a, handle) == BT_OK);
    bt_stack_pump(peer.host, 0);

    BtPairState st;
    CHECK(bt_pair_state(pair, &a, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_WAIT_PIN);

    const uint8_t pin[4] = {0x31, 0x32, 0x33, 0x34};
    CHECK(bt_pair_set_pin(pair, &a, pin, 4) == BT_OK);
    bt_stack_pump(peer.host, 0);

    CHECK(bt_pair_state(pair, &a, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_PAIRED);
    CHECK(st.bonded == 1);

    peer_free(&peer);
}

static void test_rfcomm_over_the_link(void) {
    Peer peer;
    peer_setup(&peer);

    BtAddress a;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_sim_add_device(peer.sim, &a, "Peer", 0x00010C, 0);

    peer.host = bt_stack_new(bt_sim_transport(peer.sim), NULL, NULL);
    bt_stack_connect(peer.host, &a, 0xCC18);
    bt_stack_pump(peer.host, 0);

    uint16_t handle = 0;
    CHECK(bt_stack_handle_for(peer.host, &a, &handle) == BT_OK);

    BtL2cap *l2 = bt_stack_l2cap(peer.host);
    int cid = -1;
    CHECK(bt_l2cap_connect(l2, handle, &a, BT_PSM_RFCOMM, 2048, NULL, NULL, &cid) == BT_OK);
    bt_stack_pump(peer.host, 0);

    BtL2capInfo l2info;
    CHECK(bt_l2cap_info(l2, cid, &l2info) == BT_OK);
    CHECK(l2info.state == BT_L2CAP_OPEN);

    BtRfcomm *client = bt_rfcomm_new(l2, cid, 0);
    CHECK(client != NULL);
    int dlci = -1;
    CHECK(bt_rfcomm_connect(client, 1, &dlci) == BT_OK);
    bt_stack_pump(peer.host, 0);

    CHECK(peer.rfcomm_opened == 1);
    BtRfcommInfo rinfo;
    CHECK(bt_rfcomm_info(client, dlci, &rinfo) == BT_OK);
    CHECK(rinfo.state == BT_RFCOMM_OPEN);

    const char *msg = "hello from the desktop";
    CHECK(bt_rfcomm_send(client, dlci, msg, strlen(msg)) == (int)strlen(msg));
    bt_stack_pump(peer.host, 0);
    CHECK(peer.rfcomm_rx_len == strlen(msg));
    CHECK(memcmp(peer.rfcomm_rx, msg, peer.rfcomm_rx_len) == 0);

    bt_rfcomm_free(client);
    peer_free(&peer);
}

static void test_bnep_over_the_link(void) {
    Peer peer;
    peer_setup(&peer);

    BtAddress a;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_sim_add_device(peer.sim, &a, "Peer", 0x00010C, 0);

    peer.host = bt_stack_new(bt_sim_transport(peer.sim), NULL, NULL);
    bt_stack_connect(peer.host, &a, 0xCC18);
    bt_stack_pump(peer.host, 0);

    uint16_t handle = 0;
    CHECK(bt_stack_handle_for(peer.host, &a, &handle) == BT_OK);

    BtBnep *client = bt_bnep_new(bt_stack_l2cap(peer.host));
    CHECK(client != NULL);
    int cid = -1;
    CHECK(bt_bnep_connect(client, handle, &a, &cid) == BT_OK);
    bt_stack_pump(peer.host, 0);

    CHECK(peer.bnep_opened == 1);
    BtBnepInfo info;
    CHECK(bt_bnep_info(client, cid, &info) == BT_OK);
    CHECK(info.up == 1);

    uint8_t dst[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t src[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(bt_bnep_send(client, cid, dst, src, BT_BNEP_ETHERTYPE_IPV4, payload, sizeof payload) == BT_OK);
    bt_stack_pump(peer.host, 0);

    CHECK(peer.bnep_rx_len == 14 + sizeof payload);
    CHECK(memcmp(peer.bnep_rx, dst, 6) == 0);
    CHECK(memcmp(peer.bnep_rx + 6, src, 6) == 0);
    CHECK(memcmp(peer.bnep_rx + 14, payload, sizeof payload) == 0);

    bt_bnep_free(client);
    peer_free(&peer);
}

static void test_disconnect_releases_the_link(void) {
    Peer peer;
    peer_setup(&peer);

    BtAddress a;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_sim_add_device(peer.sim, &a, "Peer", 0x00010C, 0);

    peer.host = bt_stack_new(bt_sim_transport(peer.sim), NULL, NULL);
    bt_stack_connect(peer.host, &a, 0xCC18);
    bt_stack_pump(peer.host, 0);

    uint16_t handle = 0;
    CHECK(bt_stack_handle_for(peer.host, &a, &handle) == BT_OK);
    CHECK(bt_stack_disconnect(peer.host, handle) == BT_OK);
    bt_stack_pump(peer.host, 0);

    CHECK(bt_stack_handle_for(peer.host, &a, &handle) == BT_ENOENT);
    CHECK(bt_sim_link_count(peer.sim) == 0);

    peer_free(&peer);
}

int main(void) {
    test_discovery_finds_devices();
    test_connect_and_pair();
    test_rfcomm_over_the_link();
    test_bnep_over_the_link();
    test_disconnect_releases_the_link();
    return bt_done("test_bt_stack");
}
