#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int theme_index;
    int wallpaper_index;
    int desktop_pattern;
    int cursor_blink_ms;
    int menu_blink_count;
} GeneralState;

static GeneralState g_gen = {0, -1, 0, 530, 3};

static const int k_blink_rates[4] = {0, 850, 530, 250};
static const char *const k_blink_labels[4] = {"Off", "Slow", "Medium", "Fast"};
static const char *const k_pattern_names[4] = {"Stipple", "Grid", "Weave", "Solid"};
static const char *const k_menu_labels[4] = {"Off", "1x", "2x", "3x"};

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static int nearest_blink_idx(int ms) {
    if (ms <= 0) return 0;
    if (ms >= 700) return 1;
    if (ms <= 360) return 3;
    return 2;
}

static void apply_to_session(const GeneralState *st, const os999_cdev_host_t *host) {
    if (!host || !host->session) return;
    Session *s = host->session;
    s->desktop_pattern = clamp_int(st->desktop_pattern, 0, 3);
    s->cursor_blink_ms = st->cursor_blink_ms;
    s->cursor_blink_set = 1;
    s->menu_blink_count = clamp_int(st->menu_blink_count, 0, 3);
}

static int general_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_gen;
    g_gen.theme_index = desktop_theme_current();
    g_gen.wallpaper_index = desktop_wallpaper_current();
    g_gen.desktop_pattern = 0;
    g_gen.cursor_blink_ms = 530;
    g_gen.menu_blink_count = 3;
    if (self->load_state) self->load_state(self, host);
    return 0;
}

static void general_shutdown(os999_cdev_t *self) { self->priv = NULL; }

static int general_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    g_gen.theme_index = desktop_theme_current();
    g_gen.wallpaper_index = desktop_wallpaper_current();
    if (host && host->store && host->get_int) {
        int max_t = desktop_theme_count() > 0 ? desktop_theme_count() - 1 : 0;
        g_gen.theme_index = clamp_int(
            host->get_int(host->store, "general", "theme_index", g_gen.theme_index), 0, max_t);
        g_gen.desktop_pattern = clamp_int(
            host->get_int(host->store, "general", "desktop_pattern", 0), 0, 3);
        g_gen.cursor_blink_ms = clamp_int(
            host->get_int(host->store, "general", "cursor_blink_ms", 530), 0, 2000);
        g_gen.menu_blink_count = clamp_int(
            host->get_int(host->store, "general", "menu_blink_count", 3), 0, 3);
    }
    apply_to_session(&g_gen, host);
    return 0;
}

static int general_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->set_int || !host->flush_store) return -1;
    host->set_int(host->store, "general", "theme_index", g_gen.theme_index);
    host->set_int(host->store, "general", "wallpaper_index", g_gen.wallpaper_index);
    host->set_int(host->store, "general", "desktop_pattern", g_gen.desktop_pattern);
    host->set_int(host->store, "general", "cursor_blink_ms", g_gen.cursor_blink_ms);
    host->set_int(host->store, "general", "menu_blink_count", g_gen.menu_blink_count);
    return host->flush_store(host->store);
}

static Rect theme_prev_btn(Rect c) { return (Rect){c.x + c.w - 188, c.y + 20, 82, 24}; }
static Rect theme_next_btn(Rect c) { return (Rect){c.x + c.w - 98, c.y + 20, 82, 24}; }
static Rect pattern_rect(Rect c, int i) { return (Rect){c.x + 16 + i * 68, c.y + 132, 56, 36}; }
static Rect pic_prev_btn(Rect c) { return (Rect){c.x + 312, c.y + 160, 76, 24}; }
static Rect pic_plain_btn(Rect c) { return (Rect){c.x + 396, c.y + 160, 76, 24}; }
static Rect pic_next_btn(Rect c) { return (Rect){c.x + 480, c.y + 160, 76, 24}; }
static Rect blink_btn(Rect c, int i) { return (Rect){c.x + 118 + i * 70, c.y + 256, 64, 24}; }
static Rect menu_blink_btn(Rect c, int i) { return (Rect){c.x + 118 + i * 70, c.y + 296, 64, 24}; }

