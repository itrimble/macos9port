#include "folder_view.h"
#include "desktop_theme.h"

#include <stdlib.h>
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

void folder_view_sort(Window *w) {
    if (!w || w->kind != WIN_FOLDER || w->nitems < 0 || w->nitems > OSPI_MAX_ITEMS) return;
    qsort(w->items, (size_t)w->nitems, sizeof w->items[0],
          w->sort_mode == SORT_KIND ? compare_kind : compare_name);
}

int folder_view_command(Session *s, int command) {
    if (command != CMD_VIEW_ICONS && command != CMD_VIEW_LIST &&
        command != CMD_SORT_NAME && command != CMD_SORT_KIND) return 0;
    if (!s) return 1;
    s->menu = -1;
    if (s->nwin <= 0 || s->nwin > OSPI_MAX_WIN) return 1;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind != WIN_FOLDER) return 1;
    if (command == CMD_VIEW_ICONS || command == CMD_VIEW_LIST)
        w->view_mode = command == CMD_VIEW_LIST ? VIEW_LIST : VIEW_ICONS;
    else {
        w->sort_mode = command == CMD_SORT_KIND ? SORT_KIND : SORT_NAME;
        folder_view_sort(w);
    }
    w->scroll = 0;
    s->down = s->drag = s->click_valid = 0;
    s->press_item = -1;
    return 1;
}

int folder_view_header_click(Session *s, int x, int y) {
    if (!s || s->nwin <= 0 || s->nwin > OSPI_MAX_WIN) return 0;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind != WIN_FOLDER || w->collapsed || w->view_mode != VIEW_LIST) return 0;
    Layout lay;
    window_layout(w, &lay);
    if (!rect_has(lay.content, x, y) || y >= lay.content.y + FOLDER_LIST_HEADER) return 0;
    int split = lay.content.x + lay.content.w * 2 / 3;
    folder_view_command(s, x < split ? CMD_SORT_NAME : CMD_SORT_KIND);
    return 1;
}

static const char *kind_name(int kind) {
    switch (kind) {
    case ITEM_SYSTEM: return "System Folder";
    case ITEM_FOLDER: return "Folder";
    case ITEM_APPLICATION: return "Application";
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
    if (!fb || !w || !lay || w->nitems < 0 || w->nitems > OSPI_MAX_ITEMS) return;
    Rect body = lay->content;
    int split = body.x + body.w * 2 / 3;
    int rows_y = body.y + FOLDER_LIST_HEADER;
    uint32_t foreground = desktop_theme_color(THEME_FOREGROUND);
    uint32_t panel = desktop_theme_color(THEME_PANEL);
    fb_clip(fb, body.x, rows_y, body.w, body.h > FOLDER_LIST_HEADER ? body.h - FOLDER_LIST_HEADER : 0);
    for (int i = 0; i < w->nitems; i++) {
        int y = rows_y + i * FOLDER_LIST_ROW - w->scroll;
        if (y + FOLDER_LIST_ROW <= rows_y || y >= body.y + body.h) continue;
        const Item *item = &w->items[i];
        uint32_t background = item->selected ? desktop_theme_color(THEME_ACCENT) : panel;
        uint32_t ink = item->selected ? selection_ink(background) : foreground;
        fb_fill(fb, body.x, y, body.w, FOLDER_LIST_ROW, background);
        small_icon(fb, body.x + 5, y + 4, item, w->rel, paint_icon, background);
        int top = y < rows_y ? rows_y : y;
        int bottom = y + FOLDER_LIST_ROW < body.y + body.h ? y + FOLDER_LIST_ROW : body.y + body.h;
        fb_clip(fb, body.x + 26, top, split - body.x - 30, bottom - top);
        fb_text(fb, body.x + 26, y + 5, ink, item->name);
        fb_clip(fb, split + 6, top, body.x + body.w - split - 10, bottom - top);
        fb_text(fb, split + 6, y + 5, ink, kind_name(item->kind));
        fb_clip(fb, body.x, rows_y, body.w, body.h > FOLDER_LIST_HEADER ? body.h - FOLDER_LIST_HEADER : 0);
    }
    fb_clip(fb, body.x, body.y, body.w, body.h);
    fb_fill(fb, body.x, body.y, body.w, FOLDER_LIST_HEADER, desktop_theme_color(THEME_MENU));
    fb_hline(fb, body.x, rows_y - 1, body.w, foreground);
    fb_fill(fb, split, body.y, 1, FOLDER_LIST_HEADER, foreground);
    int marker = w->sort_mode == SORT_KIND ? split + 3 : body.x + 3;
    fb_text(fb, marker, body.y + 5, foreground, ">");
    fb_text(fb, body.x + 26, body.y + 5, foreground, "Name");
    fb_text(fb, split + 18, body.y + 5, foreground, "Kind");
    if (w->nitems == 0)
        fb_text(fb, body.x + 12, rows_y + 8, desktop_theme_color(THEME_DISABLED), "This folder is empty.");
    /* Restore the caller's content clip for its subsequent window drawing. */
    fb_clip(fb, body.x, body.y, body.w, body.h);
}
