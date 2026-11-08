#include "ospi.h"

int menu_activate(Session *s, int menu, int row) {
    const MenuItem *item = menu_at(menu, row);
    if (!item || !item->label || (item->flags & MI_DISABLED)) return 0;
    int child = menu_submenu(item->cmd);
    if (child) {
        s->menu_child = child; s->menu_child_hot = -1; s->menu_hot = row;
        return 1;
    }
    int command = item->cmd;
    s->menu = s->menu_hot = -1; s->menu_child = 0; s->menu_child_hot = -1;
    s->down = 0;
    if (command) session_command(s, command);
    return 1;
}
void menu_hover(Session *s, int x, int y) {
    if (s->menu_child) {
        int row = menu_item_at(s->menu_child, x, y);
        if (row >= 0) { s->menu_child_hot = row; return; }
    }
    int title = menu_title_at(x, y);
    if (title >= 0 && title != s->menu) {
        s->menu = title; s->menu_child = 0; s->menu_hot = -1;
    }
    int row = menu_item_at(s->menu, x, y);
    if (row < 0) { s->menu_child_hot = -1; return; }
    s->menu_hot = row;
    const MenuItem *item = menu_at(s->menu, row);
    s->menu_child = item && !(item->flags & MI_DISABLED) ? menu_submenu(item->cmd) : 0;
    s->menu_child_hot = -1;
}