static void step_theme(os999_cdev_t *self, const os999_cdev_host_t *host, int delta) {
    int n = desktop_theme_count();
    if (n <= 0) return;
    int cur = (desktop_theme_current() + delta + n) % n;
    if (host && host->session) session_command(host->session, CMD_THEME_BASE + cur);
    g_gen.theme_index = desktop_theme_current();
    general_save(self, host);
}

static void select_wallpaper(os999_cdev_t *self, const os999_cdev_host_t *host, int next_idx) {
    int count = desktop_wallpaper_count();
    int limit = CMD_WALLPAPER_LAST - CMD_WALLPAPER_BASE;
    if (count > limit) count = limit;
    if (next_idx < -1) next_idx = count - 1;
    if (next_idx >= count) next_idx = -1;
    if (host && host->session)
        session_command(host->session, CMD_WALLPAPER_BASE + next_idx + 1);
    g_gen.wallpaper_index = desktop_wallpaper_current();
    general_save(self, host);
}

static int general_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                         const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key == '[' || ev->key == '-') { step_theme(self, host, -1); return 1; }
        if (ev->key == ']' || ev->key == '=' || ev->key == '+') { step_theme(self, host, 1); return 1; }
        if (ev->key >= '1' && ev->key <= '4') {
            g_gen.cursor_blink_ms = k_blink_rates[ev->key - '1'];
            apply_to_session(&g_gen, host);
            general_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    if (rect_has(theme_prev_btn(c), x, y)) { step_theme(self, host, -1); return 1; }
    if (rect_has(theme_next_btn(c), x, y)) { step_theme(self, host, 1); return 1; }
    for (int i = 0; i < 4; i++) {
        if (rect_has(pattern_rect(c, i), x, y)) {
            g_gen.desktop_pattern = i;
            if (desktop_wallpaper_current() >= 0 && host && host->session)
                session_command(host->session, CMD_WALLPAPER_BASE);
            g_gen.wallpaper_index = desktop_wallpaper_current();
            apply_to_session(&g_gen, host);
            general_save(self, host);
            return 1;
        }
    }
    if (rect_has(pic_prev_btn(c), x, y)) {
        select_wallpaper(self, host, desktop_wallpaper_current() - 1);
        return 1;
    }
    if (rect_has(pic_plain_btn(c), x, y)) {
        select_wallpaper(self, host, -1);
        return 1;
    }
    if (rect_has(pic_next_btn(c), x, y)) {
        select_wallpaper(self, host, desktop_wallpaper_current() + 1);
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (rect_has(blink_btn(c, i), x, y)) {
            g_gen.cursor_blink_ms = k_blink_rates[i];
            apply_to_session(&g_gen, host);
            general_save(self, host);
            return 1;
        }
        if (rect_has(menu_blink_btn(c, i), x, y)) {
            g_gen.menu_blink_count = i;
            apply_to_session(&g_gen, host);
            general_save(self, host);
            return 1;
        }
    }
    return 0;
}

