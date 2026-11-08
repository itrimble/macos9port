# Desktop Bluetooth PAN: design

Date: 2026-11-07
Branch: `feature/bluetooth-stack`
Status: approved

## Goal

Let a person pick a Bluetooth peer in the desktop and bring up a personal
area network to it, so that programs on the guest reach the peer over the
Bluetooth link with their own address.

The Bluetooth stack already carries frames over BNEP and the guest
network stack in `net/` already runs IP, ARP and TCP over such a link.
What is missing is the middle: the desktop has no Bluetooth object, no
way to choose a peer, and no guest network service to attach the link
to.

## What the user sees

1. **999 ▸ Bluetooth Network…** — a new menu item, placed after
   *Tailscale*, because that is where the other network service lives.
   When the platform has no radio the item is disabled and says so.
2. The panel opens and starts an inquiry. Devices appear as they
   answer; each row shows the name, the address and the device class.
3. Clicking a row selects it. The panel shows *Connecting to \<name>…*.
4. When the link is up the panel shows *Connected to \<name>*, the
   address the guest has on the link and the address of the peer.
5. *Disconnect* takes the link down and returns the panel to the list.

## Components

### 1. `bt_bnep_adopt` (bluetooth/src/bt_bnep.c)

BNEP on a channel the operating system opened. Today `bt_bnep` only
opens channels itself, through `bt_bnep_connect`. The macOS path needs
BNEP — and its CONN_REQ / CONN_RSP exchange — on the channel
IOBluetooth opened for us.

`bt_bnep_adopt(BtBnep *b, int cid, const BtAddress *peer)` attaches a
BNEP channel to an L2CAP channel that is already open, derives the
link addresses, installs the data callback and sends the connection
request. The shared part is extracted from `bnep_on_l2cap_open` so both
paths behave the same.

### 2. The manager (`src/desktop_bluetooth.c`)

Owns:

- the device list (name, address, class of device),
- the scan state,
- the link state: idle, connecting, up, failed,
- the `BtPan` socket and the `GnPan` guest IP stack behind it.

The manager never talks to a radio directly; it goes through a driver.

### 3. The driver seam (`include/desktop_bluetooth.h`)

```c
typedef struct DesktopBtDriver DesktopBtDriver;   /* driver defined */
typedef struct {
    int      (*scan)(DesktopBtDriver *d, int seconds);
    int      (*link_up)(DesktopBtDriver *d, const BtAddress *peer);
    void     (*link_down)(DesktopBtDriver *d);
    void     (*poll)(DesktopBtDriver *d);
    GnPan   *(*pan)(DesktopBtDriver *d);
    void     (*free)(DesktopBtDriver *d);
} DesktopBtDriverOps;
```

The driver reports what it finds and how the link came up through
callbacks the desktop hands it at creation:

```c
typedef struct {
    void (*found)(void *ctx, const BtAddress *addr, const char *name, uint32_t cod);
    void (*up)(void *ctx);
    void (*failed)(void *ctx, int error);
    void *ctx;
} DesktopBtCallbacks;
```

Two drivers:

- `src/desktop_bt_darwin.m` — the macOS radio. IOBluetooth runs the
  inquiry and opens the BNEP channel; the channel is adopted into the
  stack; the host's own address comes from
  `IOBluetoothHostController.addressAsString`.
- `src/desktop_bt_none.c` — the stub for platforms without a radio.
  The menu item is disabled.

The desktop test uses a third driver built on the controller simulator,
so the whole flow runs headlessly.

### 4. The guest network service (`src/desktop_guestnet.c`)

The desktop owns one broker and one MacTCP layer — the service that the
runtime hook will hand to programs. The PAN is attached to that broker
with `gn_broker_set_pan`, so:

- `gn_broker_local_ip` reports the PAN address,
- `ipctlGetAddr` reports it to the guest,
- a guest TCP connection to the peer travels over the Bluetooth link.

The service starts with a built-in policy:

```
legacy on
local on
allow both any * *
```

The policy file and the desktop policy user interface belong to the
runtime-hook work and stay out of scope.

### 5. The panel (`src/bluetooth_panel.c`)

Like the Appearance panel: a device list with a scroll, a status line
and a Disconnect button once the link is up.

### 6. The panel façade (`src/desktop_panel.c`)

The Appearance panel is called directly from eight places in the
desktop. The Bluetooth panel needs the same hooks, and only one panel
may be open at a time, so those calls go through a façade that
dispatches to whichever panel is open.

