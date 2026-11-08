# Desktop Bluetooth PAN plan — part 7

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
build/test_desktop_bt_darwin: src/desktop_bt_darwin.m tests/test_desktop_bt_darwin.m src/desktop_bluetooth.c $(BT_OUT)/libos999bt.a $(BT_OUT)/bt_darwin.o $(NET_OUT)/libos999net.a
	$(CC) $(CFLAGS) -I$(NET_DIR)/include -I$(BT_DIR)/include -I$(BT_DIR)/src -fobjc-arc -o $@ src/desktop_bt_darwin.m tests/test_desktop_bt_darwin.m src/desktop_bluetooth.c $(BT_OUT)/libos999bt.a $(BT_OUT)/bt_darwin.o $(NET_OUT)/libos999net.a $(GUEST_NET_LIBS)
.PHONY: test-desktop-bt-darwin
test-desktop-bt-darwin: build/test_desktop_bt_darwin
	./build/test_desktop_bt_darwin
test: test-desktop-bt-darwin
```

`src/desktop_bt_none.c` is deliberately not in this recipe: it defines the same `desktop_bt_platform_ops`, `desktop_bt_available` and `desktop_bt_unavailable_reason` as the macOS driver, so linking both would give duplicate symbols.

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
    /* Both ends move the frames that arrived on the link into their guest
     * stack: without the host's own pump nothing the peer sends reaches
     * the guest. */
    if (d->host_if) bt_panif_pump(d->host_if);
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
