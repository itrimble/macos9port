/* Contract-only macOS checks: never query a controller, scan, or open a peer. */
#include "bt_test.h"
#include "bt_darwin.h"
int main(void) {
    BtDarwin *d = bt_darwin_new();
    CHECK(d != NULL);
    BtAddress address;
    CHECK(bt_darwin_local_address(NULL, &address) == BT_EINVAL);
    CHECK(bt_darwin_local_address(d, NULL) == BT_EINVAL);
    CHECK(bt_darwin_discover_with_done(NULL, 1, NULL, NULL, NULL) == BT_EINVAL);
    CHECK(bt_darwin_discover_with_done(d, 61, NULL, NULL, NULL) == BT_EINVAL);
    CHECK(bt_darwin_open_l2cap(d, NULL, BT_PSM_BNEP, NULL, NULL, NULL, NULL) == BT_EINVAL);
    CHECK(bt_darwin_stop_discover(d) == BT_OK);
    CHECK(bt_darwin_close_l2cap(d) == BT_OK);
    bt_darwin_free(d);
    bt_darwin_free(NULL);
    return bt_done("test_bt_darwin_contract");
}
