#include "os999_cdev.h"
#include "cdev_ui.h"
#include "cdev_bluetooth.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <string.h>

enum { ROWS = 6, SCAN_SECONDS = 10, NAME_N = 64, ADDR_N = 24 };

static const CdevBtSource *g_src;
static int g_avail = -1;      /* -1 not asked yet; asked lazily, never per frame */
static int g_selected = -1;
static int g_last_rc;
static char g_note[96];

void cdev_bluetooth_set_source(const CdevBtSource *source) {
    g_src = source;
    g_avail = -1;
    g_selected = -1;
    g_last_rc = 0;
    g_note[0] = 0;
}

static int bt_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self; (void)host;
    g_avail = -1;
    g_selected = -1;
    return 0;
}
static void bt_shutdown(os999_cdev_t *self) { (void)self; }
static int bt_load(os999_cdev_t *self, const os999_cdev_host_t *host) { (void)self; (void)host; return 0; }
static int bt_save(os999_cdev_t *self, const os999_cdev_host_t *host) { (void)self; (void)host; return 0; }

static void refresh_available(void) {
    g_avail = (g_src && g_src->available) ? !!g_src->available() : 0;
}

static Rect scan_btn(Rect c) { return (Rect){c.x + 16, c.y + 92, 112, 24}; }
static Rect connect_btn(Rect c) { return (Rect){c.x + 136, c.y + 92, 112, 24}; }
static Rect disconnect_btn(Rect c) { return (Rect){c.x + 256, c.y + 92, 112, 24}; }
static Rect row_rect(Rect c, int i) { return (Rect){c.x + 12, c.y + 152 + i * 22, 340, 20}; }

static int busy_or_up(void) {
    int st = g_src ? g_src->state() : CDEV_BT_IDLE;
    return st == CDEV_BT_SCANNING || st == CDEV_BT_CONNECTING || st == CDEV_BT_UP;
}

static void do_scan(void) {
    if (!g_src || !g_src->scan) return;
    refresh_available();
    if (!g_avail) { g_note[0] = 0; return; }
    g_selected = -1;
    g_last_rc = g_src->scan(SCAN_SECONDS);
    snprintf(g_note, sizeof g_note, g_last_rc == 0 ? "Scan started (%d s)." : "Scan refused (code %d).",
             g_last_rc == 0 ? SCAN_SECONDS : g_last_rc);
}

static void do_connect(void) {
    if (!g_src || !g_src->connect || g_selected < 0 || g_selected >= g_src->device_count()) return;
    g_last_rc = g_src->connect(g_selected);
    snprintf(g_note, sizeof g_note, g_last_rc == 0 ? "Connection requested." : "Connect refused (code %d).",
             g_last_rc);
}

static int bt_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                    const os999_cdev_event_t *ev, Rect c) {
    (void)self; (void)host;
    if (!ev || ev->type != OS999_CDEV_EV_DOWN || !g_src) return 0;
    int x = ev->x, y = ev->y;
    if (rect_has(scan_btn(c), x, y)) { do_scan(); return 1; }
    if (rect_has(connect_btn(c), x, y)) { do_connect(); return 1; }
    if (rect_has(disconnect_btn(c), x, y)) {
        if (g_src->disconnect) g_src->disconnect();
        g_note[0] = 0;
        return 1;
    }
    int n = g_src->device_count();
    for (int i = 0; i < ROWS && i < n; i++) {
        if (rect_has(row_rect(c, i), x, y)) { g_selected = i; return 1; }
    }
    return 0;
}

static const char *state_label(int st) {
    switch (st) {
    case CDEV_BT_SCANNING: return "Scanning";
    case CDEV_BT_CONNECTING: return "Connecting";
    case CDEV_BT_UP: return "Connected (Bluetooth PAN)";
    case CDEV_BT_FAILED: return "Failed";
    default: return "Idle";
    }
}

