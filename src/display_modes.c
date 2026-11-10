#include "display_modes.h"
#include "control_panel_store.h"
#include "control_panels.h"
#include "app_switcher.h"

#include <stddef.h>

/* Landscape sizes, smallest first inside each aspect group. Portrait modes are
 * the same sizes turned on their side. */
static const struct { const char *landscape, *portrait; int size[DISPLAY_SIZES][2]; } k_groups[DISPLAY_ASPECTS] = {
    {"4:3", "3:4", {{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960}}},
    {"16:10", "10:16", {{960, 600}, {1280, 800}, {1440, 900}, {1680, 1050}, {1920, 1200}}},
    {"16:9", "9:16", {{854, 480}, {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}}},
};

int display_mode_index(int aspect, int size, int portrait) {
    if (aspect < 0 || aspect >= DISPLAY_ASPECTS || size < 0 || size >= DISPLAY_SIZES) return -1;
    return ((portrait ? DISPLAY_ASPECTS : 0) + aspect) * DISPLAY_SIZES + size;
}

int display_mode_size(int index, int *w, int *h) {
    if (index < 0 || index >= DISPLAY_MODE_COUNT) return -1;
    int group = index / DISPLAY_SIZES, size = index % DISPLAY_SIZES;
    int portrait = group >= DISPLAY_ASPECTS;
    const int *wh = k_groups[group % DISPLAY_ASPECTS].size[size];
    if (w) *w = wh[portrait ? 1 : 0];
    if (h) *h = wh[portrait ? 0 : 1];
    return 0;
}

const char *display_aspect_name(int aspect) {
    return aspect >= 0 && aspect < DISPLAY_ASPECTS ? k_groups[aspect].landscape : "";
}

const char *display_mode_aspect(int index) {
    if (index < 0 || index >= DISPLAY_MODE_COUNT) return "";
    int group = index / DISPLAY_SIZES;
    return group >= DISPLAY_ASPECTS ? k_groups[group - DISPLAY_ASPECTS].portrait : k_groups[group].landscape;
}

int display_mode_find(int w, int h) {
    for (int i = 0; i < DISPLAY_MODE_COUNT; i++) {
        int mw, mh;
        display_mode_size(i, &mw, &mh);
        if (mw == w && mh == h) return i;
    }
    return -1;
}

int display_set_size(int w, int h) {
    if (display_mode_find(w, h) < 0 || w < OSPI_MIN_W || h < OSPI_MIN_H || w > OSPI_MAX_W || h > OSPI_MAX_H)
        return -1;
    ospi_display_w = w;
    ospi_display_h = h;
    return 0;
}

void display_keep_reachable(int *x, int *y, int w, int grip) {
    int sw = OSPI_W, sh = OSPI_H;
    if (grip > w) grip = w;
    if (*x > sw - grip) *x = sw - grip;
    if (*x + w < grip) *x = grip - w;
    if (*y > sh - TITLE_H) *y = sh - TITLE_H;
    if (*y < TITLE_H) *y = TITLE_H;
}

int display_mode_apply(Session *s, int w, int h) {
    if (display_set_size(w, h) != 0) return -1;
    if (!s) return 0;
    s->sw = w;
    s->sh = h;
    /* An open menu or a drag was laid out for the old desktop. */
    s->menu = s->menu_hot = -1;
    s->menu_child = 0;
    s->down = s->drag = 0;
    control_panels_display_changed();
    Rect r;
    if (app_switcher_bounds(s, &r)) { s->switcher.x = r.x; s->switcher.y = r.y; }
    session_clamp(s);   /* Finder windows, pop-up tabs and desktop item positions */
    return 0;
}

void display_window_fit(int w, int h, int scale, int area_w, int area_h, int *out_w, int *out_h) {
    if (scale < 50 || scale > 200) scale = 100;
    int ww = w * scale / 100, wh = h * scale / 100;
    /* Never larger than the usable display area; shrink both sides together. */
    if (area_w > 0 && ww > area_w) { wh = (int)((long long)wh * area_w / ww); ww = area_w; }
    if (area_h > 0 && wh > area_h) { ww = (int)((long long)ww * area_h / wh); wh = area_h; }
    *out_w = ww < 1 ? 1 : ww;
    *out_h = wh < 1 ? 1 : wh;
}

void display_point(int x, int y, int window_w, int window_h, int *out_x, int *out_y) {
    if (window_w < 1) window_w = 1;
    if (window_h < 1) window_h = 1;
    *out_x = x * OSPI_W / window_w;
    *out_y = y * OSPI_H / window_h;
}

void display_mode_saved(const char *volume, int *w, int *h, int *scale) {
    int sw = OSPI_DEFAULT_W, sh = OSPI_DEFAULT_H, percent = 100;
    os999_cp_store_t store;
    if (volume && os999_cp_store_init(&store, volume) == 0) {
        if (os999_cp_store_load(&store) == 0) {
            sw = os999_cp_store_get_int(&store, "monitors", "desktop_width", OSPI_DEFAULT_W);
            sh = os999_cp_store_get_int(&store, "monitors", "desktop_height", OSPI_DEFAULT_H);
            percent = os999_cp_store_get_int(&store, "monitors", "window_scale", 100);
        }
        os999_cp_store_destroy(&store);
    }
    if (display_mode_find(sw, sh) < 0) { sw = OSPI_DEFAULT_W; sh = OSPI_DEFAULT_H; }
    if (w) *w = sw;
    if (h) *h = sh;
    if (scale) *scale = percent < 50 || percent > 200 ? 100 : percent;
}

int display_mode_startup(const char *volume) {
    int w, h, scale;
    display_mode_saved(volume, &w, &h, &scale);
    (void)display_set_size(w, h);
    return scale;
}
