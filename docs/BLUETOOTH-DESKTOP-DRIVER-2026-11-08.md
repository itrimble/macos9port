# Desktop Bluetooth driver integration handoff

The production app now links `desktop_bluetooth`, `desktop_guestnet` and the
macOS driver through `config/desktop-bluetooth.mk`. Other platforms link the
unavailable driver. The IOBluetooth wrapper is an ARC object compiled separately;
no simulator source is linked into the production app.

The driver opens only on an explicit connect request. It waits for a successful
BNEP response before creating the PAN or publishing a guest address, fails
refused negotiation, and bounds incomplete setup to 15 seconds. Cancel closes
only the L2CAP channel it opened. Remote close notifies the manager before the
PAN is freed. Reconnect builds fresh protocol objects. Initialization does not
query a controller or start discovery. IOBluetooth callbacks are pumped on the
caller's run loop without waiting.

The PAN-to-guest transmit adapter also now translates its byte-count result to
GN_OK correctly; it previously treated successful nonzero writes as errors.

## Host and menu ownership

The active Control Panels producer's files were not changed. Integration owns
these remaining shared hooks, which must run on the main thread:

```c
/* Once at host startup. This allocates services without scanning or connecting. */
DesktopGuestNet *guestnet = desktop_guestnet_service();
if (guestnet) desktop_bt_start(NULL, guestnet);

/* Each host-loop iteration; no blocking waits. */
desktop_bt_poll(desktop_bt_manager());
desktop_guestnet_pump(guestnet);

/* Before host destruction, in this order. */
desktop_bt_stop();
desktop_guestnet_service_stop();
```

The menu/panel should call `desktop_bt_scan`, `desktop_bt_connect`, and
`desktop_bt_disconnect` only for user actions. It can render device count/list,
state text/error, peer and local address through the manager getters. Checking
`desktop_bt_available()` queries controller power state; it is not a peer
connectivity test. The source manifest/linkage is complete, but no new menu,
panel, shared host-loop hooks, or classic guest-runtime hook is claimed here.

## Evidence and limits

`test_desktop_bt_driver` exercises the actual production driver and BNEP/PAN
behind a fake IOBluetooth boundary: delayed successful negotiation, refused
negotiation, remote close, reconnect, cancel before setup, and free while
connected. Its 31 checks pass without radio operations. ASan/UBSan and `BUILDFLEET_LOCAL=1 make -j2 test all` passed with the private
genuine SDL2 pin. Logs are in ignored `build-logs/desktop-driver-*`. Symbol and
dylib inspection confirms the driver and IOBluetooth in `build/ospi`. Existing
linker common-section alignment warnings remain.
The configured 15-second timeout is not a physical-device timing measurement.
The underlying IOBluetooth open remains synchronous and may block the calling
thread during connection establishment; user-facing latency is not verified.

This remains a general-Ethernet BNEP subset: compressed frames/extensions and
optional filters are unsupported, as recorded in the wire follow-up. Static
PAN addressing is 192.168.10.1/24 with peer 192.168.10.2; this does not implement
DHCP or discovery of a peer's IP configuration. Physical interoperability,
permission prompts and user-facing panel acceptance remain unverified. No
paired devices were inspected, connected, paired or disconnected during tests.
