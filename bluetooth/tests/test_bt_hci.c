/* HCI packet codec and command engine. */
#include "bt_test.h"

static int sink_events, sink_acl;
static uint8_t last_event_code;
static uint16_t last_acl_handle;

static void on_event(void *ctx, uint8_t code, const uint8_t *params, size_t len) {
    (void)ctx; (void)params; (void)len;
    sink_events++;
    last_event_code = code;
}

static void on_acl(void *ctx, uint16_t handle, uint8_t flags, const uint8_t *data, size_t len) {
    (void)ctx; (void)flags; (void)data; (void)len;
    sink_acl++;
    last_acl_handle = handle;
}

static void test_command_roundtrip(void) {
    uint8_t params[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t out[16];
    size_t k = bt_hci_encode_command(0x0405, params, sizeof params, out, sizeof out);
    CHECK(k == 8);
    CHECK(out[0] == BT_PKT_COMMAND);
    CHECK(out[1] == 0x04 && out[2] == 0x05);
    CHECK(out[3] == 4);
    CHECK(memcmp(out + 4, params, sizeof params) == 0);

    BtHciPacket p;
    CHECK(bt_hci_decode(out, k, &p) == BT_OK);
    CHECK(p.kind == BT_PKT_COMMAND);
    CHECK(p.opcode == 0x0405);
    CHECK(p.len == 4);
    CHECK(memcmp(p.data, params, sizeof params) == 0);
}

static void test_acl_roundtrip(void) {
    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[32];
    size_t k = bt_hci_encode_acl(0x0042, 0x02, payload, sizeof payload, out, sizeof out);
    CHECK(k == 14);
    BtHciPacket p;
    CHECK(bt_hci_decode(out, k, &p) == BT_OK);
    CHECK(p.kind == BT_PKT_ACL);
    CHECK(p.handle == 0x0042);
    CHECK(p.flags == 0x02);
    CHECK(p.len == 8);
    CHECK(memcmp(p.data, payload, sizeof payload) == 0);
}

static void test_event_roundtrip(void) {
    uint8_t params[3] = {0x11, 0x22, 0x33};
    uint8_t out[16];
    size_t k = bt_hci_encode_event(0x03, params, sizeof params, out, sizeof out);
    CHECK(k == 6);
    BtHciPacket p;
    CHECK(bt_hci_decode(out, k, &p) == BT_OK);
    CHECK(p.kind == BT_PKT_EVENT);
    CHECK(p.code == 0x03);
    CHECK(p.len == 3);
    CHECK(memcmp(p.data, params, sizeof params) == 0);
}

static void test_decode_rejects_garbage(void) {
    BtHciPacket p;
    uint8_t bad_type[4] = {0x09, 0x00, 0x00, 0x00};
    CHECK(bt_hci_decode(bad_type, sizeof bad_type, &p) == BT_EINVAL);
    uint8_t short_cmd[2] = {0x01, 0x04};
    CHECK(bt_hci_decode(short_cmd, sizeof short_cmd, &p) == BT_EINVAL);
    /* A command that claims more parameters than it carries. */
    uint8_t lying[5] = {0x01, 0x04, 0x05, 0x08, 0x00};
    CHECK(bt_hci_decode(lying, sizeof lying, &p) == BT_EINVAL);
    CHECK(bt_hci_decode(NULL, 0, &p) == BT_EINVAL);
}

static void test_encode_bounds(void) {
    uint8_t out[4];
    uint8_t big[300];
    memset(big, 0xAB, sizeof big);
    /* A command parameter block longer than one byte does not fit. */
    CHECK(bt_hci_encode_command(0x0001, big, sizeof big, out, sizeof out) == 0);
    CHECK(bt_hci_encode_event(0x01, big, sizeof big, out, sizeof out) == 0);
    CHECK(bt_hci_encode_command(0x0001, NULL, 0, out, sizeof out) == 4);
}

static void test_engine_sends_commands(void) {
    BtFakeTr tr;
    bt_fake_init(&tr);
    BtHciTransport transport = bt_fake_transport(&tr);
    BtHci *h = bt_hci_new(&transport, NULL, NULL);
    CHECK(h != NULL);

    CHECK(bt_hci_reset(h) == BT_OK);
    BtLastCommand cmd;
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0C03);
    CHECK(cmd.len == 0);

    BtAddress addr;
    CHECK(bt_addr_parse("00:11:22:33:44:55", &addr) == BT_OK);
    CHECK(bt_hci_create_connection(h, &addr, 0xCC18) == BT_OK);
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0405);
    CHECK(cmd.len == 14);
    CHECK(memcmp(cmd.params, addr.b, 6) == 0);

    CHECK(bt_hci_write_local_name(h, "OS999") == BT_OK);
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0C13);
    CHECK(cmd.len == 248);
    CHECK(memcmp(cmd.params, "OS999", 5) == 0);

    CHECK(bt_hci_disconnect(h, 0x0011, 0x13) == BT_OK);
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0406);
    CHECK(cmd.len == 3);

    bt_hci_free(h);
}