### 7. The menu item

A new command id and a new item in the `999` menu.

## Addressing

The Ethernet addresses on the link are the Bluetooth device addresses:
each end stamps its own address as the source of every frame and
expects the remote's address as the destination.

- Both ends learn the remote's address from the L2CAP link.
- The desktop learns its own address from
  `IOBluetoothHostController.addressAsString`.
- The stack's BNEP layer already derives the link addresses from the
  device addresses, so a real BNEP peer sees the address it expects.

Concretely, the desktop sets the PAN socket's local address to its own
device address with `bt_pan_set_local_mac`, and passes the peer's device
address as the socket's peer address in `bt_pan_open`. The guest IP
stack behind the link is given the same pair, so the frames the guest
stack builds carry the addresses the peer expects.

The link subnet is `192.168.10.0/24`. The side that opens the channel is
`.1` and the side that accepts it is `.2`. The desktop always opens the
channel, so the desktop is `.1` and the peer is `.2`.

## Data flow

```
menu item ──▶ panel opens ──▶ manager.scan ──▶ driver.scan (IOBluetooth inquiry)
                                                     │
                                              found(device) ──▶ device list
                                                     │
user clicks a row ──▶ manager.link_up(peer) ──▶ driver.link_up
                                                     │
                                    IOBluetooth opens the BNEP channel
                                                     │
                                    bt_l2cap_adopt ──▶ bt_bnep_adopt
                                                     │
                                    bt_pan_open ──▶ bt_panif_new ──▶ GnPan
                                                     │
                                    gn_broker_set_pan(broker, pan)
                                                     │
                                        up ──▶ panel shows the addresses
```

Every frame that arrives on the link is moved into the guest IP stack
by the driver's `poll`, which the desktop calls each tick.

## Errors

| Situation | What the desktop does |
|---|---|
| No radio on this platform | The menu item is disabled and names the reason. |
| The inquiry finds nothing | The panel says so and offers *Search Again*. |
| The link fails to come up | The device list stays, the reason is shown, the guest service is untouched. |
| The peer closes the link | The PAN is detached from the broker and the panel returns to the list. |
| A frame arrives for an unknown peer | The stack drops it, as it does today. |

## Testing

- `bluetooth/tests/test_bt_bnep.c` — `bt_bnep_adopt`: a channel is
  adopted, the connection exchange completes and frames travel over it.
- `tests/test_desktop_bluetooth.c` — the whole flow over the controller
  simulator: the panel opens, devices appear, a click brings the link
  up, the broker reports the PAN address, a guest TCP stream reaches
  the peer over the link, and the failure paths behave.
- The panel renders headlessly.

The controller simulator (`bluetooth/src/bt_hci_sim.c`) is test only
and stays out of the Bluetooth library, so the desktop test compiles it
directly, the way the Bluetooth tests do.

## Files

| File | Lines | Role |
|---|---|---|
| `include/desktop_bluetooth.h` | ~90 | The manager API and the driver seam. |
| `src/desktop_bluetooth.c` | ~420 | The manager: devices, scan, link, PAN. |
| `include/desktop_panel.h` | ~40 | The panel façade. |
| `src/desktop_panel.c` | ~60 | Dispatches to the open panel. |
| `include/bluetooth_panel.h` | ~40 | The Bluetooth panel API. |
| `src/bluetooth_panel.c` | ~430 | The device list, the status, the clicks. |
| `include/desktop_guestnet.h` | ~40 | The guest network service API. |
| `src/desktop_guestnet.c` | ~150 | The broker, the MacTCP layer, the policy. |
| `src/desktop_bt_darwin.m` | ~300 | The macOS radio driver. |
| `src/desktop_bt_none.c` | ~60 | The stub for platforms without a radio. |
| `tests/test_desktop_bluetooth.c` | ~350 | The end-to-end flow test. |

Every file stays under 500 lines.

## Out of scope

- Accepting an incoming PAN. The desktop is always the initiator, so it
  is always `.1`.
- The policy file and the desktop policy user interface; the guest
  service ships with a built-in policy.
- A real radio run. The macOS driver is compile-checked against the
  SDK, but no peer device has been used with it, so that path is
  unverified at runtime.
- Programs reaching the peer. That needs the runtime hook in `net/`,
  which is separate work; this design makes the link ready for it.
