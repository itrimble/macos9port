#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"

#include <stdio.h>

enum { APP_ROWS = 8, APP_ROW_H = 26 };

typedef struct {
    int tab;
    int pic_page;
    int theme_page;
} AppearanceState;

static AppearanceState g_app;

static int app_pic_count(void) {
    int n = desktop_wallpaper_count();
    int lim = CMD_WALLPAPER_LAST - CMD_WALLPAPER_BASE;
    return n < 0 ? 0 : n > lim ? lim : n;
}

static int app_pic_pages(void) {
    int total = app_pic_count() + 1;
    return total > 0 ? (total - 1) / APP_ROWS : 0;
}

static int app_theme_count(void) {
    int n = desktop_theme_count();
    int lim = CMD_THEME_LAST - CMD_THEME_BASE + 1;
    return n < 0 ? 0 : n > lim ? lim : n;
}

static int app_theme_pages(void) {
    int n = app_theme_count();
    return n > 0 ? (n - 1) / APP_ROWS : 0;
}

static int appearance_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)host;
    self->priv = &g_app;
    g_app.tab = 0;
    int cur = desktop_wallpaper_current();
    g_app.pic_page = (cur + 1) / APP_ROWS;
    if (g_app.pic_page > app_pic_pages()) g_app.pic_page = 0;
    g_app.theme_page = desktop_theme_current() / APP_ROWS;
    if (g_app.theme_page > app_theme_pages()) g_app.theme_page = 0;
    return 0;
}

static int appearance_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->set_int || !host->flush_store) return 0;
    host->set_int(host->store, "general", "theme_index", desktop_theme_current());
    host->set_int(host->store, "general", "wallpaper_index", desktop_wallpaper_current());
    return host->flush_store(host->store);
}

static Rect app_tab_btn(Rect c, int i) { return (Rect){c.x + i * 144, c.y + 4, 136, 26}; }
static Rect app_list_box(Rect c) { return (Rect){c.x, c.y + 40, c.w, APP_ROWS * APP_ROW_H + 2}; }
static Rect app_prev_btn(Rect c) { return (Rect){c.x, c.y + 260, 104, 26}; }
static Rect app_next_btn(Rect c) { return (Rect){c.x + c.w - 104, c.y + 260, 104, 26}; }

static int appearance_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                            const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_KEY && ev->key == '\t') {
        g_app.tab = !g_app.tab;
        return 1;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    for (int i = 0; i < 2; i++) {
        if (rect_has(app_tab_btn(c, i), x, y)) { g_app.tab = i; return 1; }
    }
    Rect list = app_list_box(c);
    if (g_app.tab == 0) {
        if (rect_has(app_prev_btn(c), x, y) && g_app.pic_page > 0) { g_app.pic_page--; return 1; }
        if (rect_has(app_next_btn(c), x, y) && g_app.pic_page < app_pic_pages()) {
            g_app.pic_page++;
            return 1;
        }
        if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
            int slot = g_app.pic_page * APP_ROWS + (y - list.y - 1) / APP_ROW_H;
            int idx = slot - 1;
            if (idx >= -1 && idx < app_pic_count() && host && host->session) {
                session_command(host->session, CMD_WALLPAPER_BASE + idx + 1);
                appearance_save(self, host);
            }
            return 1;
        }
    } else {
        if (rect_has(app_prev_btn(c), x, y) && g_app.theme_page > 0) { g_app.theme_page--; return 1; }
        if (rect_has(app_next_btn(c), x, y) && g_app.theme_page < app_theme_pages()) {
            g_app.theme_page++;
            return 1;
        }
        if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
            int idx = g_app.theme_page * APP_ROWS + (y - list.y - 1) / APP_ROW_H;
            if (idx >= 0 && idx < app_theme_count() && host && host->session) {
                session_command(host->session, CMD_THEME_BASE + idx);
                appearance_save(self, host);
            }
            return 1;
        }
    }
    return 0;
}

