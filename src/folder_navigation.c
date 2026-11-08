#include "folder_navigation.h"
#include "folder_view.h"
#include <stdint.h>

static int lower_ascii(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static Rect desktop_cell(const Session *s, const Window *w, int index) {
    Rect r, hit;
    if (index < w->nitems) { r = desktop_item_cell(w, index); r.x += r.w / 2; r.y += 24; }
    else { session_desktop_icon_rect(s, index - w->nitems, &r, &hit); r.x += 16; r.y += 16; }
    return r;
}
static const char *navigation_name(const Window *w, int index) {
    return index < w->nitems ? window_items(w)[index].name : index == w->nitems ? "OS999 HD" : "Trash";
}
static int desktop_edge(const Session *s, const Window *w, int last) {
    int best = 0; Rect chosen = desktop_cell(s, w, 0);
    for (int i = 1; i < w->nitems + 2; i++) {
        Rect r = desktop_cell(s, w, i);
        int before = r.y < chosen.y || (r.y == chosen.y && r.x < chosen.x);
        int after = r.y > chosen.y || (r.y == chosen.y && r.x > chosen.x);
        if (last ? after : before) { best = i; chosen = r; }
    }
    return best;
}
static int desktop_neighbor(const Session *s, const Window *w, int current, int key) {
    Rect origin = desktop_cell(s, w, current);
    int best = current; int64_t score = INT64_MAX;
    for (int i = 0; i < w->nitems + 2; i++) {
        if (i == current) continue;
        Rect r = desktop_cell(s, w, i);
        int64_t dx = (int64_t)r.x - origin.x, dy = (int64_t)r.y - origin.y;
        int horizontal = key == MENU_KEY_LEFT || key == MENU_KEY_RIGHT;
        int64_t forward = horizontal ? dx : dy, cross = horizontal ? dy : dx;
        if (key == MENU_KEY_LEFT || key == MENU_KEY_UP) forward = -forward;
        if (forward <= 0) continue;
        int64_t distance = forward * forward + 4 * cross * cross;
        if (distance < score) { score = distance; best = i; }
    }
    return best;
}

static void reveal(Window *w, const Layout *lay, int index, int columns) {
    if (w->desktop_surface) return;
    int list = w->view_mode == VIEW_LIST;
    int height = list ? FOLDER_LIST_ROW : CELL_H;
    int top = (list ? index : index / columns) * height;
    int viewport = lay->content.h - (list ? FOLDER_LIST_HEADER : 0);
    if (viewport <= 0) return;
    if (top < w->scroll || viewport < height) w->scroll = top;
    else if (top + height > w->scroll + viewport) w->scroll = top + height - viewport;
    int maximum = window_max_scroll(w);
    if (w->scroll > maximum) w->scroll = maximum;
    if (w->scroll < 0) w->scroll = 0;
}

int text_navigation_key(Session *s, int key, int mods) {
    if (!s || mods || s->desktop_focus || s->desk_sel >= 0 || s->nwin <= 0 ||
        s->nwin > OSPI_MAX_WIN || s->down || session_overlay_active(s)) return 0;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind != WIN_TEXT || w->collapsed) return 0;
    Layout lay;
    window_layout(w, &lay);
    int line = typeface_line_height(1);
    if (line < 1) line = 1;
    int page = lay.content.h - line;
    if (page < line) page = line;
    int maximum = window_max_scroll(w);
    int position = w->scroll < 0 ? 0 : w->scroll > maximum ? maximum : w->scroll;
    if (key == FOLDER_KEY_HOME) position = 0;
    else if (key == FOLDER_KEY_END) position = maximum;
    else if (key == MENU_KEY_UP || key == FOLDER_KEY_PAGE_UP) {
        int step = key == MENU_KEY_UP ? line : page;
        position = position < step ? 0 : position - step;
    } else if (key == MENU_KEY_DOWN || key == FOLDER_KEY_PAGE_DOWN) {
        int step = key == MENU_KEY_DOWN ? line : page;
        position = maximum - position < step ? maximum : position + step;
    } else return 0;
    w->scroll = position;
    s->click_valid = 0;
    return 1;
}

