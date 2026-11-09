/* Feeds the Bluetooth control panel from the desktop's one manager. App only. */
#include "desktop_bt_view.h"
#include "cdev_bluetooth.h"
#include "desktop_bluetooth.h"

#include <stdio.h>
#include <string.h>

static int v_available(void) { return desktop_bt_manager() && desktop_bt_available(); }
static const char *v_reason(void) {
    if (!desktop_bt_manager()) return "Bluetooth service did not start.";
    return desktop_bt_unavailable_reason();
}
static int v_state(void) {
    DesktopBtManager *m = desktop_bt_manager();
    if (!m) return CDEV_BT_IDLE;
    switch (desktop_bt_state(m)) {
    case DBT_SCANNING: return CDEV_BT_SCANNING;
    case DBT_CONNECTING: return CDEV_BT_CONNECTING;
    case DBT_UP: return CDEV_BT_UP;
    case DBT_FAILED: return CDEV_BT_FAILED;
    default: return CDEV_BT_IDLE;
    }
}
static int v_error(void) { DesktopBtManager *m = desktop_bt_manager(); return m ? desktop_bt_error(m) : 0; }
static int v_count(void) { DesktopBtManager *m = desktop_bt_manager(); return m ? desktop_bt_device_count(m) : 0; }
static int v_device(int i, char *name, size_t nn, char *addr, size_t an) {
    DesktopBtManager *m = desktop_bt_manager();
    const DesktopBtDevice *d = m ? desktop_bt_device_at(m, i) : NULL;
    if (!d) return -1;
    snprintf(name, nn, "%s", d->name);
    return bt_addr_format(&d->addr, addr, an) >= 0 ? 0 : -1;
}
static int v_scan(int seconds) {
    DesktopBtManager *m = desktop_bt_manager();
    return m ? desktop_bt_scan(m, seconds) : BT_ENOENT;
}
static int v_connect(int i) {
    DesktopBtManager *m = desktop_bt_manager();
    const DesktopBtDevice *d = m ? desktop_bt_device_at(m, i) : NULL;
    return d ? desktop_bt_connect(m, &d->addr) : BT_ENOENT;
}
static void v_disconnect(void) { desktop_bt_disconnect(desktop_bt_manager()); }

static const CdevBtSource k_source = {
    v_available, v_reason, v_state, v_error, v_count, v_device, v_scan, v_connect, v_disconnect
};

void desktop_bt_view_register(void) { cdev_bluetooth_set_source(&k_source); }
void desktop_bt_view_unregister(void) { cdev_bluetooth_set_source(NULL); }