static void test_engine_dispatch(void) {
    BtFakeTr tr;
    bt_fake_init(&tr);
    BtHciTransport transport = bt_fake_transport(&tr);
    BtHci *h = bt_hci_new(&transport, NULL, NULL);
    CHECK(h != NULL);

    BtHciSink sink;
    sink.on_event = on_event;
    sink.on_acl = on_acl;
    sink.ctx = NULL;
    bt_hci_set_sink(h, &sink);

    sink_events = 0;
    sink_acl = 0;

    uint8_t ev[4] = {0x03, 0x0D, 0xAA, 0xBB};
    bt_fake_push_event(&tr, 0x2F, ev, sizeof ev);
    bt_fake_push_acl(&tr, 0x0033, 0x02, ev, sizeof ev);

    int n = bt_hci_pump(h, 0);
    CHECK(n == 2);
    CHECK(sink_events == 1);
    CHECK(last_event_code == 0x2F);
    CHECK(sink_acl == 1);
    CHECK(last_acl_handle == 0x0033);

    bt_hci_free(h);
}

static void test_engine_wait_status(void) {
    BtFakeTr tr;
    bt_fake_init(&tr);
    BtHciTransport transport = bt_fake_transport(&tr);
    BtHci *h = bt_hci_new(&transport, NULL, NULL);
    CHECK(h != NULL);

    CHECK(bt_hci_reset(h) == BT_OK);
    /* The controller answers with a Command Complete. */
    uint8_t cc[4] = {0x01, 0x0C, 0x03, 0x00};
    bt_fake_push_event(&tr, 0x0C, cc, sizeof cc);
    CHECK(bt_hci_wait(h, 0x0C03, 100) == 0);

    /* A failure status comes back unchanged. */
    CHECK(bt_hci_reset(h) == BT_OK);
    uint8_t bad[4] = {0x01, 0x0C, 0x03, 0x0C};
    bt_fake_push_event(&tr, 0x0C, bad, sizeof bad);
    CHECK(bt_hci_wait(h, 0x0C03, 100) == 0x0C);

    bt_hci_free(h);
}

static void test_engine_write_failure(void) {
    BtFakeTr tr;
    bt_fake_init(&tr);
    tr.fail_writes = 1;
    BtHciTransport transport = bt_fake_transport(&tr);
    BtHci *h = bt_hci_new(&transport, NULL, NULL);
    CHECK(h != NULL);
    CHECK(bt_hci_reset(h) == BT_EIO);
    bt_hci_free(h);
}

static void test_engine_rejects_bad_transport(void) {
    BtHciTransport tr;
    memset(&tr, 0, sizeof tr);
    CHECK(bt_hci_new(&tr, NULL, NULL) == NULL);
    CHECK(bt_hci_new(NULL, NULL, NULL) == NULL);
}

int main(void) {
    test_command_roundtrip();
    test_acl_roundtrip();
    test_event_roundtrip();
    test_decode_rejects_garbage();
    test_encode_bounds();
    test_engine_sends_commands();
    test_engine_dispatch();
    test_engine_wait_status();
    test_engine_write_failure();
    test_engine_rejects_bad_transport();
    return bt_done("test_bt_hci");
}
