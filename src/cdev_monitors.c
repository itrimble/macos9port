#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"

#include <stdio.h>

typedef struct {
    int color_mode;
    int brightness;
    int gamma_preset;
    int scanlines;
    int integer_scale;
    int dragging_bright;
} MonitorsState;

static MonitorsState g_mon = {0, 100, 0, 0, 1, 0};

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
}

static int monitors_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_int) {
            g_mon.color_mode = clamp_int(
                host->get_int(host->store, "monitors", "color_mode", 0), 0, 3);
            g_mon.brightness = clamp_int(
                host->get_int(host->store, "monitors", "brightness", 100), 50, 120);
            g_mon.gamma_preset = clamp_int(
                host->get_int(host->store, "monitors", "gamma_preset", 0), 0, 2);
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
    g_mon = (MonitorsState){0, 100, 0, 0, 1, 0};
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
    return 0;
}

static void draw_monitor_preview(Fb *fb, Rect box, const Session *s) {
    uint32_t frame = desktop_theme_color(THEME_FRAME);
    fb_fill(fb, box.x + 28, box.y + 18, 176, 104, desktop_theme_color(THEME_MENU));
    fb_frame(fb, box.x + 28, box.y + 18, 176, 104, frame);
    Rect scr = {box.x + 38, box.y + 26, 156, 82};
    fb_fill(fb, scr.x, scr.y, scr.w, scr.h, desktop_theme_color(THEME_DESKTOP_TOP));
    fb_fill(fb, scr.x, scr.y, scr.w, 8, C_WHITE);
    fb_hline(fb, scr.x, scr.y + 8, scr.w, C_BLACK);
    fb_fill(fb, scr.x + 4, scr.y + 2, 5, 4, 0xFF3AA83Au);
    fb_fill(fb, scr.x + 18, scr.y + 18, 96, 52, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, scr.x + 18, scr.y + 18, 96, 52, frame);
    fb_fill(fb, scr.x + 19, scr.y + 19, 94, 7, cdev_ui_selection_color());
    fb_frame(fb, scr.x, scr.y, scr.w, scr.h, C_BLACK);
    fb_fill(fb, box.x + 188, box.y + 114, 6, 4, 0xFF28B44Cu);
    fb_fill(fb, box.x + 96, box.y + 122, 40, 8, desktop_theme_color(THEME_TRACK));
    fb_frame(fb, box.x + 96, box.y + 122, 40, 8, frame);
    fb_fill(fb, box.x + 76, box.y + 130, 80, 6, desktop_theme_color(THEME_MENU));
    fb_frame(fb, box.x + 76, box.y + 130, 80, 6, frame);
    char res[40];
    int sw = (s && s->sw > 0) ? s->sw : OSPI_W;
    int sh = (s && s->sh > 0) ? s->sh : OSPI_H;
    snprintf(res, sizeof res, "%d x %d", sw, sh);
    fb_text(fb, scr.x + 26, scr.y + 38, desktop_theme_color(THEME_FOREGROUND), res);
}

static void monitors_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                            Fb *fb, Rect c) {
    (void)self;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, 344, 152}, "Colors & Bit Depth");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, color_mode_btn(c, i), k_color_labels[i], g_mon.color_mode == i, 1);

    static const uint32_t k_bars[8] = {
        0xFFFFFFFFu, 0xFFF2E228u, 0xFF2CC8D8u, 0xFF2CB84Au,
        0xFFD438C4u, 0xFFD82E2Eu, 0xFF2846D8u, 0xFF181818u
    };
    for (int i = 0; i < 8; i++) {
        Rect b = {c.x + 14 + i * 39, c.y + 92, 40, 22};
        fb_fill(fb, b.x, b.y, b.w, b.h, k_bars[i]);
    }
    fb_frame(fb, c.x + 14, c.y + 92, 313, 22, desktop_theme_color(THEME_FRAME));
    fb_text(fb, c.x + 14, c.y + 123, desktop_theme_color(THEME_DISABLED),
            "Canvas: 1024 x 768 logical 4:3 desktop");

    Rect prev = {c.x + 356, c.y + 2, c.w - 356, 152};
    cdev_ui_group(fb, prev, "Display Geometry");
    draw_monitor_preview(fb, prev, host ? host->session : NULL);

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
    fb_text(fb, c.x + 16, c.y + 326, desktop_theme_color(THEME_DISABLED),
            "Bit-depth quantization, tone curves, and scanlines render live on the desktop.");
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
