#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "display_modes.h"

#include <stdio.h>

typedef struct {
    int color_mode;
    int brightness;
    int gamma_preset;
    int scanlines;
    int integer_scale;
    int dragging_bright;
    int window_scale; /* percent of the logical desktop the host window shows */
    int aspect, portrait; /* which group of display modes the size row lists */
} MonitorsState;

static const int k_window_scales[5] = {75, 100, 125, 150, 200};

static MonitorsState g_mon = {0, 100, 0, 0, 1, 0, 100, 0, 0};

static const char *const k_color_labels[4] = {
    "Millions (32-bit)",
    "Thousands (16-bit)",
    "256 Colors (8-bit)",
    "Grayscale"
};

static const char *const k_gamma_labels[3] = {
    "Classic 1.8",
    "Trinitron 9300K",
    "Warm 6500K"
};

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void sync_session(const MonitorsState *st, const os999_cdev_host_t *host) {
    if (!host || !host->session) return;
    Session *s = host->session;
    s->display_color_mode = clamp_int(st->color_mode, 0, 3);
    s->display_brightness = clamp_int(st->brightness, 50, 120);
    s->display_gamma = clamp_int(st->gamma_preset, 0, 2);
    s->display_scanlines = !!st->scanlines;
    s->display_integer_scale = !!st->integer_scale;
    s->display_window_scale = clamp_int(st->window_scale, 50, 200);
}

/* List the group that holds the current desktop size. */
static void browse_current_mode(void) {
    int mode = display_mode_find(OSPI_W, OSPI_H);
    if (mode < 0) return;
    g_mon.aspect = (mode / DISPLAY_SIZES) % DISPLAY_ASPECTS;
    g_mon.portrait = mode / DISPLAY_SIZES >= DISPLAY_ASPECTS;
}

static int monitors_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    browse_current_mode();   /* the size itself is restored by the host before the first frame */
    if (host && host->store) {
        if (host->get_int) {
            g_mon.color_mode = clamp_int(
                host->get_int(host->store, "monitors", "color_mode", 0), 0, 3);
            g_mon.brightness = clamp_int(
                host->get_int(host->store, "monitors", "brightness", 100), 50, 120);
            g_mon.gamma_preset = clamp_int(
                host->get_int(host->store, "monitors", "gamma_preset", 0), 0, 2);
            g_mon.window_scale = clamp_int(
                host->get_int(host->store, "monitors", "window_scale", 100), 50, 200);
        }
        if (host->get_bool) {
            g_mon.scanlines = host->get_bool(host->store, "monitors", "scanlines", 0);
            g_mon.integer_scale = host->get_bool(host->store, "monitors", "integer_scale", 1);
        }
    }
    sync_session(&g_mon, host);
    return 0;
}

static int monitors_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_mon;
    g_mon = (MonitorsState){0, 100, 0, 0, 1, 0, 100, 0, 0};
    return monitors_load(self, host);
}

static void monitors_shutdown(os999_cdev_t *self) {
    g_mon.dragging_bright = 0;
    self->priv = NULL;
}

static int monitors_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int) {
        host->set_int(host->store, "monitors", "color_mode", g_mon.color_mode);
        host->set_int(host->store, "monitors", "brightness", g_mon.brightness);
        host->set_int(host->store, "monitors", "gamma_preset", g_mon.gamma_preset);
        host->set_int(host->store, "monitors", "window_scale", g_mon.window_scale);
        host->set_int(host->store, "monitors", "desktop_width", OSPI_W);
        host->set_int(host->store, "monitors", "desktop_height", OSPI_H);
    }
    if (host->set_bool) {
        host->set_bool(host->store, "monitors", "scanlines", g_mon.scanlines);
        host->set_bool(host->store, "monitors", "integer_scale", g_mon.integer_scale);
    }
    return host->flush_store(host->store);
}

static Rect color_mode_btn(Rect c, int i) {
    return (Rect){c.x + 14 + (i % 2) * 162, c.y + 24 + (i / 2) * 32, 152, 26};
}
static Rect bright_minus_btn(Rect c) { return (Rect){c.x + 112, c.y + 182, 32, 24}; }
static Rect bright_track_rect(Rect c) { return (Rect){c.x + 152, c.y + 182, 248, 24}; }
static Rect bright_plus_btn(Rect c) { return (Rect){c.x + 408, c.y + 182, 32, 24}; }
static Rect bright_reset_btn(Rect c) { return (Rect){c.x + 506, c.y + 182, 66, 24}; }
static Rect gamma_btn(Rect c, int i) {
    return (Rect){c.x + 112 + i * 154, c.y + 218, 146, 24};
}
static Rect scanline_chk(Rect c) { return (Rect){c.x + 16, c.y + 294, 268, 24}; }
static Rect scale_chk(Rect c) { return (Rect){c.x + 300, c.y + 294, 276, 24}; }
static Rect window_scale_btn(Rect c, int i) { return (Rect){c.x + 112 + i * 70, c.y + 324, 66, 24}; }
/* The Desktop Size rows stay inside the left 460 pixels of the panel so they
 * are on screen on the narrowest (480 wide) desktop without moving the panel. */
