/* Pairing: PIN entry, authentication, encryption and bonding. */
#include "bt_test.h"

#define PIN_REQUEST 0x12
#define AUTH_COMPLETE 0x06
#define ENC_CHANGE 0x08
#define LINK_KEY_NOTIFY 0x17

static BtFakeTr tr;
static BtHci *hci;

static void setup(void) {
    bt_fake_init(&tr);
    BtHciTransport transport = bt_fake_transport(&tr);
    hci = bt_hci_new(&transport, NULL, NULL);
}

static void teardown(void) {
    bt_hci_free(hci);
    hci = NULL;
}

static void test_pin_flow_reaches_paired(void) {
    setup();
    BtPair *p = bt_pair_new(hci);
    CHECK(p != NULL);

    BtAddress peer;
    CHECK(bt_addr_parse("00:00:00:00:00:07", &peer) == BT_OK);

    BtPairState st;
    CHECK(bt_pair_state(p, &peer, &st) == BT_ENOENT);
    CHECK(st.state == BT_PAIR_IDLE);

    /* Start authenticating: the controller is asked to authenticate. */
    CHECK(bt_pair_begin(p, &peer, 0x0042) == BT_OK);
    BtLastCommand cmd;
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0411);

    /* The controller asks for a PIN. */
    uint8_t ev[6];
    memcpy(ev, peer.b, 6);
    bt_pair_on_event(p, PIN_REQUEST, ev, sizeof ev);
    CHECK(bt_pair_state(p, &peer, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_WAIT_PIN);

    /* The user types the PIN. */
    const uint8_t pin[4] = {1, 2, 3, 4};
    CHECK(bt_pair_set_pin(p, &peer, pin, 4) == BT_OK);
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x040D);
    CHECK(cmd.params[0] == 4);
    CHECK(memcmp(cmd.params + 1, pin, 4) == 0);

    /* Authentication succeeded: the link is encrypted next. */
    uint8_t auth[3] = {0x00, 0x00, 0x42};
    bt_pair_on_event(p, AUTH_COMPLETE, auth, sizeof auth);
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x0413);
    CHECK(bt_pair_state(p, &peer, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_ENCRYPTING);

    /* Encryption came up: the link is bonded. */
    uint8_t enc[4] = {0x00, 0x00, 0x42, 0x01};
    bt_pair_on_event(p, ENC_CHANGE, enc, sizeof enc);
    CHECK(bt_pair_state(p, &peer, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_PAIRED);
    CHECK(st.bonded == 1);
    CHECK(st.encrypted == 1);
    CHECK(st.authenticated == 1);

    bt_pair_free(p);
    teardown();
}

static void test_authentication_failure(void) {
    setup();
    BtPair *p = bt_pair_new(hci);
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:08", &peer);

    CHECK(bt_pair_begin(p, &peer, 0x0009) == BT_OK);
    uint8_t auth[3] = {0x04, 0x00, 0x09}; /* authentication failure */
    bt_pair_on_event(p, AUTH_COMPLETE, auth, sizeof auth);

    BtPairState st;
    CHECK(bt_pair_state(p, &peer, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_FAILED);
    CHECK(st.error == BT_EAUTH);

    bt_pair_free(p);
    teardown();
}

static void test_link_key_notification_stores_key(void) {
    setup();
    BtPair *p = bt_pair_new(hci);
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:09", &peer);

    uint8_t ev[23];
    memcpy(ev, peer.b, 6);
    for (int i = 0; i < 16; i++) ev[6 + i] = (uint8_t)(i + 1);
    ev[22] = 0x04;
    bt_pair_on_event(p, LINK_KEY_NOTIFY, ev, sizeof ev);

    uint8_t key[16];
    CHECK(bt_pair_has_key(p, &peer, key) == BT_OK);
    CHECK(key[0] == 1 && key[15] == 16);

    BtAddress other;
    bt_addr_parse("00:00:00:00:00:0A", &other);
    CHECK(bt_pair_has_key(p, &other, key) == BT_ENOENT);

    bt_pair_free(p);
    teardown();
}

static void test_cancel_and_bad_input(void) {
    setup();
    BtPair *p = bt_pair_new(hci);
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:0B", &peer);

    CHECK(bt_pair_cancel(p, &peer) == BT_ENOENT);
    CHECK(bt_pair_begin(p, &peer, 0x0003) == BT_OK);
    CHECK(bt_pair_cancel(p, &peer) == BT_OK);
    BtPairState st;
    CHECK(bt_pair_state(p, &peer, &st) == BT_OK);
    CHECK(st.state == BT_PAIR_IDLE);

    /* A PIN is only accepted while one is requested. */
    const uint8_t pin[4] = {9, 9, 9, 9};
    CHECK(bt_pair_set_pin(p, &peer, pin, 4) == BT_EBUSY);
    CHECK(bt_pair_set_pin(p, &peer, pin, 0) == BT_EINVAL);
    CHECK(bt_pair_set_pin(p, &peer, pin, 17) == BT_EINVAL);

    bt_pair_free(p);
    teardown();
}

static void test_link_key_request_replies(void) {
    setup();
    BtPair *p = bt_pair_new(hci);
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:0C", &peer);

    /* No key for this peer: the controller is told so. */
    uint8_t ev[6];
    memcpy(ev, peer.b, 6);
    bt_pair_on_event(p, 0x16, ev, sizeof ev);
    BtLastCommand cmd;
    CHECK(bt_fake_last_command(&tr, &cmd));
    CHECK(cmd.opcode == 0x040C);

    bt_pair_free(p);
    teardown();
}

int main(void) {
    test_pin_flow_reaches_paired();
    test_authentication_failure();
    test_link_key_notification_stores_key();
    test_cancel_and_bad_input();
    test_link_key_request_replies();
    return bt_done("test_bt_pair");
}