static void appearance_render(os999_cdev_t *self, const os999_cdev_host_t *host, Fb *fb, Rect c) {
    (void)self;
    (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    cdev_ui_button(fb, app_tab_btn(c, 0), "Desktop Pictures", g_app.tab == 0, 1);
    cdev_ui_button(fb, app_tab_btn(c, 1), "Color Themes", g_app.tab == 1, 1);

    Rect list = app_list_box(c);
    fb_fill(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_FRAME));

    if (g_app.tab == 0) {
        int cur = desktop_wallpaper_current();
        for (int row = 0; row < APP_ROWS; row++) {
            int idx = g_app.pic_page * APP_ROWS + row - 1;
            if (idx >= app_pic_count()) break;
            int ry = list.y + 1 + row * APP_ROW_H;
            int sel = (cur == idx);
            if (sel) fb_fill(fb, list.x + 1, ry, list.w - 2, APP_ROW_H, cdev_ui_selection_color());
            uint32_t ink = sel ? cdev_ui_accent_text() : fg;
            char line[OSPI_NAME + 16];
            if (idx < 0) snprintf(line, sizeof line, "Plain Desktop");
            else snprintf(line, sizeof line, "%02d  %s", idx + 1, desktop_wallpaper_name(idx));
            fb_text(fb, list.x + 10, ry + 5, ink, sel ? "*" : " ");
            fb_text(fb, list.x + 28, ry + 5, ink, line);
        }
        cdev_ui_button(fb, app_prev_btn(c), "Previous", 0, g_app.pic_page > 0);
        cdev_ui_button(fb, app_next_btn(c), "Next", 0, g_app.pic_page < app_pic_pages());
        char pg[48];
        snprintf(pg, sizeof pg, "Page %d of %d", g_app.pic_page + 1, app_pic_pages() + 1);
        fb_text(fb, c.x + (c.w - fb_text_width(pg)) / 2, c.y + 266, fg, pg);
    } else {
        int cur = desktop_theme_current();
        for (int row = 0; row < APP_ROWS; row++) {
            int idx = g_app.theme_page * APP_ROWS + row;
            if (idx >= app_theme_count()) break;
            int ry = list.y + 1 + row * APP_ROW_H;
            int sel = (cur == idx);
            if (sel) fb_fill(fb, list.x + 1, ry, list.w - 2, APP_ROW_H, cdev_ui_selection_color());
            uint32_t ink = sel ? cdev_ui_accent_text() : fg;
            fb_text(fb, list.x + 10, ry + 5, ink, sel ? "*" : " ");
            fb_text(fb, list.x + 28, ry + 5, ink, desktop_theme_name(idx));
        }
        cdev_ui_button(fb, app_prev_btn(c), "Previous", 0, g_app.theme_page > 0);
        cdev_ui_button(fb, app_next_btn(c), "Next", 0, g_app.theme_page < app_theme_pages());
        char pg[48];
        snprintf(pg, sizeof pg, "Page %d of %d", g_app.theme_page + 1, app_theme_pages() + 1);
        fb_text(fb, c.x + (c.w - fb_text_width(pg)) / 2, c.y + 266, fg, pg);
    }
    fb_text(fb, c.x, c.y + 306, desktop_theme_color(THEME_DISABLED),
            "Tab switches between Desktop Pictures and Color Themes.");
}

static const os999_cdev_t k_appearance_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "appearance",
    .title = "Appearance",
    .summary = "Desktop wallpaper gallery and 16 classic color themes",
    .icon_name = "control_panel",
    .category = OS999_CDEV_CAT_APPEARANCE,
    .init = appearance_init,
    .shutdown = NULL,
    .load_state = NULL,
    .save_state = appearance_save,
    .handle_event = appearance_event,
    .render = appearance_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_appearance_module(void) { return &k_appearance_cdev; }
