#include "folder_navigation.h"
#include "folder_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures, overlay;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
/* Overlay/menu ownership is stubbed; geometry and scroll limits are real. */
int appearance_panel_active(void) { return 0; }
int appearance_panel_can_open(void) { return 0; }
int desktop_command_enabled(Session *s, int command) { (void)s; (void)command; return 0; }
int session_overlay_active(const Session *s) { return s->dialog || s->menu >= 0 || overlay; }
static int selected(Window *w) {
    int found = -1;
    for (int i = 0; i < w->nitems; i++) if (w->items[i].selected) {
        CHECK(found == -1); found = i;
    }
    return found;
}
static void visible(Session *s) {
    Window *w = &s->win[s->nwin - 1];
    int index = selected(w), x, y;
    CHECK(!ospi_item_center(s, s->nwin - 1, index, &x, &y));
    CHECK(item_index_at(w, x, y) == index);
    CHECK(w->scroll >= 0 && w->scroll <= window_max_scroll(w));
    Layout lay; window_layout(w, &lay);
    CHECK(y >= lay.content.y + (w->view_mode == VIEW_LIST ? FOLDER_LIST_HEADER : 0));
    CHECK(y < lay.content.y + lay.content.h);
}
int main(void) {
    Session *s = calloc(1, sizeof *s);
    CHECK(s != NULL); if (!s) return 1;
    s->nwin = 2; s->menu = -1; s->desk_sel = -1;
    Window *w = &s->win[1];
    w->kind = WIN_FOLDER; w->w = CELL_W * 3 + 2 + SB;
    w->h = 220; w->x = 20; w->y = 30; w->nitems = 14;
    for (int i = 0; i < w->nitems; i++) snprintf(w->items[i].name, sizeof w->items[i].name, "file%d", i);
    strcpy(w->items[2].name, "Apple"); strcpy(w->items[8].name, "apricot");
    CHECK(folder_navigation_key(s, MENU_KEY_DOWN, 0)); CHECK(selected(w) == 0);
    CHECK(folder_navigation_key(s, MENU_KEY_DOWN, 0)); CHECK(selected(w) == 3);
    CHECK(folder_navigation_key(s, MENU_KEY_RIGHT, 0)); CHECK(selected(w) == 4);
    CHECK(folder_navigation_key(s, MENU_KEY_UP, 0)); CHECK(selected(w) == 1);
    CHECK(folder_navigation_key(s, MENU_KEY_LEFT, 0)); CHECK(selected(w) == 0);
    CHECK(folder_navigation_key(s, MENU_KEY_UP, 0)); CHECK(selected(w) == 0);
    CHECK(folder_navigation_key(s, FOLDER_KEY_END, 0)); CHECK(selected(w) == 13); visible(s);
    CHECK(folder_navigation_key(s, MENU_KEY_DOWN, 0)); CHECK(selected(w) == 13);
    CHECK(folder_navigation_key(s, FOLDER_KEY_HOME, 0)); CHECK(selected(w) == 0 && !w->scroll);
    CHECK(folder_navigation_key(s, 'A', 0)); CHECK(selected(w) == 2);
    CHECK(folder_navigation_key(s, 'a', 0)); CHECK(selected(w) == 8); visible(s);
    CHECK(folder_navigation_key(s, 'a', 0)); CHECK(selected(w) == 2);
    int scroll = w->scroll;
    CHECK(folder_navigation_key(s, 'z', 0)); CHECK(selected(w) == 2 && w->scroll == scroll);
    CHECK(!folder_navigation_key(s, 'a', MOD_CMD)); CHECK(selected(w) == 2);
    CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 2)); CHECK(selected(w) == 2);
    CHECK(!folder_navigation_key(s, '\r', 0)); CHECK(!folder_navigation_key(s, 27, 0));
    w->view_mode = VIEW_LIST;
    CHECK(folder_navigation_key(s, MENU_KEY_DOWN, 0)); CHECK(selected(w) == 3);
    CHECK(folder_navigation_key(s, FOLDER_KEY_END, 0)); CHECK(selected(w) == 13); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_HOME, 0)); CHECK(!w->scroll); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 5); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 10); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 13); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_UP, 0)); CHECK(selected(w) == 8); visible(s);
    w->view_mode = VIEW_ICONS;
    folder_navigation_key(s, FOLDER_KEY_HOME, 0);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 6); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 12); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0)); CHECK(selected(w) == 13); visible(s);
    CHECK(folder_navigation_key(s, FOLDER_KEY_PAGE_UP, 0)); CHECK(selected(w) == 7); visible(s);
    CHECK(!folder_navigation_key(s, FOLDER_KEY_PAGE_UP, MOD_OTHER)); CHECK(selected(w) == 7);
    s->desk_sel = 0; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); s->desk_sel = -1;
    overlay = 1; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); overlay = 0;
    s->dialog = 1; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); s->dialog = 0;
    s->menu = 0; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); s->menu = -1;
    s->down = 1; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); s->down = 0;
    w->collapsed = 1; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); w->collapsed = 0;
    w->kind = WIN_TEXT; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0)); w->kind = WIN_FOLDER;
    w->nitems = 0; CHECK(folder_navigation_key(s, MENU_KEY_DOWN, 0));
    w->nitems = OSPI_MAX_ITEMS + 1; CHECK(!folder_navigation_key(s, MENU_KEY_DOWN, 0));
    CHECK(!folder_navigation_key(NULL, MENU_KEY_DOWN, 0));
    CHECK(!s->win[0].items[0].selected);
    w->kind = WIN_TEXT; w->nitems = 0;
    char text[4096]; memset(text, 'w', sizeof text - 1); text[sizeof text - 1] = 0;
    memcpy(w->text, text, sizeof text); w->scroll = 0;
    CHECK(text_navigation_key(s, MENU_KEY_DOWN, 0)); CHECK(w->scroll == typeface_line_height(1));
    CHECK(text_navigation_key(s, MENU_KEY_UP, 0)); CHECK(w->scroll == 0);
    Layout lay; window_layout(w, &lay);
    CHECK(text_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0));
    CHECK(w->scroll == lay.content.h - typeface_line_height(1));
    CHECK(text_navigation_key(s, FOLDER_KEY_PAGE_UP, 0)); CHECK(w->scroll == 0);
    CHECK(text_navigation_key(s, FOLDER_KEY_END, 0)); CHECK(w->scroll == window_max_scroll(w));
    int oldmax = w->scroll; w->w /= 2;
    CHECK(text_navigation_key(s, FOLDER_KEY_END, 0)); CHECK(w->scroll > oldmax);
    for (int i = 0; i < 20; i++) text_navigation_key(s, FOLDER_KEY_PAGE_DOWN, 0);
    CHECK(w->scroll == window_max_scroll(w));
    CHECK(!text_navigation_key(s, FOLDER_KEY_HOME, MOD_OTHER)); CHECK(w->scroll > 0);
    s->desk_sel = DESK_BACKGROUND; CHECK(!text_navigation_key(s, FOLDER_KEY_HOME, 0));
    s->desk_sel = DESK_WINDOW; overlay = 1; CHECK(!text_navigation_key(s, FOLDER_KEY_HOME, 0)); overlay = 0;
    w->collapsed = 1; CHECK(!text_navigation_key(s, FOLDER_KEY_HOME, 0)); w->collapsed = 0;
    CHECK(text_navigation_key(s, FOLDER_KEY_HOME, 0)); CHECK(w->scroll == 0);
    w->text[0] = 0; CHECK(text_navigation_key(s, FOLDER_KEY_END, 0)); CHECK(w->scroll == 0);
    free(s);
    printf("folder navigation: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
