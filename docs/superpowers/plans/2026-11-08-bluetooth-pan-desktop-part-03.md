# Desktop Bluetooth PAN plan — part 3

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
