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
}
