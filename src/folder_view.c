#include "folder_view.h"
#include "desktop_theme.h"
#include "finder_settings.h"
#include "finder_labels.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <strings.h>

static int compare_name(const void *a, const void *b) {
    const Item *left = a, *right = b;
    int order = strcasecmp(left->name, right->name);
    return order ? order : strcmp(left->name, right->name);
}

static int kind_rank(int kind) {
    switch (kind) {
    case ITEM_SYSTEM: return 0;
    case ITEM_FOLDER: return 1;
    case ITEM_APPLICATION: return 2;
    default: return 3;
    }
}

static int compare_kind(const void *a, const void *b) {
    const Item *left = a, *right = b;
    int order = kind_rank(left->kind) - kind_rank(right->kind);
    return order ? order : compare_name(a, b);
}

static int compare_date(const void *a, const void *b) {
    const Item *l = a, *r = b;
    return l->modified == r->modified ? compare_name(a, b) : l->modified > r->modified ? -1 : 1;
}
static int compare_size(const void *a, const void *b) {
    const Item *l = a, *r = b;
    return l->data_size == r->data_size ? compare_name(a, b) : l->data_size > r->data_size ? -1 : 1;
}
static int compare_created(const void *a, const void *b) {
    const Item *l = a, *r = b;
    if (!l->created != !r->created) return l->created ? -1 : 1;
    return l->created == r->created ? compare_name(a, b) : l->created > r->created ? -1 : 1;
}
static int compare_label(const void *a, const void *b) {
    const Item *l = a, *r = b;
    return l->label == r->label ? compare_name(a, b) : l->label < r->label ? -1 : 1;
}
void folder_view_sort(Window *w) {
    if (!w || w->kind != WIN_FOLDER || w->nitems < 0 || w->nitems > window_item_capacity(w)) return;
    w->selection_anchor = w->selection_cursor = -1;
    qsort(window_items(w), (size_t)w->nitems, sizeof window_items(w)[0],
          w->sort_mode == SORT_KIND ? compare_kind : w->sort_mode == SORT_DATE ? compare_date :
          w->sort_mode == SORT_SIZE ? compare_size : w->sort_mode == SORT_CREATED ? compare_created :
          w->sort_mode == SORT_LABEL ? compare_label : compare_name);
    if (w->sort_reverse) for (int i = 0; i < w->nitems / 2; i++) {
        Item item = window_items(w)[i]; window_items(w)[i] = window_items(w)[w->nitems - i - 1];
        window_items(w)[w->nitems - i - 1] = item;
    }
}

int folder_view_command(Session *s, int command) {
    if (command != CMD_VIEW_ICONS && command != CMD_VIEW_LIST &&
        command != CMD_SORT_NAME && command != CMD_SORT_KIND && command != CMD_SORT_DATE &&
        command != CMD_SORT_SIZE && command != CMD_SORT_CREATED && command != CMD_SORT_LABEL && command != CMD_VIEW_BUTTONS) return 0;
    Window *w = finder_current(s);
    if (!s || !w) return 1;
    s->menu = -1;
    if (w->desktop_surface && (command == CMD_VIEW_LIST || command == CMD_VIEW_BUTTONS)) return 1;
    if (w->kind != WIN_FOLDER) return 1;
    if (command == CMD_VIEW_ICONS || command == CMD_VIEW_LIST || command == CMD_VIEW_BUTTONS)
        w->view_mode = command == CMD_VIEW_LIST ? VIEW_LIST : command == CMD_VIEW_BUTTONS ? VIEW_BUTTONS : VIEW_ICONS;
    else {
        w->sort_reverse = 0;
        w->sort_mode = command == CMD_SORT_KIND ? SORT_KIND : command == CMD_SORT_DATE ? SORT_DATE :
                       command == CMD_SORT_SIZE ? SORT_SIZE : command == CMD_SORT_CREATED ? SORT_CREATED :
                       command == CMD_SORT_LABEL ? SORT_LABEL : SORT_NAME;
        folder_view_sort(w);
    }
    w->scroll = 0;
    s->down = s->drag = s->click_valid = 0;
    s->press_item = -1;
    return 1;
}

