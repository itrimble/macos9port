#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"

#include <stdio.h>

typedef struct {
    int key_repeat_rate;
    int key_repeat_delay_ms;
    int double_click_ms;
    int wheel_speed;
    int natural_scroll;
    uint32_t test_last_ms;
    uint32_t test_delta_ms;
    int test_double_ok;
    int test_clicks_seen;
} KeyboardMouseState;

static KeyboardMouseState g_km = {2, 400, 500, 2, 0, 0, 0, 0, 0};

static const char *const k_repeat_labels[4] = {"Off", "Slow", "Medium", "Fast"};
static const int k_delay_ms[3] = {600, 400, 250};
static const char *const k_delay_labels[3] = {"Long (600ms)", "Med (400ms)", "Short (250ms)"};
static const int k_dbl_ms[3] = {750, 500, 300};
static const char *const k_dbl_labels[3] = {"Slow", "Medium", "Fast"};
static const char *const k_wheel_labels[3] = {"1x Gentle", "2x Normal", "3x Fast"};

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static int delay_idx(int ms) {
    if (ms >= 500) return 0;
    if (ms <= 300) return 2;
    return 1;
}

static int dbl_idx(int ms) {
    if (ms >= 650) return 0;
    if (ms <= 380) return 2;
    return 1;
}

static void sync_session(const KeyboardMouseState *st, const os999_cdev_host_t *host) {
    if (!host || !host->session) return;
    Session *s = host->session;
    s->key_repeat_rate = clamp_int(st->key_repeat_rate, 0, 3);
    s->key_repeat_delay_ms = clamp_int(st->key_repeat_delay_ms, 200, 1000);
    s->double_click_ms = clamp_int(st->double_click_ms, 200, 1000);
    s->wheel_speed = clamp_int(st->wheel_speed, 1, 3);
    s->natural_scroll = !!st->natural_scroll;
}

static int km_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_int) {
            g_km.key_repeat_rate = clamp_int(
                host->get_int(host->store, "keyboard_mouse", "key_repeat_rate", 2), 0, 3);
            g_km.key_repeat_delay_ms = clamp_int(
                host->get_int(host->store, "keyboard_mouse", "key_repeat_delay_ms", 400), 200, 1000);
            g_km.double_click_ms = clamp_int(
                host->get_int(host->store, "keyboard_mouse", "double_click_ms", 500), 200, 1000);
            g_km.wheel_speed = clamp_int(
                host->get_int(host->store, "keyboard_mouse", "wheel_speed", 2), 1, 3);
        }
        if (host->get_bool) {
            g_km.natural_scroll = host->get_bool(
                host->store, "keyboard_mouse", "natural_scroll", 0);
        }
    }
    sync_session(&g_km, host);
    return 0;
}

static int km_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_km;
    g_km = (KeyboardMouseState){2, 400, 500, 2, 0, 0, 0, 0, 0};
    return km_load(self, host);
}

static void km_shutdown(os999_cdev_t *self) { self->priv = NULL; }

static int km_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int) {
        host->set_int(host->store, "keyboard_mouse", "key_repeat_rate", g_km.key_repeat_rate);
        host->set_int(host->store, "keyboard_mouse", "key_repeat_delay_ms", g_km.key_repeat_delay_ms);
        host->set_int(host->store, "keyboard_mouse", "double_click_ms", g_km.double_click_ms);
        host->set_int(host->store, "keyboard_mouse", "wheel_speed", g_km.wheel_speed);
    }
    if (host->set_bool)
        host->set_bool(host->store, "keyboard_mouse", "natural_scroll", g_km.natural_scroll);
    return host->flush_store(host->store);
}

static Rect repeat_btn(Rect c, int i) { return (Rect){c.x + 190 + i * 96, c.y + 24, 88, 24}; }
static Rect delay_btn(Rect c, int i) { return (Rect){c.x + 190 + i * 128, c.y + 60, 120, 24}; }
static Rect dbl_btn(Rect c, int i) { return (Rect){c.x + 158 + i * 64, c.y + 158, 58, 24}; }
static Rect wheel_btn(Rect c, int i) { return (Rect){c.x + 122 + i * 76, c.y + 202, 70, 24}; }
static Rect natural_chk(Rect c) { return (Rect){c.x + 16, c.y + 246, 326, 24}; }
static Rect test_pad_rect(Rect c) { return (Rect){c.x + 384, c.y + 162, 188, 126}; }

static void apply_and_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    sync_session(&g_km, host);
    km_save(self, host);
}

