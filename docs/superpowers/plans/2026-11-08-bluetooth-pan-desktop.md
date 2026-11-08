# Desktop Bluetooth PAN Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a person pick a Bluetooth peer from the desktop and bring up a personal area network to it, so that programs in the guest reach the peer over Bluetooth with their own address.

**Architecture:** The Bluetooth stack in `bluetooth/` carries Ethernet frames over BNEP and the guest network stack in `net/` runs IP, ARP and TCP over such a link. This plan adds the missing middle: a desktop Bluetooth manager that owns the device list, the scan and the link, a driver seam so the macOS radio and the controller simulator are interchangeable, a desktop guest network service that owns the broker and the MacTCP layer, and an Appearance-style panel that lets the person choose a peer. The panel reaches the desktop through the existing `SessionMediaHooks` pattern, so the Bluetooth and network libraries never enter the core desktop link.

**Tech Stack:** C11, POSIX sockets and pthreads, SDL2 for the desktop, Objective-C against IOBluetooth for the macOS radio, `CHECK(cond)` headless test binaries.

## Global Constraints

- Every file stays under 500 lines. `src/typeface.c` (499) is the reference; `bluetooth/src/bt_hci.c` (480) is the current maximum.
- C11, 4-space indent, same-line braces, `snake_case` functions and variables, `PascalCase` typedefs, uppercase constants, `-Wall -Wextra`. No formatter or linter is configured.
- Tests are headless: no SDL, no window, no display. Each test is its own binary built from one `tests/test_*.c`.
- TDD is mandatory: write the failing test, watch it fail for the right reason, then write the minimal code that makes it pass, then refactor.
- Build and test with `BUILDFLEET_LOCAL=1 make test`; module suites run with `make -f net/net.mk test-net` and `make -f bluetooth/bluetooth.mk test-bluetooth`.
- Commits use a short imperative subject. Author stays `Ian <ian@example.com>`. No `Co-Authored-By`. Never force-push.
- Clean room: no Apple ROMs, System files, Toolbox code or proprietary assets. Musashi stays the only vendored third-party code.
- The PAN Ethernet addresses are the Bluetooth device addresses. The desktop stamps its own device address on every frame it sends and expects the peer's device address in return.
- The link subnet is `192.168.10.0/24`. The side that opens the channel is `.1`, the side that accepts it is `.2`. The desktop always opens, so the desktop is `192.168.10.1` and the peer is `192.168.10.2`.
- The built-in guest policy is `legacy on`, `local on`, `allow both any * *`. The policy file and the desktop policy user interface stay out of scope.
- The macOS radio path is compile-checked against the SDK only. No peer device is available, so that path is unverified at runtime and must be reported as such.
- Out of scope: accepting an incoming PAN, programs reaching the peer (that needs the runtime hook in `net/`).

---

## File Structure

**Bluetooth module (`bluetooth/`)**

| File | Role |
|---|---|
| `bluetooth/include/os999_bluetooth.h` | Gains `bt_bnep_adopt`. |
| `bluetooth/src/bt_bnep.c` | The shared attach path and `bt_bnep_adopt`. |
| `bluetooth/src/bt_darwin.h` | Gains the inquiry-complete callback and the host address query. |
| `bluetooth/src/bt_darwin.m` | Reports the end of an inquiry; reads the host controller address. |
| `bluetooth/tests/test_bt_bnep.c` | The adopt test. |

**Guest network module (`net/`)**

| File | Role |
|---|---|
| `net/include/os999_guestnet.h` | Gains `gn_mactcp_set_addr`. |
| `net/src/gn_mactcp.c` | Lets the MacTCP layer learn a new address. |
| `net/tests/test_gn_mactcp.c` | The address test. |

**Desktop (`src/`, `include/`)**

| File | Lines | Role |
|---|---|---|
| `include/desktop_bluetooth.h` | ~110 | The manager API, the driver seam, the platform drivers. |
| `src/desktop_bluetooth.c` | ~330 | The manager: devices, scan, link, the PAN attachment. |
| `src/desktop_bt_none.c` | ~70 | The driver for platforms without a radio. |
| `src/desktop_bt_darwin.m` | ~300 | The macOS radio driver. |
| `include/desktop_guestnet.h` | ~35 | The guest network service API. |
| `src/desktop_guestnet.c` | ~140 | The broker, the MacTCP layer, the built-in policy. |
| `include/bluetooth_panel.h` | ~30 | The Bluetooth panel API. |
| `src/bluetooth_panel.c` | ~400 | The device list, the status line, the clicks, the keys. |
| `include/desktop_panel.h` | ~30 | The panel façade. |
| `src/desktop_panel.c` | ~90 | Dispatches to whichever panel is open. |

**Desktop tests and build**

| File | Role |
|---|---|
| `tests/test_desktop_guestnet.c` | The guest network service on its own. |
| `tests/test_desktop_bluetooth.c` | The manager over a fake driver, then the whole flow over the simulator. |
| `tests/desktop_bt_sim.c`, `tests/desktop_bt_sim.h` | The simulator-backed driver and the peer at the far end. |
| `tests/test_bluetooth_panel.c` | The panel on its own. |
| `tests/test_desktop_panel.c` | The façade dispatches to the right panel. |
| `tests/test_desktop_bt_darwin.m` | The macOS driver, macOS only. |
| `config/core-sources.mk` | Gains `src/desktop_panel.c`. |
| `config/desktop-tests.mk` | The new test targets. |
| `Makefile` | Links the new sources and the two libraries into `build/ospi`. |
| `bluetooth/bluetooth.mk` | A file rule for `bt_darwin.o`. |

---

### Task 1: BNEP on an operating-system-opened channel

The macOS radio opens the L2CAP channel for us, so the BNEP connection exchange has to run on a channel the stack did not open. Today `bt_bnep` only opens channels itself, through `bt_bnep_connect`. This task adds `bt_bnep_adopt`, which attaches a BNEP channel to a channel that is already open, derives the link addresses the same way the open path does, and sends the connection request.

**Files:**
- Modify: `bluetooth/include/os999_bluetooth.h` (after `bt_bnep_connect`, line 301)
- Modify: `bluetooth/src/bt_bnep.c` (`bnep_on_l2cap_open`, lines 92-117)
- Test: `bluetooth/tests/test_bt_bnep.c`

**Interfaces:**
- Consumes: `bt_l2cap_adopt(l, handle, peer, psm, mtu, pipe, &cid)`, `bt_l2cap_pipe_rx(l, cid, p, n)`, `bt_l2cap_chan(l, cid)`, `bt_l2cap_set_chan_data_fn(chan, fn, ctx)` from `bluetooth/include/os999_bluetooth.h`.
- Produces: `int bt_bnep_adopt(BtBnep *b, int cid, const BtAddress *peer);` — returns `BT_OK`, `BT_EINVAL` (no `b` or no `peer`), `BT_ENOENT` (no L2CAP channel with that CID) or `BT_ELIMIT` (no free BNEP channel).

- [ ] **Step 1: Write the failing test**

Add to `bluetooth/tests/test_bt_bnep.c`, above `main`:

```c
/* What the host writes on a channel it adopted. */
static uint8_t adopted_out[BT_WIRE_BUF];
static size_t adopted_out_len;

static long adopted_write(void *ctx, const void *p, size_t n) {
    (void)ctx;
    if (n > sizeof adopted_out) return -1;
    memcpy(adopted_out, p, n);
    adopted_out_len = n;
    return (long)n;
}

/* A channel the operating system opened: the host adopts it, sends the
 * connection request, and the peer answers. */
static void test_bnep_adopt_completes_exchange(void) {
    BtWire w;
    bt_wire_init(&w);
    BtAddress peer;
    CHECK(bt_addr_parse("00:01:02:03:04:05", &peer) == BT_OK);

    BtL2capPipe pipe;
    pipe.write = adopted_write;
    pipe.read = NULL;
    pipe.ctx = NULL;
    int cid = -1;
    CHECK(bt_l2cap_adopt(w.a, w.handle, &peer, BT_PSM_BNEP, 1024, &pipe, &cid) == BT_OK);

    BtBnep *bnep = bt_bnep_new(w.a);
    CHECK(bnep != NULL);
    adopted_out_len = 0;
    CHECK(bt_bnep_adopt(bnep, cid, &peer) == BT_OK);

    /* The host sent the connection request. */
    CHECK(adopted_out_len == 6);
    CHECK(adopted_out[0] == BT_BNEP_HDR_BNEP);
    CHECK(adopted_out[1] == BT_BNEP_CMD_CONN_REQ);

    /* The peer answers and the channel comes up. */
    uint8_t rsp[6] = {BT_BNEP_HDR_BNEP, BT_BNEP_CMD_CONN_RSP, 0, 0, 0, 0x00};
    bt_l2cap_pipe_rx(w.a, cid, rsp, sizeof rsp);
    BtBnepInfo info;
    CHECK(bt_bnep_info(bnep, cid, &info) == BT_OK);
    CHECK(info.up == 1);
    CHECK(memcmp(info.dest_mac, peer_mac, 6) == 0);

    /* A frame from the peer reaches the frame callback. */
    got_len = 0;
    bt_bnep_set_frame_fn(bnep, on_frame, NULL);
    uint8_t frame[14 + 4];
    memcpy(frame, our_mac, 6);
    memcpy(frame + 6, peer_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;
    memcpy(frame + 14, "\x01\x02\x03\x04", 4);
    bt_l2cap_pipe_rx(w.a, cid, frame, sizeof frame);
    CHECK(got_len == sizeof frame);
    CHECK(memcmp(got_frame + 6, peer_mac, 6) == 0);

    /* An unknown channel is refused. */
    CHECK(bt_bnep_adopt(bnep, 4242, &peer) == BT_ENOENT);
    CHECK(bt_bnep_adopt(bnep, cid, NULL) == BT_EINVAL);

    bt_bnep_free(bnep);
    bt_wire_free(&w);
}
```

Call it from `main`, after `test_bnep_connects_and_carries_frames()`:

```c
    test_bnep_adopt_completes_exchange();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -f bluetooth/bluetooth.mk test-bluetooth`
Expected: the build fails with `implicit declaration of function 'bt_bnep_adopt'` in `bluetooth/tests/test_bt_bnep.c`.

- [ ] **Step 3: Declare the function**

In `bluetooth/include/os999_bluetooth.h`, after the `bt_bnep_connect` declaration (line 301), add:

```c
/* Attach a BNEP channel to an L2CAP channel the operating system opened.
 * The channel is already open, so the connection request goes straight out
 * on it. peer is the device address of the far end. */
int bt_bnep_adopt(BtBnep *b, int cid, const BtAddress *peer);
```

- [ ] **Step 4: Share the attach path**

In `bluetooth/src/bt_bnep.c`, replace `bnep_on_l2cap_open` (lines 92-117) with:

```c
/* Attach a BNEP channel to an L2CAP channel: derive the link addresses,
 * mark the channel and, on the opening side, send the connection request.
 * Returns the index of the BNEP channel, or -1. */
static int bnep_attach(BtBnep *b, int cid, const BtAddress *peer, int client) {
    int idx = bnep_alloc(b, cid, !client);
    if (idx < 0) return -1;
    BtBnepChan *c = &b->chans[idx];
    if (peer) {
        /* The device address seeds the peer MAC. */
        memcpy(c->peer_mac, peer->b, 6);
        /* Our own address is locally administered and one above it. */
        memcpy(c->our_mac, peer->b, 6);
        c->our_mac[0] |= 0x02;
        c->our_mac[5] = (uint8_t)(peer->b[5] + 1);
    }
    if (client) {
        c->state = BT_BNEP_S_REQ;
        bnep_send_cmd(b, idx, BT_BNEP_CMD_CONN_REQ, NULL, 0);
    } else {
        c->state = BT_BNEP_S_WAIT;
    }
    return idx;
}

static void bnep_on_l2cap_open(void *ctx, BtL2capChan *chan) {
    BtBnep *b = ctx;
    int cid = bt_l2cap_chan_cid(chan);
    int client = 0;
    for (int i = 0; i < BT_MAX_BNEP; i++)
        if (b->outgoing[i] == cid) client = 1;
    BtAddress peer;
    int have = bt_l2cap_chan_peer(chan, &peer) == BT_OK;
    int idx = bnep_attach(b, cid, have ? &peer : NULL, client);
    if (idx < 0) return;
    bt_l2cap_set_chan_data_fn(chan, bnep_on_l2cap_data, b);
    if (b->on_open) b->on_open(b->open_ctx, (BtBnepChan *)&b->chans[idx]);
}
```

- [ ] **Step 5: Implement the adopt path**

In `bluetooth/src/bt_bnep.c`, after `bt_bnep_connect` (line 169), add:

```c
int bt_bnep_adopt(BtBnep *b, int cid, const BtAddress *peer) {
    if (!b || !peer) return BT_EINVAL;
    BtL2capChan *chan = bt_l2cap_chan(b->l2, cid);
    if (!chan) return BT_ENOENT;
    int idx = bnep_attach(b, cid, peer, 1);
    if (idx < 0) return BT_ELIMIT;
    bt_l2cap_set_chan_data_fn(chan, bnep_on_l2cap_data, b);
    return BT_OK;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make -f bluetooth/bluetooth.mk test-bluetooth`
Expected: every Bluetooth test passes and `test_bt_bnep` reports more checks than before and zero failures.

- [ ] **Step 7: Commit**

```bash
git add bluetooth/include/os999_bluetooth.h bluetooth/src/bt_bnep.c bluetooth/tests/test_bt_bnep.c
git commit -m "Run BNEP on a channel the system opened"
```

---

### Task 2: The macOS accelerator reports the end of an inquiry and the host address

The desktop driver needs two more things from the IOBluetooth wrapper: to know when an inquiry finished, so the panel stops saying "Searching", and to know the host controller's own Bluetooth address, so the desktop can stamp it on every frame it sends.

**Files:**
- Modify: `bluetooth/src/bt_darwin.h`
- Modify: `bluetooth/src/bt_darwin.m`

**Interfaces:**
- Consumes: `IOBluetoothHostController.defaultController.addressAsString`, `IOBluetoothDeviceInquiryDelegate.deviceInquiryComplete:error:aborted:`.
- Produces: `int bt_darwin_discover(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn, BtDarwinDoneFn done, void *ctx);` (the `done` parameter is new) and `int bt_darwin_local_address(BtDarwin *d, BtAddress *out);`.

`bt_darwin_discover` has no other callers in the tree, so changing its signature is safe.

- [ ] **Step 1: Declare the new interface**

In `bluetooth/src/bt_darwin.h`, replace the `bt_darwin_discover` declaration (line 37) and add the new one:

```c
/* Reported once the inquiry finishes, whether it found anything or not. */
typedef void (*BtDarwinDoneFn)(void *ctx);
/* Run a standard inquiry for the given number of seconds. */
int bt_darwin_discover(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn, BtDarwinDoneFn done, void *ctx);
/* The host controller's own Bluetooth address. */
int bt_darwin_local_address(BtDarwin *d, BtAddress *out);
```

- [ ] **Step 2: Report the end of the inquiry**

In `bluetooth/src/bt_darwin.m`, extend the inquiry delegate (lines 25-39):

