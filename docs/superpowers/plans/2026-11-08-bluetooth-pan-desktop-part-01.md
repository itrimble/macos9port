# Desktop Bluetooth PAN plan — part 1

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
