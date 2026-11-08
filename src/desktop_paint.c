#include "finder_desktop_surface.h"
#include "session_int.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"
#include "finder_selection.h"
#include "draw.h"

static void theme_desktop(Fb *fb) {
    if (!desktop_theme_current()) { fb_desktop(fb); return; }
    uint32_t a = desktop_theme_color(THEME_DESKTOP_TOP);
    uint32_t b = desktop_theme_color(THEME_DESKTOP_BOTTOM);
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++) fb->px[y * fb->w + x] = ((x ^ y) & 1) ? a : b;
}

void session_paint(Session *s, uint32_t *px, int w, int h) {
    s->sw = w;
    s->sh = h;
    session_clamp(s);
    Fb fb;
    fb_init(&fb, px, w, h);
    if (!desktop_wallpaper_draw(&fb)) theme_desktop(&fb);
    session_desktop_icons_paint(&fb, s);
    finder_desktop_overflow_paint(&fb, s);
    session_windows_paint(s, px, w, h);
    session_overlay_paint(s, px, w, h);
}
