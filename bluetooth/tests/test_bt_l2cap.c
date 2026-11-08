/* L2CAP: connection setup, data transfer, segmentation and teardown. */
#include "bt_test.h"

static void test_connect_opens_both_sides(void) {
    BtWire w;
    bt_wire_init(&w);
    CHECK(bt_l2cap_listen(w.b, BT_PSM_RFCOMM, bt_wire_on_open_b, &w) == BT_OK);

    int cid = -1;
    CHECK(bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 0, bt_wire_on_open_a, &w, &cid) == BT_OK);
    CHECK(cid >= 0);
    bt_wire_run(&w, 12);

    CHECK(w.opened_a == 1);
    CHECK(w.opened_b == 1);

    BtL2capInfo info;
    CHECK(bt_l2cap_info(w.a, cid, &info) == BT_OK);
    CHECK(info.state == BT_L2CAP_OPEN);
    CHECK(info.psm == BT_PSM_RFCOMM);
    /* Each side owns its own CID space; the peer addresses ours. */
    CHECK(info.cid == cid);
    CHECK(info.remote_cid == w.cid_b);

    bt_wire_free(&w);
}

static void test_data_flows(void) {
    BtWire w;
    bt_wire_init(&w);
    bt_l2cap_listen(w.b, BT_PSM_RFCOMM, bt_wire_on_open_b, &w);
    int cid = -1;
    bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 0, bt_wire_on_open_a, &w, &cid);
    bt_wire_run(&w, 12);
    CHECK(w.opened_a == 1 && w.opened_b == 1);

    const char *msg = "hello l2cap";
    CHECK(bt_l2cap_send(w.a, cid, msg, strlen(msg)) == BT_OK);
    bt_wire_run(&w, 4);

    char buf[64];
    long n = bt_l2cap_recv(w.b, w.cid_b, buf, sizeof buf);
    CHECK(n == (long)strlen(msg));
    CHECK(memcmp(buf, msg, (size_t)n) == 0);

    /* And back the other way. */
    const char *reply = "ack";
    CHECK(bt_l2cap_send(w.b, w.cid_b, reply, strlen(reply)) == BT_OK);
    bt_wire_run(&w, 4);
    n = bt_l2cap_recv(w.a, cid, buf, sizeof buf);
    CHECK(n == (long)strlen(reply));
    CHECK(memcmp(buf, reply, (size_t)n) == 0);

    bt_wire_free(&w);
}

static void test_large_payload_is_reassembled(void) {
    BtWire w;
    bt_wire_init(&w);
    bt_l2cap_listen(w.b, BT_PSM_RFCOMM, bt_wire_on_open_b, &w);
    int cid = -1;
    bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 2048, bt_wire_on_open_a, &w, &cid);
    bt_wire_run(&w, 12);
    CHECK(w.opened_a == 1 && w.opened_b == 1);

    /* Larger than the negotiated MTU, so it is segmented on the wire. */
    static uint8_t payload[3000];
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 7);
    CHECK(bt_l2cap_send(w.a, cid, payload, sizeof payload) == BT_OK);
    /* Enough rounds for every fragment to arrive. */
    bt_wire_run(&w, 40);

    static uint8_t got[6000];
    long n = bt_l2cap_recv(w.b, w.cid_b, got, sizeof got);
    CHECK(n == (long)sizeof payload);
    CHECK(memcmp(got, payload, sizeof payload) == 0);

    bt_wire_free(&w);
}

static void test_disconnect_closes_channel(void) {
    BtWire w;
    bt_wire_init(&w);
    bt_l2cap_listen(w.b, BT_PSM_RFCOMM, bt_wire_on_open_b, &w);
    int cid = -1;
    bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 0, bt_wire_on_open_a, &w, &cid);
    bt_wire_run(&w, 12);
    CHECK(w.opened_a == 1 && w.opened_b == 1);

    CHECK(bt_l2cap_disconnect(w.a, cid) == BT_OK);
    bt_wire_run(&w, 6);

    BtL2capInfo info;
    CHECK(bt_l2cap_info(w.a, cid, &info) == BT_ENOENT);
    CHECK(bt_l2cap_recv(w.b, w.cid_b, (char[8]){0}, 8) == BT_ENOENT);

    bt_wire_free(&w);
}

static void test_unsupported_psm_is_refused(void) {
    BtWire w;
    bt_wire_init(&w);
    /* Nothing listens on this PSM. */
    int cid = -1;
    CHECK(bt_l2cap_connect(w.a, w.handle, NULL, 0x0099, 0, NULL, NULL, &cid) == BT_OK);
    bt_wire_run(&w, 12);
    CHECK(w.opened_a == 0);
    CHECK(w.opened_b == 0);
    BtL2capInfo info;
    CHECK(bt_l2cap_info(w.a, cid, &info) == BT_ENOENT);

    bt_wire_free(&w);
}