```c
@interface BtDarwinInquiry : NSObject <IOBluetoothDeviceInquiryDelegate>
@property(nonatomic, assign) BtDarwinDeviceFn found;
@property(nonatomic, assign) BtDarwinDoneFn done;
@property(nonatomic, assign) void *ctx;
@end

@implementation BtDarwinInquiry
- (void)deviceInquiryDeviceFound:(IOBluetoothDeviceInquiry *)sender device:(IOBluetoothDevice *)device {
    if (!self.found || !device) return;
    const char *text = [[device addressString] UTF8String];
    BtAddress addr;
    if (!text || bt_addr_parse(text, &addr) != BT_OK) return;
    const char *name = [[device name] UTF8String];
    self.found(self.ctx, &addr, name ? name : "", (uint32_t)[device classOfDevice]);
}

- (void)deviceInquiryComplete:(IOBluetoothDeviceInquiry *)sender error:(IOReturn)error aborted:(BOOL)aborted {
    (void)sender; (void)error; (void)aborted;
    if (self.done) self.done(self.ctx);
}
@end
```

- [ ] **Step 3: Wire the callback and the host address**

In `bluetooth/src/bt_darwin.m`, add `BtDarwinDoneFn done;` to `struct BtDarwin` (after `BtDarwinDeviceFn` is not in the struct; add it after `on_close`), and replace `bt_darwin_discover` (lines 106-129) with:

```c
int bt_darwin_discover(BtDarwin *d, uint32_t seconds, BtDarwinDeviceFn fn, BtDarwinDoneFn done, void *ctx) {
    if (!d) return BT_EINVAL;
    bt_darwin_stop_discover(d);
    BtDarwinInquiry *delegate = [[BtDarwinInquiry alloc] init];
    delegate.found = fn;
    delegate.done = done;
    delegate.ctx = ctx;
    IOBluetoothDeviceInquiry *inquiry = [[IOBluetoothDeviceInquiry alloc] initWithDelegate:delegate];
    if (!inquiry) {
        return BT_ENOMEM;
    }
    [inquiry setSearchCriteria:kBluetoothServiceClassMajorAny
               majorDeviceClass:kBluetoothDeviceClassMajorAny
                minorDeviceClass:kBluetoothDeviceClassMinorAny];
    inquiry.inquiryLength = (uint8_t)(seconds ? seconds : 10);
    IOReturn rc = [inquiry start];
    if (rc != kIOReturnSuccess) {
        d->inquiry = nil;
        d->inquiry_delegate = nil;
        return BT_EIO;
    }
    d->inquiry = inquiry;
    d->inquiry_delegate = delegate;
    return BT_OK;
}

int bt_darwin_local_address(BtDarwin *d, BtAddress *out) {
    if (!d || !out) return BT_EINVAL;
    IOBluetoothHostController *controller = [IOBluetoothHostController defaultController];
    const char *text = [[controller addressAsString] UTF8String];
    if (!text || bt_addr_parse(text, out) != BT_OK) return BT_ENOENT;
    return BT_OK;
}
```

- [ ] **Step 4: Compile the accelerator**

Run: `make bt-darwin`
Expected: `build/bluetooth/bt_darwin.o` builds with no warnings. This is a compile check only: no peer device is available, so the macOS path stays unverified at runtime and must be reported that way.

- [ ] **Step 5: Commit**

```bash
git add bluetooth/src/bt_darwin.h bluetooth/src/bt_darwin.m
git commit -m "Report the end of an inquiry and the host address"
```

---

### Task 3: The desktop guest network service

The desktop owns one broker and one MacTCP layer: the service the runtime hook will hand to programs. The PAN attaches to that broker, so a guest TCP connection to the peer travels over the Bluetooth link and the guest keeps its own address.

The service is created before a PAN exists, so the MacTCP layer has to learn the address when the PAN arrives. `gn_mactcp_new` takes the address once, so this task adds a setter to `net/`.

**Files:**
- Modify: `net/include/os999_guestnet.h` (after `gn_mactcp_new`, line 161)
- Modify: `net/src/gn_mactcp.c` (after `gn_mactcp_new`, line 21)
- Test: `net/tests/test_gn_mactcp.c`
- Create: `include/desktop_guestnet.h`
- Create: `src/desktop_guestnet.c`
- Test: `tests/test_desktop_guestnet.c`

**Interfaces:**
- Consumes: `gn_policy_init`, `gn_policy_parse`, `gn_broker_new`, `gn_broker_set_pan`, `gn_broker_local_ip`, `gn_broker_netmask`, `gn_resolver_new`, `gn_mactcp_new`, `gn_mactcp_set_addr`, `gn_pump` from `net/include/os999_guestnet.h`.
- Produces:
  ```c
  typedef struct DesktopGuestNet DesktopGuestNet;
  DesktopGuestNet *desktop_guestnet_new(void);
  void desktop_guestnet_free(DesktopGuestNet *g);
  int desktop_guestnet_set_pan(DesktopGuestNet *g, GnPan *pan);
  uint32_t desktop_guestnet_local_ip(DesktopGuestNet *g);
  uint32_t desktop_guestnet_netmask(DesktopGuestNet *g);
  GnBroker *desktop_guestnet_broker(DesktopGuestNet *g);
  GnMacTcp *desktop_guestnet_mactcp(DesktopGuestNet *g);
  void desktop_guestnet_pump(DesktopGuestNet *g);
  DesktopGuestNet *desktop_guestnet_service(void);
  void desktop_guestnet_service_stop(void);
  ```
  and in `net/include/os999_guestnet.h`:
  ```c
  /* Give the MacTCP layer a new address, for instance when a personal area
   * network comes up and the guest keeps the address on that link. */
  void gn_mactcp_set_addr(GnMacTcp *m, uint32_t ip, uint32_t netmask);
  ```

- [ ] **Step 1: Write the failing test in `net/`**

Add to `net/tests/test_gn_mactcp.c`, above `main`:

```c
static void test_set_addr(void) {
    gn_mactcp_set_addr(mt, 0xC0A80A01u, 0xFFFFFF00u);
    pb_new(PB, GN_IPCTL_GETADDR, 0);
    CHECK(call(PB, 0) == 0);
    CHECK(r(PB + 28, 4) == 0xC0A80A01u);
    CHECK(r(PB + 32, 4) == 0xFFFFFF00u);
    /* The address never goes back to zero. */
    gn_mactcp_set_addr(mt, 0, 0);
    pb_new(PB, GN_IPCTL_GETADDR, 0);
    CHECK(call(PB, 0) == 0);
    CHECK(r(PB + 28, 4) == 0xC0A80A01u);
    CHECK(r(PB + 32, 4) == 0xFFFFFF00u);
}
```

Call it from `main`, before `return`:

```c
    test_set_addr();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -f net/net.mk test-net`
Expected: the build fails with `implicit declaration of function 'gn_mactcp_set_addr'`.

- [ ] **Step 3: Implement the setter**

In `net/include/os999_guestnet.h`, after the `gn_mactcp_new` declaration (line 161), add:

```c
/* Give the MacTCP layer a new address, for instance when a personal area
 * network comes up and the guest keeps the address on that link. Zero keeps
 * the current value. */
void gn_mactcp_set_addr(GnMacTcp *m, uint32_t ip, uint32_t netmask);
```

In `net/src/gn_mactcp.c`, after `gn_mactcp_new` (line 21), add:

```c
void gn_mactcp_set_addr(GnMacTcp *m, uint32_t ip, uint32_t netmask) {
    if (!m) return;
    if (ip) m->our_ip = ip;
    if (netmask) m->netmask = netmask;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make -f net/net.mk test-net`
Expected: every guest network test passes, zero failures.

- [ ] **Step 5: Write the failing test for the desktop service**

Create `tests/test_desktop_guestnet.c`:

```c
/* The desktop guest network service: the broker, the MacTCP layer and the
 * built-in policy, with a personal area network attached. */
#include "desktop_guestnet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* A link that keeps every frame, so the test can look at what the guest
 * stack sends. */
#define FRAMES 16
static uint8_t frames[FRAMES][128];
static size_t frame_len[FRAMES];
static int nframes;

static int link_tx(void *ctx, const void *frame, size_t len) {
    (void)ctx;
    if (nframes >= FRAMES || len > sizeof frames[0]) return GN_EIO;
    memcpy(frames[nframes], frame, len);
    frame_len[nframes] = len;
    nframes++;
    return GN_OK;
}

int main(void) {
    DesktopGuestNet *g = desktop_guestnet_new();
    CHECK(g != NULL);
    if (!g) return 1;
    CHECK(desktop_guestnet_broker(g) != NULL);
    CHECK(desktop_guestnet_mactcp(g) != NULL);
    CHECK(desktop_guestnet_local_ip(g) == 0);
    CHECK(desktop_guestnet_netmask(g) == 0);

    /* A personal area network gives the guest its own address. */
    GnPanLink link = {.tx = link_tx, .ctx = NULL};
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t peer_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x07};
    GnPan *pan = gn_pan_new(&link, 0xC0A80A01u, 0xFFFFFF00u, mac);
    CHECK(pan != NULL);
    CHECK(gn_pan_set_peer(pan, 0xC0A80A02u, peer_mac) == GN_OK);
    CHECK(desktop_guestnet_set_pan(g, pan) == GN_OK);
    CHECK(desktop_guestnet_local_ip(g) == 0xC0A80A01u);
    CHECK(desktop_guestnet_netmask(g) == 0xFFFFFF00u);
    CHECK(gn_broker_local_ip(desktop_guestnet_broker(g)) == 0xC0A80A01u);

    /* The guest asks for its address and gets the one on the link. */
    uint8_t ram[1 << 16];
    GnMem mem = {ram, sizeof ram};
    memset(ram, 0, sizeof ram);
    uint32_t pb = 0x1000;
    memset(ram + pb, 0, 128);
    gn_mactcp_control(desktop_guestnet_mactcp(g), &mem, pb, 0, 1000);
    CHECK((uint16_t)((ram[pb + 16] << 8) | ram[pb + 17]) == 0);
    CHECK(((uint32_t)ram[pb + 28] << 24 | (uint32_t)ram[pb + 29] << 16 |
           (uint32_t)ram[pb + 30] << 8 | ram[pb + 31]) == 0xC0A80A01u);
    CHECK(((uint32_t)ram[pb + 32] << 24 | (uint32_t)ram[pb + 33] << 16 |
           (uint32_t)ram[pb + 34] << 8 | ram[pb + 35]) == 0xFFFFFF00u);

    /* Detaching the link leaves the guest without a link address. */
    CHECK(desktop_guestnet_set_pan(g, NULL) == GN_OK);
    CHECK(desktop_guestnet_local_ip(g) == 0);

    gn_pan_free(pan);
    desktop_guestnet_free(g);
    printf("desktop guest network: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 6: Run the test to verify it fails**

Run: `make build/test_desktop_guestnet`
Expected: the build fails because `include/desktop_guestnet.h` does not exist.

- [ ] **Step 7: Write the service header**

Create `include/desktop_guestnet.h`:

```c
/* The desktop's guest network service.
 *
 * The desktop owns one broker and one MacTCP layer: the service the runtime
 * hook will hand to programs. A Bluetooth personal area network attaches to
 * that broker, so a guest TCP connection to the peer travels over the
 * Bluetooth link and the guest keeps its own address there. */
#ifndef OS999_DESKTOP_GUESTNET_H
#define OS999_DESKTOP_GUESTNET_H

#include "os999_guestnet.h"

typedef struct DesktopGuestNet DesktopGuestNet;

DesktopGuestNet *desktop_guestnet_new(void);
void desktop_guestnet_free(DesktopGuestNet *g);

/* Attach a personal area network, or detach it with NULL. */
int desktop_guestnet_set_pan(DesktopGuestNet *g, GnPan *pan);
/* The address the guest has on the link; 0 without one. */
uint32_t desktop_guestnet_local_ip(DesktopGuestNet *g);
uint32_t desktop_guestnet_netmask(DesktopGuestNet *g);

GnBroker *desktop_guestnet_broker(DesktopGuestNet *g);
GnMacTcp *desktop_guestnet_mactcp(DesktopGuestNet *g);
/* Move bytes on the guest links. */
void desktop_guestnet_pump(DesktopGuestNet *g);

/* The desktop's one service, created on first use. */
DesktopGuestNet *desktop_guestnet_service(void);
void desktop_guestnet_service_stop(void);

#endif
```

- [ ] **Step 8: Write the service**

Create `src/desktop_guestnet.c`:

```c
/* The desktop's guest network service: one broker, one MacTCP layer and a
 * built-in policy, so no policy file has to be present for the guest to
 * reach a peer over a personal area network. */
#include "desktop_guestnet.h"

#include <stdlib.h>
#include <string.h>

/* The built-in policy: guest programs may use plaintext traffic, local
 * destinations are permitted, and every protocol to every host is allowed.
 * The policy file and the desktop policy window belong to the runtime hook
 * work and are out of scope here. */
static const char *k_builtin_policy =
    "legacy on\n"
    "local on\n"
    "allow both any * *\n";

struct DesktopGuestNet {
    GnPolicy policy;
    GnBroker *broker;
    GnResolver *resolver;
    GnMacTcp *mactcp;
};

DesktopGuestNet *desktop_guestnet_new(void) {
    DesktopGuestNet *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    gn_policy_init(&g->policy);
    if (gn_policy_parse(&g->policy, k_builtin_policy) != 0) {
        free(g);
        return NULL;
    }
    g->broker = gn_broker_new(&g->policy, "OS999", NULL, NULL, NULL);
    g->resolver = gn_resolver_new();
    if (!g->broker || !g->resolver) {
        desktop_guestnet_free(g);
        return NULL;
    }
    g->mactcp = gn_mactcp_new(g->broker, g->resolver, gn_broker_local_ip(g->broker));
    if (!g->mactcp) {
        desktop_guestnet_free(g);
        return NULL;
    }
    return g;
}

void desktop_guestnet_free(DesktopGuestNet *g) {
    if (!g) return;
    gn_mactcp_free(g->mactcp);
    gn_resolver_free(g->resolver);
    gn_broker_free(g->broker);
    free(g);
}

int desktop_guestnet_set_pan(DesktopGuestNet *g, GnPan *pan) {
    if (!g) return GN_EINVAL;
    int rc = gn_broker_set_pan(g->broker, pan);
    if (rc != GN_OK) return rc;
    /* The guest keeps the address it has on the link. */
    gn_mactcp_set_addr(g->mactcp, gn_broker_local_ip(g->broker), gn_broker_netmask(g->broker));
    return GN_OK;
}

uint32_t desktop_guestnet_local_ip(DesktopGuestNet *g) {
    return g ? gn_broker_local_ip(g->broker) : 0;
}

uint32_t desktop_guestnet_netmask(DesktopGuestNet *g) {
    return g ? gn_broker_netmask(g->broker) : 0;
}

GnBroker *desktop_guestnet_broker(DesktopGuestNet *g) { return g ? g->broker : NULL; }
GnMacTcp *desktop_guestnet_mactcp(DesktopGuestNet *g) { return g ? g->mactcp : NULL; }

void desktop_guestnet_pump(DesktopGuestNet *g) {
    if (!g) return;
    gn_pump(g->broker, 0);
}

/* The desktop keeps one service for its whole life. */
static DesktopGuestNet *g_service;

DesktopGuestNet *desktop_guestnet_service(void) {
    if (!g_service) g_service = desktop_guestnet_new();
    return g_service;
}

