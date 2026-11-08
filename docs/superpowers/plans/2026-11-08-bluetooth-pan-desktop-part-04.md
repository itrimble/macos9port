# Desktop Bluetooth PAN plan — part 4

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