static void test_send_before_open_is_refused(void) {
    BtWire w;
    bt_wire_init(&w);
    int cid = -1;
    bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 0, NULL, NULL, &cid);
    /* The channel is still waiting for the connection response. */
    CHECK(bt_l2cap_send(w.a, cid, "x", 1) == BT_ECLOSED);
    CHECK(bt_l2cap_disconnect(w.a, 9999) == BT_ENOENT);
    CHECK(bt_l2cap_info(w.a, 9999, NULL) == BT_EINVAL);
    bt_wire_free(&w);
}

static void test_listen_rejects_duplicate_psm(void) {
    BtWire w;
    bt_wire_init(&w);
    CHECK(bt_l2cap_listen(w.b, BT_PSM_BNEP, bt_wire_on_open_b, &w) == BT_OK);
    CHECK(bt_l2cap_listen(w.b, BT_PSM_BNEP, bt_wire_on_open_b, &w) == BT_EBUSY);
    CHECK(bt_l2cap_listen(w.b, 0, bt_wire_on_open_b, &w) == BT_EINVAL);
    bt_wire_free(&w);
}

static void test_link_down_closes_channels(void) {
    BtWire w;
    bt_wire_init(&w);
    bt_l2cap_listen(w.b, BT_PSM_RFCOMM, bt_wire_on_open_b, &w);
    int cid = -1;
    bt_l2cap_connect(w.a, w.handle, NULL, BT_PSM_RFCOMM, 0, bt_wire_on_open_a, &w, &cid);
    bt_wire_run(&w, 12);
    CHECK(w.opened_a == 1);

    bt_l2cap_link_down(w.a, w.handle);
    BtL2capInfo info;
    CHECK(bt_l2cap_info(w.a, cid, &info) == BT_ENOENT);

    bt_wire_free(&w);
}

static uint8_t pipe_out[256];
static size_t pipe_out_len;
static uint8_t adopted_got[128];
static int adopted_got_len;

static long pipe_write(void *ctx, const void *p, size_t n) {
    (void)ctx;
    if (n > sizeof pipe_out) n = sizeof pipe_out;
    memcpy(pipe_out, p, n);
    pipe_out_len = n;
    return (long)n;
}

static long pipe_read(void *ctx, void *p, size_t cap) {
    (void)ctx; (void)p; (void)cap;
    return 0;
}

static void adopted_data(void *ctx, BtL2capChan *ch, const uint8_t *p, size_t n) {
    (void)ctx; (void)ch;
    if (n > sizeof adopted_got) n = sizeof adopted_got;
    memcpy(adopted_got, p, n);
    adopted_got_len = (int)n;
}

/* A channel the operating system opened for us carries the payload
 * without an L2CAP header. */
static void test_adopted_channel(void) {
    BtWire w;
    bt_wire_init(&w);
    BtL2capPipe pipe;
    pipe.write = pipe_write;
    pipe.read = pipe_read;
    pipe.ctx = NULL;

    int cid = -1;
    CHECK(bt_l2cap_adopt(w.a, w.handle, NULL, BT_PSM_RFCOMM, 1024, &pipe, &cid) == BT_OK);
    CHECK(cid >= 0);

    BtL2capInfo info;
    CHECK(bt_l2cap_info(w.a, cid, &info) == BT_OK);
    CHECK(info.state == BT_L2CAP_OPEN);
    CHECK(info.psm == BT_PSM_RFCOMM);

    bt_l2cap_set_chan_data_fn(bt_l2cap_chan(w.a, cid), adopted_data, NULL);

    const char *msg = "from the system";
    bt_l2cap_pipe_rx(w.a, cid, (const uint8_t *)msg, strlen(msg));
    CHECK(adopted_got_len == (int)strlen(msg));
    CHECK(memcmp(adopted_got, msg, strlen(msg)) == 0);

    /* The stack writes straight into the pipe. */
    CHECK(bt_l2cap_send(w.a, cid, "out", 3) == BT_OK);
    CHECK(pipe_out_len == 3);
    CHECK(memcmp(pipe_out, "out", 3) == 0);

    bt_wire_free(&w);
}

int main(void) {
    test_connect_opens_both_sides();
    test_data_flows();
    test_large_payload_is_reassembled();
    test_disconnect_closes_channel();
    test_unsupported_psm_is_refused();
    test_send_before_open_is_refused();
    test_listen_rejects_duplicate_psm();
    test_link_down_closes_channels();
    test_adopted_channel();
    return bt_done("test_bt_l2cap");
}