static Rect aspect_btn(Rect c, int i) { return (Rect){c.x + 70 + i * 56, c.y + 110, 52, 22}; }
static Rect orient_btn(Rect c, int i) { return (Rect){c.x + 250 + i * 96, c.y + 110, 92, 22}; }
static Rect size_btn(Rect c, int i) { return (Rect){c.x + 16 + i * 88, c.y + 136, 84, 22}; }

static int bright_from_x(Rect track, int x) {
    int span = track.w - 12;
    if (span < 1) return 100;
    int rel = clamp_int(x - (track.x + 6), 0, span);
    return 50 + (rel * 70 + span / 2) / span;
}

static void apply_and_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    sync_session(&g_mon, host);
    monitors_save(self, host);
}

/* Switch the desktop now; the host reallocates and resizes before the next frame. */
static int set_mode(os999_cdev_t *self, const os999_cdev_host_t *host, int w, int h) {
    if ((w != OSPI_W || h != OSPI_H) && display_mode_apply(host ? host->session : NULL, w, h) == 0)
        monitors_save(self, host);
    browse_current_mode();
    return 1;
}

static int monitors_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                          const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_MOVE && g_mon.dragging_bright) {
        g_mon.brightness = bright_from_x(bright_track_rect(c), ev->x);
        apply_and_save(self, host);
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_UP && g_mon.dragging_bright) {
        g_mon.dragging_bright = 0;
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key >= '1' && ev->key <= '4') {
            g_mon.color_mode = ev->key - '1';
            apply_and_save(self, host);
            return 1;
        }
        if (ev->key == '-' || ev->key == '[') {
            g_mon.brightness = clamp_int(g_mon.brightness - 5, 50, 120);
            apply_and_save(self, host);
            return 1;
        }
        if (ev->key == '=' || ev->key == '+' || ev->key == ']') {
            g_mon.brightness = clamp_int(g_mon.brightness + 5, 50, 120);
            apply_and_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    for (int i = 0; i < 4; i++) {
        if (rect_has(color_mode_btn(c, i), x, y)) {
            g_mon.color_mode = i;
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(bright_minus_btn(c), x, y)) {
        g_mon.brightness = clamp_int(g_mon.brightness - 5, 50, 120);
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(bright_plus_btn(c), x, y)) {
        g_mon.brightness = clamp_int(g_mon.brightness + 5, 50, 120);
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(bright_reset_btn(c), x, y)) {
        g_mon.brightness = 100;
        g_mon.gamma_preset = 0;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(bright_track_rect(c), x, y)) {
        g_mon.dragging_bright = 1;
        g_mon.brightness = bright_from_x(bright_track_rect(c), x);
        apply_and_save(self, host);
        return 1;
    }
    for (int i = 0; i < 3; i++) {
        if (rect_has(gamma_btn(c, i), x, y)) {
            g_mon.gamma_preset = i;
            apply_and_save(self, host);
            return 1;
        }
    }
    if (rect_has(scanline_chk(c), x, y)) {
        g_mon.scanlines = !g_mon.scanlines;
        apply_and_save(self, host);
        return 1;
    }
    if (rect_has(scale_chk(c), x, y)) {
        g_mon.integer_scale = !g_mon.integer_scale;
        apply_and_save(self, host);
        return 1;
    }
    for (int i = 0; i < 5; i++) {
        if (rect_has(window_scale_btn(c, i), x, y)) {
            g_mon.window_scale = k_window_scales[i];
            apply_and_save(self, host);
            return 1;
        }
    }
    for (int i = 0; i < DISPLAY_ASPECTS; i++)
        if (rect_has(aspect_btn(c, i), x, y)) { g_mon.aspect = i; return 1; }   /* lists sizes; no switch yet */
    for (int i = 0; i < 2; i++)   /* Landscape / Portrait turns the current desktop on its side */
        if (rect_has(orient_btn(c, i), x, y))
            return (OSPI_H > OSPI_W) == i ? 1 : set_mode(self, host, OSPI_H, OSPI_W);
    for (int i = 0; i < DISPLAY_SIZES; i++) {
        int w, h;
        if (rect_has(size_btn(c, i), x, y) &&
            display_mode_size(display_mode_index(g_mon.aspect, i, g_mon.portrait), &w, &h) == 0)
            return set_mode(self, host, w, h);
    }
    return 0;
}

/* A small screen in the shape of the current mode, and what it is. */
static void draw_monitor_preview(Fb *fb, Rect box) {
    uint32_t frame = desktop_theme_color(THEME_FRAME), fg = desktop_theme_color(THEME_FOREGROUND);
    int w = 64, h = 64 * OSPI_H / OSPI_W;
    if (h > 56) { h = 56; w = 56 * OSPI_W / OSPI_H; }
    Rect scr = {box.x + 12 + (64 - w) / 2, box.y + 20 + (56 - h) / 2, w, h};
    fb_fill(fb, scr.x - 3, scr.y - 3, scr.w + 6, scr.h + 6, desktop_theme_color(THEME_MENU));
    fb_frame(fb, scr.x - 3, scr.y - 3, scr.w + 6, scr.h + 6, frame);
    fb_fill(fb, scr.x, scr.y, scr.w, scr.h, desktop_theme_color(THEME_DESKTOP_TOP));
    fb_fill(fb, scr.x, scr.y, scr.w, 4, C_WHITE);
    fb_frame(fb, scr.x, scr.y, scr.w, scr.h, C_BLACK);
    int mode = display_mode_find(OSPI_W, OSPI_H);
    char line[48];
    snprintf(line, sizeof line, "Canvas: %d x %d", OSPI_W, OSPI_H);
    fb_text(fb, box.x + 88, box.y + 20, fg, line);
    snprintf(line, sizeof line, "%s %s", display_mode_aspect(mode), OSPI_H > OSPI_W ? "portrait" : "landscape"); /* must fit the group box */
    fb_text(fb, box.x + 88, box.y + 40, fg, line);
    snprintf(line, sizeof line, "Shown at %d%%", g_mon.window_scale);
    fb_text(fb, box.x + 88, box.y + 60, desktop_theme_color(THEME_DISABLED), line);
}

static void monitors_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                            Fb *fb, Rect c) {
    (void)self; (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, 344, 86}, "Colors & Bit Depth");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, color_mode_btn(c, i), k_color_labels[i], g_mon.color_mode == i, 1);

    Rect prev = {c.x + 356, c.y + 2, c.w - 356, 86};
    cdev_ui_group(fb, prev, "Display Geometry");
    draw_monitor_preview(fb, prev);

    cdev_ui_group(fb, (Rect){c.x, c.y + 92, c.w, 70}, "Desktop Size");
    fb_text(fb, c.x + 16, c.y + 114, fg, "Shape:");
    for (int i = 0; i < DISPLAY_ASPECTS; i++)
        cdev_ui_button(fb, aspect_btn(c, i), display_aspect_name(i), g_mon.aspect == i, 1);
    cdev_ui_button(fb, orient_btn(c, 0), "Landscape", OSPI_W >= OSPI_H, 1);
    cdev_ui_button(fb, orient_btn(c, 1), "Portrait", OSPI_H > OSPI_W, 1);
    for (int i = 0; i < DISPLAY_SIZES; i++) {
        int w = 0, h = 0;
        char label[16];
        display_mode_size(display_mode_index(g_mon.aspect, i, g_mon.portrait), &w, &h);
        snprintf(label, sizeof label, "%dx%d", w, h);
        cdev_ui_button(fb, size_btn(c, i), label, w == OSPI_W && h == OSPI_H, 1);
    }

    cdev_ui_group(fb, (Rect){c.x, c.y + 162, c.w, 100}, "Brightness & Color Profile");
    fb_text(fb, c.x + 16, c.y + 187, fg, "Brightness:");
    cdev_ui_button(fb, bright_minus_btn(c), "-", 0, g_mon.brightness > 50);
    cdev_ui_slider(fb, bright_track_rect(c), g_mon.brightness - 50, 70, 0);
    cdev_ui_button(fb, bright_plus_btn(c), "+", 0, g_mon.brightness < 120);
    char bpct[16];
    snprintf(bpct, sizeof bpct, "%d%%", g_mon.brightness);
    fb_text(fb, c.x + 448, c.y + 187, fg, bpct);
    cdev_ui_button(fb, bright_reset_btn(c), "Reset", 0,
                   g_mon.brightness != 100 || g_mon.gamma_preset != 0);

    fb_text(fb, c.x + 16, c.y + 223, fg, "Gamma:");
    for (int i = 0; i < 3; i++)
        cdev_ui_button(fb, gamma_btn(c, i), k_gamma_labels[i], g_mon.gamma_preset == i, 1);

    cdev_ui_group(fb, (Rect){c.x, c.y + 270, c.w, 86}, "Raster & Scaling Options");
    cdev_ui_checkbox(fb, scanline_chk(c), "CRT Aperture Scanlines", g_mon.scanlines);
    cdev_ui_checkbox(fb, scale_chk(c), "Crisp Nearest-Neighbor Scaling", g_mon.integer_scale);
    fb_text(fb, c.x + 16, c.y + 329, fg, "Screen size:");
    for (int i = 0; i < 5; i++) {
        char label[8];
        snprintf(label, sizeof label, "%d%%", k_window_scales[i]);
        cdev_ui_button(fb, window_scale_btn(c, i), label, g_mon.window_scale == k_window_scales[i], 1);
    }
}

static const os999_cdev_t k_monitors_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "monitors",
    .title = "Monitors & Display",
    .summary = "Bit depth, brightness, gamma, and scaling",
    .icon_name = "monitor",
    .category = OS999_CDEV_CAT_HARDWARE,
    .init = monitors_init,
    .shutdown = monitors_shutdown,
    .load_state = monitors_load,
    .save_state = monitors_save,
    .handle_event = monitors_event,
    .render = monitors_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_monitors_module(void) { return &k_monitors_cdev; }
