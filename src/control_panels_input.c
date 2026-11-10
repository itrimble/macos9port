#include "control_panels_int.h"
#include "finder_apps.h"
#include "folder_navigation.h"
#include "menu_navigation.h"

static void run_context_row(Session *s, int row) {
    g_cp_ui.context_open = 0;
    if (g_cp_ui.open_module < 0) {
        if (row == 0) cp_open_selected_slot(s);
        else if (row == 1) control_panels_set_category_filter(OS999_CDEV_CAT_ALL);
    } else {
        if (row == 0) control_panels_return_to_grid(s);
        else if (row == 1) cp_save_active_module(s);
    }
}

int control_panels_down(Session *s, int x, int y, int clicks) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || s->finder_hidden) return 0;
    Rect r = cp_window_rect(s);
    if (!g_cp_ui.context_open &&
        (y < TITLE_H || !rect_has(r, x, y) || finder_apps_occluded(s, 1, x, y)))
        return 0;
    if (!control_panels_active()) {
        if (s->media_focused || s->browser_focused || s->guest_focused)
            session_command(s, CMD_APP_FINDER);
        g_cp_ui.focused = 1;
    }
    if (g_cp_ui.context_open) {
        s->down = s->drag = 0;
        Rect cb = g_cp_ui.context_box;
        if (x > cb.x && x < cb.x + cb.w - 1 && y > cb.y && y < cb.y + cb.h - 1)
            run_context_row(s, (y - cb.y - 1) / 24);
        else
            g_cp_ui.context_open = 0;
        return 1;
    }
    if (y < TITLE_H) return 0;
    s->down = s->drag = 0;
    if (rect_has(cp_close_rect(r), x, y)) {
        control_panels_close();
        return 1;
    }
    if (y < r.y + TITLE_H) {
        g_cp_ui.win_x = r.x;
        g_cp_ui.win_y = r.y;
        g_cp_ui.pos_valid = 1;
        g_cp_ui.dragging_win = 1;
        g_cp_ui.drag_dx = x - r.x;
        g_cp_ui.drag_dy = y - r.y;
        return 1;
    }
    if (g_cp_ui.open_module < 0) {
        for (int c = 0; c < CP_CAT_COUNT; c++) {
            if (rect_has(cp_cat_rect(r, c), x, y)) {
                control_panels_set_category_filter(c);
                return 1;
            }
        }
        if (rect_has(cp_open_btn_rect(r), x, y)) {
            cp_open_selected_slot(s);
            return 1;
        }
        Rect grid = cp_grid_rect(r);
        if (rect_has(grid, x, y)) {
            int indices[OS999_CDEV_MAX_MODULES];
            int n = cp_filtered_modules(indices);
            int max_sc = cp_max_grid_scroll(grid, n);
            Rect sb = cp_scrollbar_rect(grid);
            if (max_sc > 0 && rect_has(sb, x, y)) {
                int rel = cp_clamp(y - sb.y, 0, sb.h);
                g_cp_ui.scroll = (rel * max_sc) / (sb.h > 1 ? sb.h : 1);
                return 1;
            }
            uint32_t dbl_win = s->double_click_ms > 0 ? (uint32_t)s->double_click_ms : 500u;
            for (int slot = 0; slot < n; slot++) {
                if (rect_has(cp_card_rect(grid, slot, g_cp_ui.scroll), x, y)) {
                    uint32_t dt = s->input_now_ms - g_cp_ui.last_ms;
                    int dx = x - g_cp_ui.last_x, dy = y - g_cp_ui.last_y;
                    int repeat = g_cp_ui.last_slot == slot && dt > 0 && dt <= dbl_win &&
                                 dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6;
                    int dbl = (g_cp_ui.last_slot >= 0 && s->input_now_ms > 0)
                                  ? repeat
                                  : (clicks >= 2 || repeat);
                    g_cp_ui.selected_slot = slot;
                    g_cp_ui.last_slot = slot;
                    g_cp_ui.last_ms = s->input_now_ms;
                    g_cp_ui.last_x = x;
                    g_cp_ui.last_y = y;
                    if (dbl) cp_open_selected_slot(s);
                    return 1;
                }
            }
        }
        return 1;
    }

    if (rect_has(cp_back_btn_rect(r), x, y)) {
        control_panels_return_to_grid(s);
        return 1;
    }
    if (rect_has(cp_save_btn_rect(r), x, y)) {
        cp_save_active_module(s);
        return 1;
    }
    os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
    Rect content = cp_content_rect(r);
    if (m && m->handle_event && rect_has(content, x, y)) {
        os999_cdev_event_t ev = {
            .type = OS999_CDEV_EV_DOWN, .x = x, .y = y,
            .clicks = clicks, .now_ms = s->input_now_ms
        };
        m->handle_event(m, control_panels_host(s), &ev, content);
    }
    return 1;
}

