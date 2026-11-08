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
            int cmd = selectable(s->menu_child, s->menu_child_hot) ? menu_at(s->menu_child, s->menu_child_hot)->cmd : CMD_NONE;
            dismiss(s); if (cmd) session_command(s, cmd); return 1;
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
        if (count == 8) {
            static const int order[] = {0, 1, 2, 3, 4, 6, 5, 7};
            for (int i = 0; i < count; i++) if (order[i] == s->menu) {
                s->menu = order[(i + (key == MENU_KEY_RIGHT ? 1 : count - 1)) % count]; break;
            }
        } else s->menu = key == MENU_KEY_RIGHT ? (s->menu + 1) % count :
                                        (s->menu + count - 1) % count;
        s->menu_hot = advance(s->menu, -1, 1);
    } else if (key == '\r' || key == '\n') {
        int command = CMD_NONE;
        if (selectable(s->menu, s->menu_hot)) command = menu_at(s->menu, s->menu_hot)->cmd;
        dismiss(s);
        if (command != CMD_NONE) session_command(s, command);
    }
    return 1;
}