static void general_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                           Fb *fb, Rect c) {
    (void)self;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    g_gen.theme_index = desktop_theme_current();
    g_gen.wallpaper_index = desktop_wallpaper_current();

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 96}, "System Appearance");
    char theme_line[96];
    snprintf(theme_line, sizeof theme_line, "Theme: %s (%d of %d)",
             desktop_theme_name(g_gen.theme_index), g_gen.theme_index + 1,
             desktop_theme_count() > 0 ? desktop_theme_count() : 1);
    fb_text(fb, c.x + 16, c.y + 25, fg, theme_line);
    cdev_ui_button(fb, theme_prev_btn(c), "< Prev", 0, desktop_theme_count() > 1);
    cdev_ui_button(fb, theme_next_btn(c), "Next >", 0, desktop_theme_count() > 1);
    const int roles[5] = {THEME_PANEL, THEME_MENU, THEME_ACCENT,
                          THEME_DESKTOP_TOP, THEME_DESKTOP_BOTTOM};
    const char *const rnames[5] = {"Panel", "Menu", "Accent", "Desk A", "Desk B"};
    for (int i = 0; i < 5; i++) {
        Rect sw = {c.x + 16 + i * 112, c.y + 54, 100, 22};
        fb_fill(fb, sw.x, sw.y, sw.w, sw.h, desktop_theme_color(roles[i]));
        fb_frame(fb, sw.x, sw.y, sw.w, sw.h, desktop_theme_color(THEME_FRAME));
        fb_fill(fb, sw.x + 4, sw.y + 3, fb_text_width(rnames[i]) + 6, 15,
                desktop_theme_color(THEME_PANEL));
        fb_text(fb, sw.x + 7, sw.y + 4, fg, rnames[i]);
    }

    cdev_ui_group(fb, (Rect){c.x, c.y + 106, c.w, 114}, "Desktop Pattern & Picture");
    for (int i = 0; i < 4; i++) {
        Rect pr = pattern_rect(c, i);
        int active = (g_gen.wallpaper_index < 0 && g_gen.desktop_pattern == i);
        cdev_ui_pattern_tile(fb, pr, i, active);
        int tw = fb_text_width(k_pattern_names[i]);
        fb_text(fb, pr.x + (pr.w - tw) / 2, pr.y + pr.h + 4, fg, k_pattern_names[i]);
    }
    char pic_line[96];
    if (g_gen.wallpaper_index < 0)
        snprintf(pic_line, sizeof pic_line, "Picture: Plain (%s)",
                 k_pattern_names[clamp_int(g_gen.desktop_pattern, 0, 3)]);
    else
        snprintf(pic_line, sizeof pic_line, "Picture %02d: %s",
                 g_gen.wallpaper_index + 1, desktop_wallpaper_name(g_gen.wallpaper_index));
    fb_clip(fb, c.x + 312, c.y + 128, c.w - 324, 24);
    fb_text(fb, c.x + 312, c.y + 132, fg, pic_line);
    fb_unclip(fb);
    cdev_ui_button(fb, pic_prev_btn(c), "< Pic", 0, desktop_wallpaper_count() > 0);
    cdev_ui_button(fb, pic_plain_btn(c), "Plain", g_gen.wallpaper_index < 0, 1);
    cdev_ui_button(fb, pic_next_btn(c), "Pic >", 0, desktop_wallpaper_count() > 0);

    cdev_ui_group(fb, (Rect){c.x, c.y + 228, c.w, 128}, "Insertion Point & Menu Blinking");
    fb_text(fb, c.x + 16, c.y + 261, fg, "Cursor Blink:");
    int bidx = nearest_blink_idx(g_gen.cursor_blink_ms);
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, blink_btn(c, i), k_blink_labels[i], bidx == i, 1);

    Rect sample = {c.x + 404, c.y + 254, c.w - 420, 28};
    fb_fill(fb, sample.x, sample.y, sample.w, sample.h, C_WHITE);
    fb_frame(fb, sample.x, sample.y, sample.w, sample.h, C_BLACK);
    const char *demo = "OS999 Text";
    int ty = sample.y + (sample.h - typeface_line_height(0)) / 2;
    fb_text(fb, sample.x + 8, ty, C_BLACK, demo);
    uint32_t now = (host && host->session) ? host->session->input_now_ms : 0;
    int show_caret = g_gen.cursor_blink_ms <= 0 ||
                     ((now / (uint32_t)g_gen.cursor_blink_ms) % 2u == 0);
    if (show_caret)
        fb_fill(fb, sample.x + 10 + fb_text_width(demo), ty - 1, 2,
                typeface_line_height(0) + 2, C_BLACK);

    fb_text(fb, c.x + 16, c.y + 301, fg, "Menu Blink:");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, menu_blink_btn(c, i), k_menu_labels[i],
                       g_gen.menu_blink_count == i, 1);
    fb_text(fb, c.x + 16, c.y + 333, desktop_theme_color(THEME_DISABLED),
            "Changes apply immediately and persist in the unified Control Panels store.");
}

static const os999_cdev_t k_general_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "general",
    .title = "General Controls",
    .summary = "Desktop pattern, themes, and cursor blink",
    .icon_name = "settings",
    .category = OS999_CDEV_CAT_SYSTEM,
    .init = general_init,
    .shutdown = general_shutdown,
    .load_state = general_load,
    .save_state = general_save,
    .handle_event = general_event,
    .render = general_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_general_module(void) { return &k_general_cdev; }
