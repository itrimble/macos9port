/* Independent Bluetooth SIG BNEP setup/control vectors, no peer encoder. */
#include "bt_test.h"
static uint8_t sent[128];
static size_t sent_n;
static int frames, closed, short_write;
static long capture(void *ctx, const void *p, size_t n) {
    (void)ctx;
    if (short_write) return 0;
    if (n > sizeof sent) return -1;
    memcpy(sent, p, n); sent_n = n; return (long)n;
}
static void frame(void *ctx, BtBnepChan *c, const uint8_t *p, size_t n) {
    (void)ctx; (void)c; (void)p; (void)n; frames++;
}
static void close_link(void *ctx, BtBnepChan *c) { (void)ctx; (void)c; closed++; }
int main(void) {
    BtL2cap *l = bt_l2cap_new();
    BtBnep *b = bt_bnep_new(l);
    BtAddress peer = {{0,1,2,3,4,5}};
    BtL2capPipe pipe = {.write = capture};
    int cid = -1;
    CHECK(bt_l2cap_adopt(l, 1, &peer, BT_PSM_BNEP, 1691, &pipe, &cid) == BT_OK);
    CHECK(bt_bnep_adopt(b, cid, &peer) == BT_OK);
    const uint8_t request[] = {1,1,2,0x11,0x16,0x11,0x15};
    CHECK(sent_n == sizeof request && !memcmp(sent, request, sizeof request));
    CHECK(bt_bnep_adopt(b, cid, &peer) == BT_EBUSY);
    bt_bnep_set_frame_fn(b, frame, NULL);
    bt_bnep_set_close_fn(b, close_link, NULL);
    uint8_t ethernet[15] = {0};
    bt_bnep_rx(b, cid, ethernet, sizeof ethernet);
    CHECK(frames == 0); /* No data before successful setup. */
    const uint8_t response[] = {1,2,0,0};
    BtBnepInfo info;
    for (size_t n = 0; n < sizeof response; n++) {
        bt_bnep_rx(b, cid, response, n);
        CHECK(bt_bnep_info(b, cid, &info) == BT_OK && !info.up);
    }
    bt_bnep_rx(b, cid, response, sizeof response);
    CHECK(bt_bnep_info(b, cid, &info) == BT_OK && info.up);
    bt_bnep_rx(b, cid, ethernet, sizeof ethernet);
    CHECK(frames == 1);
    CHECK(bt_bnep_send(b, cid, peer.b, peer.b, 0x0800, ethernet, SIZE_MAX) == BT_ELIMIT);
    short_write = 1;
    CHECK(bt_bnep_send(b, cid, peer.b, peer.b, 0x0800, ethernet, 1) == BT_EIO);
    short_write = 0;
    const uint8_t unknown[] = {1,0xff}, not_understood[] = {1,0,0xff};
    bt_bnep_rx(b, cid, unknown, sizeof unknown);
    CHECK(sent_n == 3 && !memcmp(sent, not_understood, 3));
    /* 0x03/0x04 are filter commands, never disconnect commands. */
    const uint8_t filter[] = {1,3,0,4,8,0,8,6};
    bt_bnep_rx(b, cid, filter, sizeof filter);
    const uint8_t unsupported[] = {1,4,0,1};
    CHECK(sent_n == 4 && !memcmp(sent, unsupported, 4));
    CHECK(bt_bnep_info(b, cid, &info) == BT_OK && info.up && closed == 0);
    const uint8_t bad_uuid[] = {1,1,2,0x99,0x99,0x11,0x15};
    bt_bnep_rx(b, cid, bad_uuid, sizeof bad_uuid);
    CHECK(sent_n == 4 && sent[1] == 2 && sent[3] == 1);
    bt_bnep_rx(b, cid, request, sizeof request);
    CHECK(sent_n == 4 && !memcmp(sent, response, 4));
    for (size_t n = 0; n < sizeof request; n++) {
        sent_n = 0; bt_bnep_rx(b, cid, request, n); CHECK(sent_n == 0);
    }
    const uint8_t bad_size[] = {1,1,3};
    bt_bnep_rx(b, cid, bad_size, sizeof bad_size);
    CHECK(sent_n == 4 && sent[3] == 3);
    const uint8_t bad_src[] = {1,1,2,0x11,0x16,0x99,0x99};
    bt_bnep_rx(b, cid, bad_src, sizeof bad_src);
    CHECK(sent_n == 4 && sent[3] == 2);
    const uint8_t uuid32[] = {1,1,4,0,0,0x11,0x16,0,0,0x11,0x15};
    bt_bnep_rx(b, cid, uuid32, sizeof uuid32);
    CHECK(sent_n == 4 && sent[3] == 0);
    const uint8_t uuid128[] = {1,1,16,
        0,0,0x11,0x16,0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb,
        0,0,0x11,0x15,0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    bt_bnep_rx(b, cid, uuid128, sizeof uuid128);
    CHECK(sent_n == 4 && sent[3] == 0);
    CHECK(bt_l2cap_disconnect(l, cid) == BT_EUNSUP);
    bt_l2cap_pipe_closed(l, cid);
    CHECK(closed == 1);
    CHECK(bt_bnep_info(b, cid, &info) == BT_ENOENT);
    bt_bnep_free(b);
    b = bt_bnep_new(l);
    CHECK(bt_l2cap_adopt(l, 2, &peer, BT_PSM_BNEP, 1691, &pipe, &cid) == BT_OK);
    CHECK(bt_bnep_adopt(b, cid, &peer) == BT_OK);
    const uint8_t denied[] = {1,2,0,4};
    bt_bnep_rx(b, cid, denied, sizeof denied);
    CHECK(bt_bnep_info(b, cid, &info) == BT_OK && !info.up);
    bt_bnep_rx(b, cid, response, sizeof response); /* unsolicited late success */
    CHECK(bt_bnep_info(b, cid, &info) == BT_OK && !info.up);
    bt_bnep_free(b);
    bt_l2cap_pipe_rx(l, cid, response, sizeof response); /* no freed callback */
    bt_l2cap_free(l);
    BtWire wire;
    bt_wire_init(&wire);
    BtBnep *server = bt_bnep_new(wire.b);
    CHECK(bt_bnep_listen(server, NULL, NULL) == BT_OK);
    b = bt_bnep_new(wire.a);
    CHECK(bt_bnep_connect(b, wire.handle, &peer, &cid) == BT_OK);
    bt_bnep_free(b); /* pending open must not call freed protocol context */
    bt_wire_run(&wire, 16);
    bt_bnep_free(server);
    server = bt_bnep_new(wire.b);
    CHECK(bt_bnep_listen(server, NULL, NULL) == BT_OK);
    bt_bnep_free(server);
    bt_wire_free(&wire);
    return bt_done("test_bt_bnep_wire");
}