void desktop_guestnet_service_stop(void) {
    desktop_guestnet_free(g_service);
    g_service = NULL;
}
```

- [ ] **Step 9: Add the test target**

In `config/desktop-tests.mk`, after the `include bluetooth/bluetooth.mk` line and its `test: test-bluetooth`, add:

```make
# The desktop guest network service (the broker, the MacTCP layer, the policy).
build/test_desktop_guestnet: src/desktop_guestnet.c tests/test_desktop_guestnet.c include/desktop_guestnet.h $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -o $@ src/desktop_guestnet.c tests/test_desktop_guestnet.c $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
test: build/test_desktop_guestnet
```

- [ ] **Step 10: Run the test to verify it passes**

Run: `make build/test_desktop_guestnet && ./build/test_desktop_guestnet`
Expected: `desktop guest network: 14 checks, 0 failures`.

- [ ] **Step 11: Commit**

```bash
git add net/include/os999_guestnet.h net/src/gn_mactcp.c net/tests/test_gn_mactcp.c include/desktop_guestnet.h src/desktop_guestnet.c tests/test_desktop_guestnet.c config/desktop-tests.mk
git commit -m "Give the desktop its own guest network service"
```

---

### Task 4: The desktop Bluetooth manager and the driver seam

The manager owns the device list, the scan state, the link state, and the PAN behind the link. It never talks to a radio directly: it goes through a driver, so the macOS radio and the controller simulator are interchangeable and the whole flow can run headlessly.

**Files:**
- Create: `include/desktop_bluetooth.h`
- Create: `src/desktop_bluetooth.c`
- Create: `src/desktop_bt_none.c`
- Test: `tests/test_desktop_bluetooth.c`

**Interfaces:**
- Consumes: `bt_addr_parse`, `bt_addr_format`, `bt_addr_cmp`, `BtAddress` from `bluetooth/include/os999_bluetooth.h`; `GnPan` from `net/include/os999_guestnet.h`; the guest network service from `include/desktop_guestnet.h`.
- Produces: the manager API and the driver seam, exactly as written below. The panel (Task 5) and the drivers (Task 7) use these names.

```c
#ifndef OS999_DESKTOP_BLUETOOTH_H
#define OS999_DESKTOP_BLUETOOTH_H

#include "os999_bluetooth.h"
#include "os999_guestnet.h"
#include "desktop_guestnet.h"

/* The link states the panel shows. */
enum { DBT_IDLE = 0, DBT_SCANNING, DBT_CONNECTING, DBT_UP, DBT_FAILED };
enum { DBT_MAX_DEVICES = 32, DBT_NAME_MAX = 64 };

/* The address the desktop has on the link, and the peer's. */
#define DBT_PAN_LOCAL_IP 0xC0A80A01u  /* 192.168.10.1 */
#define DBT_PAN_PEER_IP  0xC0A80A02u  /* 192.168.10.2 */
#define DBT_PAN_NETMASK  0xFFFFFF00u  /* 255.255.255.0 */

typedef struct {
    BtAddress addr;
    char name[DBT_NAME_MAX];
    uint32_t class_of_device;
} DesktopBtDevice;

typedef struct DesktopBtDriver DesktopBtDriver;

/* What the driver reports. */
typedef struct {
    void (*found)(void *ctx, const BtAddress *addr, const char *name, uint32_t cod);
    void (*scan_done)(void *ctx);
    void (*up)(void *ctx);
    void (*down)(void *ctx);
    void (*failed)(void *ctx, int error);
    void *ctx;
} DesktopBtCallbacks;

/* What the driver does. link_up returns BT_OK when the link is already up,
 * BT_EAGAIN when it is still coming up (the driver then reports up), or a
 * negative BT_* code when it failed. */
typedef struct {
    DesktopBtDriver *(*create)(const DesktopBtCallbacks *cb);
    int  (*scan)(DesktopBtDriver *d, int seconds);
    int  (*link_up)(DesktopBtDriver *d, const BtAddress *peer);
    void (*link_down)(DesktopBtDriver *d);
    void (*poll)(DesktopBtDriver *d);
    GnPan *(*pan)(DesktopBtDriver *d);
    void (*free)(DesktopBtDriver *d);
} DesktopBtDriverOps;

typedef struct DesktopBtManager DesktopBtManager;

DesktopBtManager *desktop_bt_new(const DesktopBtDriverOps *ops, DesktopGuestNet *net);
void desktop_bt_free(DesktopBtManager *m);

/* Start a scan. The devices that answer arrive through the driver. */
int desktop_bt_scan(DesktopBtManager *m, int seconds);
/* Bring the link up to a peer. */
int desktop_bt_connect(DesktopBtManager *m, const BtAddress *peer);
void desktop_bt_disconnect(DesktopBtManager *m);
/* Move the frames that arrived on the link into the guest stack. */
void desktop_bt_poll(DesktopBtManager *m);

int desktop_bt_state(const DesktopBtManager *m);
const char *desktop_bt_state_text(const DesktopBtManager *m);
int desktop_bt_error(const DesktopBtManager *m);
int desktop_bt_device_count(const DesktopBtManager *m);
const DesktopBtDevice *desktop_bt_device_at(const DesktopBtManager *m, int index);
const DesktopBtDevice *desktop_bt_device_find(const DesktopBtManager *m, const BtAddress *addr);
GnPan *desktop_bt_pan(const DesktopBtManager *m);
const BtAddress *desktop_bt_peer(const DesktopBtManager *m);
/* The address the guest has on the link. */
uint32_t desktop_bt_local_ip(const DesktopBtManager *m);

/* The desktop's one manager. ops NULL means the platform driver. */
int desktop_bt_start(const DesktopBtDriverOps *ops, DesktopGuestNet *net);
void desktop_bt_stop(void);
DesktopBtManager *desktop_bt_manager(void);

/* The driver for this platform, and the reason when there is none. */
const DesktopBtDriverOps *desktop_bt_platform_ops(void);
int desktop_bt_available(void);
const char *desktop_bt_unavailable_reason(void);

/* The drivers themselves. */
const DesktopBtDriverOps *desktop_bt_none_ops(void);
const DesktopBtDriverOps *desktop_bt_darwin_ops(void); /* macOS only */

#endif
```

- [ ] **Step 1: Write the failing test**

Create `tests/test_desktop_bluetooth.c` with a fake driver. The fake driver reports two devices, brings a link up over a loopback PAN and takes it down again:

```c
/* The desktop Bluetooth manager over a fake driver: devices arrive, a link
 * comes up, the guest stack gets the address on the link, and the link goes
 * down again. */
#include "desktop_bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* ---- the fake driver ---- */

typedef struct {
    DesktopBtCallbacks cb;
    GnPanLink link;
    GnPan *pan;
    BtAddress peer;
    int scanned, linked, closed, freed;
} FakeDriver;

static int fake_tx(void *ctx, const void *frame, size_t len) {
    (void)ctx; (void)frame; (void)len;
    return GN_OK;
}

static DesktopBtDriver *fake_create(const DesktopBtCallbacks *cb) {
    FakeDriver *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cb = *cb;
    d->link.tx = fake_tx;
    d->link.ctx = d;
    return (DesktopBtDriver *)d;
}

static int fake_scan(DesktopBtDriver *driver, int seconds) {
    FakeDriver *d = (FakeDriver *)driver;
    (void)seconds;
    d->scanned++;
    BtAddress a, b;
    bt_addr_parse("00:00:00:00:00:07", &a);
    bt_addr_parse("00:00:00:00:00:08", &b);
    d->cb.found(d->cb.ctx, &a, "Peer One", 0x00010C);
    d->cb.found(d->cb.ctx, &b, "Peer Two", 0x000104);
    d->cb.found(d->cb.ctx, &a, "Peer One", 0x00010C); /* The same device again. */
    d->cb.scan_done(d->cb.ctx);
    return BT_OK;
}

static int fake_link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    FakeDriver *d = (FakeDriver *)driver;
    d->linked++;
    d->peer = *peer;
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t peer_mac[6];
    memcpy(peer_mac, peer->b, 6);
    d->pan = gn_pan_new(&d->link, DBT_PAN_LOCAL_IP, DBT_PAN_NETMASK, mac);
    if (!d->pan) return BT_ENOMEM;
    gn_pan_set_peer(d->pan, DBT_PAN_PEER_IP, peer_mac);
    return BT_OK;
}

static void fake_link_down(DesktopBtDriver *driver) {
    FakeDriver *d = (FakeDriver *)driver;
    d->closed++;
    gn_pan_free(d->pan);
    d->pan = NULL;
}

static void fake_poll(DesktopBtDriver *driver) { (void)driver; }
static GnPan *fake_pan(DesktopBtDriver *driver) { return ((FakeDriver *)driver)->pan; }
static void fake_free(DesktopBtDriver *driver) {
    FakeDriver *d = (FakeDriver *)driver;
    d->freed++;
    gn_pan_free(d->pan);
    free(d);
}

static const DesktopBtDriverOps k_fake_ops = {
    .create = fake_create,
    .scan = fake_scan,
    .link_up = fake_link_up,
    .link_down = fake_link_down,
    .poll = fake_poll,
    .pan = fake_pan,
    .free = fake_free,
};

/* ---- the test ---- */

