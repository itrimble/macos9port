#include "folder_navigation.h"
#include "folder_view.h"

static int lower_ascii(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static void reveal(Window *w, const Layout *lay, int index, int columns) {
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
    if (!s || mods || s->desk_sel != DESK_WINDOW || s->nwin <= 0 ||
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
    if (!s || mods || s->desk_sel != DESK_WINDOW || s->nwin <= 0 || s->nwin > OSPI_MAX_WIN ||
        s->down || session_overlay_active(s)) return 0;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind != WIN_FOLDER || w->collapsed || w->nitems < 0 ||
        w->nitems > OSPI_MAX_ITEMS) return 0;
    int arrow = key == MENU_KEY_LEFT || key == MENU_KEY_RIGHT ||
                key == MENU_KEY_UP || key == MENU_KEY_DOWN;
    int initial = (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') ||
                  (key >= '0' && key <= '9');
    int page_key = key == FOLDER_KEY_PAGE_UP || key == FOLDER_KEY_PAGE_DOWN;
    if (!arrow && !initial && !page_key && key != FOLDER_KEY_HOME && key != FOLDER_KEY_END) return 0;
    if (!w->nitems) return 1;
    Layout lay;
    window_layout(w, &lay);
    int columns = w->view_mode == VIEW_LIST ? 1 : lay.content.w / CELL_W;
    if (columns < 1) columns = 1;
    int current = -1;
    for (int i = 0; i < w->nitems; i++) if (w->items[i].selected) { current = i; break; }
    int target = current;
    if (initial) {
        target = -1;
        for (int step = 1; step <= w->nitems; step++) {
            int index = (current + step) % w->nitems;
            if (lower_ascii((unsigned char)w->items[index].name[0]) == lower_ascii(key)) {
                target = index;
                break;
            }
        }
        if (target < 0) return 1;
    } else if (key == FOLDER_KEY_HOME) target = 0;
    else if (key == FOLDER_KEY_END) target = w->nitems - 1;
    else if (current < 0) target = 0;
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
    for (int i = 0; i < w->nitems; i++) w->items[i].selected = i == target;
    s->desk_sel = -1;
    s->click_valid = s->drag = 0;
    s->press_item = -1;
    reveal(w, &lay, target, columns);
    return 1;
}
