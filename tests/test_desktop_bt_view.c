/* The panel's source over a fake driver and the real manager: no radio. */
#include "cdev_bluetooth.h"
#include "desktop_bt_view.h"
#include "desktop_bluetooth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const CdevBtSource *registered;
void cdev_bluetooth_set_source(const CdevBtSource *s) { registered = s; }

static DesktopBtCallbacks cbs;
static int scans, links;
static DesktopBtDriver *d_create(const DesktopBtCallbacks *cb) { cbs = *cb; return calloc(1, 1); }
static int d_scan(DesktopBtDriver *d, int s) { (void)d; (void)s; scans++; return BT_OK; }
static int d_link_up(DesktopBtDriver *d, const BtAddress *p) { (void)d; (void)p; links++; return BT_EAGAIN; }
static void d_link_down(DesktopBtDriver *d) { (void)d; }
static GnPan *d_pan(DesktopBtDriver *d) { (void)d; return NULL; }
static void d_free(DesktopBtDriver *d) { free(d); }
static const DesktopBtDriverOps k_ops = {d_create, d_scan, d_link_up, d_link_down, NULL, d_pan, d_free};

int main(void) {
    desktop_bt_view_register();
    CHECK(registered != NULL);
    if (!registered) return 1;
    /* Without a manager the source is inert and honest. */
    CHECK(!registered->available() && registered->device_count() == 0);
    CHECK(registered->scan(10) == BT_ENOENT && registered->state() == CDEV_BT_IDLE);
    CHECK(registered->unavailable_reason() != NULL);

    DesktopGuestNet *net = desktop_guestnet_service();
    CHECK(net != NULL);
    CHECK(desktop_bt_start(&k_ops, net) == 0);
    CHECK(registered->scan(10) == 0 && scans == 1 && registered->state() == CDEV_BT_SCANNING);
    BtAddress a = {{0, 1, 2, 3, 4, 5}};
    cbs.found(cbs.ctx, &a, "Test Phone", 0x200404);
    cbs.scan_done(cbs.ctx);
    CHECK(registered->state() == CDEV_BT_IDLE && registered->device_count() == 1);
    char name[64], addr[24];
    CHECK(registered->device(0, name, sizeof name, addr, sizeof addr) == 0);
    CHECK(strcmp(name, "Test Phone") == 0 && strcmp(addr, "00:01:02:03:04:05") == 0);
    CHECK(registered->device(1, name, sizeof name, addr, sizeof addr) != 0);
    CHECK(registered->connect(5) != 0 && links == 0);
    CHECK(registered->connect(0) == 0 && links == 1 && registered->state() == CDEV_BT_CONNECTING);
    registered->disconnect();
    CHECK(registered->state() == CDEV_BT_IDLE);
    cbs.failed(cbs.ctx, BT_EIO);
    desktop_bt_stop();
    desktop_guestnet_service_stop();
    desktop_bt_view_unregister();
    CHECK(registered == NULL);
    printf("desktop_bt_view: %s\n", failures ? "FAILED" : "ok (fake driver, no radio)");
    return failures ? 1 : 0;
}