void folder_view_columns(const Window *w, int positions[5]) {
    positions[0] = 0; positions[4] = 1000;
    int valid = w->list_columns[0] >= 200 && w->list_columns[0] <= 700 &&
        w->list_columns[1] >= 300 && w->list_columns[1] <= 800 &&
        w->list_columns[2] >= 400 && w->list_columns[2] <= 900 &&
        w->list_columns[1] - w->list_columns[0] >= 100 &&
        w->list_columns[2] - w->list_columns[1] >= 100;
    const int defaults[] = {400, 600, 750};
    for (int i = 0; i < 3; i++) positions[i + 1] = valid ? w->list_columns[i] : defaults[i];
}
void folder_view_column_move(Session *s, int x) {
    if (s->press_win < 0 || s->press_win >= s->nwin || s->press_item < 1 || s->press_item > 3) return;
    Window *w = &s->win[s->press_win]; Layout lay; window_layout(w, &lay);
    if (lay.content.w <= 0) return;
    int positions[5]; folder_view_columns(w, positions);
    int divider = s->press_item;
    int value = (int)((int64_t)(x - lay.content.x) * 1000 / lay.content.w);
    int low = positions[divider - 1] + (divider == 1 ? 200 : 100);
    int high = positions[divider + 1] - 100;
    positions[divider] = value < low ? low : value > high ? high : value;
    for (int i = 0; i < 3; i++) w->list_columns[i] = positions[i + 1];
    s->drag = 6;
}
int folder_view_header_click(Session *s, int x, int y) {
    if (!s || s->nwin <= 0 || s->nwin > OSPI_MAX_WIN) return 0;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind != WIN_FOLDER || w->collapsed || w->view_mode != VIEW_LIST) return 0;
    Layout lay; window_layout(w, &lay);
    if (!rect_has(lay.content, x, y) || y >= lay.content.y + FOLDER_LIST_HEADER) return 0;
    int positions[5]; folder_view_columns(w, positions);
    for (int i = 1; i < 4; i++) {
        int split = lay.content.x + lay.content.w * positions[i] / 1000;
        if (abs(x - split) <= 3) {
            s->press_zone = Z_COLUMN; s->press_item = i;
            s->press_win = s->nwin - 1; return 1;
        }
    }
    int column = 0;
    while (column < 3 && x >= lay.content.x + lay.content.w * positions[column + 1] / 1000) column++;
    const int modes[] = {SORT_NAME, SORT_KIND, SORT_SIZE, SORT_DATE};
    w->sort_reverse = w->sort_mode == modes[column] ? !w->sort_reverse : 0;
    w->sort_mode = modes[column]; folder_view_sort(w); w->scroll = 0;
    s->down = s->drag = s->click_valid = 0;
    return 1;
}

static const char *kind_name(int kind) {
    switch (kind) {
    case ITEM_SYSTEM: return "System Folder";
    case ITEM_FOLDER: return "Folder";
    case ITEM_APPLICATION: return "Application";
    case ITEM_ALIAS: return "Alias";
    default: return "File";
    }
}

static uint32_t selection_ink(uint32_t color) {
    unsigned luma = ((color >> 16) & 255u) * 299u +
                    ((color >> 8) & 255u) * 587u + (color & 255u) * 114u;
    return luma >= 150000u ? C_BLACK : C_WHITE;
}

static void small_icon(Fb *fb, int x, int y, const Item *item, const char *parent,
                       FolderItemPainter paint_icon, uint32_t background) {
    if (!paint_icon) return;
    uint32_t pixels[32 * 32];
    for (int i = 0; i < 32 * 32; i++) pixels[i] = background;
    Fb icon;
    fb_init(&icon, pixels, 32, 32);
    paint_icon(&icon, 0, 0, item, parent);
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 16; col++)
            fb_fill(fb, x + col, y + row, 1, 1, pixels[row * 2 * 32 + col * 2]);
}

