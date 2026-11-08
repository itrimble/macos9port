#include "finder_popup.h"
#include "draw.h"

void finder_popup_layout(Session *s, Window *w) {
    if (!s || !w || !w->popup) return;
    int width = w->collapsed ? fb_text_width(w->title) + 36 : w->popup_w;
    if (width < (w->collapsed ? 100 : 260)) width = w->collapsed ? 100 : 260;
    if (width > s->sw) width = s->sw;
    w->w = width;
    w->h = w->collapsed ? TITLE_H : w->popup_h;
    if (w->h < TITLE_H + INFO_H + 40 && !w->collapsed) w->h = 180;
    if (w->h > s->sh - 20) w->h = s->sh - 20;
    w->x = w->popup_tab_x;
    if (w->x > s->sw - w->w) w->x = s->sw - w->w;
    if (w->x < 0) w->x = 0;
    w->y = s->sh - w->h;
}
void finder_popup_set(Session *s, Window *w, int enabled) {
    if (!s || !w || w->kind != WIN_FOLDER || w->popup == !!enabled) return;
    if (enabled) {
        w->popup_x = w->zoomed ? w->zoom_x : w->x;
        w->popup_y = w->zoomed ? w->zoom_y : w->y;
        w->popup_w = w->zoomed ? w->zoom_w : w->w;
        w->popup_h = w->zoomed ? w->zoom_h : w->collapsed ? w->shade_h : w->h;
        w->popup_tab_x = w->x;
        /* Avoid an identical starting tab position for multiple folders. */
        for (int i = 0; i < s->nwin; i++) if (&s->win[i] != w && s->win[i].popup &&
            s->win[i].popup_tab_x == w->popup_tab_x) w->popup_tab_x += 140;
        w->popup = 1; w->collapsed = 1; w->zoomed = 0;
        finder_popup_layout(s, w);
    } else {
        w->popup = 0; w->collapsed = 0;
        w->x = w->popup_x; w->y = w->popup_y; w->w = w->popup_w; w->h = w->shade_h = w->popup_h;
    }
}
void finder_popup_toggle(Session *s, Window *w) {
    if (!w || !w->popup) return;
    w->collapsed = !w->collapsed; finder_popup_layout(s, w);
}
void finder_popup_collapse_others(Session *s, int keep) {
    for (int i = 0; i < s->nwin; i++) if (i != keep && s->win[i].popup) {
        s->win[i].collapsed = 1; finder_popup_layout(s, &s->win[i]);
    }
}
