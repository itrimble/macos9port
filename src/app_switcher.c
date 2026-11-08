#include "app_switcher.h"
#include "finder_apps.h"
#include "desktop_theme.h"
#include "draw.h"

static const int commands[] = {CMD_APP_FINDER, CMD_APP_GUEST, CMD_APP_MEDIA, CMD_APP_BROWSER};
static int apps(const Session *s, int list[4]) {
    int n = 0;
    for (int i = 0; i < 4; i++) if (finder_apps_enabled(s, commands[i])) list[n++] = i + 1;
    return n;
}
static int row_height(const Session *s) { return s->switcher.small ? 26 : 42; }
static int cell_width(const Session *s) {
    return s->switcher.no_names ? row_height(s) : s->switcher.width > 90 ? s->switcher.width : 190;
}
int app_switcher_bounds(const Session *s, Rect *r) {
    if (!s->switcher.open) return 0;
    /* Dialogs disable activation but do not make the palette vanish. */
    int n = 1 + !!s->guest_active + !!finder_apps_media_running(s) + !!s->browser_active;
    int cw = cell_width(s), width = s->switcher.horizontal ? cw * n + 4 : cw + 4;
    int height = s->switcher.collapsed && !s->switcher.horizontal ? 20 :
        24 + row_height(s) * (s->switcher.horizontal ? 1 : n);
    if (width < 76) width = 76;
    if (width > s->sw) width = s->sw;
    int x = s->switcher.x, y = s->switcher.y;
    if (x + width > s->sw) x = s->sw - width;
    if (x < 0) x = 0;
    if (y + height > s->sh) y = s->sh - height;
    if (y < TITLE_H) y = TITLE_H;
    *r = (Rect){x, y, width, height}; return 1;
}
static Rect cell(const Session *s, Rect r, int index, int count) {
    int width = s->switcher.horizontal ? (r.w - 4) / count : r.w - 4;
    return (Rect){r.x + 2 + (s->switcher.horizontal ? index * width : 0),
        r.y + 22 + (s->switcher.horizontal ? 0 : index * row_height(s)), width, row_height(s)};
}
static int hit(const Session *s, int x, int y) {
    Rect r; if (!app_switcher_bounds(s, &r) || !rect_has(r, x, y)) return 0;
    if (y < r.y + 20) {
        if (x < r.x + 20) return -2; /* close */
        if (x >= r.x + r.w - 20) return -3; /* zoom/options */
        if (!s->switcher.horizontal && x >= r.x + r.w - 38) return -4;
        return -1;
    }
    if (x >= r.x + r.w - 5) return -5;
    int list[4], n = apps(s, list);
    for (int i = 0; i < n; i++) if (rect_has(cell(s, r, i, n), x, y)) return list[i];
    return -1;
}
int app_switcher_hit(const Session *s, int x, int y) {
    Rect r; return app_switcher_bounds(s, &r) && rect_has(r, x, y);
}
int app_switcher_application_at(const Session *s, int x, int y) {
    if (finder_apps_modal(s)) return 0;
    int part = hit(s, x, y);
    return part > 0 ? part : 0;
}
void app_switcher_open(Session *s) {
    if (!s->switcher.open) {
        if (!s->switcher.width) { s->switcher.width = 190; s->switcher.x = s->sw - 210; s->switcher.y = TITLE_H + 32; }
        s->switcher.open = 1;
    }
    s->menu = s->menu_hot = -1; s->menu_child = 0;
}
int app_switcher_tear(Session *s, int x, int y) {
    if (!s->switcher.tear_armed || s->menu != 7 || y < TITLE_H + 12) return 0;
    Rect first, last;
    menu_item_rect(7, 0, &first); menu_item_rect(7, menu_nitems(7) - 1, &last);
    Rect menu = {first.x, first.y, first.w, last.y + last.h - first.y};
    if (rect_has(menu, x, y)) return 0;
    app_switcher_open(s); s->switcher.tear_armed = 0;
    s->switcher.x = x - 35; s->switcher.y = y - 10;
    s->switcher.dx = 35; s->switcher.dy = 10; s->switcher.gesture = 1;
    s->down = 1; return app_switcher_move(s, x, y);
}
int app_switcher_down(Session *s, int x, int y) {
    if (finder_apps_modal(s)) return 0;
    int part = hit(s, x, y); if (!part) return 0;
    Rect r; app_switcher_bounds(s, &r);
    s->switcher.x = r.x; s->switcher.y = r.y;
    s->switcher.pressed = part;
    s->switcher.gesture = part == -1 ? 1 : part == -5 ? 2 : 3;
    s->switcher.dx = x - r.x; s->switcher.dy = y - r.y;
    return 1;
}
int app_switcher_move(Session *s, int x, int y) {
    if (!s->switcher.gesture) return 0;
    if (s->switcher.gesture == 1) {
        s->switcher.x = x - s->switcher.dx; s->switcher.y = y - s->switcher.dy;
        Rect r; app_switcher_bounds(s, &r); s->switcher.x = r.x; s->switcher.y = r.y;
    } else if (s->switcher.gesture == 2) {
        int list[4], n = apps(s, list); if (!n) n = 1;
        int width = (x - s->switcher.x - 4) / (s->switcher.horizontal ? n : 1);
        s->switcher.width = width < 96 ? 96 : width > 300 ? 300 : width;
    }
    return 1;
}
int app_switcher_up(Session *s, int x, int y) {
    if (!s->switcher.gesture) return 0;
    /* A release can follow a coalesced or missing final motion event. */
    if (s->switcher.gesture != 3 && !finder_apps_modal(s)) app_switcher_move(s, x, y);
    int pressed = s->switcher.pressed, activate = s->switcher.gesture == 3 && hit(s, x, y) == pressed;
    s->switcher.gesture = 0; s->down = s->drag = 0;
    if (!activate || finder_apps_modal(s)) return 1;
    if (pressed == -2) s->switcher.open = 0;
    else if (pressed == -3) {
        if ((s->mouse_mods & (MOD_OPTION | MOD_SHIFT)) == (MOD_OPTION | MOD_SHIFT)) {
            s->switcher.horizontal = !s->switcher.horizontal; s->switcher.collapsed = 0;
        } else if (s->mouse_mods & MOD_OPTION) s->switcher.small = !s->switcher.small;
        else s->switcher.no_names = !s->switcher.no_names;
    } else if (pressed == -4) s->switcher.collapsed = !s->switcher.collapsed;
    else if (pressed > 0 && finder_apps_enabled(s, commands[pressed - 1])) {
        int old = finder_apps_front(s);
        if ((s->mouse_mods & MOD_OPTION) && old != pressed) {
            if (old == 1) s->finder_hidden = 1;
            if (old == 2) s->guest_hidden = 1;
            if (old == 3) s->media_hidden = 1;
            if (old == 4) s->browser_hidden = 1;
        }
        finder_apps_command(s, commands[pressed - 1]);
    }
    return 1;
}
void app_switcher_paint(const Session *s, Fb *fb) {
    Rect r; if (!app_switcher_bounds(s, &r)) return;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND), panel = desktop_theme_color(THEME_PANEL);
    fb_fill(fb, r.x, r.y, r.w, r.h, panel); fb_frame(fb, r.x, r.y, r.w, r.h, fg);
    for (int y = r.y + 4; y < r.y + 18; y += 3) fb_hline(fb, r.x + 20, y, r.w - 60, C_GRAY);
    fb_frame(fb, r.x + 4, r.y + 4, 12, 12, fg);
    fb_frame(fb, r.x + r.w - 16, r.y + 4, 12, 12, fg);
    fb_hline(fb, r.x + r.w - 15, r.y + 7, 10, fg);
    if (!s->switcher.horizontal) fb_frame(fb, r.x + r.w - 34, r.y + 9, 12, 6, fg);
    if (s->switcher.collapsed && !s->switcher.horizontal) return;
    int list[4], n = apps(s, list);
    /* Keep rows visible behind a modal dialog, without enabling them. */
    if (s->dialog) { n = 0; list[n++] = 1; if (s->guest_active) list[n++] = 2;
        if (finder_apps_media_running(s)) list[n++] = 3;
        if (s->browser_active) list[n++] = 4; }
    const char *names[] = {"Finder", s->guest_title, "OS999Amp", "Browser"};
    const char *marks[] = {"F", "G", "M", "B"};
    for (int i = 0; i < n; i++) {
        Rect c = cell(s, r, i, n); int app = list[i], size = s->switcher.small ? 18 : 30;
        fb_clip(fb, c.x, c.y, c.w, c.h);
        if (app == finder_apps_front(s)) fb_frame(fb, c.x, c.y, c.w, c.h, fg);
        fb_fill(fb, c.x + 4, c.y + 4, size, size, C_FOLDER);
        fb_frame(fb, c.x + 4, c.y + 4, size, size, fg);
        fb_text(fb, c.x + 7, c.y + 6, C_BLACK, marks[app - 1]);
        if (!s->switcher.no_names) fb_text(fb, c.x + size + 10, c.y + (c.h - 14) / 2, fg, names[app - 1]);
        fb_unclip(fb);
    }
}