void control_panels_display_changed(void) {
    g_cp_ui.pos_valid = 0;   /* centre again in the new desktop */
    g_cp_ui.context_open = g_cp_ui.dragging_win = 0;
}

int control_panels_move(Session *s, int x, int y) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || !control_panels_active()) return 0;
    if (g_cp_ui.dragging_win) {
        g_cp_ui.win_x = x - g_cp_ui.drag_dx;
        g_cp_ui.win_y = y - g_cp_ui.drag_dy;
        g_cp_ui.pos_valid = 1;
        Rect held = cp_window_rect(s);   /* inside the desktop, or panning when wider than it */
        g_cp_ui.win_x = held.x;
        g_cp_ui.win_y = held.y;
        return 1;
    }
    if (g_cp_ui.open_module >= 0) {
        os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
        Rect content = cp_content_rect(cp_window_rect(s));
        if (m && m->handle_event) {
            os999_cdev_event_t ev = {
                .type = OS999_CDEV_EV_MOVE, .x = x, .y = y, .now_ms = s->input_now_ms
            };
            if (m->handle_event(m, control_panels_host(s), &ev, content)) return 1;
        }
    }
    return 0;
}

int control_panels_up(Session *s, int x, int y) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s) return 0;
    if (g_cp_ui.dragging_win) {
        g_cp_ui.dragging_win = 0;
        return 1;
    }
    if (g_cp_ui.open_module >= 0 && control_panels_active()) {
        os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
        Rect content = cp_content_rect(cp_window_rect(s));
        if (m && m->handle_event) {
            os999_cdev_event_t ev = {
                .type = OS999_CDEV_EV_UP, .x = x, .y = y, .now_ms = s->input_now_ms
            };
            if (m->handle_event(m, control_panels_host(s), &ev, content)) return 1;
        }
    }
    return 0;
}

int control_panels_wheel(Session *s, int dy) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || !control_panels_active()) return 0;
    Rect r = cp_window_rect(s);
    if (!rect_has(r, s->mx, s->my) && (s->mx != 0 || s->my != 0)) return 0;
    if (g_cp_ui.open_module < 0) {
        int indices[OS999_CDEV_MAX_MODULES];
        int n = cp_filtered_modules(indices);
        Rect grid = cp_grid_rect(r);
        int step = s->wheel_speed == 1 ? 14 : s->wheel_speed == 3 ? 42 : 24;
        g_cp_ui.scroll = cp_clamp(g_cp_ui.scroll - dy * step, 0, cp_max_grid_scroll(grid, n));
        return 1;
    }
    os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
    if (m && m->handle_event) {
        os999_cdev_event_t ev = {
            .type = OS999_CDEV_EV_WHEEL, .x = s->mx, .y = s->my,
            .dy = dy, .now_ms = s->input_now_ms
        };
        m->handle_event(m, control_panels_host(s), &ev, cp_content_rect(r));
    }
    return 1;
}

