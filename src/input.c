#include "desktop_info.h"
#include "notes.h"
#include "finder_system_icons.h"
#include "finder_desktop_surface.h"
#include "menu_navigation.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "finder_files.h"
#include "finder_settings.h"
#include "finder_labels.h"
#include "finder_popup.h"
#include "finder_selection.h"
#include "finder_drag.h"
#include "finder_spring.h"
#include "folder_view.h"
#include "folder_navigation.h"
#include "appearance_panel.h"
#include "control_panels.h"
#include "desktop_context.h"
#include "movie_window.h"
#include "font_book_window.h"
#include "finder_edit.h"
#include "ospi.h"
#include "session_int.h"
#include "term.h"
#include <stdio.h>
#include <string.h>
void session_wheel(Session *s, int dy) {
    s->click_valid = s->display_dimmed = 0; s->last_activity_ms = s->input_now_ms;
    int eff = s->natural_scroll ? -dy : dy;
    if (control_panels_wheel(s, eff)) return;
    if (session_overlay_active(s) || s->desktop_focus || s->nwin <= 0) return;
    Window *w = &s->win[s->nwin - 1];
    if (w->kind == WIN_FONT_BOOK && font_book_window_wheel(w, eff)) return;
    if (w->collapsed) return;
    int step = s->wheel_speed == 1 ? 9 : s->wheel_speed == 3 ? 30 : 18;
    w->scroll -= eff * step;
    session_clamp(s);
}

static void note_mouse(Session *s, int x, int y) { s->mx = x; s->my = y; }

/* Some event sources report every press as clicks=1. Promote only a completed
 * click on this exact icon, with elapsed time supplied by session_tick. */
static int icon_double(Session *s, int valid, int kind, const char *dir,
                       const char *name, int x, int y, int clicks) {
    uint32_t elapsed = s->input_now_ms - s->click_ms;
    uint32_t win_ms = s->double_click_ms > 0 ? (uint32_t)s->double_click_ms : 500u;
    int dx = x - s->click_x, dy = y - s->click_y;
    int repeat = valid && (elapsed > 0 || clicks >= 2) && elapsed <= win_ms && dx >= -4 && dx <= 4 &&
                 dy >= -4 && dy <= 4 && kind == s->click_kind && strcmp(dir, s->click_dir) == 0 &&
                 strcmp(name, s->click_name) == 0;
    s->click_ms = s->input_now_ms; s->click_kind = kind; s->click_x = x; s->click_y = y;
    snprintf(s->click_dir, sizeof s->click_dir, "%s", dir);
    snprintf(s->click_name, sizeof s->click_name, "%s", name);
    return (valid && s->input_now_ms > 0) ? repeat : (clicks >= 2 || repeat);
}