void folder_view_draw_list(Fb *fb, const Window *w, const Layout *lay,
                           FolderItemPainter paint_icon) {
    if (!fb || !w || !lay || w->nitems < 0 || w->nitems > window_item_capacity(w)) return;
    Rect body = lay->content;
    int positions[5]; folder_view_columns(w, positions);
    for (int i = 0; i < 5; i++) positions[i] = body.x + body.w * positions[i] / 1000;
    int rows_y = body.y + FOLDER_LIST_HEADER;
    uint32_t foreground = desktop_theme_color(THEME_FOREGROUND);
    uint32_t panel = desktop_theme_color(THEME_PANEL);
    fb_clip(fb, body.x, rows_y, body.w, body.h > FOLDER_LIST_HEADER ? body.h - FOLDER_LIST_HEADER : 0);
    for (int i = 0; i < w->nitems; i++) {
        int y = rows_y + i * FOLDER_LIST_ROW - w->scroll;
        if (y + FOLDER_LIST_ROW <= rows_y || y >= body.y + body.h) continue;
        const Item *item = &window_items(w)[i];
        uint32_t background = item->selected ? desktop_theme_color(THEME_ACCENT) : item->label > 0 ? finder_label_color(item->label) : panel;
        uint32_t ink = item->selected ? selection_ink(background) : item->label > 0 ? finder_label_ink(item->label) : foreground;
        fb_fill(fb, body.x, y, body.w, FOLDER_LIST_ROW, background);
        small_icon(fb, body.x + 5, y + 4, item, w->rel, paint_icon, background);
        int top = y < rows_y ? rows_y : y;
        int bottom = y + FOLDER_LIST_ROW < body.y + body.h ? y + FOLDER_LIST_ROW : body.y + body.h;
        char size[32], date[32] = "--";
        if (item->kind == ITEM_FOLDER || item->kind == ITEM_SYSTEM) snprintf(size, sizeof size, "--");
        else if (item->data_size < 1024) snprintf(size, sizeof size, "%llu B", (unsigned long long)item->data_size);
        else if (item->data_size < 1024 * 1024) snprintf(size, sizeof size, "%.1f KB", item->data_size / 1024.0);
        else if (item->data_size < 1024ull * 1024 * 1024) snprintf(size, sizeof size, "%.1f MB", item->data_size / (1024.0 * 1024));
        else snprintf(size, sizeof size, "%.1f GB", item->data_size / (1024.0 * 1024 * 1024));
        time_t timestamp = (time_t)item->modified; struct tm tm;
        if (localtime_r(&timestamp, &tm)) strftime(date, sizeof date, "%Y-%m-%d", &tm);
        const char *values[] = {item->name, kind_name(item->kind), size, date};
        for (int column = 0; column < 4; column++) {
            int left = positions[column] + (column ? 6 : 26);
            int width = positions[column + 1] - left - 4;
            fb_clip(fb, left, top, width > 0 ? width : 0, bottom - top);
            fb_text(fb, left, y + 5, ink, values[column]);
        }
        fb_clip(fb, body.x, rows_y, body.w, body.h > FOLDER_LIST_HEADER ? body.h - FOLDER_LIST_HEADER : 0);
    }
    fb_clip(fb, body.x, body.y, body.w, body.h);
    fb_fill(fb, body.x, body.y, body.w, FOLDER_LIST_HEADER, desktop_theme_color(THEME_MENU));
    fb_hline(fb, body.x, rows_y - 1, body.w, foreground);
    const char *titles[] = {"Name", "Kind", "Size", "Date Modified"};
    const int modes[] = {SORT_NAME, SORT_KIND, SORT_SIZE, SORT_DATE};
    for (int column = 0; column < 4; column++) {
        int left = positions[column], width = positions[column + 1] - left;
        fb_clip(fb, left, body.y, width, FOLDER_LIST_HEADER);
        if (column) fb_fill(fb, left, body.y, 1, FOLDER_LIST_HEADER, foreground);
        if (w->sort_mode == modes[column]) {
            int descending = (w->sort_mode == SORT_DATE || w->sort_mode == SORT_SIZE) ^ !!w->sort_reverse;
            fb_text(fb, left + 3, body.y + 5, foreground, descending ? "v" : "^");
        }
        fb_text(fb, left + (column ? 18 : 26), body.y + 5, foreground, titles[column]);
    }
    fb_clip(fb, body.x, rows_y, body.w, body.h > FOLDER_LIST_HEADER ? body.h - FOLDER_LIST_HEADER : 0);
    if (w->nitems == 0)
        fb_text(fb, body.x + 12, rows_y + 8, desktop_theme_color(THEME_DISABLED), "This folder is empty.");
    /* Restore the caller's content clip for its subsequent window drawing. */
    fb_clip(fb, body.x, body.y, body.w, body.h);
}