int main(void) {
    DesktopGuestNet *net = desktop_guestnet_new();
    CHECK(net != NULL);
    DesktopBtManager *m = desktop_bt_new(&k_fake_ops, net);
    CHECK(m != NULL);
    if (!m) return 1;

    /* Nothing has happened yet. */
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_device_count(m) == 0);
    CHECK(desktop_bt_pan(m) == NULL);
    CHECK(desktop_bt_local_ip(m) == 0);

    /* A scan reports the devices that answer, once each. */
    CHECK(desktop_bt_scan(m, 5) == BT_OK);
    CHECK(desktop_bt_device_count(m) == 2);
    const DesktopBtDevice *d0 = desktop_bt_device_at(m, 0);
    const DesktopBtDevice *d1 = desktop_bt_device_at(m, 1);
    CHECK(d0 && strcmp(d0->name, "Peer One") == 0);
    CHECK(d1 && strcmp(d1->name, "Peer Two") == 0);
    CHECK(d0 && d0->class_of_device == 0x00010C);
    CHECK(desktop_bt_device_at(m, 2) == NULL);

    /* The manager finds a device by address. */
    BtAddress peer;
    bt_addr_parse("00:00:00:00:00:08", &peer);
    const DesktopBtDevice *found = desktop_bt_device_find(m, &peer);
    CHECK(found && found == d1);

    /* Clicking a device brings the link up. */
    CHECK(desktop_bt_connect(m, &peer) == BT_OK);
    CHECK(desktop_bt_state(m) == DBT_UP);
    CHECK(desktop_bt_pan(m) != NULL);
    CHECK(desktop_bt_peer(m) && bt_addr_cmp(desktop_bt_peer(m), &peer) == 0);

    /* The guest stack keeps its own address on the link. */
    CHECK(desktop_guestnet_local_ip(net) == DBT_PAN_LOCAL_IP);
    CHECK(desktop_guestnet_netmask(net) == DBT_PAN_NETMASK);
    CHECK(desktop_bt_local_ip(m) == DBT_PAN_LOCAL_IP);

    /* The link goes down and the guest stack is untouched again. */
    desktop_bt_disconnect(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_pan(m) == NULL);
    CHECK(desktop_guestnet_local_ip(net) == 0);

    /* A peer that is not in the list is refused. */
    BtAddress unknown;
    bt_addr_parse("00:00:00:00:00:09", &unknown);
    CHECK(desktop_bt_connect(m, &unknown) == BT_ENOENT);

    desktop_bt_free(m);
    desktop_guestnet_free(net);
    printf("desktop bluetooth: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_desktop_bluetooth`
Expected: the build fails because `include/desktop_bluetooth.h` does not exist.

- [ ] **Step 3: Write the manager header**

Create `include/desktop_bluetooth.h` with the interface block given above.

- [ ] **Step 4: Write the manager**

Create `src/desktop_bluetooth.c`:

```c
/* The desktop's Bluetooth manager.
 *
 * It owns the list of devices an inquiry found, the state of the link and
 * the personal area network behind the link. It never talks to a radio
 * itself: the driver reports what it finds and how the link came up, so the
 * macOS radio and the controller simulator are interchangeable. */
#include "desktop_bluetooth.h"

#include <stdlib.h>
#include <string.h>

struct DesktopBtManager {
    const DesktopBtDriverOps *ops;
    DesktopBtDriver *driver;
    DesktopGuestNet *net;
    DesktopBtCallbacks cb;
    DesktopBtDevice devices[DBT_MAX_DEVICES];
    int ndevices;
    int state;
    int error;
    BtAddress peer;
    int have_peer;
};

static void manager_found(void *ctx, const BtAddress *addr, const char *name, uint32_t cod) {
    DesktopBtManager *m = ctx;
    if (!m || !addr) return;
    for (int i = 0; i < m->ndevices; i++) {
        if (bt_addr_cmp(&m->devices[i].addr, addr) == 0) {
            /* A device we already know keeps its name. */
            if (name && name[0] && !m->devices[i].name[0])
                snprintf(m->devices[i].name, sizeof m->devices[i].name, "%s", name);
            return;
        }
    }
    if (m->ndevices >= DBT_MAX_DEVICES) return;
    DesktopBtDevice *d = &m->devices[m->ndevices++];
    memset(d, 0, sizeof *d);
    d->addr = *addr;
    d->class_of_device = cod;
    if (name) snprintf(d->name, sizeof d->name, "%s", name);
}

static void manager_scan_done(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m || m->state != DBT_SCANNING) return;
    m->state = DBT_IDLE;
}

static void manager_up(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m) return;
    m->state = DBT_UP;
    m->error = 0;
    /* The guest stack keeps the address it has on the link. */
    if (m->net && m->ops->pan) desktop_guestnet_set_pan(m->net, m->ops->pan(m->driver));
}

static void manager_down(void *ctx) {
    DesktopBtManager *m = ctx;
    if (!m) return;
    m->state = DBT_IDLE;
    m->have_peer = 0;
    if (m->net) desktop_guestnet_set_pan(m->net, NULL);
}

static void manager_failed(void *ctx, int error) {
    DesktopBtManager *m = ctx;
    if (!m) return;
    m->state = DBT_FAILED;
    m->error = error;
    if (m->net) desktop_guestnet_set_pan(m->net, NULL);
}

DesktopBtManager *desktop_bt_new(const DesktopBtDriverOps *ops, DesktopGuestNet *net) {
    if (!ops || !ops->create) return NULL;
    DesktopBtManager *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->ops = ops;
    m->net = net;
    m->cb.found = manager_found;
    m->cb.scan_done = manager_scan_done;
    m->cb.up = manager_up;
    m->cb.down = manager_down;
    m->cb.failed = manager_failed;
    m->cb.ctx = m;
    m->driver = ops->create(&m->cb);
    if (!m->driver) {
        free(m);
        return NULL;
    }
    return m;
}

void desktop_bt_free(DesktopBtManager *m) {
    if (!m) return;
    if (m->ops->free) m->ops->free(m->driver);
    free(m);
}

int desktop_bt_scan(DesktopBtManager *m, int seconds) {
    if (!m || !m->ops->scan) return BT_EINVAL;
    if (m->state == DBT_UP) return BT_EBUSY;
    m->ndevices = 0;
    m->state = DBT_SCANNING;
    m->error = 0;
    int rc = m->ops->scan(m->driver, seconds);
    if (rc != BT_OK) {
        m->state = DBT_FAILED;
        m->error = rc;
    }
    return rc;
}

int desktop_bt_connect(DesktopBtManager *m, const BtAddress *peer) {
    if (!m || !peer || !m->ops->link_up) return BT_EINVAL;
    if (m->state == DBT_UP) return BT_EBUSY;
    if (!desktop_bt_device_find(m, peer)) return BT_ENOENT;
    m->peer = *peer;
    m->have_peer = 1;
    m->state = DBT_CONNECTING;
    m->error = 0;
    int rc = m->ops->link_up(m->driver, peer);
    if (rc == BT_OK) {
        /* The link is already up. */
        manager_up(m);
        return BT_OK;
    }
    if (rc == BT_EAGAIN) return BT_OK; /* The driver reports it later. */
    m->state = DBT_FAILED;
    m->error = rc;
    return rc;
}

void desktop_bt_disconnect(DesktopBtManager *m) {
    if (!m) return;
    if (m->state == DBT_UP && m->ops->link_down) m->ops->link_down(m->driver);
    manager_down(m);
}

void desktop_bt_poll(DesktopBtManager *m) {
    if (!m) return;
    if (m->ops->poll) m->ops->poll(m->driver);
}

int desktop_bt_state(const DesktopBtManager *m) { return m ? m->state : DBT_IDLE; }

const char *desktop_bt_state_text(const DesktopBtManager *m) {
    switch (desktop_bt_state(m)) {
    case DBT_SCANNING: return "Searching for devices...";
    case DBT_CONNECTING: return "Connecting...";
    case DBT_UP: return "Connected.";
    case DBT_FAILED: return "The link could not be brought up.";
    default: return "Ready.";
    }
}

int desktop_bt_error(const DesktopBtManager *m) { return m ? m->error : 0; }

int desktop_bt_device_count(const DesktopBtManager *m) { return m ? m->ndevices : 0; }

const DesktopBtDevice *desktop_bt_device_at(const DesktopBtManager *m, int index) {
    if (!m || index < 0 || index >= m->ndevices) return NULL;
    return &m->devices[index];
}

const DesktopBtDevice *desktop_bt_device_find(const DesktopBtManager *m, const BtAddress *addr) {
    if (!m || !addr) return NULL;
    for (int i = 0; i < m->ndevices; i++)
        if (bt_addr_cmp(&m->devices[i].addr, addr) == 0) return &m->devices[i];
    return NULL;
}

GnPan *desktop_bt_pan(const DesktopBtManager *m) {
    if (!m || !m->ops->pan) return NULL;
    return m->ops->pan(m->driver);
}

const BtAddress *desktop_bt_peer(const DesktopBtManager *m) {
    if (!m || !m->have_peer) return NULL;
    return &m->peer;
}

uint32_t desktop_bt_local_ip(const DesktopBtManager *m) {
    if (!m || !m->net) return 0;
    return desktop_guestnet_local_ip(m->net);
}

/* The desktop keeps one manager. */
static DesktopBtManager *g_manager;

int desktop_bt_start(const DesktopBtDriverOps *ops, DesktopGuestNet *net) {
    desktop_bt_stop();
    const DesktopBtDriverOps *use = ops ? ops : desktop_bt_platform_ops();
    if (!use) return BT_ENOENT;
    g_manager = desktop_bt_new(use, net);
    return g_manager ? BT_OK : BT_ENOMEM;
}

void desktop_bt_stop(void) {
    desktop_bt_free(g_manager);
    g_manager = NULL;
}

DesktopBtManager *desktop_bt_manager(void) { return g_manager; }
```

The file also needs `#include <stdio.h>` for `snprintf`.

- [ ] **Step 5: Write the driver for platforms without a radio**

Create `src/desktop_bt_none.c`:

```c
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

const char *desktop_bt_unavailable_reason(void) { return "no Bluetooth radio on this platform"; }
```

- [ ] **Step 6: Add the test target**

In `config/desktop-tests.mk`, after the guest network target, add:

```make
# The desktop Bluetooth manager over a fake driver.
build/test_desktop_bluetooth: src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_desktop_bluetooth.c include/desktop_bluetooth.h include/desktop_guestnet.h $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -o $@ src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_desktop_bluetooth.c $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
test: build/test_desktop_bluetooth
```

- [ ] **Step 7: Run the test to verify it passes**

Run: `make build/test_desktop_bluetooth && ./build/test_desktop_bluetooth`
Expected: `desktop bluetooth: 25 checks, 0 failures`.

- [ ] **Step 8: Commit**

```bash
git add include/desktop_bluetooth.h src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_desktop_bluetooth.c config/desktop-tests.mk
git commit -m "Add the desktop Bluetooth manager and its driver seam"
```

---

### Task 5: The Bluetooth panel

The panel looks like the Appearance panel: a device list with a scroll, a status line, and a Disconnect button once the link is up. It reaches the manager through `desktop_bt_manager()`.

**Files:**
- Create: `include/bluetooth_panel.h`
- Create: `src/bluetooth_panel.c`
- Test: `tests/test_bluetooth_panel.c`

**Interfaces:**
- Consumes: the manager API from `include/desktop_bluetooth.h`; `fb_fill`, `fb_frame`, `fb_text`, `fb_text_width`, `fb_clip`, `fb_unclip`, `Fb` from `include/draw.h`; `desktop_theme_color`, `THEME_*` from `include/desktop_theme.h`; `Session`, `Rect` from `include/ospi.h`.
- Produces:
  ```c
  void bluetooth_panel_open(Session *s);
  int bluetooth_panel_active(void);
  int bluetooth_panel_is_open(void);
  int bluetooth_panel_regions(const Session *s, Rect out[2]);
  void bluetooth_panel_close(void);
  void bluetooth_panel_deactivate(void);
  void bluetooth_panel_blur(void);
  void bluetooth_panel_draw(Session *s, Fb *fb);
  int bluetooth_panel_down(Session *s, int x, int y);
  int bluetooth_panel_key(Session *s, int key, int mods);
  void bluetooth_panel_init(void);   /* registers the panel with the desktop */
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_bluetooth_panel.c`:

```c
/* The Bluetooth panel: the device list, the status line, the clicks and the
 * keys. The manager is the fake driver from the manager test. */
#include "bluetooth_panel.h"
#include "desktop_bluetooth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* The hook seam and the theme, so the panel can register itself and draw in
 * this fixture without the rest of the desktop. */
static const SessionBluetoothHooks *g_hooks;
void session_set_bluetooth_hooks(const SessionBluetoothHooks *hooks) { g_hooks = hooks; }
const SessionBluetoothHooks *session_bluetooth_hooks(void) { return g_hooks; }
int desktop_theme_count(void) { return 1; }
int desktop_theme_current(void) { return 0; }
const char *desktop_theme_name(int index) { (void)index; return "Platinum"; }
uint32_t desktop_theme_color(int role) { (void)role; return C_WHITE; }

/* ---- a fake driver ---- */

typedef struct {
    DesktopBtCallbacks cb;
    int linked, closed;
    BtAddress peer;
} FakeDriver;

static DesktopBtDriver *fake_create(const DesktopBtCallbacks *cb) {
    FakeDriver *d = calloc(1, sizeof *d);
    if (d) d->cb = *cb;
    return (DesktopBtDriver *)d;
}
static int fake_scan(DesktopBtDriver *driver, int seconds) {
    FakeDriver *d = (FakeDriver *)driver;
    (void)seconds;
    BtAddress a;
    bt_addr_parse("00:00:00:00:00:07", &a);
    d->cb.found(d->cb.ctx, &a, "Peer One", 0x00010C);
    d->cb.scan_done(d->cb.ctx);
    return BT_OK;
}
static int fake_link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    FakeDriver *d = (FakeDriver *)driver;
    d->linked++;
    d->peer = *peer;
    return BT_OK;
}
static void fake_link_down(DesktopBtDriver *driver) { ((FakeDriver *)driver)->closed++; }
static void fake_poll(DesktopBtDriver *driver) { (void)driver; }
static GnPan *fake_pan(DesktopBtDriver *driver) { (void)driver; return NULL; }
static void fake_free(DesktopBtDriver *driver) { free(driver); }

static const DesktopBtDriverOps k_fake_ops = {
    .create = fake_create, .scan = fake_scan, .link_up = fake_link_up,
    .link_down = fake_link_down, .poll = fake_poll, .pan = fake_pan, .free = fake_free,
};

/* ---- the test ---- */

int main(void) {
    Session s = {.sw = OSPI_W, .sh = OSPI_H};
    bluetooth_panel_close();
    CHECK(!bluetooth_panel_active());
    CHECK(!bluetooth_panel_down(&s, 10, 40));

    CHECK(desktop_bt_start(&k_fake_ops, NULL) == BT_OK);
    bluetooth_panel_open(&s);
    CHECK(bluetooth_panel_is_open());
    CHECK(bluetooth_panel_active());

    /* The panel scans when it opens. */
    CHECK(desktop_bt_manager() != NULL);
    CHECK(desktop_bt_device_count(desktop_bt_manager()) == 1);

    Rect regions[2];
    CHECK(bluetooth_panel_regions(&s, regions) == 1);
    CHECK(regions[0].w > 100 && regions[0].h > 100);

    /* Clicking the device row brings the link up. */
    Rect row = bluetooth_panel_row_rect(&s, 0);
    CHECK(row.w > 0);
    CHECK(bluetooth_panel_down(&s, row.x + 10, row.y + 4));
    CHECK(desktop_bt_state(desktop_bt_manager()) == DBT_UP);

    /* The Disconnect button takes the link down. */
    Rect off = bluetooth_panel_disconnect_rect(&s);
    CHECK(off.w > 0);
    CHECK(bluetooth_panel_down(&s, off.x + 5, off.y + 5));
    CHECK(desktop_bt_state(desktop_bt_manager()) == DBT_IDLE);

    /* The panel draws headlessly. */
    uint32_t *pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Fb fb;
        fb_init(&fb, pixels, OSPI_W, OSPI_H);
        bluetooth_panel_draw(&s, &fb);
        int painted = 0;
        for (int i = 0; i < OSPI_W * OSPI_H; i++) painted |= pixels[i] != 0;
        CHECK(painted);
        free(pixels);
    }

    /* Escape closes the panel. */
    CHECK(bluetooth_panel_key(&s, 27, 0));
    CHECK(!bluetooth_panel_active());

    bluetooth_panel_close();
    desktop_bt_stop();
    printf("bluetooth panel: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

The test uses two helpers the panel exposes so the test does not have to know the geometry:

```c
/* The row of a device in the list, and the Disconnect button. */
Rect bluetooth_panel_row_rect(const Session *s, int index);
Rect bluetooth_panel_disconnect_rect(const Session *s);
```

Add both to `include/bluetooth_panel.h`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_bluetooth_panel`
Expected: the build fails because `include/bluetooth_panel.h` does not exist.

- [ ] **Step 3: Write the panel header**

Create `include/bluetooth_panel.h`:

```c
/* The Bluetooth network panel: the devices an inquiry found, the state of
 * the link and the address the guest has on it. */
#ifndef OS999_BLUETOOTH_PANEL_H
#define OS999_BLUETOOTH_PANEL_H

#include "ospi.h"
#include "draw.h"

void bluetooth_panel_open(Session *s);
int bluetooth_panel_active(void);
int bluetooth_panel_is_open(void);
int bluetooth_panel_regions(const Session *s, Rect out[2]);
void bluetooth_panel_close(void);
void bluetooth_panel_deactivate(void);
void bluetooth_panel_blur(void);
void bluetooth_panel_draw(Session *s, Fb *fb);
int bluetooth_panel_down(Session *s, int x, int y);
int bluetooth_panel_key(Session *s, int key, int mods);
/* Register the panel with the desktop. */
void bluetooth_panel_init(void);

/* The row of a device in the list, and the Disconnect button. */
Rect bluetooth_panel_row_rect(const Session *s, int index);
Rect bluetooth_panel_disconnect_rect(const Session *s);

#endif
```

- [ ] **Step 4: Write the panel**

Create `src/bluetooth_panel.c`:

```c
/* The Bluetooth network panel.
 *
 * It looks like the Appearance panel: a device list with a scroll, a status
 * line and a Disconnect button once the link is up. The manager owns the
 * devices and the link, so the panel only draws them and turns clicks into
 * manager calls. */
#include "bluetooth_panel.h"
#include "desktop_bluetooth.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <string.h>

enum { BT_ROW_H = 25, BT_ROWS = 9, BT_LIST_H = BT_ROW_H * BT_ROWS };
static Session *owner;
static int panel_focus, scroll, selected;

static Rect panel_rect(const Session *s) {
    int sw = s->sw > 0 ? s->sw : OSPI_W;
    int sh = s->sh > 0 ? s->sh : OSPI_H;
    int w = 600, h = 420;
    int x = (sw - w) / 2, y = (sh - h) / 2;
    if (x < 0) x = 0;
    if (y < TITLE_H) y = TITLE_H;
    return (Rect){x, y, w, h};
}

static Rect close_rect(Rect r) { return (Rect){r.x + 8, r.y + 5, 12, 12}; }
static Rect list_rect(Rect r) { return (Rect){r.x + 18, r.y + 72, r.w - 36, BT_LIST_H}; }
static Rect search_rect(Rect r) { return (Rect){r.x + 18, r.y + 330, 120, 27}; }
static Rect disconnect_rect(Rect r) { return (Rect){r.x + r.w - 138, r.y + 330, 120, 27}; }

static DesktopBtManager *manager(void) { return desktop_bt_manager(); }

static int device_count(void) {
    DesktopBtManager *m = manager();
    return m ? desktop_bt_device_count(m) : 0;
}

static const DesktopBtDevice *device_at(int index) {
    DesktopBtManager *m = manager();
    return m ? desktop_bt_device_at(m, index) : NULL;
}

Rect bluetooth_panel_row_rect(const Session *s, int index) {
    Rect r = panel_rect(s);
    Rect list = list_rect(r);
    int count = device_count();
    if (index < 0 || index >= count) return (Rect){0, 0, 0, 0};
    if (index < scroll) scroll = index;
    if (index >= scroll + BT_ROWS) scroll = index - BT_ROWS + 1;
    return (Rect){list.x + 1, list.y + 1 + (index - scroll) * BT_ROW_H, list.w - 2, BT_ROW_H};
}

Rect bluetooth_panel_disconnect_rect(const Session *s) {
    return disconnect_rect(panel_rect(s));
}

int bluetooth_panel_is_open(void) { return owner != NULL; }

int bluetooth_panel_active(void) {
    return owner && panel_focus && !owner->media_focused && !owner->browser_focused &&
           !owner->guest_focused && !owner->finder_hidden;
}

void bluetooth_panel_open(Session *s) {
    if (!s) return;
    owner = s;
    panel_focus = 1;
    scroll = selected = 0;
    s->down = s->drag = 0;
    s->menu = s->menu_hot = -1;
    DesktopBtManager *m = manager();
    if (m) desktop_bt_scan(m, 10);
}

void bluetooth_panel_close(void) {
    if (owner) owner->down = owner->drag = 0;
    owner = NULL;
    panel_focus = scroll = selected = 0;
}

void bluetooth_panel_deactivate(void) { panel_focus = 0; }
void bluetooth_panel_blur(void) { panel_focus = 0; }

int bluetooth_panel_regions(const Session *s, Rect out[2]) {
    if (owner != s || !owner) return 0;
    out[0] = panel_rect(s);
    out[0].w += 3;
    out[0].h += 3;
    return 1;
}

static uint32_t accent(void) {
    return desktop_theme_current() ? desktop_theme_color(THEME_ACCENT) : 0xFF344768u;
}

/* The major class of a device, in the words a person would use. */
static const char *class_name(uint32_t cod) {
    switch ((cod >> 8) & 0x1F) {
    case 0x01: return "Computer";
    case 0x02: return "Phone";
    case 0x03: return "Network";
    case 0x04: return "Audio/Video";
    case 0x05: return "Peripheral";
    case 0x06: return "Imaging";
    case 0x07: return "Toy";
    default: return "Device";
    }
}

static void button(Fb *fb, Rect r, const char *title, int enabled) {
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, r.x, r.y, r.w, r.h,
             enabled ? desktop_theme_color(THEME_FRAME) : desktop_theme_color(THEME_DISABLED));
    int x = r.x + (r.w - fb_text_width(title)) / 2;
    int y = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_text(fb, x, y, enabled ? desktop_theme_color(THEME_FOREGROUND) : desktop_theme_color(THEME_DISABLED),
            title);
}

void bluetooth_panel_draw(Session *s, Fb *fb) {
    if (!s || owner != s || !fb) return;
    Rect r = panel_rect(s);
    uint32_t ink = desktop_theme_color(THEME_FOREGROUND);
    fb_fill(fb, r.x + 3, r.y + 3, r.w, r.h, 0xFF26304Au);
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FRAME));
    for (int y = 4; y < TITLE_H - 2; y += 2)
        fb_hline(fb, r.x + 3, r.y + y, r.w - 6, desktop_theme_color(THEME_STRIPE_B));
    const char *title = "Bluetooth Network";
    int tw = fb_text_width(title);
    fb_fill(fb, r.x + (r.w - tw) / 2 - 5, r.y + 1, tw + 10, TITLE_H - 2, desktop_theme_color(THEME_MENU));
    fb_text(fb, r.x + (r.w - tw) / 2, r.y + 3, ink, title);
    fb_hline(fb, r.x, r.y + TITLE_H, r.w, desktop_theme_color(THEME_FRAME));
    Rect close = close_rect(r);
    fb_fill(fb, close.x, close.y, close.w, close.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, close.x, close.y, close.w, close.h, desktop_theme_color(THEME_FRAME));

    DesktopBtManager *m = manager();
    char status[160];
    if (!m) {
        snprintf(status, sizeof status, "%s.", desktop_bt_unavailable_reason());
    } else if (desktop_bt_state(m) == DBT_UP) {
        const DesktopBtDevice *peer = NULL;
        const BtAddress *addr = desktop_bt_peer(m);
        if (addr) {
            for (int i = 0; i < desktop_bt_device_count(m); i++) {
                const DesktopBtDevice *d = desktop_bt_device_at(m, i);
                if (d && bt_addr_cmp(&d->addr, addr) == 0) peer = d;
            }
        }
        snprintf(status, sizeof status, "Connected to %s.  This computer: 192.168.10.1  Peer: 192.168.10.2",
                 peer && peer->name[0] ? peer->name : "the peer");
    } else if (desktop_bt_state(m) == DBT_CONNECTING) {
        snprintf(status, sizeof status, "Connecting...");
    } else if (desktop_bt_state(m) == DBT_FAILED) {
        snprintf(status, sizeof status, "The link could not be brought up (%s).",
                 bt_status_text(desktop_bt_error(m)));
    } else if (device_count() == 0) {
        snprintf(status, sizeof status, "No devices found. Search again.");
    } else {
        snprintf(status, sizeof status, "%d device%s found.", device_count(), device_count() == 1 ? "" : "s");
    }
    fb_text(fb, r.x + 20, r.y + 34, ink, status);

    Rect list = list_rect(r);
    fb_fill(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_FRAME));
    for (int row = 0; row < BT_ROWS; row++) {
        int index = scroll + row;
        const DesktopBtDevice *d = device_at(index);
        if (!d) break;
        int y = list.y + 1 + row * BT_ROW_H;
        int hot = index == selected;
        if (hot) fb_fill(fb, list.x + 1, y, list.w - 2, BT_ROW_H, accent());
        uint32_t color = hot ? desktop_theme_color(THEME_PANEL) : ink;
        char address[24];
        bt_addr_format(&d->addr, address, sizeof address);
        fb_clip(fb, list.x + 6, y, list.w - 12, BT_ROW_H);
        fb_text(fb, list.x + 8, y + 5, color, hot ? "*" : " ");
        fb_text(fb, list.x + 26, y + 5, color, d->name[0] ? d->name : "Unnamed device");
        fb_text(fb, list.x + 220, y + 5, color, address);
        fb_text(fb, list.x + 340, y + 5, color, class_name(d->class_of_device));
        fb_unclip(fb);
    }
    button(fb, search_rect(r), "Search Again", m != NULL);
    button(fb, disconnect_rect(r), "Disconnect", m && desktop_bt_state(m) == DBT_UP);
    fb_text(fb, r.x + 20, r.y + 389, ink, "The guest keeps its own address on the link.");
}

int bluetooth_panel_down(Session *s, int x, int y) {
    if (!s || owner != s) return 0;
    Rect r = panel_rect(s);
    if (y < TITLE_H || !rect_has(r, x, y)) return 0;
    s->down = s->drag = 0;
    if (!bluetooth_panel_active()) panel_focus = 1;
    if (rect_has(close_rect(r), x, y)) { bluetooth_panel_close(); return 1; }
    DesktopBtManager *m = manager();
    if (rect_has(search_rect(r), x, y)) {
        if (m) desktop_bt_scan(m, 10);
        return 1;
    }
    if (rect_has(disconnect_rect(r), x, y)) {
        if (m) desktop_bt_disconnect(m);
        return 1;
    }
    Rect list = list_rect(r);
    if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
        int row = (y - list.y - 1) / BT_ROW_H;
        int index = scroll + row;
        const DesktopBtDevice *d = device_at(index);
        if (!d) return 1;
        selected = index;
        if (m) desktop_bt_connect(m, &d->addr);
        return 1;
    }
    return 1;
}

int bluetooth_panel_key(Session *s, int key, int mods) {
    if (!s || owner != s || !bluetooth_panel_active()) return 0;
    if ((mods & MOD_CMD) && (key == 'q' || key == 'Q' || key == 't' || key == 'T')) return 0;
    int count = device_count();
    if (key == 27 || ((mods & MOD_CMD) && (key == 'w' || key == 'W'))) {
        bluetooth_panel_close();
    } else if (key == 'j' || key == 0x148) { /* Down. */
        if (count) selected = (selected + 1) % count;
    } else if (key == 'k' || key == 0x147) { /* Up. */
        if (count) selected = (selected + count - 1) % count;
    } else if ((key == '\r' || key == '\n') && count) {
        DesktopBtManager *m = manager();
        const DesktopBtDevice *d = device_at(selected);
        if (m && d) desktop_bt_connect(m, &d->addr);
    }
    return 1;
}

/* The desktop reaches the panel through the same seam the media player
 * uses, so the Bluetooth libraries never enter the core desktop link. */
void bluetooth_panel_init(void) {
    static const SessionBluetoothHooks hooks = {
        .available = desktop_bt_available,
        .reason = desktop_bt_unavailable_reason,
        .open = bluetooth_panel_open,
        .active = bluetooth_panel_active,
        .is_open = bluetooth_panel_is_open,
        .regions = bluetooth_panel_regions,
        .close = bluetooth_panel_close,
        .deactivate = bluetooth_panel_deactivate,
        .blur = bluetooth_panel_blur,
        .draw = bluetooth_panel_draw,
        .down = bluetooth_panel_down,
        .key = bluetooth_panel_key,
        .poll = desktop_bt_poll,
    };
    session_set_bluetooth_hooks(&hooks);
}
```

The panel needs `fb_hline`, which `include/draw.h` declares (line 37), and `SessionBluetoothHooks` and `session_set_bluetooth_hooks`, which Task 6 adds to `include/ospi.h`. Task 6 must therefore land before this test can build; to keep this task self-contained, add the hook declarations to `include/ospi.h` in this task as well, and Task 6 then only uses them.

- [ ] **Step 5: Add the hook seam to `include/ospi.h`**

After the `SessionMediaHooks` block (line 274), add:

```c
/* The optional Bluetooth panel, registered by the Bluetooth module. The
 * desktop never links the Bluetooth or the guest network libraries into its
 * core, so the panel reaches the desktop through this seam. */
typedef struct {
    int  (*available)(void);
    const char *(*reason)(void);
    void (*open)(Session *s);
    int  (*active)(void);
    int  (*is_open)(void);
    int  (*regions)(const Session *s, Rect out[2]);
    void (*close)(void);
    void (*deactivate)(void);
    void (*blur)(void);
    void (*draw)(Session *s, void *fb);
    int  (*down)(Session *s, int x, int y);
    int  (*key)(Session *s, int key, int mods);
    void (*poll)(void);
} SessionBluetoothHooks;

void session_set_bluetooth_hooks(const SessionBluetoothHooks *hooks);
const SessionBluetoothHooks *session_bluetooth_hooks(void);
```

In `src/session_state.c`, next to the media hooks (line 25-28), add:

```c
static const SessionBluetoothHooks *g_bluetooth_hooks;

void session_set_bluetooth_hooks(const SessionBluetoothHooks *hooks) { g_bluetooth_hooks = hooks; }
const SessionBluetoothHooks *session_bluetooth_hooks(void) { return g_bluetooth_hooks; }
```

- [ ] **Step 6: Add the test target**

In `config/desktop-tests.mk`, add:

```make
# The Bluetooth panel on its own.
build/test_bluetooth_panel: src/bluetooth_panel.c src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_bluetooth_panel.c include/bluetooth_panel.h include/desktop_bluetooth.h $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -o $@ src/bluetooth_panel.c src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_bluetooth_panel.c $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS) $(FT_LIBS) -lm
test: build/test_bluetooth_panel
```

The panel calls `fb_fill`, `fb_frame`, `fb_text` and `fb_text_width`, so `src/draw.c` and `src/font.c` have to be linked. Add them to the recipe the way `build/test_appearance_panel` does:

```make
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -o $@ src/bluetooth_panel.c src/desktop_bluetooth.c src/desktop_bt_none.c src/draw.c src/font.c src/geom.c src/text.c src/typeface.c src/typeface_metrics.c tests/test_bluetooth_panel.c $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS) $(FT_LIBS) -lm
```

If the link still reports a missing symbol, add the file that owns it to the recipe; do not stub a symbol the panel really calls.

- [ ] **Step 7: Run the test to verify it passes**

Run: `make build/test_bluetooth_panel && ./build/test_bluetooth_panel`
Expected: `bluetooth panel: 20 checks, 0 failures`.

- [ ] **Step 8: Commit**

```bash
git add include/bluetooth_panel.h src/bluetooth_panel.c include/ospi.h src/session_state.c tests/test_bluetooth_panel.c config/desktop-tests.mk
git commit -m "Add the Bluetooth network panel"
```

---

### Task 6: The panel façade, the menu item and the call sites

The Appearance panel is called from sixteen desktop files. The Bluetooth panel needs the same hooks, and only one panel may be open at a time, so those calls go through a façade that dispatches to whichever panel is open.

**Files:**
- Create: `include/desktop_panel.h`
- Create: `src/desktop_panel.c`
- Modify: `include/appearance_panel.h` (add `appearance_panel_is_open`)
- Modify: `src/appearance_panel.c` (add `appearance_panel_is_open`)
- Modify: `include/ospi.h` (add `CMD_BLUETOOTH_NETWORK`)
- Modify: `src/menu.c` (the menu item and the enabled state)
- Modify: `src/session.c` (the command and the call sites)
- Modify: `src/input.c`, `src/paint.c`, `src/app_stack.c`, `src/browser_input.c`, `src/browser_window.c`, `src/desktop_finder.c`, `src/desktop_help.c`, `src/finder_apps.c`, `src/finder_drag.c`, `src/finder_open.c`, `src/finder_print.c`, `src/finder_spring.c`, `src/finder_system_icons.c`, `src/session_state.c` (the call sites)
- Modify: `config/core-sources.mk` (add `src/desktop_panel.c`)
- Test: `tests/test_desktop_panel.c`

**Interfaces:**
- Consumes: the Appearance panel API from `include/appearance_panel.h`; the Bluetooth hooks from `include/ospi.h`.
- Produces:
  ```c
  void desktop_panel_open_appearance(Session *s, int tab);
  void desktop_panel_open_controls(Session *s);
  void desktop_panel_open_bluetooth(Session *s);
  int desktop_panel_active(void);
  int desktop_panel_regions(const Session *s, Rect out[2]);
  void desktop_panel_close(void);
  void desktop_panel_deactivate(void);
  void desktop_panel_blur(void);
  void desktop_panel_draw(Session *s, Fb *fb);
  int desktop_panel_down(Session *s, int x, int y);
  int desktop_panel_context(Session *s, int x, int y);
  int desktop_panel_key(Session *s, int key, int mods);
  ```
  and `int appearance_panel_is_open(void);`

- [ ] **Step 1: Write the failing test**

Create `tests/test_desktop_panel.c`:

```c
/* The panel façade: it dispatches to whichever panel is open, and only one
 * panel is open at a time. */
#include "desktop_panel.h"
#include "appearance_panel.h"
#include "bluetooth_panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

/* The hook seam the Bluetooth panel registers itself with. */
static const SessionBluetoothHooks *g_hooks;
void session_set_bluetooth_hooks(const SessionBluetoothHooks *hooks) { g_hooks = hooks; }
const SessionBluetoothHooks *session_bluetooth_hooks(void) { return g_hooks; }

/* The Appearance panel and the desktop furniture it needs. */
const char *finder_label_name(int label) { (void)label; return "Fixture Label"; }
int finder_apps_occluded(const Session *s, int app, int x, int y) { (void)s; (void)app; (void)x; (void)y; return 0; }
int desktop_theme_count(void) { return 1; }
int desktop_theme_current(void) { return 0; }
const char *desktop_theme_name(int index) { (void)index; return "Platinum"; }
uint32_t desktop_theme_color(int role) { (void)role; return C_WHITE; }
int desktop_asset_draw(Fb *fb, int x, int y, const char *name) { (void)fb; (void)x; (void)y; (void)name; return 0; }
int desktop_wallpaper_count(void) { return 1; }
int desktop_wallpaper_current(void) { return 0; }
const char *desktop_wallpaper_name(int index) { (void)index; return "Fixture Picture"; }
void session_command(Session *s, int cmd) { (void)s; (void)cmd; }
int desktop_command_enabled(Session *s, int cmd) { (void)s; (void)cmd; return 0; }
int finder_apps_enabled(const Session *s, int command) { (void)s; (void)command; return 0; }
int finder_apps_media_running(const Session *s) { (void)s; return 0; }
int finder_command_enabled(const Session *s, int cmd) { (void)s; (void)cmd; return 0; }

int main(void) {
    Session s = {.sw = OSPI_W, .sh = OSPI_H};
    desktop_panel_close();
    CHECK(!desktop_panel_active());

    /* The Appearance panel opens through the façade. */
    desktop_panel_open_controls(&s);
    CHECK(desktop_panel_active());
    CHECK(appearance_panel_is_open());
    CHECK(!bluetooth_panel_is_open());
    Rect regions[2];
    CHECK(desktop_panel_regions(&s, regions) == 1);

    /* Opening the Bluetooth panel closes the Appearance panel. */
    desktop_panel_open_bluetooth(&s);
    CHECK(bluetooth_panel_is_open());
    CHECK(!appearance_panel_is_open());
    CHECK(desktop_panel_active());
    CHECK(desktop_panel_regions(&s, regions) == 1);

    /* Closing closes both. */
    desktop_panel_close();
    CHECK(!desktop_panel_active());
    CHECK(!appearance_panel_is_open());
    CHECK(!bluetooth_panel_is_open());

    printf("desktop panel: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_desktop_panel`
Expected: the build fails because `include/desktop_panel.h` does not exist.

- [ ] **Step 3: Write the façade header and source**

Create `include/desktop_panel.h`:

```c
/* The desktop's modal panels.
 *
 * The Appearance panel and the Bluetooth panel cannot be open at the same
 * time, so every desktop call goes through this façade, which dispatches to
 * whichever panel is open. */
#ifndef OS999_DESKTOP_PANEL_H
#define OS999_DESKTOP_PANEL_H

#include "ospi.h"
#include "draw.h"

void desktop_panel_open_appearance(Session *s, int tab);
void desktop_panel_open_controls(Session *s);
void desktop_panel_open_bluetooth(Session *s);
int desktop_panel_active(void);
int desktop_panel_regions(const Session *s, Rect out[2]);
void desktop_panel_close(void);
void desktop_panel_deactivate(void);
void desktop_panel_blur(void);
void desktop_panel_draw(Session *s, Fb *fb);
int desktop_panel_down(Session *s, int x, int y);
int desktop_panel_context(Session *s, int x, int y);
int desktop_panel_key(Session *s, int key, int mods);

#endif
```

Create `src/desktop_panel.c`:

```c
/* The desktop's modal panels.
 *
 * Only one panel is open at a time. The Appearance panel lives in the core
 * desktop; the Bluetooth panel lives in the Bluetooth module and reaches the
 * desktop through the hook seam in ospi.h. This façade dispatches to
 * whichever one is open, so the desktop never has to know which it is. */
#include "desktop_panel.h"
#include "appearance_panel.h"
#include "bluetooth_panel.h"

#include <string.h>

static const SessionBluetoothHooks *bt(void) { return session_bluetooth_hooks(); }

static int bt_open(void) {
    const SessionBluetoothHooks *h = bt();
    return h && h->is_open && h->is_open();
}

void desktop_panel_open_appearance(Session *s, int tab) {
    if (bt_open() && bt()->close) bt()->close();
    appearance_panel_open(s, tab);
}

void desktop_panel_open_controls(Session *s) {
    if (bt_open() && bt()->close) bt()->close();
    appearance_panel_open_controls(s);
}

void desktop_panel_open_bluetooth(Session *s) {
    appearance_panel_close();
    const SessionBluetoothHooks *h = bt();
    if (h && h->open) h->open(s);
}

int desktop_panel_active(void) {
    if (bt_open()) {
        const SessionBluetoothHooks *h = bt();
        return h->active ? h->active() : 0;
    }
    return appearance_panel_active();
}

int desktop_panel_regions(const Session *s, Rect out[2]) {
    if (bt_open()) {
        const SessionBluetoothHooks *h = bt();
        return h->regions ? h->regions(s, out) : 0;
    }
    return appearance_panel_regions(s, out);
}

void desktop_panel_close(void) {
    if (bt_open() && bt()->close) bt()->close();
    appearance_panel_close();
}

void desktop_panel_deactivate(void) {
    if (bt_open() && bt()->deactivate) bt()->deactivate();
    appearance_panel_deactivate();
}

void desktop_panel_blur(void) {
    if (bt_open() && bt()->blur) bt()->blur();
    appearance_panel_blur();
}

void desktop_panel_draw(Session *s, Fb *fb) {
    if (bt_open()) {
        const SessionBluetoothHooks *h = bt();
        if (h->draw) h->draw(s, fb);
        return;
    }
    appearance_panel_draw(s, fb);
}

int desktop_panel_down(Session *s, int x, int y) {
    if (bt_open()) {
        const SessionBluetoothHooks *h = bt();
        return h->down ? h->down(s, x, y) : 0;
    }
    return appearance_panel_down(s, x, y);
}

int desktop_panel_context(Session *s, int x, int y) {
    if (bt_open()) return 1; /* The Bluetooth panel has no context menu. */
    return appearance_panel_context(s, x, y);
}

int desktop_panel_key(Session *s, int key, int mods) {
    if (bt_open()) {
        const SessionBluetoothHooks *h = bt();
        return h->key ? h->key(s, key, mods) : 0;
    }
    return appearance_panel_key(s, key, mods);
}
```

- [ ] **Step 4: Add the open query to the Appearance panel**

In `include/appearance_panel.h`, after `int appearance_panel_active(void);` add:

```c
/* The panel is open, whether or not it currently has the focus. */
int appearance_panel_is_open(void);
```

In `src/appearance_panel.c`, after `appearance_panel_active` (line 89), add:

```c
int appearance_panel_is_open(void) { return mode != PANEL_CLOSED && owner != NULL; }
```

- [ ] **Step 5: Move the desktop call sites to the façade**

In every desktop file listed below, replace the Appearance panel calls with the façade calls. Keep `appearance_panel_open_controls` and `appearance_panel_open` only inside `src/desktop_panel.c`.

| File | Replace |
|---|---|
| `src/session.c` | `appearance_panel_blur()` → `desktop_panel_blur()`; `appearance_panel_open_controls(s)` → `desktop_panel_open_controls(s)`; `appearance_panel_open(s, x)` → `desktop_panel_open_appearance(s, x)`; `appearance_panel_close()` → `desktop_panel_close()`; `appearance_panel_active()` → `desktop_panel_active()`; `appearance_panel_key(...)` → `desktop_panel_key(...)` |
| `src/input.c` | `appearance_panel_down(...)` → `desktop_panel_down(...)`; `appearance_panel_key(...)` → `desktop_panel_key(...)`; `appearance_panel_active()` → `desktop_panel_active()`; `appearance_panel_context(...)` → `desktop_panel_context(...)` |
| `src/paint.c` | `appearance_panel_draw(s, &fb)` → `desktop_panel_draw(s, &fb)` |
| `src/app_stack.c` | `appearance_panel_regions(...)` → `desktop_panel_regions(...)` |
| `src/browser_input.c` | `appearance_panel_deactivate()` → `desktop_panel_deactivate()` |
| `src/browser_window.c` | `appearance_panel_regions(...)` → `desktop_panel_regions(...)`; `appearance_panel_deactivate()` → `desktop_panel_deactivate()` |
| `src/desktop_finder.c` | `appearance_panel_active()` → `desktop_panel_active()`; `appearance_panel_close()` → `desktop_panel_close()`; `appearance_panel_open(s, 0)` → `desktop_panel_open_appearance(s, 0)` |
| `src/desktop_help.c` | `appearance_panel_close()` → `desktop_panel_close()` |
| `src/finder_apps.c` | `appearance_panel_deactivate()` → `desktop_panel_deactivate()` |
| `src/finder_drag.c` | `appearance_panel_regions(...)` → `desktop_panel_regions(...)` |
| `src/finder_open.c` | `appearance_panel_close()` → `desktop_panel_close()` |
| `src/finder_print.c` | `appearance_panel_active()` → `desktop_panel_active()` |
| `src/finder_spring.c` | `appearance_panel_regions(...)` → `desktop_panel_regions(...)` |
| `src/finder_system_icons.c` | `appearance_panel_regions(...)` → `desktop_panel_regions(...)` |
| `src/session_state.c` | `appearance_panel_close()` → `desktop_panel_close()` |

Every one of those files needs `#include "desktop_panel.h"`. Keep the `#include "appearance_panel.h"` only in the files that still call the Appearance panel directly, which after this step is only `src/desktop_panel.c` and `src/appearance_panel.c`.

`src/menu.c` keeps `appearance_panel_can_open()`, which asks whether the Control Panels window is open, and changes `appearance_panel_active()` to `desktop_panel_active()`.

- [ ] **Step 6: Add the menu item**

In `include/ospi.h`, extend the first command enum (line 47) to:

```c
    CMD_TERM_PASTE_DO, CMD_TAILSCALE, CMD_QUAKE3, CMD_BLUETOOTH_NETWORK
```

In `src/menu.c`, in `k_pi` (line 21), add the item after *Tailscale*:

```c
    ITEM("Tailscale", CMD_TAILSCALE), ITEM("Bluetooth Network...", CMD_BLUETOOTH_NETWORK), ITEM("Terminal", CMD_TERMINAL_SHELL), ITEM("OS999 Console", CMD_TERMINAL_CONSOLE),
```

In `src/menu.c`, in `update_items` (the switch at line 111), add:

```c
        case CMD_BLUETOOTH_NETWORK: {
            const SessionBluetoothHooks *bt = session_bluetooth_hooks();
            int on = bt && bt->available && bt->available();
            yes = on;
            item->label = on ? "Bluetooth Network..." : "Bluetooth Network... (no radio)";
            break;
        }
```

- [ ] **Step 7: Handle the command**

In `src/session.c`, in the `session_command` switch (line 267), add after `case CMD_TAILSCALE:`:

```c
    case CMD_BLUETOOTH_NETWORK:
        context_close();
        finder_apps_command(s, CMD_APP_FINDER);
        desktop_panel_open_bluetooth(s);
        break;
```

- [ ] **Step 8: Add the façade to the core sources**

In `config/core-sources.mk`, add `src/desktop_panel.c` to the `CORE` list, right after `src/appearance_panel.c`.

- [ ] **Step 9: Add the test target**

In `config/desktop-tests.mk`, add:

```make
# The panel façade dispatches to whichever panel is open.
build/test_desktop_panel: src/desktop_panel.c src/appearance_panel.c src/bluetooth_panel.c src/desktop_bluetooth.c src/desktop_bt_none.c tests/test_desktop_panel.c include/desktop_panel.h $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -o $@ src/desktop_panel.c src/appearance_panel.c src/bluetooth_panel.c src/desktop_bluetooth.c src/desktop_bt_none.c src/draw.c src/font.c src/geom.c src/text.c src/typeface.c src/typeface_metrics.c tests/test_desktop_panel.c $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS) $(FT_LIBS) -lm
test: build/test_desktop_panel
```

If the link reports a missing symbol, add the file that owns it to the recipe; do not stub a symbol the panels really call.

- [ ] **Step 10: Run the test to verify it passes**

Run: `make build/test_desktop_panel && ./build/test_desktop_panel`
Expected: `desktop panel: 12 checks, 0 failures`.

- [ ] **Step 11: Run the desktop suites**

Run: `BUILDFLEET_LOCAL=1 make test`
Expected: every test passes. Any test that linked the Appearance panel directly now goes through the façade and must still behave the same.

- [ ] **Step 12: Commit**

```bash
git add include/desktop_panel.h src/desktop_panel.c include/appearance_panel.h src/appearance_panel.c include/ospi.h src/menu.c src/session.c src/input.c src/paint.c src/app_stack.c src/browser_input.c src/browser_window.c src/desktop_finder.c src/desktop_help.c src/finder_apps.c src/finder_drag.c src/finder_open.c src/finder_print.c src/finder_spring.c src/finder_system_icons.c src/session_state.c config/core-sources.mk tests/test_desktop_panel.c config/desktop-tests.mk
git commit -m "Put the desktop panels behind one façade and add the menu item"
```

---

### Task 7: The macOS radio driver

The macOS driver runs the inquiry through IOBluetooth, opens the BNEP channel, adopts it into the stack, and brings up the PAN. It is compile-checked against the SDK; no peer device is available, so it stays unverified at runtime.

**Files:**
- Create: `src/desktop_bt_darwin.m`
- Modify: `bluetooth/bluetooth.mk` (a file rule for `bt_darwin.o`)
- Test: `tests/test_desktop_bt_darwin.m` (macOS only)

**Interfaces:**
- Consumes: `bt_darwin_new`, `bt_darwin_free`, `bt_darwin_discover`, `bt_darwin_stop_discover`, `bt_darwin_open_l2cap`, `bt_darwin_close_l2cap`, `bt_darwin_local_address`, `bt_darwin_available` from `bluetooth/src/bt_darwin.h`; `bt_l2cap_new`, `bt_l2cap_adopt`, `bt_l2cap_pipe_rx`, `bt_l2cap_set_chan_close_fn` from `bluetooth/include/os999_bluetooth.h`; `bt_bnep_new`, `bt_bnep_adopt`; `bt_pan_new`, `bt_pan_open`, `bt_pan_set_local_mac`; `bt_panif_new`, `bt_panif_set_peer`, `bt_panif_pump` from `bluetooth/include/os999_bt_panif.h`.
- Produces: `const DesktopBtDriverOps *desktop_bt_darwin_ops(void);`, and `desktop_bt_platform_ops()` returns it on macOS.

- [ ] **Step 1: Write the driver**

Create `src/desktop_bt_darwin.m`:

```objc
/* The macOS radio driver.
 *
 * IOBluetooth runs the inquiry and opens the BNEP channel for us, so the
 * channel is adopted into the stack and the personal area network is built
 * on top of it. The host's own Bluetooth address comes from the host
 * controller, and that is the address the guest sees on the link. */
#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>

#include "desktop_bluetooth.h"
#include "bt_darwin.h"
#include "os999_bt_panif.h"

#include <stdlib.h>
#include <string.h>

typedef struct DesktopBtDarwin {
    DesktopBtCallbacks cb;
    BtDarwin *darwin;
    BtL2cap *l2cap;
    BtBnep *bnep;
    BtPan *pan;
    BtPanIf *iface;
    int pan_id;
    int cid;
    int up;
    BtAddress local;
    BtAddress peer;
    uint8_t peer_mac[6];
    uint8_t local_mac[6];
    uint16_t handle;
} DesktopBtDarwin;

static DesktopBtDriver *darwin_create(const DesktopBtCallbacks *cb) {
    DesktopBtDarwin *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->cb = *cb;
    d->darwin = bt_darwin_new();
    d->l2cap = bt_l2cap_new();
    if (!d->darwin || !d->l2cap) {
        bt_darwin_free(d->darwin);
        bt_l2cap_free(d->l2cap);
        free(d);
        return NULL;
    }
    d->bnep = bt_bnep_new(d->l2cap);
    d->pan = bt_pan_new(d->bnep);
    if (!d->bnep || !d->pan) {
        bt_pan_free(d->pan);
        bt_bnep_free(d->bnep);
        bt_l2cap_free(d->l2cap);
        bt_darwin_free(d->darwin);
        free(d);
        return NULL;
    }
    return (DesktopBtDriver *)d;
}

/* The channel the system opened carries the frames, so the stack takes it
 * over as a byte pipe. */
static void darwin_on_open(void *ctx, uint16_t handle, const BtDarwinPipe *pipe) {
    DesktopBtDarwin *d = ctx;
    d->handle = handle;
    BtL2capPipe l2pipe;
    l2pipe.write = pipe->write;
    l2pipe.read = pipe->read;
    l2pipe.ctx = pipe->ctx;
    if (bt_l2cap_adopt(d->l2cap, handle, &d->peer, BT_PSM_BNEP, 2048, &l2pipe, &d->cid) != BT_OK) {
        if (d->cb.failed) d->cb.failed(d->cb.ctx, BT_EIO);
        return;
    }
    if (bt_bnep_adopt(d->bnep, d->cid, &d->peer) != BT_OK) {
        if (d->cb.failed) d->cb.failed(d->cb.ctx, BT_EIO);
        return;
    }
    /* The guest stack sits on the link and keeps our own address. */
    bt_pan_set_local_mac(d->pan, d->local_mac);
    if (bt_pan_open(d->pan, handle, &d->peer, d->peer_mac, &d->pan_id) != BT_OK) {
        if (d->cb.failed) d->cb.failed(d->cb.ctx, BT_EIO);
        return;
    }
    d->iface = bt_panif_new(d->pan, d->pan_id, DBT_PAN_LOCAL_IP, DBT_PAN_NETMASK, d->local_mac);
    if (!d->iface) {
        if (d->cb.failed) d->cb.failed(d->cb.ctx, BT_ENOMEM);
        return;
    }
    bt_panif_set_peer(d->iface, DBT_PAN_PEER_IP, d->peer_mac);
    d->up = 1;
    if (d->cb.up) d->cb.up(d->cb.ctx);
}

static void darwin_on_data(void *ctx, const uint8_t *p, size_t n) {
    DesktopBtDarwin *d = ctx;
    if (!d || !d->cid) return;
    bt_l2cap_pipe_rx(d->l2cap, d->cid, p, n);
}

static void darwin_on_close(void *ctx) {
    DesktopBtDarwin *d = ctx;
    if (!d) return;
    d->up = 0;
    if (d->cb.down) d->cb.down(d->cb.ctx);
}

static void darwin_found(void *ctx, const BtAddress *addr, const char *name, uint32_t cod) {
    DesktopBtDarwin *d = ctx;
    if (!d || !d->cb.found) return;
    d->cb.found(d->cb.ctx, addr, name, cod);
}

static void darwin_scan_done(void *ctx) {
    DesktopBtDarwin *d = ctx;
    if (d && d->cb.scan_done) d->cb.scan_done(d->cb.ctx);
}

static int darwin_scan(DesktopBtDriver *driver, int seconds) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    return bt_darwin_discover(d->darwin, (uint32_t)(seconds > 0 ? seconds : 10),
                              darwin_found, darwin_scan_done, d);
}

static int darwin_link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    if (!d || !peer) return BT_EINVAL;
    d->peer = *peer;
    memcpy(d->peer_mac, peer->b, 6);
    if (bt_darwin_local_address(d->darwin, &d->local) != BT_OK) return BT_ENOENT;
    memcpy(d->local_mac, d->local.b, 6);
    int rc = bt_darwin_open_l2cap(d->darwin, peer, BT_PSM_BNEP, darwin_on_open, darwin_on_data,
                                  darwin_on_close, d);
    if (rc != BT_OK) return rc;
    /* The channel opens on the system's own schedule. */
    return BT_EAGAIN;
}

static void darwin_link_down(DesktopBtDriver *driver) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    if (!d) return;
    bt_darwin_close_l2cap(d->darwin);
    bt_panif_free(d->iface);
    d->iface = NULL;
    d->up = 0;
}

static void darwin_poll(DesktopBtDriver *driver) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    if (!d || !d->iface) return;
    bt_panif_pump(d->iface);
}

static GnPan *darwin_pan(DesktopBtDriver *driver) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    if (!d || !d->iface) return NULL;
    return bt_panif_pan(d->iface);
}

static void darwin_free(DesktopBtDriver *driver) {
    DesktopBtDarwin *d = (DesktopBtDarwin *)driver;
    if (!d) return;
    bt_panif_free(d->iface);
    bt_pan_free(d->pan);
    bt_bnep_free(d->bnep);
    bt_l2cap_free(d->l2cap);
    bt_darwin_free(d->darwin);
    free(d);
}

static const DesktopBtDriverOps k_darwin_ops = {
    .create = darwin_create,
    .scan = darwin_scan,
    .link_up = darwin_link_up,
    .link_down = darwin_link_down,
    .poll = darwin_poll,
    .pan = darwin_pan,
    .free = darwin_free,
};

const DesktopBtDriverOps *desktop_bt_darwin_ops(void) { return &k_darwin_ops; }

const DesktopBtDriverOps *desktop_bt_platform_ops(void) { return desktop_bt_darwin_ops(); }

int desktop_bt_available(void) { return bt_darwin_available(); }

const char *desktop_bt_unavailable_reason(void) { return "the macOS Bluetooth radio is not usable"; }
```

- [ ] **Step 2: Make the accelerator a file rule**

In `bluetooth/bluetooth.mk`, replace the `bt-darwin` target (lines 59-62) with:

```make
# The macOS accelerator is Objective-C against IOBluetooth. It is built
# on its own so the rest of the stack stays plain C.
$(BT_OUT)/bt_darwin.o: $(BT_DIR)/src/bt_darwin.m $(BT_HDR)
	@mkdir -p $(BT_OUT)
	$(CC) $(BT_CFLAGS) $(BT_INC) -fobjc-arc -c $< -o $@

.PHONY: bt-darwin
bt-darwin: $(BT_OUT)/bt_darwin.o
```

- [ ] **Step 3: Write the macOS driver test**

Create `tests/test_desktop_bt_darwin.m`:

```objc
/* The macOS radio driver: the platform has a radio and the host controller
 * answers with an address. No peer device is available here, so the link
 * itself stays unverified. */
#import <Foundation/Foundation.h>

#include "desktop_bluetooth.h"

#include <stdio.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    CHECK(desktop_bt_available() == 1);
    CHECK(desktop_bt_platform_ops() == desktop_bt_darwin_ops());
    CHECK(desktop_bt_unavailable_reason() != NULL);

    DesktopBtManager *m = desktop_bt_new(desktop_bt_darwin_ops(), NULL);
    CHECK(m != NULL);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_bt_device_count(m) == 0);
    desktop_bt_free(m);

    printf("desktop bluetooth (macOS): %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 4: Add the macOS test target**

In `config/desktop-tests.mk`, inside the existing `ifeq ($(shell uname -s),Darwin)` block (line 146), add:

```make
build/test_desktop_bt_darwin: src/desktop_bt_darwin.m tests/test_desktop_bt_darwin.m src/desktop_bluetooth.c src/desktop_bt_none.c include/desktop_bluetooth.h $(BT_OUT)/libos999bt.a $(BT_OUT)/bt_darwin.o $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -I$(BT_DIR)/src -fobjc-arc -o $@ src/desktop_bt_darwin.m tests/test_desktop_bt_darwin.m src/desktop_bluetooth.c src/desktop_bt_none.c $(BT_OUT)/libos999bt.a $(BT_OUT)/bt_darwin.o $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
.PHONY: test-desktop-bt-darwin
test-desktop-bt-darwin: build/test_desktop_bt_darwin
	./build/test_desktop_bt_darwin
test: test-desktop-bt-darwin
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make test-desktop-bt-darwin`
Expected: `desktop bluetooth (macOS): 6 checks, 0 failures`. The link itself is not exercised: no peer device is available.

- [ ] **Step 6: Commit**

```bash
git add src/desktop_bt_darwin.m tests/test_desktop_bt_darwin.m bluetooth/bluetooth.mk config/desktop-tests.mk
git commit -m "Add the macOS radio driver"
```

---

### Task 8: The whole flow over the controller simulator

This is the acceptance test from the design: the panel opens, devices appear, a click brings the link up, the broker reports the PAN address, a guest TCP stream reaches the peer over the link, and the failure paths behave.

The driver here is the controller simulator: the same Bluetooth stack, but the radio is the simulator instead of IOBluetooth, and the far end of the link is a peer stack that answers.

**Files:**
- Create: `tests/desktop_bt_sim.h`
- Create: `tests/desktop_bt_sim.c`
- Modify: `tests/test_desktop_bluetooth.c` (add the end-to-end test)

**Interfaces:**
- Consumes: `bt_sim_new`, `bt_sim_free`, `bt_sim_transport`, `bt_sim_set_peer_l2cap`, `bt_sim_add_device`, `bt_sim_local_addr`, `bt_sim_send_acl` from `bluetooth/src/bt_hci_sim.h`; `bt_stack_new`, `bt_stack_pump`, `bt_stack_connect`, `bt_stack_handle_for` from `bluetooth/include/os999_bluetooth.h`; the manager API from `include/desktop_bluetooth.h`.
- Produces:
  ```c
  /* The simulator-backed driver and the peer at the far end. */
  const DesktopBtDriverOps *desktop_bt_sim_ops(void);
  void desktop_bt_sim_reset(void);
  /* Move the bytes at the far end; the peer answers whatever arrives. */
  void desktop_bt_sim_peer_pump(void);
  int desktop_bt_sim_peer_connected(void);
  ```

- [ ] **Step 1: Write the simulator driver**

Create `tests/desktop_bt_sim.h`:

```c
/* The controller simulator behind the desktop Bluetooth driver, so the
 * whole flow runs headlessly. */
#ifndef OS999_DESKTOP_BT_SIM_H
#define OS999_DESKTOP_BT_SIM_H

#include "desktop_bluetooth.h"

const DesktopBtDriverOps *desktop_bt_sim_ops(void);
/* Forget the peer, for a test that starts over. */
void desktop_bt_sim_reset(void);
/* Move the bytes at the far end of the link. */
void desktop_bt_sim_peer_pump(void);
/* 1 once the guest's connection reached the peer. */
int desktop_bt_sim_peer_connected(void);

#endif
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_desktop_bluetooth`
Expected: the build fails because `tests/desktop_bt_sim.c` does not exist.

- [ ] **Step 3: Write the simulator driver**

Create `tests/desktop_bt_sim.c`:

```c
/* The controller simulator behind the desktop Bluetooth driver.
 *
 * The host side is the ordinary Bluetooth stack on a simulated controller;
 * the far end is a second stack that answers the network link and echoes
 * whatever the guest sends. The whole desktop flow therefore runs without a
 * radio and without a peer device. */
#include "desktop_bt_sim.h"

#include "bt_hci_sim.h"
#include "os999_bt_panif.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    DesktopBtCallbacks cb;
    BtSim *sim;
    BtStack *host;
    BtL2cap *peer_l2cap;
    BtBnep *peer_bnep;
    BtBnep *host_bnep;
    BtPan *peer_pan;
    BtPan *host_pan;
    BtPanIf *host_if;
    BtPanIf *peer_if;
    GnPanTcp *peer_srv;
    GnPanTcp *peer_conn;
    BtAddress peer_addr;
    BtAddress host_addr;
    uint8_t host_mac[6], peer_mac[6];
    uint16_t handle;
    int pan_id;
    int connected;
} SimDriver;