void session_down(Session *s, int x, int y, int clicks) {
    s->display_dimmed = 0; s->last_activity_ms = s->input_now_ms; s->menu_flash_end_ms = 0;
    finder_spring_finish(s, 0, 0, 0);
    s->switcher.tear_armed = 0;
    int prior_click = s->click_valid;
    s->click_valid = 0;
    if (finder_edit_down(s, x, y, clicks)) return;   /* placing the caret in a name */
    session_clamp(s);
    menu_update(s);
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
        if (finder_labels_down(s, x, y) || finder_settings_down(s, x, y)) return;
        Rect box, cancel, ok;
        dialog_layout(s, &box, &cancel, &ok);
        (void)box;
        if (rect_has(ok, x, y)) {
            s->dialog_down = 1;
        } else if (s->dialog >= 2 && rect_has(cancel, x, y)) {
            s->dialog_down = 2;
        } else {
            s->dialog_down = 0;
        }
        return;
    }

    if (context_down(s, x, y)) return;
    if (app_switcher_down(s, x, y)) return;
    if (s->menu < 0 && appearance_panel_down(s, x, y)) return;
    if (s->menu < 0 && session_media_hooks() && session_media_hooks()->down &&
        session_media_hooks()->down(s, x, y)) return;

    if (s->menu >= 0) {
        int active = s->menu_child && menu_item_at(s->menu_child, x, y) >= 0 ? s->menu_child : s->menu;
        int item = menu_item_at(active, x, y);
        if (item >= 0) {
            menu_activate(s, active, item);
            s->down = 0;
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
        s->menu_child = 0;
        s->menu = title;
        s->menu_hot = -1;
        s->down = 0;
        return;
    }

    int title = menu_title_at(x, y);
    if (title >= 0) {
        s->switcher.tear_armed = title == 7;
        s->menu_child = 0;
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
            if (clicks >= 2 && !w->popup) {
                toggle_shade(w);
                snprintf(s->sound_event, sizeof s->sound_event,
                         w->collapsed ? "window.collapse" : "window.expand");
                s->down = 0;
                return;
            }
            s->press_zone = Z_TITLE;
            s->orig_x = w->x;
            s->orig_y = w->y;
            return;
        }
        if (!w->collapsed && rect_has(lay.vbar, x, y)) {
            int step = w->kind == WIN_FOLDER && w->view_mode == VIEW_LIST ? FOLDER_LIST_ROW : CELL_H;
            if (y < lay.vbar.y + 16) {
                w->scroll -= step;
            } else if (y >= lay.vbar.y + lay.vbar.h - 16) {
                w->scroll += step;
            }
            session_clamp(s);
            s->down = 0;
            return;
        }
        if (!w->collapsed && w->kind == WIN_FOLDER && rect_has(lay.content, x, y)) {
            if (folder_view_header_click(s, x, y)) return;
            int idx = item_index_at(w, x, y);
            s->desk_sel = -1;
            if (idx >= 0) {
                int was_selected = window_items(w)[idx].selected;
                finder_selection_press(s, w, idx);
                s->press_zone = Z_ITEM;
                s->press_item = idx;
                finder_edit_press(s, w, idx, x, y, was_selected);
                if (!(s->mouse_mods & (MOD_SHIFT | MOD_CMD)) && (w->view_mode == VIEW_BUTTONS || icon_double(s, prior_click, 1, w->rel, window_items(w)[idx].name, x, y, clicks))) {
                    s->edit_cand = 0;
                    open_item(s, w, idx);
                    s->down = 0;
                }
                return;
            }
            finder_selection_press(s, w, -1);
        }
        if (!w->collapsed && w->kind == WIN_FONT_BOOK && rect_has(lay.content, x, y)) {
            font_book_window_down(s, w, &lay, x, y); s->press_zone = Z_CONTENT; return;
        }
        if (!w->collapsed && w->kind == WIN_TERMINAL) {
            terminal_down(s, w, &lay, x, y, clicks);
        }
        if (notes_down(s, x, y)) { s->down = 0; return; }
        s->press_zone = Z_CONTENT;
        return;
    }

    finder_apps_command(s, CMD_APP_FINDER); appearance_panel_blur();
    finder_popup_collapse_others(s, -1);
    s->desktop_focus = 1;
    for (int i = 0; i < s->nwin; i++) select_only(&s->win[i], -1);
    if (finder_desktop_overflow_down(s, x, y)) return;
    int which = session_desktop_which(s, x, y);
    if (which == 0 || which == 1) {
        select_only(&s->desktop, -1);
        s->desk_sel = which;
        s->press_zone = Z_DESKTOP;
        s->press_item = which;
        Rect icon, hit; session_desktop_icon_rect(s, which, &icon, &hit);
        s->orig_x = icon.x; s->orig_y = icon.y;
        if (icon_double(s, prior_click, 2, "", which == 0 ? "disk" : "trash", x, y, clicks)) {
            open_folder(s, which == 0 ? "" : "Trash");
            s->down = 0;
        }
        return;
    }
    s->desk_sel = -1;
    for (int i = 0; i < s->nwin; i++) select_only(&s->win[i], -1);
    Window *desktop = &s->desktop;
    int index = item_index_at(desktop, x, y);
    finder_selection_press(s, desktop, index);
    s->press_win = OSPI_DESKTOP_WINDOW; s->press_item = index;
    s->press_zone = index >= 0 ? Z_ITEM : Z_CONTENT;
    if (index >= 0 && !(s->mouse_mods & (MOD_SHIFT | MOD_CMD)) &&
        icon_double(s, prior_click, 1, desktop->rel, window_items(desktop)[index].name, x, y, clicks)) {
        open_item(s, desktop, index); s->down = 0;
    }
}

