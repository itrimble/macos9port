#include "ospi.h"
#include "session_int.h"

#include <stdio.h>
#include <string.h>

void session_wheel(Session *s, int dy) {
    if (s->dialog || s->nwin <= 0) {
        return;
    }
    Window *w = &s->win[s->nwin - 1];
    if (w->collapsed) {
        return;
    }
    w->scroll -= dy * 18;
    session_clamp(s);
}

static void note_mouse(Session *s, int x, int y) {
    s->mx = x;
    s->my = y;
}

void session_down(Session *s, int x, int y, int clicks) {
    session_clamp(s);
    note_mouse(s, x, y);
    s->down = 1;
    s->press_x = x;
    s->press_y = y;
    s->press_clicks = clicks;
    s->drag = 0;
    s->press_zone = Z_NONE;
    s->press_win = -1;
    s->press_item = -1;

    if (s->dialog) {
        Rect box, cancel, ok;
        dialog_layout(s, &box, &cancel, &ok);
        (void)box;
        if (rect_has(ok, x, y)) {
            s->dialog_down = 1;
        } else if (s->dialog == 2 && rect_has(cancel, x, y)) {
            s->dialog_down = 2;
        } else {
            s->dialog_down = 0;
        }
        return;
    }

    if (s->menu >= 0) {
        int item = menu_item_at(s->menu, x, y);
        if (item >= 0) {
            const MenuItem *it = menu_at(s->menu, item);
            int run = it && it->label && !(it->flags & MI_DISABLED) ? it->cmd : CMD_NONE;
            s->menu = -1;
            s->down = 0;
            if (run) {
                session_command(s, run);
            }
            return;
        }
        int title = menu_title_at(x, y);
        if (title < 0) {
            s->menu = -1;
            s->down = 0;
            return;
        }
        if (title == s->menu) {
            s->menu = -1;
            s->down = 0;
            return;
        }
        s->menu = title;
        s->menu_hot = -1;
        s->down = 0;
        return;
    }

    int title = menu_title_at(x, y);
    if (title >= 0) {
        s->menu = title;
        s->menu_hot = -1;
        s->down = 0;
        return;
    }

    for (int i = s->nwin - 1; i >= 0; i--) {
        Layout lay;
        window_layout(&s->win[i], &lay);
        if (!rect_has(lay.outer, x, y)) {
            continue;
        }
        bring_front(s, i);
        Window *w = &s->win[s->nwin - 1];
        window_layout(w, &lay);
        s->press_win = s->nwin - 1;
        if (rect_has(lay.closeb, x, y)) {
            s->press_zone = Z_CLOSE;
            return;
        }
        if (rect_has(lay.shade, x, y)) {
            s->press_zone = Z_SHADE;
            return;
        }
        if (rect_has(lay.zoom, x, y)) {
            s->press_zone = Z_ZOOM;
            return;
        }
        if (!w->collapsed && rect_has(lay.grow, x, y)) {
            s->press_zone = Z_GROW;
            s->orig_x = w->x;
            s->orig_y = w->y;
            s->orig_w = w->w;
            s->orig_h = w->h;
            return;
        }
        if (rect_has(lay.title, x, y)) {
            if (clicks >= 2) {
                toggle_shade(w);
                s->down = 0;
                return;
            }
            s->press_zone = Z_TITLE;
            s->orig_x = w->x;
            s->orig_y = w->y;
            return;
        }
        if (!w->collapsed && rect_has(lay.vbar, x, y)) {
            if (y < lay.vbar.y + 16) {
                w->scroll -= CELL_H;
            } else if (y >= lay.vbar.y + lay.vbar.h - 16) {
                w->scroll += CELL_H;
            }
            session_clamp(s);
            s->down = 0;
            return;
        }
        if (!w->collapsed && w->kind == WIN_FOLDER && rect_has(lay.content, x, y)) {
            int idx = item_index_at(w, x, y);
            s->desk_sel = -1;
            if (idx >= 0) {
                select_only(w, idx);
                s->press_zone = Z_ITEM;
                s->press_item = idx;
                if (clicks >= 2) {
                    open_item(s, w, idx);
                    s->down = 0;
                }
                return;
            }
            select_only(w, -1);
        }
        s->press_zone = Z_CONTENT;
        return;
    }

    int which = desktop_which(s->sw, s->sh, x, y);
    if (which == 0 || which == 1) {
        s->desk_sel = which;
        if (clicks >= 2) {
            open_folder(s, which == 0 ? "" : "Trash");
            s->down = 0;
        }
        return;
    }
    s->desk_sel = -1;
}

