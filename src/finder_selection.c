#include "finder_selection.h"
#include "session_int.h"
#include "desktop_theme.h"
#include "folder_view.h"
#include <stdlib.h>
void finder_selection_press(Session *s, Window *w, int index) {
    int extend = s->mouse_mods & (MOD_SHIFT | MOD_CMD);
    s->press_selection_keep = 0;
    for (int i = 0; i < w->nitems; i++) s->selection_before[i] = !!window_items(w)[i].selected;
    if (index < 0) {
        if (!extend) select_only(w, -1);
        return;
    }
    if (extend && w->view_mode == VIEW_LIST && (s->mouse_mods & MOD_SHIFT) &&
        w->selection_anchor >= 0 && w->selection_anchor < w->nitems) {
        int low = index < w->selection_anchor ? index : w->selection_anchor;
        int high = index > w->selection_anchor ? index : w->selection_anchor;
        for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = i >= low && i <= high;
    } else if (extend) {
        window_items(w)[index].selected = !window_items(w)[index].selected; w->selection_anchor = index;
    } else if (!window_items(w)[index].selected) select_only(w, index);
    else s->press_selection_keep = 1;
    w->selection_cursor = index;
}
static Rect selection_rect(const Session *s) {
    int x = s->press_x < s->mx ? s->press_x : s->mx;
    int y = s->press_y < s->my ? s->press_y : s->my;
    return (Rect){x, y, abs(s->mx - s->press_x) + 1, abs(s->my - s->press_y) + 1};
}
void finder_selection_marquee(Session *s, int x, int y) {
    Window *w = finder_window(s, s->press_win);
    if (!w) return;
    if (w->kind != WIN_FOLDER || w->collapsed) return;
    int dx = x - s->press_x, dy = y - s->press_y;
    if (!s->drag && dx * dx + dy * dy <= 16) return;
    s->drag = 5;
    Layout lay; window_layout(w, &lay); Rect box = selection_rect(s);
    int left = box.x > lay.content.x ? box.x : lay.content.x;
    int right = box.x + box.w < lay.content.x + lay.content.w ? box.x + box.w : lay.content.x + lay.content.w;
    int top = box.y > lay.content.y ? box.y : lay.content.y;
    int bottom = box.y + box.h < lay.content.y + lay.content.h ? box.y + box.h : lay.content.y + lay.content.h;
    for (int i = 0; i < w->nitems; i++) {
        int cx, cy; ospi_item_center(s, s->press_win, i, &cx, &cy);
        int half_width = w->view_mode == VIEW_LIST ? lay.content.w / 2 : CELL_W / 2 - 4;
        int half_height = w->view_mode == VIEW_LIST ? FOLDER_LIST_ROW / 2 : 24;
        int hit = left < right && top < bottom && cx + half_width >= left && cx - half_width <= right &&
                  cy + half_height >= top && cy - half_height <= bottom;
        window_items(w)[i].selected = hit || ((s->mouse_mods & (MOD_SHIFT | MOD_CMD)) && s->selection_before[i]);
    }
    w->selection_anchor = w->selection_cursor = -1;
}
void finder_selection_paint(Fb *fb, const Session *s) {
    Window *w = finder_window(s, s->press_win);
    if (s->drag != 5 || !w) return;
    Layout lay; window_layout(w, &lay);
    Rect r = selection_rect(s);
    fb_clip(fb, lay.content.x, lay.content.y, lay.content.w, lay.content.h);
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FOREGROUND));
    fb_unclip(fb);
}