int folder_navigation_key(Session *s, int key, int mods) {
    if (!s || s->guest_focused || s->media_focused || s->browser_focused || (mods & ~MOD_SHIFT) ||
        s->down || session_overlay_active(s)) return 0;
    Window *w = finder_current(s);
    if (!w && s->desktop_focus && s->desk_sel >= 0) w = &s->desktop;
    if (!w || w->kind != WIN_FOLDER || w->collapsed || w->nitems < 0 ||
        w->nitems > window_item_capacity(w)) return 0;
    int arrow = key == MENU_KEY_LEFT || key == MENU_KEY_RIGHT ||
                key == MENU_KEY_UP || key == MENU_KEY_DOWN;
    int initial = (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
                  (key >= '0' && key <= '9');
    int page_key = key == FOLDER_KEY_PAGE_UP || key == FOLDER_KEY_PAGE_DOWN;
    if (!arrow && !initial && !page_key && key != FOLDER_KEY_HOME && key != FOLDER_KEY_END) return 0;
    if (!w->nitems && !w->desktop_surface) return 1;
    int count = w->nitems + (w->desktop_surface ? 2 : 0);
    Layout lay;
    window_layout(w, &lay);
    int columns = w->view_mode == VIEW_LIST ? 1 : lay.content.w / CELL_W;
    if (columns < 1) columns = 1;
    int current = -1;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) { current = i; break; }
    if (s->desk_sel < 0 && w->selection_cursor >= 0 && w->selection_cursor < w->nitems && window_items(w)[w->selection_cursor].selected)
        current = w->selection_cursor;
    if (w->desktop_surface && s->desk_sel >= 0 && s->desk_sel < 2) current = w->nitems + s->desk_sel;
    int target = current;
    if (initial) {
        target = -1;
        for (int step = 1; step <= count; step++) {
            int index = (current + step) % count;
            if (lower_ascii((unsigned char)navigation_name(w, index)[0]) == lower_ascii(key)) {
                target = index;
                break;
            }
        }
        if (target < 0) return 1;
    } else if (key == FOLDER_KEY_HOME) target = w->desktop_surface ? desktop_edge(s, w, 0) : 0;
    else if (key == FOLDER_KEY_END) target = w->desktop_surface ? desktop_edge(s, w, 1) : w->nitems - 1;
    else if (current < 0) target = w->desktop_surface ? desktop_edge(s, w, 0) : 0;
    else if (w->desktop_surface) target = desktop_neighbor(s, w, current, key);
    else if (page_key) {
        int list = w->view_mode == VIEW_LIST;
        int rows = (lay.content.h - (list ? FOLDER_LIST_HEADER : 0)) /
                   (list ? FOLDER_LIST_ROW : CELL_H);
        if (rows < 1) rows = 1;
        int step = rows * columns;
        target = key == FOLDER_KEY_PAGE_UP ? current - step : current + step;
        if (target < 0) target = 0;
        if (target >= w->nitems) target = w->nitems - 1;
    }
    else if (key == MENU_KEY_UP) target = current >= columns ? current - columns : current;
    else if (key == MENU_KEY_DOWN) {
        if ((current / columns + 1) * columns < w->nitems) {
            target = current + columns;
            if (target >= w->nitems) target = w->nitems - 1;
        }
    } else if (key == MENU_KEY_LEFT) target = current > 0 ? current - 1 : 0;
    else target = current + 1 < w->nitems ? current + 1 : current;
    if (w->desktop_surface && target >= w->nitems) {
        for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = 0;
        w->selection_anchor = w->selection_cursor = -1;
        s->desk_sel = target - w->nitems; s->desktop_focus = 1;
        s->click_valid = s->drag = 0; s->press_item = -1; return 1;
    }
    if ((mods & MOD_SHIFT) && w->desktop_surface) {
        window_items(w)[target].selected = 1;
        if (w->selection_anchor < 0) w->selection_anchor = current < 0 ? target : current;
    } else if (mods & MOD_SHIFT) {
        if (w->selection_anchor < 0 || w->selection_anchor >= w->nitems || current < 0)
            w->selection_anchor = current < 0 ? target : current;
        int low = target < w->selection_anchor ? target : w->selection_anchor;
        int high = target > w->selection_anchor ? target : w->selection_anchor;
        for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = i >= low && i <= high;
    } else {
        for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = i == target;
        w->selection_anchor = target;
    }
    w->selection_cursor = target;
    s->desk_sel = -1;
    s->click_valid = s->drag = 0;
    s->press_item = -1;
    reveal(w, &lay, target, columns);
    return 1;
}
