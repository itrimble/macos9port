#include "desktop_gesture.h"
#include "desktop_theme.h"
#include "draw.h"
#include <stdlib.h>

static void edges(Rect r, Rect out[4]) {
    out[0] = (Rect){r.x, r.y, r.w, 1};
    out[1] = (Rect){r.x, r.y + r.h - 1, r.w, 1};
    out[2] = (Rect){r.x, r.y, 1, r.h};
    out[3] = (Rect){r.x + r.w - 1, r.y, 1, r.h};
}
static Rect intersect(Rect a, Rect b) {
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (a.x < b.x) a.x = b.x;
    if (a.y < b.y) a.y = b.y;
    a.w = right > a.x ? right - a.x : 0; a.h = bottom > a.y ? bottom - a.y : 0;
    return a;
}
int session_gesture_regions(const Session *s, Rect out[DESKTOP_GESTURE_REGIONS]) {
    int n = 0; Rect clip = {0, TITLE_H, s->sw, s->sh - TITLE_H};
    if (s->drag == 3) {
        edges((Rect){s->mx - 16, s->my - 12, 32, 28}, out);
        edges((Rect){s->mx - 15, s->my - 11, 30, 26}, out + 4); n = 8;
        if (session_desktop_which(s, s->mx, s->my) == 1 && s->desk_sel != 1) {
            Rect icon, hit; session_desktop_icon_rect(s, 1, &icon, &hit);
            edges(icon, out + n); n += 4;
        }
    } else if (s->drag == 4 && s->press_zone == Z_DESKTOP) {
        Rect icon, hit;
        desktop_position_rect(s->sw, s->sh, s->orig_x + s->mx - s->press_x,
            s->orig_y + s->my - s->press_y, &icon, &hit);
        edges(icon, out); n = 4;
    } else if (s->drag == 5 && finder_window(s, s->press_win)) {
        int x = s->press_x < s->mx ? s->press_x : s->mx, y = s->press_y < s->my ? s->press_y : s->my;
        edges((Rect){x, y, abs(s->mx - s->press_x) + 1, abs(s->my - s->press_y) + 1}, out); n = 4;
        Layout l; window_layout(finder_window(s, s->press_win), &l); clip = intersect(clip, l.content);
    }
    for (int i = 0; i < n; i++) out[i] = intersect(out[i], clip);
    return n;
}
void session_gesture_paint(Fb *fb, const Session *s) {
    Rect regions[DESKTOP_GESTURE_REGIONS]; int count = session_gesture_regions(s, regions);
    for (int i = 0; i < count; i++) {
        Rect r = regions[i];
        fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(i >= 4 && i < 8 ? THEME_PANEL : THEME_FOREGROUND));
    }
}
