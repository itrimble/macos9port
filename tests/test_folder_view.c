#include "folder_view.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int checks, failures, icon_calls;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void test_icon(Fb *fb, int x, int y, const Item *item, const char *parent) {
    (void)item; (void)parent;
    icon_calls++;
    fb_fill(fb, x, y, 32, 32, 0xFFCC1122u);
}

int main(void) {
    Session *s = calloc(1, sizeof *s);
    CHECK(s != NULL);
    if (!s) return 1;
    s->nwin = 2; s->desk_sel = -1;
    Window *w = &s->win[1];
    *w = (Window){.kind=WIN_FOLDER, .x=30, .y=40, .w=400, .h=240, .nitems=4};
    const char *names[] = {"zeta.txt", "Alpha", "app", "System"};
    const int kinds[] = {ITEM_FILE, ITEM_FOLDER, ITEM_APPLICATION, ITEM_SYSTEM};
    for (int i = 0; i < 4; i++) {
        snprintf(w->items[i].name, sizeof w->items[i].name, "%s", names[i]);
        w->items[i].kind = kinds[i];
    }
    w->items[0].selected = w->items[0].has_guest_icon = 1;
    memset(w->items[0].guest_icon, 25, sizeof w->items[0].guest_icon);
    memset(w->items[0].guest_icon_mask, 0xAA, sizeof w->items[0].guest_icon_mask);
    Item original = w->items[0];
    CHECK(w->view_mode == VIEW_ICONS && w->sort_mode == SORT_NAME);
    CHECK(folder_view_command(s, CMD_SORT_NAME));
    CHECK(!strcmp(w->items[0].name, "Alpha") && !strcmp(w->items[3].name, "zeta.txt"));
    CHECK(!memcmp(&w->items[3], &original, sizeof original));
    CHECK(folder_view_command(s, CMD_SORT_KIND));
    CHECK(w->items[0].kind == ITEM_SYSTEM && w->items[1].kind == ITEM_FOLDER);
    CHECK(w->items[2].kind == ITEM_APPLICATION && w->items[3].kind == ITEM_FILE);
    CHECK(!memcmp(&w->items[3], &original, sizeof original));
    s->down = s->drag = s->click_valid = 1;
    w->scroll = 100;
    CHECK(folder_view_command(s, CMD_VIEW_LIST));
    CHECK(w->view_mode == VIEW_LIST && !w->scroll && !s->down && !s->drag && !s->click_valid);
    CHECK(s->win[0].view_mode == VIEW_ICONS); /* Per-window state. */
    CHECK(!folder_view_command(s, 9999));
    Layout lay;
    window_layout(w, &lay);
    int rows_y = lay.content.y + FOLDER_LIST_HEADER;
    CHECK(item_index_at(w, lay.content.x + 10, lay.content.y + 12) == -1);
    CHECK(item_index_at(w, lay.content.x + 10, rows_y + 12) == 0);
    CHECK(item_index_at(w, lay.content.x + 10, rows_y + FOLDER_LIST_ROW + 12) == 1);
    CHECK(item_index_at(w, lay.content.x + 10, rows_y + 4 * FOLDER_LIST_ROW) == -1);
    CHECK(item_index_at(w, lay.content.x - 1, rows_y + 12) == -1);
    int x, y;
    CHECK(!ospi_item_center(s, 1, 2, &x, &y));
    CHECK(item_index_at(w, x, y) == 2);
    w->nitems = OSPI_MAX_ITEMS;
    CHECK(window_max_scroll(w) == FOLDER_LIST_HEADER + OSPI_MAX_ITEMS * FOLDER_LIST_ROW - lay.content.h);
    w->scroll = FOLDER_LIST_ROW + 5;
    CHECK(item_index_at(w, lay.content.x + 10, rows_y) == 1);
    CHECK(item_index_at(w, lay.content.x + 10, rows_y - 1) == -1);
    w->collapsed = 1;
    CHECK(window_max_scroll(w) == 0 && item_index_at(w, x, y) == -1);
    w->collapsed = 0;
    w->nitems = 4;
    w->scroll = 0;
    desktop_theme_init(NULL);
    uint32_t *pixels = calloc(800 * 600, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Fb fb;
        fb_init(&fb, pixels, 800, 600);
        fb_clip(&fb, lay.content.x, lay.content.y, lay.content.w, lay.content.h);
        folder_view_draw_list(&fb, w, &lay, test_icon);
        CHECK(icon_calls == 4);
        CHECK(pixels[(size_t)(rows_y + 4) * 800 + lay.content.x + 5] == 0xFFCC1122u);
        CHECK(pixels[(size_t)lay.content.y * 800 + lay.content.x + 2] == desktop_theme_color(THEME_MENU));
        CHECK(pixels[(size_t)lay.content.y * 800 + lay.content.x - 1] == 0);
        CHECK(pixels[(size_t)(lay.content.y - 1) * 800 + lay.content.x + 5] == 0);
        w->scroll = 5;
        folder_view_draw_list(&fb, w, &lay, test_icon);
        CHECK(pixels[(size_t)(rows_y - 1) * 800 + lay.content.x + 5] == desktop_theme_color(THEME_FOREGROUND));
        CHECK(pixels[(size_t)lay.content.y * 800 + lay.content.x + 2] == desktop_theme_color(THEME_MENU));
        free(pixels);
    }
    int positions[5]; folder_view_columns(w,positions);
    CHECK(positions[0]==0 && positions[4]==1000);
    w->items[0].data_size=1;w->items[1].data_size=9000;w->items[2].data_size=20;w->items[3].data_size=500;
    w->items[0].modified=100;w->items[1].modified=200;w->items[2].modified=300;w->items[3].modified=400;
    int header_y=lay.content.y+12;
    CHECK(folder_view_header_click(s,lay.content.x+lay.content.w*675/1000,header_y));
    CHECK(w->sort_mode==SORT_SIZE && !w->sort_reverse && w->items[0].data_size==9000);
    CHECK(folder_view_header_click(s,lay.content.x+lay.content.w*675/1000,header_y));
    CHECK(w->sort_reverse && w->items[0].data_size==1);
    CHECK(folder_view_header_click(s,lay.content.x+lay.content.w*875/1000,header_y));
    CHECK(w->sort_mode==SORT_DATE && !w->sort_reverse && w->items[0].modified==400);
    s->down=1;
    CHECK(folder_view_header_click(s,lay.content.x+lay.content.w*400/1000,header_y));
    CHECK(s->press_zone==Z_COLUMN && s->down && w->sort_mode==SORT_DATE);
    folder_view_column_move(s,lay.content.x+lay.content.w*480/1000);
    CHECK(w->list_columns[0]>=475 && w->list_columns[0]<=480 && w->sort_mode==SORT_DATE);
    folder_view_column_move(s,lay.content.x-10000);CHECK(w->list_columns[0]==200);
    folder_view_column_move(s,lay.content.x+10000);CHECK(w->list_columns[0]==500);
    session_command(s,CMD_RESET_COLUMNS);folder_view_columns(w,positions);
    CHECK(positions[1]==400 && w->sort_mode==SORT_DATE);
    CHECK(folder_view_command(s, CMD_VIEW_ICONS));
    CHECK(w->view_mode == VIEW_ICONS && !w->scroll);
    CHECK(!ospi_item_center(s, 1, 0, &x, &y));
    CHECK(item_index_at(w, x, y) == 0);
    w->kind = WIN_TEXT;
    CHECK(folder_view_command(s, CMD_VIEW_LIST));
    CHECK(w->view_mode == VIEW_ICONS);
    w->kind = WIN_FOLDER;
    const int64_t created[] = {0, 100, 400, 400};
    for (int i=0;i<4;i++) { w->items[i].created=created[i]; w->items[i].label=i==0?7:i==1?0:2; }
    CHECK(folder_view_command(s,CMD_SORT_CREATED));
    CHECK(w->sort_mode==SORT_CREATED && w->items[0].created==400 && w->items[3].created==0);
    CHECK(strcasecmp(w->items[0].name,w->items[1].name)<0);
    CHECK(folder_view_command(s,CMD_SORT_LABEL));
    CHECK(w->sort_mode==SORT_LABEL && w->items[0].label==0 && w->items[3].label==7);
    CHECK(strcasecmp(w->items[1].name,w->items[2].name)<0);
    w->nitems = OSPI_MAX_ITEMS + 1;
    folder_view_sort(w); /* Invalid counts never overrun item storage. */
    free(s);
    printf("folder view: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