void session_move(Session *s, int x, int y) {
    note_mouse(s, x, y);
    if (finder_settings_move(s, x, y)) return;
    if (app_switcher_tear(s, x, y) || app_switcher_move(s, x, y)) return;
    if (context_active()) { context_move(s, x, y); return; }
    if (s->menu >= 0) {
        menu_hover(s, x, y);
    }
    if (s->menu < 0 && control_panels_move(s, x, y)) return;
    if (s->menu < 0 && session_media_hooks() && session_media_hooks()->move &&
        session_media_hooks()->move(s, x, y)) return;
    if (!s->down || s->dialog) {
        return;
    }
    if (s->press_zone == Z_COLUMN) {
        folder_view_column_move(s, x);
    } else if (s->press_zone == Z_TITLE && s->press_win >= 0) {
        Window *w = &s->win[s->press_win];
        int dx = x - s->press_x, dy = y - s->press_y;
        if (!s->drag && dx * dx + dy * dy <= 16) return;
        if (w->popup) {
            finder_popup_set(s, w, 0);
            s->orig_x = s->press_x - w->w / 2; s->orig_y = s->press_y - TITLE_H / 2;
        }
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
    } else if (s->press_zone == Z_CONTENT && s->press_win >= 0 && s->press_win < s->nwin && s->win[s->press_win].kind == WIN_TERMINAL) {
        Layout lay;
        window_layout(&s->win[s->press_win], &lay);
        terminal_move(s, &s->win[s->press_win], &lay, x, y);
    } else if (s->press_zone == Z_CONTENT) {
        finder_selection_marquee(s, x, y);
    } else if (s->press_zone == Z_ITEM || s->press_zone == Z_DESKTOP) {
        int dx = x - s->press_x;
        int dy = y - s->press_y;
        if (dx * dx + dy * dy > 16) {
            s->drag = s->press_zone == Z_ITEM ? 3 : 4;
        }
    }
}

void session_up(Session *s, int x, int y) {
    note_mouse(s, x, y);
    if (finder_settings_move(s, x, y)) { s->down = s->drag = 0; return; }
    s->switcher.tear_armed = 0;
    if (app_switcher_up(s, x, y)) return;
    if (control_panels_up(s, x, y)) return;
    if (session_media_hooks() && session_media_hooks()->up &&
        session_media_hooks()->up(s, x, y)) return;
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
            if (finder_labels_cancel(s)) return;
            if (s->dialog_cmd == CMD_TERM_PASTE_DO && terminal_front(s)) {
                terminal_cancel_paste(terminal_front(s));
            }
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
    int dx = x - s->press_x, dy = y - s->press_y;
    int clean = !drag && dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4;
    if ((zone == Z_ITEM || zone == Z_DESKTOP) && clean) {
        s->click_valid = 1;
    }
    finder_edit_release(s, zone == Z_ITEM && clean);
    s->down = 0;
    s->drag = 0;
    if (win == OSPI_DESKTOP_WINDOW && !drag && zone == Z_ITEM && s->press_selection_keep && !(s->mouse_mods & (MOD_SHIFT | MOD_CMD)))
        select_only(&s->desktop, item);
    if (win >= 0 && win < s->nwin) {
        if (!drag && zone == Z_ITEM && s->press_selection_keep && !(s->mouse_mods & (MOD_SHIFT | MOD_CMD)))
            select_only(&s->win[win], item);
        Layout lay;
        window_layout(&s->win[win], &lay);
        if (zone == Z_TITLE && !drag && s->win[win].popup && rect_has(lay.title, x, y)) {
            finder_popup_toggle(s, &s->win[win]); return;
        }
        if (zone == Z_CLOSE && rect_has(lay.closeb, x, y)) {
            close_index(s, win);
            return;
        }
        if (zone == Z_SHADE && rect_has(lay.shade, x, y)) {
            toggle_shade(&s->win[win]);
            snprintf(s->sound_event, sizeof s->sound_event,
                     s->win[win].collapsed ? "window.collapse" : "window.expand");
            return;
        }
        if (zone == Z_ZOOM && rect_has(lay.zoom, x, y)) {
            toggle_zoom(s, &s->win[win]);
            return;
        }
        if (s->win[win].kind == WIN_TERMINAL) {
            terminal_up(s, &s->win[win], &lay, x, y);
        }
        if (zone == Z_CONTENT && !drag && desktop_info_click(s, &s->win[win], x, y)) return;
        if (zone == Z_CONTENT && !drag && s->win[win].kind == WIN_MOVIE && rect_has(lay.content, x, y)) {
            movie_window_click(&s->win[win], x, y);
            return;
        }
    }
    if (drag == 3) {
        finder_drag_drop(s, win, x, y);
        finder_spring_finish(s, x, y, 1);
    } else if (drag == 4 && zone == Z_DESKTOP) {
        finder_system_icons_move(s, item, s->orig_x + dx, s->orig_y + dy);
    }

}

