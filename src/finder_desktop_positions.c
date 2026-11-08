#include "finder_desktop_positions.h"
#include "finder_icon_position.h"
#include "session_int.h"
#include <stdlib.h>
#include <string.h>

static int bounded(int value, int maximum) {
    if (maximum < 0) maximum = 0;
    return value < 0 ? 0 : value > maximum ? maximum : value;
}
static int save(Session *s, Item *item, int x, int y) {
    Window *w = &s->desktop;
    x = bounded(x, w->w - CELL_W); y = bounded(y, w->h - CELL_H);
    if (finder_icon_position_write(s->root, w->rel, item->name, x, y)) return -1;
    item->desktop_x = x; item->desktop_y = y; item->desktop_position = 1;
    w->folder_stamp_valid = 0;
    return 0;
}
void finder_desktop_positions_load(Session *s) {
    Window *w = &s->desktop;
    int cols = w->w / CELL_W, rows = w->h / CELL_H;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 1024 || rows > 1024) return;
    int slots = cols * rows;
    unsigned char *used = calloc((size_t)slots, 1);
    for (int i = 0; i < w->nitems; i++) {
        Item *item = &window_items(w)[i];
        item->desktop_position = finder_icon_position_read(s->root, w->rel, item->name,
            &item->desktop_x, &item->desktop_y) == 1;
        if (!used || !item->desktop_position) continue;
        int x = bounded(item->desktop_x, w->w - CELL_W), y = bounded(item->desktop_y, w->h - CELL_H);
        int left = x / CELL_W, right = (x + CELL_W - 1) / CELL_W;
        int top = y / CELL_H, bottom = (y + CELL_H - 1) / CELL_H;
        for (int r = top; r <= bottom && r < rows; r++)
            for (int c = left; c <= right && c < cols; c++) used[r * cols + c] = 1;
    }
    if (used) for (int i = 0, next = 0; i < w->nitems; i++) {
        Item *item = &window_items(w)[i];
        if (item->desktop_position) continue;
        while (next < slots && used[next]) next++;
        item->desktop_x = next % cols * CELL_W; item->desktop_y = next / cols * CELL_H;
        item->desktop_position = 2; /* Automatic position, not a saved preference. */
        if (next < slots) used[next] = 1;
        next++;
    }
    free(used);
}
void finder_desktop_positions_clean(Session *s) {
    Window *w = &s->desktop; int cols = w->w / CELL_W, failed = 0;
    if (cols < 1) cols = 1;
    for (int i = 0; i < w->nitems; i++) {
        Item *item = &window_items(w)[i];
        if ((i / cols + 1) * CELL_H > w->h) { failed++; continue; }
        failed += save(s, item, i % cols * CELL_W, i / cols * CELL_H) != 0;
    }
    if (failed) dialog_info(s, "Clean Up", "Some icons did not fit or their positions could not be saved. Those icons kept their previous positions.");
}
void finder_desktop_positions_move(Session *s, int dx, int dy) {
    Window *w = &s->desktop;
    int left = w->w, top = w->h, right = 0, bottom = 0, selected = 0;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) {
        Rect r = desktop_item_cell(w, i); r.x -= w->x; r.y -= w->y;
        if (r.x < left) left = r.x;
        if (r.y < top) top = r.y;
        if (r.x + r.w > right) right = r.x + r.w;
        if (r.y + r.h > bottom) bottom = r.y + r.h;
        selected++;
    }
    if (!selected) return;
    if (dx < -left) dx = -left;
    if (dx > w->w - right) dx = w->w - right;
    if (dy < -top) dy = -top;
    if (dy > w->h - bottom) dy = w->h - bottom;
    int failed = 0;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) {
        Rect r = desktop_item_cell(w, i);
        failed += save(s, &window_items(w)[i], r.x - w->x + dx, r.y - w->y + dy) != 0;
    }
    if (failed) dialog_info(s, "Move Icons", "Some icon positions could not be saved. Those icons kept their previous positions; files were not moved.");
}
int finder_desktop_position_item(Session *s, const char *name, int x, int y) {
    Window *w = &s->desktop;
    for (int i = 0; i < w->nitems; i++) if (!strcmp(window_items(w)[i].name, name))
        return save(s, &window_items(w)[i], x - w->x - CELL_W / 2, y - w->y - 24);
    return -1;
}
