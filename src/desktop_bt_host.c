#include "desktop_bt_host.h"
#include "desktop_bluetooth.h"
#include "desktop_guestnet.h"

static DesktopGuestNet *owned_network;

void desktop_bt_host_shutdown(void) {
    if (!owned_network) return;
    desktop_bt_stop();
    desktop_guestnet_service_stop();
    owned_network = NULL;
}

void desktop_bt_host_init(void) {
    desktop_bt_host_shutdown();
    owned_network = desktop_guestnet_service();
    if (owned_network && desktop_bt_start(NULL, owned_network) != 0)
        desktop_bt_host_shutdown();
}

void desktop_bt_host_tick(void) {
    if (!owned_network) return;
    desktop_bt_poll(desktop_bt_manager());
    desktop_guestnet_pump(owned_network);
}
