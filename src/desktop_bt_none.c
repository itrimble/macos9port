/* The driver for platforms without a Bluetooth radio. The menu item is
 * disabled and the panel says why. */
#include "desktop_bluetooth.h"

#include <stdlib.h>

static DesktopBtDriver *none_create(const DesktopBtCallbacks *cb) {
    (void)cb;
    return calloc(1, 1);
}

static int none_scan(DesktopBtDriver *d, int seconds) { (void)d; (void)seconds; return BT_ENOENT; }
static int none_link_up(DesktopBtDriver *d, const BtAddress *peer) { (void)d; (void)peer; return BT_ENOENT; }
static void none_link_down(DesktopBtDriver *d) { (void)d; }
static void none_poll(DesktopBtDriver *d) { (void)d; }
static GnPan *none_pan(DesktopBtDriver *d) { (void)d; return NULL; }
static void none_free(DesktopBtDriver *d) { free(d); }

static const DesktopBtDriverOps k_none_ops = {
    .create = none_create,
    .scan = none_scan,
    .link_up = none_link_up,
    .link_down = none_link_down,
    .poll = none_poll,
    .pan = none_pan,
    .free = none_free,
};

const DesktopBtDriverOps *desktop_bt_none_ops(void) { return &k_none_ops; }

const DesktopBtDriverOps *desktop_bt_platform_ops(void) { return desktop_bt_none_ops(); }

int desktop_bt_available(void) { return 0; }

const char *desktop_bt_unavailable_reason(void) { return "Bluetooth PAN radio integration is not available"; }
