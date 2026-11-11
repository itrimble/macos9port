#include "menu_navigation.h"

static void dismiss(Session *s) {
    s->menu = s->menu_hot = -1;
    s->menu_child = 0; s->menu_child_hot = -1;
    s->down = s->drag = s->click_valid = 0;
}

static int selectable(int menu, int row) {
    const MenuItem *item = menu_at(menu, row);
    return item && item->label && !(item->flags & MI_DISABLED);
}

static int advance(int menu, int current, int step) {
    int count = menu_nitems(menu);
    if (count <= 0) return -1;
    if (current < 0 || current >= count) current = step > 0 ? count - 1 : 0;
    for (int visited = 0; visited < count; visited++) {
        current = step > 0 ? (current + 1) % count : (current + count - 1) % count;
        if (selectable(menu, current)) return current;
    }
    return -1;
}

static void activate_row(Session *s, int menu, int row) {
    int command = menu_at(menu, row)->cmd;
    if (command && s->menu_blink_count > 0 && s->input_now_ms > 0) {
        s->menu_flash_menu = menu;
        s->menu_flash_row = row;
        s->menu_flash_start_ms = s->input_now_ms;
        s->menu_flash_end_ms = s->input_now_ms + (uint32_t)s->menu_blink_count * 90u;
    } else {
        s->menu_flash_end_ms = 0;
    }
    dismiss(s);
    if (command) session_command(s, command);
}

int menu_navigation_key(Session *s, int key, int mods) {
    if (!s || s->menu < 0) return 0;
    if (mods & MOD_CMD) { dismiss(s); return 0; }
    if (key == 27) {
        if (s->menu_child) { s->menu_child = 0; s->menu_child_hot = -1; }
        else dismiss(s);
        return 1;
    }
    int count = menu_count();
    if (s->menu >= count || count <= 0) { dismiss(s); return 1; }
    s->click_valid = 0;
    if (s->menu_child) {
        if (key == MENU_KEY_LEFT) { s->menu_child = 0; s->menu_child_hot = -1; return 1; }
        if (key == MENU_KEY_UP || key == MENU_KEY_DOWN) {
            s->menu_child_hot = advance(s->menu_child, s->menu_child_hot, key == MENU_KEY_DOWN ? 1 : -1);
            return 1;
        }
        if (key == '\r' || key == '\n') {
            if (selectable(s->menu_child, s->menu_child_hot))
                activate_row(s, s->menu_child, s->menu_child_hot);
            else dismiss(s);
            return 1;
        }
        return 1;
    }
    if ((key == MENU_KEY_RIGHT || key == '\r' || key == '\n') && selectable(s->menu, s->menu_hot)) {
        int child = menu_submenu(menu_at(s->menu, s->menu_hot)->cmd);
        if (child) { s->menu_child = child; s->menu_child_hot = advance(child, -1, 1); return 1; }
    }
    if (key == MENU_KEY_UP || key == MENU_KEY_DOWN) {
        s->menu_hot = advance(s->menu, s->menu_hot, key == MENU_KEY_DOWN ? 1 : -1);
    } else if (key == MENU_KEY_LEFT || key == MENU_KEY_RIGHT) {
        if (count == 9) {
            static const int order[] = {0, 1, 2, 3, 8, 4, 6, 5, 7};
            for (int i = 0; i < count; i++) if (order[i] == s->menu) {
                Rect title = {0};   /* skip titles that left a narrow menu bar */
                for (int n = 0; n < count && title.w <= 0; n++) {
                    i = (i + (key == MENU_KEY_RIGHT ? 1 : count - 1)) % count;
                    menu_title_rect(order[i], &title);
                }
                s->menu = order[i]; break;
            }
        } else s->menu = key == MENU_KEY_RIGHT ? (s->menu + 1) % count :
                                        (s->menu + count - 1) % count;
        s->menu_hot = advance(s->menu, -1, 1);
    } else if (key == '\r' || key == '\n') {
        if (selectable(s->menu, s->menu_hot)) activate_row(s, s->menu, s->menu_hot);
        else dismiss(s);
    }
    return 1;
}
