/* Real driver + real BNEP/PAN, fake IOBluetooth boundary; no radio accessed. */
#include "desktop_bluetooth.h"
#include "bt_darwin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct BtDarwin { BtDarwinDataFn data; BtDarwinCloseFn close; void *ctx; int response; };
static BtDarwin *host;
static int checks, failures, opened, closed_count, refuse;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
BtDarwin *bt_darwin_new(void) { host = calloc(1, sizeof *host); return host; }
void bt_darwin_free(BtDarwin *d) { free(d); host = NULL; }
int bt_darwin_available(void) { return 1; }
int bt_darwin_local_address(BtDarwin *d, BtAddress *a) {
    (void)d; return bt_addr_parse("00:11:22:33:44:55", a);
}
int bt_darwin_discover_with_done(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn found,
                               BtDarwinDoneFn done, void *ctx) {
    (void)d; (void)seconds;
    BtAddress a; bt_addr_parse("00:00:00:00:00:07", &a);
    found(ctx, &a, "Synthetic peer", 0);
    done(ctx, BT_OK);
    return BT_OK;
}
int bt_darwin_stop_discover(BtDarwin *d) { (void)d; return BT_OK; }
static long write_pipe(void *ctx, const void *p, size_t n) {
    BtDarwin *d = ctx;
    const uint8_t setup[] = {1,1,2,0x11,0x16,0x11,0x15};
    if (n == sizeof setup) {
        CHECK(!memcmp(p, setup, sizeof setup));
        d->response = 1;
    }
    return (long)n;
}
int bt_darwin_open_l2cap(BtDarwin *d, const BtAddress *peer, uint16_t psm,
                       BtDarwinOpenFn on_open, BtDarwinDataFn data,
                       BtDarwinCloseFn close, void *ctx) {
    (void)peer; CHECK(psm == BT_PSM_BNEP);
    opened++;
    d->data = data; d->close = close; d->ctx = ctx;
    BtDarwinPipe pipe = {.write = write_pipe, .ctx = d};
    on_open(ctx, 1, &pipe);
    return BT_OK;
}
int bt_darwin_close_l2cap(BtDarwin *d) {
    if (d && d->data) closed_count++;
    if (d) { d->data = NULL; d->close = NULL; d->ctx = NULL; d->response = 0; }
    return BT_OK;
}
void bt_darwin_poll(BtDarwin *d) {
    if (d && d->response && d->data) {
        d->response = 0;
        uint8_t response[] = {1,2,0,(uint8_t)(refuse ? 4 : 0)};
        d->data(d->ctx, response, sizeof response);
    }
}
int main(void) {
    DesktopGuestNet *net = desktop_guestnet_new();
    DesktopBtManager *m = desktop_bt_new(desktop_bt_darwin_ops(), net);
    CHECK(m && opened == 0); /* construction never touches a device */
    CHECK(desktop_bt_scan(m, 1) == BT_OK);
    CHECK(desktop_bt_device_count(m) == 1);
    BtAddress peer = desktop_bt_device_at(m, 0)->addr;
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    CHECK(desktop_bt_state(m) == DBT_CONNECTING);
    CHECK(desktop_bt_local_ip(m) == 0);
    desktop_bt_poll(m);
    CHECK(desktop_bt_state(m) == DBT_UP);
    CHECK(desktop_bt_local_ip(m) == DBT_PAN_LOCAL_IP);
    CHECK(opened == 1);
    host->close(host->ctx);
    desktop_bt_poll(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_local_ip(m) == 0);
    CHECK(closed_count == 1);
    /* Reconnect uses a fresh protocol/channel, then cancel before handshake. */
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    desktop_bt_disconnect(m);
    desktop_bt_poll(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_local_ip(m) == 0);
    refuse = 1;
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    desktop_bt_poll(m);
    CHECK(desktop_bt_state(m) == DBT_FAILED);
    CHECK(desktop_bt_error(m) == BT_EDENIED);
    CHECK(desktop_bt_local_ip(m) == 0);
    refuse = 0;
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    desktop_bt_poll(m);
    CHECK(desktop_bt_state(m) == DBT_UP);
    desktop_bt_free(m);
    CHECK(host == NULL && closed_count == 4);
    CHECK(desktop_guestnet_local_ip(net) == 0);
    desktop_guestnet_free(net);
    printf("desktop Bluetooth driver: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
