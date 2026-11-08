#include "desktop_bt_host.h"
#include "desktop_bluetooth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char events[128];
static int used, no_service, start_failure;
static int network_token, manager_token;
static void event(char c) { assert(used < 127); events[used++] = c; events[used] = 0; }
DesktopGuestNet *desktop_guestnet_service(void) {
    event('N'); return no_service ? NULL : (DesktopGuestNet *)&network_token;
}
int desktop_bt_start(const DesktopBtDriverOps *ops, DesktopGuestNet *net) {
    assert(!ops && net == (DesktopGuestNet *)&network_token); event('S'); return start_failure;
}
DesktopBtManager *desktop_bt_manager(void) { return (DesktopBtManager *)&manager_token; }
void desktop_bt_poll(DesktopBtManager *manager) { assert(manager == (DesktopBtManager *)&manager_token); event('P'); }
void desktop_guestnet_pump(DesktopGuestNet *net) { assert(net == (DesktopGuestNet *)&network_token); event('F'); }
void desktop_bt_stop(void) { event('B'); }
void desktop_guestnet_service_stop(void) { event('G'); }
static void expect(const char *text) { assert(!strcmp(events, text)); used = 0; events[0] = 0; }
int main(void) {
    desktop_bt_host_tick(); desktop_bt_host_shutdown(); expect("");
    desktop_bt_host_init(); desktop_bt_host_tick(); expect("NSPF");
    desktop_bt_host_init(); expect("BGNS");
    desktop_bt_host_shutdown(); desktop_bt_host_shutdown(); expect("BG");
    no_service = 1; desktop_bt_host_init(); desktop_bt_host_tick(); expect("N");
    no_service = 0; start_failure = -1;
    desktop_bt_host_init(); desktop_bt_host_tick(); desktop_bt_host_shutdown(); expect("NSBG");
    puts("Bluetooth host lifecycle: startup failure, ordered teardown, restart and inactive ticks passed; no radio calls");
    return 0;
}