void session_move(Session *s, int x, int y) {
    note_mouse(s, x, y);
    if (s->menu >= 0) {
        s->menu_hot = menu_item_at(s->menu, x, y);
    }
    if (!s->down || s->dialog) {
        return;
    }
    if (s->press_zone == Z_TITLE && s->press_win >= 0) {
        Window *w = &s->win[s->press_win];
        s->drag = 1;
        w->x = s->orig_x + (x - s->press_x);
        w->y = s->orig_y + (y - s->press_y);
        w->zoomed = 0;
        session_clamp(s);
    } else if (s->press_zone == Z_GROW && s->press_win >= 0) {
        Window *w = &s->win[s->press_win];
        s->drag = 2;
        w->w = s->orig_w + (x - s->press_x);
        w->h = s->orig_h + (y - s->press_y);
        w->zoomed = 0;
        session_clamp(s);
    } else if (s->press_zone == Z_ITEM) {
        int dx = x - s->press_x;
        int dy = y - s->press_y;
        if (dx * dx + dy * dy > 16) {
            s->drag = 3;
        }
    }
}

void session_up(Session *s, int x, int y) {
    note_mouse(s, x, y);
    if (s->dialog) {
        int which = s->dialog_down;
        s->dialog_down = 0;
        s->down = 0;
        s->drag = 0;
        Rect box, cancel, ok;
        dialog_layout(s, &box, &cancel, &ok);
        (void)box;
        if (which == 1 && rect_has(ok, x, y)) {
            dialog_accept(s);
        } else if (which == 2 && rect_has(cancel, x, y)) {
            s->dialog = 0;
            s->dialog_cmd = 0;
        }
        return;
    }
    if (!s->down) {
        s->drag = 0;
        return;
    }
    int zone = s->press_zone;
    int win = s->press_win;
    int drag = s->drag;
    int item = s->press_item;
    s->down = 0;
    s->drag = 0;
    if (win >= 0 && win < s->nwin) {
        Layout lay;
        window_layout(&s->win[win], &lay);
        if (zone == Z_CLOSE && rect_has(lay.closeb, x, y)) {
            close_index(s, win);
            return;
        }
        if (zone == Z_SHADE && rect_has(lay.shade, x, y)) {
            toggle_shade(&s->win[win]);
            return;
        }
        if (zone == Z_ZOOM && rect_has(lay.zoom, x, y)) {
            toggle_zoom(s, &s->win[win]);
            return;
        }
    }
    if (drag == 3 && win >= 0 && win < s->nwin && item >= 0 && desktop_which(s->sw, s->sh, x, y) == 1) {
        Window *w = &s->win[win];
        if (w->kind == WIN_FOLDER && strcmp(w->rel, "Trash") != 0 && item < w->nitems) {
            char name[OSPI_NAME];
            snprintf(name, sizeof name, "%s", w->items[item].name);
            if (volume_trash(s->root, w->rel, name) != 0) {
                dialog_info(s, "Trash", "Could not move that to the Trash.");
            }
            reload_folders(s);
        }
    }
}

void session_key(Session *s, int key, int mods) {
    if (key >= 'A' && key <= 'Z') {
        key = key - 'A' + 'a';
    }
    if (s->dialog) {
        if ((mods & MOD_CMD) && key == 'q') {
            s->quit = 1;
            return;
        }
        if (key == 27) {
            s->dialog = 0;
            s->dialog_cmd = 0;
            return;
        }
        if (key == '\r') {
            dialog_accept(s);
        }
        return;
    }
    if (key == 27) {
        s->menu = -1;
        return;
    }
    if (mods & MOD_CMD) {
        int cmd = menu_cmd_for_key(key);
        if (cmd) {
            session_command(s, cmd);
        }
        return;
    }
    if (key == '\r') {
        session_command(s, CMD_OPEN);
    }
}
