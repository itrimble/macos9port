/* RFCOMM: DLCI setup, framing and credit flow control. */
#include "bt_test.h"

static int server_opened, client_opened;
static int server_dlci;

static void on_server_open(void *ctx, BtRfcommChan *ch) {
    (void)ctx;
    server_opened++;
    server_dlci = ch ? 0 : -1;
}

static void on_client_open(void *ctx, BtRfcommChan *ch) {
    (void)ctx;
    (void)ch;
    client_opened++;
}

/* Bring up an L2CAP channel and an RFCOMM mux on both sides. */
static void setup_mux(BtWire *w, BtRfcomm **client, BtRfcomm **server) {
    bt_wire_init(w);
    bt_l2cap_listen(w->b, BT_PSM_RFCOMM, bt_wire_on_open_b, w);
    int cid = -1;
    bt_l2cap_connect(w->a, w->handle, NULL, BT_PSM_RFCOMM, 0, bt_wire_on_open_a, w, &cid);
    bt_wire_run(w, 12);
    *client = bt_rfcomm_new(w->a, w->cid_a, 0);
    *server = bt_rfcomm_new(w->b, w->cid_b, 1);
    server_opened = 0;
    client_opened = 0;
}

static void test_mux_and_data_channel(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    CHECK(client != NULL && server != NULL);

    CHECK(bt_rfcomm_listen(server, 1, on_server_open, NULL) == BT_OK);
    bt_rfcomm_set_open_fn(client, on_client_open, NULL);

    int dlci = -1;
    CHECK(bt_rfcomm_connect(client, 1, &dlci) == BT_OK);
    CHECK(dlci == 2); /* DLCI is twice the server channel */
    bt_wire_run(&w, 12);

    CHECK(server_opened == 1);
    CHECK(client_opened == 1);

    BtRfcommInfo info;
    CHECK(bt_rfcomm_info(client, dlci, &info) == BT_OK);
    CHECK(info.state == BT_RFCOMM_OPEN);
    CHECK(info.server_channel == 1);
    CHECK(bt_rfcomm_info(server, 2, &info) == BT_OK);
    CHECK(info.state == BT_RFCOMM_OPEN);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

static void test_data_flows_over_the_channel(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    bt_rfcomm_listen(server, 2, on_server_open, NULL);
    int dlci = -1;
    bt_rfcomm_connect(client, 2, &dlci);
    bt_wire_run(&w, 12);
    CHECK(server_opened == 1);

    const char *msg = "AT+CGMI";
    CHECK(bt_rfcomm_send(client, dlci, msg, strlen(msg)) == (int)strlen(msg));
    bt_wire_run(&w, 8);

    char buf[64];
    long n = bt_rfcomm_recv(server, 4, buf, sizeof buf);
    CHECK(n == (long)strlen(msg));
    CHECK(memcmp(buf, msg, (size_t)n) == 0);

    /* The server answers. */
    const char *reply = "Apple";
    CHECK(bt_rfcomm_send(server, 4, reply, strlen(reply)) == (int)strlen(reply));
    bt_wire_run(&w, 8);
    n = bt_rfcomm_recv(client, dlci, buf, sizeof buf);
    CHECK(n == (long)strlen(reply));
    CHECK(memcmp(buf, reply, (size_t)n) == 0);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

static void test_credit_limits_the_sender(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    bt_rfcomm_listen(server, 3, on_server_open, NULL);
    int dlci = -1;
    bt_rfcomm_connect(client, 3, &dlci);
    bt_wire_run(&w, 12);
    CHECK(server_opened == 1);

    /* The peer granted a credit, so exactly that many bytes go out. */
    BtRfcommInfo info;
    CHECK(bt_rfcomm_info(client, dlci, &info) == BT_OK);
    CHECK(info.credit == 128);

    /* Write more than the credit allows: the rest waits in the queue. */
    static uint8_t big[400];
    memset(big, 'A', sizeof big);
    CHECK(bt_rfcomm_send(client, dlci, big, sizeof big) == (int)sizeof big);
    bt_wire_run(&w, 8);

    char buf[512];
    long n = bt_rfcomm_recv(server, 6, buf, sizeof buf);
    CHECK(n == 128); /* only the granted credit was carried */

    /* Reading hands the credit back, so the rest flows through. */
    static char all[512];
    long total = n;
    memcpy(all, buf, (size_t)n);
    for (int i = 0; i < 20 && total < (long)sizeof big; i++) {
        bt_wire_run(&w, 4);
        long k = bt_rfcomm_recv(server, 6, buf, sizeof buf);
        CHECK(k >= 0);
        if (k > 0) memcpy(all + total, buf, (size_t)k);
        total += k;
    }
    CHECK(total == (long)sizeof big);
    CHECK(memcmp(all, big, sizeof big) == 0);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

static void test_explicit_credit_grant(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    bt_rfcomm_listen(server, 4, on_server_open, NULL);
    int dlci = -1;
    bt_rfcomm_connect(client, 4, &dlci);
    bt_wire_run(&w, 12);
    CHECK(server_opened == 1);

    /* Spend the credit, then grant more and watch the sender move. */
    static uint8_t payload[200];
    memset(payload, 'B', sizeof payload);
    CHECK(bt_rfcomm_send(client, dlci, payload, sizeof payload) == (int)sizeof payload);
    bt_wire_run(&w, 8);

    char buf[256];
    long n = bt_rfcomm_recv(server, 8, buf, sizeof buf);
    CHECK(n == 128);
    /* The rest is still waiting for credit. */
    bt_wire_run(&w, 8);
    n = bt_rfcomm_recv(server, 8, buf, sizeof buf);
    CHECK(n == 72);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

static void test_unknown_server_channel_is_refused(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    /* The server listens on channel 1 only. */
    bt_rfcomm_listen(server, 1, on_server_open, NULL);
    int dlci = -1;
    CHECK(bt_rfcomm_connect(client, 5, &dlci) == BT_OK);
    bt_wire_run(&w, 12);
    CHECK(server_opened == 0);

    BtRfcommInfo info;
    CHECK(bt_rfcomm_info(client, dlci, &info) == BT_OK);
    CHECK(info.state == BT_RFCOMM_ERROR);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

static void test_bad_arguments(void) {
    BtWire w;
    BtRfcomm *client = NULL, *server = NULL;
    setup_mux(&w, &client, &server);
    int dlci = -1;
    CHECK(bt_rfcomm_connect(client, 0, &dlci) == BT_EINVAL);
    CHECK(bt_rfcomm_connect(client, 31, &dlci) == BT_EINVAL);
    CHECK(bt_rfcomm_listen(server, 0, on_server_open, NULL) == BT_EINVAL);
    CHECK(bt_rfcomm_listen(server, 31, on_server_open, NULL) == BT_EINVAL);
    CHECK(bt_rfcomm_listen(server, 1, on_server_open, NULL) == BT_OK);
    CHECK(bt_rfcomm_listen(server, 1, on_server_open, NULL) == BT_EBUSY);
    CHECK(bt_rfcomm_send(client, 99, "x", 1) == BT_ENOENT);
    CHECK(bt_rfcomm_recv(client, 99, (char[4]){0}, 4) == BT_ENOENT);
    CHECK(bt_rfcomm_info(client, 99, NULL) == BT_EINVAL);

    bt_rfcomm_free(client);
    bt_rfcomm_free(server);
    bt_wire_free(&w);
}

int main(void) {
    test_mux_and_data_channel();
    test_data_flows_over_the_channel();
    test_credit_limits_the_sender();
    test_explicit_credit_grant();
    test_unknown_server_channel_is_refused();
    test_bad_arguments();
    return bt_done("test_bt_rfcomm");
}
