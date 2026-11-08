# Desktop Bluetooth PAN plan — part 2

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
