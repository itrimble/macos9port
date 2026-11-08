#include "finder_desktop_surface.h"
#include "session_int.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"
#include "finder_selection.h"
#include "draw.h"

static void theme_desktop(Fb *fb, int pattern) {
    if (pattern <= 0 && !desktop_theme_current()) { fb_desktop(fb); return; }
    uint32_t a = desktop_theme_current() ? desktop_theme_color(THEME_DESKTOP_TOP) : C_DESK;
    uint32_t b = desktop_theme_current() ? desktop_theme_color(THEME_DESKTOP_BOTTOM) : C_DESK2;
    for (int y = 0; y < fb->h; y++) {
        uint32_t *row = fb->px + y * fb->w;
        for (int x = 0; x < fb->w; x++) {
            int alt = 0;
            if (pattern <= 0) alt = (x ^ y) & 1;
            else if (pattern == 1) alt = ((x & 3) == 0) || ((y & 3) == 0);
            else if (pattern == 2) alt = ((x + y) & 3) < 2;
            row[x] = alt ? a : b;
        }
    }
}

static void apply_display_fx(Fb *fb, const Session *s) {
    if (!s || !fb || !fb->px) return;
    int br = s->display_brightness > 0 ? s->display_brightness : 100;
    if (s->display_dimmed) br = (br * 55) / 100;
    int cm = s->display_color_mode;
    int gm = s->display_gamma;
    int sl = s->display_scanlines;
    if (cm <= 0 && br == 100 && gm <= 0 && !sl) return;

    int r_mul = br, g_mul = br, b_mul = br;
    if (gm == 1) { r_mul = br * 94 / 100; b_mul = br * 106 / 100; }
    else if (gm == 2) { r_mul = br * 106 / 100; b_mul = br * 91 / 100; }

    for (int y = 0; y < fb->h; y++) {
        uint32_t *row = fb->px + y * fb->w;
        int row_r = (sl && (y & 1)) ? (r_mul * 86) / 100 : r_mul;
        int row_g = (sl && (y & 1)) ? (g_mul * 86) / 100 : g_mul;
        int row_b = (sl && (y & 1)) ? (b_mul * 86) / 100 : b_mul;
        for (int x = 0; x < fb->w; x++) {
            uint32_t p = row[x];
            unsigned r = ((p >> 16) & 255u) * (unsigned)row_r / 100u;
            unsigned g = ((p >> 8) & 255u) * (unsigned)row_g / 100u;
            unsigned b = (p & 255u) * (unsigned)row_b / 100u;
            if (r > 255u) r = 255u;
            if (g > 255u) g = 255u;
            if (b > 255u) b = 255u;
            if (cm == 1) {
                r = (r & 0xF8u) | (r >> 5);
                g = (g & 0xFCu) | (g >> 6);
                b = (b & 0xF8u) | (b >> 5);
            } else if (cm == 2) {
                r = ((r + 25u) / 51u) * 51u;
                g = ((g + 25u) / 51u) * 51u;
                b = ((b + 25u) / 51u) * 51u;
                if (r > 255u) r = 255u;
                if (g > 255u) g = 255u;
                if (b > 255u) b = 255u;
            } else if (cm == 3) {
                unsigned l = (r * 77u + g * 150u + b * 29u) >> 8;
                r = g = b = l;
            }
            row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

void session_paint(Session *s, uint32_t *px, int w, int h) {
    s->sw = w;
    s->sh = h;
    session_clamp(s);
    Fb fb;
    fb_init(&fb, px, w, h);
    if (!desktop_wallpaper_draw(&fb)) theme_desktop(&fb, s->desktop_pattern);
    session_desktop_icons_paint(&fb, s);
    finder_desktop_overflow_paint(&fb, s);
    session_windows_paint(s, px, w, h);
    session_overlay_paint(s, px, w, h);
    apply_display_fx(&fb, s);
}