static SimDriver g_sim;

static void sim_found(void *ctx, const BtAddress *addr, const char *name, uint32_t cod) {
    SimDriver *d = ctx;
    if (d->cb.found) d->cb.found(d->cb.ctx, addr, name, cod);
}

static void sim_scan_done(void *ctx) {
    SimDriver *d = ctx;
    if (d->cb.scan_done) d->cb.scan_done(d->cb.ctx);
}

static void sim_peer_bnep_open(void *ctx, BtBnepChan *chan) {
    (void)ctx; (void)chan;
}

static void sim_free_driver(SimDriver *d) {
    if (d->peer_conn) gn_pan_tcp_free(d->peer_conn);
    if (d->peer_srv) gn_pan_tcp_free(d->peer_srv);
    if (d->peer_if) bt_panif_free(d->peer_if);
    if (d->host_if) bt_panif_free(d->host_if);
    if (d->host_pan) bt_pan_free(d->host_pan);
    if (d->peer_pan) bt_pan_free(d->peer_pan);
    if (d->host_bnep) bt_bnep_free(d->host_bnep);
    if (d->peer_bnep) bt_bnep_free(d->peer_bnep);
    if (d->peer_l2cap) bt_l2cap_free(d->peer_l2cap);
    if (d->host) bt_stack_free(d->host);
    if (d->sim) bt_sim_free(d->sim);
    memset(d, 0, sizeof *d);
}