int control_panels_context(Session *s, int x, int y) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || !control_panels_active()) return 0;
    s->down = s->drag = 0;
    s->menu = s->menu_hot = -1;
    g_cp_ui.context_open = 0;
    Rect r = cp_window_rect(s);
    if (!rect_has(r, x, y) || y <= r.y + TITLE_H) return 1;
    if (g_cp_ui.open_module < 0) {
        Rect grid = cp_grid_rect(r);
        int indices[OS999_CDEV_MAX_MODULES];
        int n = cp_filtered_modules(indices);
        for (int slot = 0; slot < n; slot++) {
            if (rect_has(cp_card_rect(grid, slot, g_cp_ui.scroll), x, y)) {
                g_cp_ui.selected_slot = slot;
                break;
            }
        }
    }
    int sw = s->sw > 0 ? s->sw : OSPI_W, sh = s->sh > 0 ? s->sh : OSPI_H;
    g_cp_ui.context_box = (Rect){
        cp_clamp(x, 0, sw - 204),
        cp_clamp(y, TITLE_H, sh - 50),
        204, 50
    };
    g_cp_ui.context_open = 1;
    return 1;
}

int control_panels_key(Session *s, int key, int mods) {
    if (!s || !g_cp_ui.open || g_cp_ui.owner != s || !control_panels_active()) return 0;
    if ((mods & MOD_CMD) && (key == 'q' || key == 'Q' || key == 't' || key == 'T'))
        return 0;
    if ((mods & MOD_CMD) && (key == 'o' || key == 'O') && g_cp_ui.open_module < 0)
        return 0;
    if (g_cp_ui.context_open) {
        if (key == 27) { g_cp_ui.context_open = 0; return 1; }
        if (key == '\r' || key == '\n') { run_context_row(s, 0); return 1; }
    }
    if ((mods & MOD_CMD) && (key == 'w' || key == 'W')) {
        control_panels_close();
        return 1;
    }
    if (g_cp_ui.open_module < 0) {
        if (key == 27) { control_panels_close(); return 1; }
        if (key == '\t') {
            int step = (mods & MOD_SHIFT) ? CP_CAT_COUNT - 1 : 1;
            control_panels_set_category_filter((g_cp_ui.category + step) % CP_CAT_COUNT);
            return 1;
        }
        int indices[OS999_CDEV_MAX_MODULES];
        int n = cp_filtered_modules(indices);
        if (n > 0) {
            int next = g_cp_ui.selected_slot;
            if (key == MENU_KEY_RIGHT || key == 29) next = cp_clamp(next + 1, 0, n - 1);
            else if (key == MENU_KEY_LEFT || key == 28) next = cp_clamp(next - 1, 0, n - 1);
            else if (key == MENU_KEY_DOWN || key == 31) next = cp_clamp(next + 3, 0, n - 1);
            else if (key == MENU_KEY_UP || key == 30) next = cp_clamp(next - 3, 0, n - 1);
            else if (key == FOLDER_KEY_HOME) next = 0;
            else if (key == FOLDER_KEY_END) next = n - 1;
            if (next != g_cp_ui.selected_slot) {
                g_cp_ui.selected_slot = next;
                cp_ensure_slot_visible(s, next);
                return 1;
            }
            if (key == '\r' || key == '\n') {
                cp_open_selected_slot(s);
                return 1;
            }
        }
        return 1;
    }

    if (key == 27 || key == 8 || key == 127) {
        control_panels_return_to_grid(s);
        return 1;
    }
    os999_cdev_t *m = os999_cdev_at(g_cp_ui.open_module);
    if (m && m->handle_event) {
        os999_cdev_event_t ev = {
            .type = OS999_CDEV_EV_KEY, .key = key, .mods = mods, .now_ms = s->input_now_ms
        };
        m->handle_event(m, control_panels_host(s), &ev, cp_content_rect(cp_window_rect(s)));
    }
    return 1;
}