void session_key(Session *s, int key, int mods) {
    s->display_dimmed = 0; s->last_activity_ms = s->input_now_ms;
    if (s->down && (s->drag == 3 || s->drag == 4)) {
        if (key == 27) { s->down = s->drag = s->click_valid = 0; finder_spring_finish(s, 0, 0, 0); }
        return;
    }
    menu_update(s);
    int orig_key = key;
    s->click_valid = 0;
    if (s->dialog) {
        if (finder_labels_key(s, key, mods) || finder_settings_key(s, key, mods)) return;
        if (s->dialog == 3) {
            if ((mods & MOD_CMD) && (key == 'a' || key == 'A')) { s->edit_selected = 1; return; }
            if (!(mods & MOD_CMD) && (key == 8 || key == 127)) {
                size_t n = strlen(s->edit_text);
                if (s->edit_selected) s->edit_text[0] = 0;
                else if (n) {
                    do { n--; } while (n && ((unsigned char)s->edit_text[n] & 0xc0) == 0x80);
                    s->edit_text[n] = 0;
                }
                s->edit_selected = 0; return;
            }
        }
        /* Modal dialogs disable application commands, including Quit. */
        if ((mods & MOD_CMD) && (key == 'q' || key == 'Q')) return;
        if (key == 27) {
            s->down = s->drag = s->dialog_down = 0;
            if (finder_labels_cancel(s)) return;
            if (s->dialog_cmd == CMD_TERM_PASTE_DO && terminal_front(s)) {
                terminal_cancel_paste(terminal_front(s));
            }
            s->dialog = 0;
            s->dialog_cmd = 0;
            return;
        }
        if (key == '\r') {
            s->down = s->drag = s->dialog_down = 0;
            dialog_accept(s);
        }
        return;
    }
    if (notes_key(s, key, mods)) return;
    if (finder_edit_key(s, key, mods)) return;
    if (s->menu < 0 && s->nwin && font_book_window_key(s, &s->win[s->nwin - 1], key, mods)) return;
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    if (context_key(s, key, mods)) return;
    if (menu_navigation_key(s, key, mods)) return;
    if (s->menu < 0 && appearance_panel_key(s, key, mods)) return;
    if (s->menu < 0 && session_media_hooks() && session_media_hooks()->key &&
        session_media_hooks()->key(s, key, mods)) return;
    if (s->menu >= 0 && key == 27) {
        s->menu = -1;
        return;
    }
    /* Typing selects by name prefix (Mac OS 8/9 type-ahead, finder_nav_key);
     * arrows, Home/End and Shift-extension stay with folder_navigation_key;
     * finder_nav_key also adds Command-arrows and the empty-desktop walk. */
    int typed = (key >= 'a' && key <= 'z') || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9');
    if (s->menu < 0 && typed && !mods && finder_nav_key(s, key, mods)) return;
    if (s->menu < 0 && folder_navigation_key(s, key, mods)) { finder_desktop_reveal_selected(s); return; }
    if (s->menu < 0 && finder_nav_key(s, key, mods)) return;
    if (mods & MOD_CMD) {
        int cmd = menu_cmd_for_key(key);
        if (cmd) {
            session_command(s, cmd);
        }
        return;
    }
    if (s->menu < 0 && terminal_front(s) && terminal_key(s, orig_key, mods)) {
        return;
    }
    if (s->menu < 0 && s->nwin > 0 && s->win[s->nwin - 1].kind == WIN_MOVIE &&
        movie_window_key(&s->win[s->nwin - 1], key)) {
        return;
    }
    if (key == 27) {
        s->menu = -1;
        return;
    }
    if (text_navigation_key(s, key, mods)) return;
    if (key == '\r' && !finder_edit_return(s)) {
        session_command(s, CMD_OPEN);
    }
}

int session_overlay_active(const Session *s) {
    return s->dialog || s->menu >= 0 || context_active() || appearance_panel_active() ||
           (session_media_hooks() && session_media_hooks()->active && session_media_hooks()->active());
}

void session_context(Session *s, int x, int y) {
    s->click_valid = s->down = s->drag = 0;
    Rect palette;
    if (app_switcher_bounds(s, &palette) && rect_has(palette, x, y)) return;
    finder_edit_commit(s);
    if (s->dialog) return;
    if (session_media_hooks() && session_media_hooks()->active && session_media_hooks()->active()) return;
    if (appearance_panel_context(s, x, y)) return;
    s->menu = -1;
    context_open(s, x, y);
}

void session_text_input(Session *s, const char *text) {
    if (notes_input(s, text)) return;
    if (s && text && finder_labels_text(s, text)) return;
    if (!s || s->dialog != 3 || !text) return;
    size_t used = s->edit_selected ? 0 : strlen(s->edit_text), n = strlen(text);
    if (!n || used + n >= sizeof s->edit_text) return;
    for (size_t i = 0; i < n; i++) if ((unsigned char)text[i] < 32 || text[i] == 127) return;
    memcpy(s->edit_text + used, text, n + 1); s->edit_selected = 0;
}