static DesktopBtDriver *sim_create(const DesktopBtCallbacks *cb) {
    SimDriver *d = &g_sim;
    memset(d, 0, sizeof *d);
    d->cb = *cb;
    bt_addr_parse("00:00:00:00:00:07", &d->peer_addr);
    bt_addr_parse("00:11:22:33:44:55", &d->host_addr);
    memcpy(d->host_mac, d->host_addr.b, 6);
    memcpy(d->peer_mac, d->peer_addr.b, 6);

    d->sim = bt_sim_new();
    d->peer_l2cap = bt_l2cap_new();
    if (!d->sim || !d->l2cap) return NULL;
    bt_l2cap_set_acl_sender(d->peer_l2cap, (BtAclSendFn)bt_sim_send_acl, d->sim);
    bt_sim_set_peer_l2cap(d->sim, d->peer_l2cap);
    bt_sim_add_device(d->sim, &d->peer_addr, "Peer", 0x00010C, 0);

    d->host = bt_stack_new(bt_sim_transport(d->sim), NULL, NULL);
    d->peer_bnep = bt_bnep_new(d->peer_l2cap);
    if (!d->host || !d->peer_bnep) return NULL;
    bt_bnep_listen(d->peer_bnep, sim_peer_bnep_open, d);
    return (DesktopBtDriver *)d;
}

