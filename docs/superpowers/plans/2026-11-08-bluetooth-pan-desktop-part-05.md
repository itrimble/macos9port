# Desktop Bluetooth PAN plan — part 5

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

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