static int km_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                    const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key >= '1' && ev->key <= '4') {
            g_km.key_repeat_rate = ev->key - '1';
            apply_and_save(self, host);
            return 1;
        }
        if (ev->key == 'n') {
            g_km.natural_scroll = !g_km.natural_scroll;
            apply_and_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    for (int i = 0; i < 4; i++) {
        if (rect_has(repeat_btn(c, i), x, y)) {
            g_km.key_repeat_rate = i;
            apply_and_save(self, host);
            return 1;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (rect_has(delay_btn(c, i), x, y)) {
            g_km.key_repeat_delay_ms = k_delay_ms[i];
            apply_and_save(self, host);
            return 1;
        }
        if (rect_has(dbl_btn(c, i), x, y)) {
            g_km.double_click_ms = k_dbl_ms[i];
            apply_and_save(self, host);
            return 1;
        }
        if (rect_has(wheel_btn(c, i), x, y)) {
            g_km.wheel_speed = i + 1;
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(natural_chk(c), x, y)) {
        g_km.natural_scroll = !g_km.natural_scroll;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(test_pad_rect(c), x, y)) {
        uint32_t dt = ev->now_ms - g_km.test_last_ms;
        int ok = ev->clicks >= 2 ||
                 (g_km.test_clicks_seen > 0 && dt > 0 && dt <= (uint32_t)g_km.double_click_ms);
        g_km.test_double_ok = ok;
        g_km.test_delta_ms = g_km.test_clicks_seen > 0 ? dt : 0;
        g_km.test_last_ms = ev->now_ms;
        g_km.test_clicks_seen++;
        if (ok && host && host->play_sound)
            host->play_sound(host->session, "window.zoom");
        return 1;
    }
    return 0;
}

static void km_render(os999_cdev_t *self, const os999_cdev_host_t *host, Fb *fb, Rect c) {
    (void)self;
    (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 120}, "Keyboard Typematic Repeat");
    cdev_ui_icon(fb, c.x + 16, c.y + 28, "keyboard");
    fb_text(fb, c.x + 62, c.y + 29, fg, "Key Repeat:");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, repeat_btn(c, i), k_repeat_labels[i], g_km.key_repeat_rate == i, 1);

    fb_text(fb, c.x + 62, c.y + 65, fg, "Repeat Delay:");
    int didx = delay_idx(g_km.key_repeat_delay_ms);
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, delay_btn(c, i), k_delay_labels[i], didx == i, g_km.key_repeat_rate > 0);
    fb_text(fb, c.x + 62, c.y + 96, desktop_theme_color(THEME_DISABLED),
            "Applies to Finder rename fields, dialogs, and host key auto-repeat.");

    cdev_ui_group(fb, (Rect){c.x, c.y + 130, 356, 226}, "Mouse & Scroll Wheel");
    cdev_ui_icon(fb, c.x + 14, c.y + 154, "mouse");
    fb_text(fb, c.x + 54, c.y + 163, fg, "Double-Click:");
    int db = dbl_idx(g_km.double_click_ms);
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, dbl_btn(c, i), k_dbl_labels[i], db == i, 1);

    fb_text(fb, c.x + 16, c.y + 207, fg, "Wheel Speed:");
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, wheel_btn(c, i), k_wheel_labels[i], g_km.wheel_speed == i + 1, 1);

    cdev_ui_checkbox(fb, natural_chk(c), "Natural Wheel Scroll Direction", g_km.natural_scroll);
    int step_px = g_km.wheel_speed == 1 ? 9 : g_km.wheel_speed == 3 ? 30 : 18;
    char stats[80];
    snprintf(stats, sizeof stats, "Window: %d ms  |  Wheel step: %d px/tick",
             g_km.double_click_ms, step_px);
    fb_text(fb, c.x + 16, c.y + 292, desktop_theme_color(THEME_DISABLED), stats);
    fb_text(fb, c.x + 16, c.y + 314, desktop_theme_color(THEME_DISABLED),
            "Live Finder & Control Panel double-click timing.");

    cdev_ui_group(fb, (Rect){c.x + 368, c.y + 130, c.w - 368, 226}, "Double-Click Test");
    Rect pad = test_pad_rect(c);
    fb_fill(fb, pad.x, pad.y, pad.w, pad.h,
            g_km.test_double_ok ? desktop_theme_color(THEME_MENU) : desktop_theme_color(THEME_PANEL));
    fb_frame(fb, pad.x, pad.y, pad.w, pad.h,
             g_km.test_double_ok ? cdev_ui_selection_color() : desktop_theme_color(THEME_FRAME));
    cdev_ui_icon(fb, pad.x + (pad.w - 32) / 2, pad.y + 18,
                 g_km.test_double_ok ? "folder_open" : "folder");
    const char *status = g_km.test_clicks_seen == 0
                             ? "Double-click here"
                             : (g_km.test_double_ok ? "Double-Click OK!" : "Single Click");
    int sw = fb_text_width(status);
    fb_text(fb, pad.x + (pad.w - sw) / 2, pad.y + 64, fg, status);
    if (g_km.test_delta_ms > 0) {
        char dt_line[48];
        snprintf(dt_line, sizeof dt_line, "Interval: %u ms", (unsigned)g_km.test_delta_ms);
        int dw = fb_text_width(dt_line);
        fb_text(fb, pad.x + (pad.w - dw) / 2, pad.y + 88,
                desktop_theme_color(THEME_DISABLED), dt_line);
    }
    fb_text(fb, c.x + 384, c.y + 306, desktop_theme_color(THEME_DISABLED), "Test folder reacts");
    fb_text(fb, c.x + 384, c.y + 326, desktop_theme_color(THEME_DISABLED), "to active threshold.");
}

static const os999_cdev_t k_km_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "keyboard_mouse",
    .title = "Keyboard & Mouse",
    .summary = "Key repeat, double-click speed, and wheel",
    .icon_name = "keyboard",
    .category = OS999_CDEV_CAT_HARDWARE,
    .init = km_init,
    .shutdown = km_shutdown,
    .load_state = km_load,
    .save_state = km_save,
    .handle_event = km_event,
    .render = km_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_keyboard_module(void) { return &k_km_cdev; }