static int sim_scan(DesktopBtDriver *driver, int seconds) {
    SimDriver *d = (SimDriver *)driver;
    (void)seconds;
    bt_stack_discover(d->host, 8, 1);
    for (int i = 0; i < 8; i++) bt_stack_pump(d->host, 0);
    const BtAddress *local = bt_sim_local_addr(d->sim);
    if (local) sim_found(d, &d->peer_addr, "Peer", 0x00010C);
    sim_scan_done(d);
    return BT_OK;
}

static int sim_link_up(DesktopBtDriver *driver, const BtAddress *peer) {
    SimDriver *d = (SimDriver *)driver;
    if (!d || !peer) return BT_EINVAL;
    if (bt_stack_connect(d->host, peer, 0xCC18) != BT_OK) return BT_EIO;
    bt_stack_pump(d->host, 0);
    if (bt_stack_handle_for(d->host, peer, &d->handle) != BT_OK) return BT_EIO;

    BtBnep *host_bnep = bt_bnep_new(bt_stack_l2cap(d->host));
    if (!host_bnep) return BT_ENOMEM;
    d->host_bnep = host_bnep;
    int host_cid = -1;
    if (bt_bnep_connect(host_bnep, d->handle, peer, &host_cid) != BT_OK) return BT_EIO;
    BtBnepInfo binfo;
    memset(&binfo, 0, sizeof binfo);
    for (int i = 0; i < 32 && !(binfo.up); i++) {
        bt_stack_pump(d->host, 0);
        bt_bnep_info(host_bnep, host_cid, &binfo);
    }
    if (!binfo.up) return BT_ETIMEOUT;

    BtPan *host_pan = bt_pan_new(host_bnep);
    if (!host_pan) return BT_ENOMEM;
    d->host_pan = host_pan;
    bt_pan_set_local_mac(host_pan, d->host_mac);
    int pan_id = -1;
    if (bt_pan_open(host_pan, d->handle, peer, d->peer_mac, &pan_id) != BT_OK) return BT_EIO;
    d->pan_id = pan_id;

    BtPanIf *host_if = bt_panif_new(host_pan, pan_id, DBT_PAN_LOCAL_IP, DBT_PAN_NETMASK, d->host_mac);
    if (!host_if) return BT_ENOMEM;
    bt_panif_set_peer(host_if, DBT_PAN_PEER_IP, d->peer_mac);
    d->host_if = host_if;

    /* The far end of the link answers. */
    d->peer_pan = bt_pan_new(d->peer_bnep);
    bt_pan_set_local_mac(d->peer_pan, d->peer_mac);
    int peer_pan_id = -1;
    bt_pan_open(d->peer_pan, d->handle, &d->host_addr, d->host_mac, &peer_pan_id);
    d->peer_if = bt_panif_new(d->peer_pan, peer_pan_id, DBT_PAN_PEER_IP, DBT_PAN_NETMASK, d->peer_mac);
    bt_panif_set_peer(d->peer_if, DBT_PAN_LOCAL_IP, d->host_mac);
    d->peer_srv = gn_pan_tcp_new(bt_panif_pan(d->peer_if));
    gn_pan_tcp_listen(d->peer_srv, 8080);
    return BT_OK;
}