static void bt_render(os999_cdev_t *self, const os999_cdev_host_t *host, Fb *fb, Rect c) {
    (void)self; (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    uint32_t dim = desktop_theme_color(THEME_DISABLED);
    char line[160];
    if (g_avail < 0) refresh_available();
    int st = g_src ? g_src->state() : CDEV_BT_IDLE;

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 122}, "Bluetooth Adapter");
    cdev_ui_icon(fb, c.x + 16, c.y + 24, "wireless");
    if (!g_src) {
        fb_text(fb, c.x + 60, c.y + 29, fg, "Adapter: not part of this build");
    } else if (!g_avail) {
        const char *why = g_src->unavailable_reason ? g_src->unavailable_reason() : NULL;
        snprintf(line, sizeof line, "Adapter: unavailable. %s", why ? why : "No powered controller found.");
        fb_text(fb, c.x + 60, c.y + 29, fg, line);
    } else {
        fb_text(fb, c.x + 60, c.y + 29, fg, "Adapter: present and powered on");
    }
    snprintf(line, sizeof line, "State: %s", state_label(st));
    fb_text(fb, c.x + 60, c.y + 48, fg, line);
    if (g_src && st == CDEV_BT_FAILED) {
        snprintf(line, sizeof line, "Error code: %d", g_src->error());
        fb_text(fb, c.x + 60, c.y + 66, fg, line);
    } else if (g_note[0]) {
        fb_text(fb, c.x + 60, c.y + 66, dim, g_note);
    }
    int can = g_src && g_avail;
    cdev_ui_button(fb, scan_btn(c), "Scan", st == CDEV_BT_SCANNING, can && st != CDEV_BT_UP);
    cdev_ui_button(fb, connect_btn(c), "Connect", 0, can && g_selected >= 0 && !busy_or_up());
    cdev_ui_button(fb, disconnect_btn(c), "Disconnect", 0, can && st == CDEV_BT_UP);

    cdev_ui_group(fb, (Rect){c.x, c.y + 130, 364, 168}, "Devices Found");
    int n = g_src ? g_src->device_count() : 0;
    if (n == 0)
        fb_text(fb, c.x + 16, c.y + 156, dim, st == CDEV_BT_SCANNING ? "Searching..." : "No devices. Press Scan.");
    for (int i = 0; i < ROWS && i < n; i++) {
        char name[NAME_N], addr[ADDR_N];
        if (g_src->device(i, name, sizeof name, addr, sizeof addr) != 0) continue;
        Rect r = row_rect(c, i);
        if (i == g_selected) fb_fill(fb, r.x, r.y, r.w, r.h, cdev_ui_selection_color());
        snprintf(line, sizeof line, "%.40s  %s", name[0] ? name : "(unnamed)", addr);
        fb_text(fb, r.x + 4, r.y + 4, fg, line);
    }

    Rect st_box = {c.x + 376, c.y + 130, c.w - 376, 168};
    cdev_ui_group(fb, st_box, "What Is Verified");
    static const char *const notes[] = {
        "Not verified:", "- Radio pairing", "- Real devices", "- Connect timing",
        "PAN address is fixed:", "192.168.10.1 / .2",
    };
    for (unsigned i = 0; i < sizeof notes / sizeof *notes; i++)
        fb_text(fb, st_box.x + 12, st_box.y + 26 + (int)i * 18, i == 0 ? fg : dim, notes[i]);

    fb_text(fb, c.x + 16, c.y + 308, dim, "Tested with a fake radio only.");
    fb_text(fb, c.x + 16, c.y + 326, dim, "Scan and Connect are untested on hardware; Connect can block.");
}

static const os999_cdev_t k_bt_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "bluetooth",
    .title = "Bluetooth",
    .summary = "Adapter state, device scan, PAN link (experimental)",
    .icon_name = "wireless",
    .category = OS999_CDEV_CAT_HARDWARE,
    .init = bt_init,
    .shutdown = bt_shutdown,
    .load_state = bt_load,
    .save_state = bt_save,
    .handle_event = bt_event,
    .render = bt_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_bluetooth_module(void) { return &k_bt_cdev; }
