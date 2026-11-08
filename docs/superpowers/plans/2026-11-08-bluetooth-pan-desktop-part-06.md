# Desktop Bluetooth PAN plan — part 6

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