static void sim_link_down(DesktopBtDriver *driver) {
    SimDriver *d = (SimDriver *)driver;
    if (d->peer_conn) {
        gn_pan_tcp_free(d->peer_conn);
        d->peer_conn = NULL;
    }
    if (d->peer_srv) {
        gn_pan_tcp_free(d->peer_srv);
        d->peer_srv = NULL;
    }
    if (d->peer_if) {
        bt_panif_free(d->peer_if);
        d->peer_if = NULL;
    }
    if (d->peer_pan) {
        bt_pan_free(d->peer_pan);
        d->peer_pan = NULL;
    }
    d->connected = 0;
}

static void sim_poll(DesktopBtDriver *driver) {
    SimDriver *d = (SimDriver *)driver;
    if (!d || !d->host) return;
    bt_stack_pump(d->host, 0);
    if (d->peer_if) bt_panif_pump(d->peer_if);
}

static GnPan *sim_pan(DesktopBtDriver *driver) {
    SimDriver *d = (SimDriver *)driver;
    /* The manager asks the driver for the guest stack once the link is up. */
    return d && d->host_if ? bt_panif_pan(d->host_if) : NULL;
}

static void sim_free(DesktopBtDriver *driver) {
    sim_free_driver((SimDriver *)driver);
}

static const DesktopBtDriverOps k_sim_ops = {
    .create = sim_create,
    .scan = sim_scan,
    .link_up = sim_link_up,
    .link_down = sim_link_down,
    .poll = sim_poll,
    .pan = sim_pan,
    .free = sim_free,
};

const DesktopBtDriverOps *desktop_bt_sim_ops(void) { return &k_sim_ops; }

void desktop_bt_sim_reset(void) { sim_free_driver(&g_sim); }

void desktop_bt_sim_peer_pump(void) {
    SimDriver *d = &g_sim;
    if (!d->peer_srv) return;
    if (!d->peer_conn) {
        GnPanTcp *c = NULL;
        if (gn_pan_tcp_accept(d->peer_srv, &c) == GN_OK) {
            d->peer_conn = c;
            d->connected = 1;
        }
    }
    if (!d->peer_conn) return;
    char buf[256];
    long k = gn_pan_tcp_recv(d->peer_conn, buf, sizeof buf);
    if (k > 0) gn_pan_tcp_send(d->peer_conn, buf, (size_t)k);
}

int desktop_bt_sim_peer_connected(void) { return g_sim.connected; }
```

The `sim_pan` function returns the `GnPan` behind the host's link, because the manager asks the driver for it once the link comes up.

- [ ] **Step 4: Write the end-to-end test**

In `tests/test_desktop_bluetooth.c`, add above `main`:

```c
/* The whole flow over the controller simulator: the panel opens, devices
 * appear, a click brings the link up, the broker reports the address on the
 * link, and a guest TCP stream reaches the peer. */
static void test_the_whole_flow(void) {
    DesktopGuestNet *net = desktop_guestnet_new();
    CHECK(net != NULL);
    CHECK(desktop_bt_start(desktop_bt_sim_ops(), net) == BT_OK);
    DesktopBtManager *m = desktop_bt_manager();
    CHECK(m != NULL);
    if (!m) return;

    /* The inquiry finds the peer. */
    CHECK(desktop_bt_scan(m, 5) == BT_OK);
    CHECK(desktop_bt_device_count(m) == 1);
    const DesktopBtDevice *d = desktop_bt_device_at(m, 0);
    CHECK(d && strcmp(d->name, "Peer") == 0);

    /* Clicking the device brings the link up. */
    CHECK(desktop_bt_connect(m, &d->addr) == BT_OK);
    CHECK(desktop_bt_state(m) == DBT_UP);
    CHECK(desktop_bt_pan(m) != NULL);

    /* The broker reports the address the guest has on the link. */
    CHECK(desktop_guestnet_local_ip(net) == DBT_PAN_LOCAL_IP);
    CHECK(desktop_guestnet_netmask(net) == DBT_PAN_NETMASK);
    CHECK(gn_broker_local_ip(desktop_guestnet_broker(net)) == DBT_PAN_LOCAL_IP);

    /* A guest TCP connection to the peer travels over the Bluetooth link. */
    struct sockaddr_in peer;
    memset(&peer, 0, sizeof peer);
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(DBT_PAN_PEER_IP);
    peer.sin_port = htons(8080);
    int id = -1;
    CHECK(gn_tcp_connect(desktop_guestnet_broker(net), (struct sockaddr *)&peer, sizeof peer,
                         "192.168.10.2", &id) == GN_OK);
    for (int i = 0; i < 200; i++) {
        desktop_bt_poll(m);
        desktop_guestnet_pump(net);
        desktop_bt_sim_peer_pump();
        if (desktop_bt_sim_peer_connected()) break;
        usleep(1000);
    }
    CHECK(desktop_bt_sim_peer_connected());

    /* The bytes come back the other way. */
    gn_pump(desktop_guestnet_broker(net), 0);
    GnInfo info;
    CHECK(gn_info(desktop_guestnet_broker(net), id, &info) == GN_OK);
    CHECK(info.state == GN_S_OPEN);
    CHECK(gn_send(desktop_guestnet_broker(net), id, "ping", 4) == 4);
    char got[16] = {0};
    int total = 0;
    for (int i = 0; i < 400 && total < 4; i++) {
        desktop_bt_poll(m);
        desktop_guestnet_pump(net);
        desktop_bt_sim_peer_pump();
        long k = gn_recv(desktop_guestnet_broker(net), id, got + total, sizeof got - total);
        if (k > 0) total += (int)k;
        usleep(1000);
    }
    CHECK(total == 4);
    CHECK(memcmp(got, "ping", 4) == 0);

    /* Taking the link down leaves the guest service untouched. */
    desktop_bt_disconnect(m);
    CHECK(desktop_bt_state(m) == DBT_IDLE);
    CHECK(desktop_guestnet_local_ip(net) == 0);

    desktop_bt_stop();
    desktop_bt_sim_reset();
    desktop_guestnet_free(net);
}
```

The test needs `<arpa/inet.h>` and `<unistd.h>`; add them to the includes at the top of the file. Call `test_the_whole_flow()` from `main` after the manager test.

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_desktop_bluetooth && ./build/test_desktop_bluetooth`
Expected: `desktop bluetooth: 40 checks, 0 failures`.

- [ ] **Step 6: Commit**

```bash
git add tests/desktop_bt_sim.h tests/desktop_bt_sim.c tests/test_desktop_bluetooth.c config/desktop-tests.mk
git commit -m "Run the desktop Bluetooth flow over the simulator"
```

---

### Task 9: Wire the modules into the desktop build and document it

The desktop binary has to carry the new sources and the two libraries, and the Bluetooth panel has to register itself at startup.

**Files:**
- Modify: `Makefile`
- Modify: `config/desktop-tests.mk`
- Modify: `src/main.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: `bluetooth_panel_init()` from `include/bluetooth_panel.h`.
- Produces: `build/ospi` with the Bluetooth panel, the manager and the guest network service linked in.

- [ ] **Step 1: Add the libraries to the root Makefile**

In `Makefile`, after the `STORAGE_LINK` line (line 28), add:

```make
# Guest networking (net/net.mk) and the Bluetooth stack (bluetooth/bluetooth.mk).
# The desktop owns the guest network service and the Bluetooth PAN panel.
NET_DIR ?= net
NET_OUT ?= build/net
BT_DIR ?= bluetooth
BT_OUT ?= build/bluetooth
NET_A := $(NET_OUT)/libos999net.a
BT_A := $(BT_OUT)/libos999bt.a
CFLAGS += -I$(NET_DIR)/include -I$(BT_DIR)/include -I$(BT_DIR)/src
```

- [ ] **Step 2: Link the new sources into the desktop**

In `Makefile`, in the `build/ospi` rule (line 61), add the new sources and libraries.

The prerequisites become:

```make
build/ospi: $(SHORTCUT_HOST) include/desktop_shortcuts.h $(PRINT_HOST) $(CORE) $(STORAGE_A) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/os999_startup.c src/main.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(HDRS) $(AUDIO_HDRS) include/winamp_skin.h include/media_player_panel.h include/os999_startup.h include/desktop_guest.h include/desktop_host.h include/desktop_sound.h $(BROWSER_SRC) $(BROWSER_OVERLAY) $(BROWSER_HDRS) src/desktop_bluetooth.c src/desktop_guestnet.c src/bluetooth_panel.c src/desktop_panel.c src/desktop_bt_none.c $(NET_A) $(BT_A)
```

and the recipe becomes:

```make
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ $(SHORTCUT_HOST) $(PRINT_HOST) $(CORE) $(STORAGE_LINK) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/os999_startup.c src/main.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(BROWSER_SRC) $(BROWSER_OVERLAY) src/desktop_bluetooth.c src/desktop_guestnet.c src/bluetooth_panel.c src/desktop_panel.c src/desktop_bt_none.c $(NET_A) $(BT_A) $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(BROWSER_LIBS) -pthread -lm $(NET_LIBS)
```

- [ ] **Step 3: Use the macOS radio on macOS**

In `Makefile`, in the `ifeq ($(shell uname -s),Darwin)` block (line 39), add:

```make
BT_HOST_SRC := src/desktop_bt_darwin.m
BT_HOST_OBJ := build/desktop_bt_darwin.o
BT_HOST_LIBS := -framework IOBluetooth -framework Foundation
build/desktop_bt_darwin.o: src/desktop_bt_darwin.m include/desktop_bluetooth.h bluetooth/src/bt_darwin.h
	mkdir -p build
	$(CC) $(CFLAGS) -I$(BT_DIR)/include -I$(BT_DIR)/src -fobjc-arc -c $< -o $@
```

and in the `else` branch (after line 60) add:

```make
BT_HOST_SRC := src/desktop_bt_none.c
BT_HOST_OBJ :=
BT_HOST_LIBS :=
```

Then replace `src/desktop_bt_none.c` in the `build/ospi` rule with `$(BT_HOST_SRC)` and add `$(BT_HOST_OBJ)` to the prerequisites and `$(BT_HOST_LIBS)` to the recipe. On macOS `src/desktop_bt_none.c` is still compiled by the `none` driver file for the tests, but the desktop binary uses the macOS driver, so `desktop_bt_platform_ops()` must come from the macOS driver. Both files define `desktop_bt_platform_ops()`, so only one may be linked into `build/ospi`: link `$(BT_HOST_SRC)` and not `src/desktop_bt_none.c`.

- [ ] **Step 4: Register the panel at startup**

In `src/main.c`, after the session is created, add:

```c
    bluetooth_panel_init();
```

and add `#include "bluetooth_panel.h"` at the top.

- [ ] **Step 5: Poll the link every tick**

In `src/session.c`, in `session_tick`, add:

```c
    const SessionBluetoothHooks *bt = session_bluetooth_hooks();
    if (bt && bt->poll) bt->poll();
```

- [ ] **Step 6: Build the desktop**

Run: `BUILDFLEET_LOCAL=1 make`
Expected: `build/ospi` links with no undefined symbols and no new warnings.

- [ ] **Step 7: Run the whole suite**

Run: `BUILDFLEET_LOCAL=1 make test`
Expected: every test passes.

- [ ] **Step 8: Run the sanitizers and the leak check**

Run:

```bash
make bt-asan
make -f net/net.mk net-asan
leaks --atExit -- ./build/test_desktop_bluetooth
leaks --atExit -- ./build/test_bluetooth_panel
leaks --atExit -- ./build/test_desktop_guestnet
```

Expected: every sanitizer build is clean and every leak check reports zero leaks. LeakSanitizer is not available on this platform, so `leaks --atExit` is the check.

- [ ] **Step 9: Check the file sizes**

Run:

```bash
wc -l include/desktop_bluetooth.h src/desktop_bluetooth.c src/desktop_bt_darwin.m src/desktop_bt_none.c \
  include/desktop_guestnet.h src/desktop_guestnet.c include/bluetooth_panel.h src/bluetooth_panel.c \
  include/desktop_panel.h src/desktop_panel.c tests/test_desktop_bluetooth.c tests/desktop_bt_sim.c \
  tests/test_bluetooth_panel.c tests/test_desktop_panel.c tests/test_desktop_guestnet.c
```

Expected: no file reaches 500 lines. Split a file that does.

- [ ] **Step 10: Document the flow**

In `README.md`, add a short section:

```markdown
### Bluetooth network

`999 > Bluetooth Network...` opens a panel that lists the Bluetooth peers an
inquiry found. Picking one brings up a personal area network: the guest keeps
its own address on the link (`192.168.10.1`) and reaches the peer
(`192.168.10.2`) over Bluetooth. The macOS radio uses IOBluetooth for the
inquiry and for the channel; the link itself is unverified at runtime because
no peer device was available. The panel is disabled and says so on a platform
without a radio.
```

- [ ] **Step 11: Commit**

```bash
git add Makefile config/desktop-tests.mk src/main.c src/session.c README.md
git commit -m "Wire the Bluetooth panel into the desktop"
```

---

## Self-Review

**Spec coverage**

| Spec section | Task |
|---|---|
| `bt_bnep_adopt` | 1 |
| The manager | 4 |
| The driver seam, the macOS driver, the no-radio stub | 4, 7 |
| The guest network service | 3 |
| The panel | 5 |
| The panel façade | 6 |
| The menu item, after *Tailscale*, disabled with a reason | 6 |
| Addressing: the device addresses on the link, `192.168.10.0/24`, the desktop is `.1` | 4, 7, 8 |
| The built-in policy | 3 |
| The errors: no radio, nothing found, the link fails, the peer closes the link | 4, 5, 6 |
| The tests: the adopt test, the whole flow, the panel renders | 1, 5, 8 |
| The files stay under 500 lines | 9 |
| Out of scope: an incoming PAN, the policy file, a real radio run | not in any task |

**Type consistency**

`DesktopBtDriverOps`, `DesktopBtCallbacks`, `DesktopBtDevice`, `DesktopBtManager` and every manager function are declared once in `include/desktop_bluetooth.h` and used with the same names in Tasks 4, 5, 7 and 8. `desktop_bt_platform_ops`, `desktop_bt_available` and `desktop_bt_unavailable_reason` are defined in `src/desktop_bt_none.c` for the stub and in `src/desktop_bt_darwin.m` for macOS; only one of the two is linked into `build/ospi`, and Task 9 spells that out. `bluetooth_panel_row_rect` and `bluetooth_panel_disconnect_rect` are declared in `include/bluetooth_panel.h` and used by the Task 5 test. `SessionBluetoothHooks` is declared in `include/ospi.h` in Task 5 and used by Tasks 5, 6 and 9.

**No placeholders**

Every step names the file, the function, the command and the expected result, and every code block is the code to write.
