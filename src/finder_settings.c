#include "finder_settings.h"
#include "finder_labels.h"
#include "finder_preferences.h"
#include "finder_files.h"
#include "desktop_theme.h"
#include "session_int.h"
#include "menu_navigation.h"
#include "app_mode.h"
#include <stdio.h>
#include <string.h>
static const char *views[] = {"Icons", "Buttons (single-click to open)", "List", "Arrange by name", "Arrange by kind", "Arrange by date modified", "Arrange by size", "Arrange by date created", "Arrange by label"};
static const char *prefs[] = {"Play interface sounds", "Show startup introduction", "Labels...", "Spring-loaded folders", "Opening delay"};
static int desktop_view(const Session *s) {
    Window *w = finder_current(s);
    return s->settings_kind == CMD_VIEW_OPTIONS && w && w->desktop_surface;
}
static int view_row(const Session *s, int row) { return desktop_view(s) && row > 0 ? row + 2 : row; }
static int top(const Session *s) { return s->settings_kind == CMD_APP_MODE ? APP_MODE_ROWS_TOP : 40; }
static int count(const Session *s) { return s->settings_kind == CMD_APP_MODE ? APP_MODE_COUNT : s->settings_kind == CMD_LABEL ? 8 : s->settings_kind == CMD_PREFERENCES ? 5 : desktop_view(s) ? 7 : 9; }
static const char *name(const Session *s, int row) {
    if (s->settings_kind == CMD_APP_MODE) return app_mode_label(row);
    return s->settings_kind == CMD_LABEL ? finder_label_name(row) : s->settings_kind == CMD_PREFERENCES ? prefs[row] : views[view_row(s, row)];
}
static int chosen(const Session *s, int row) {
    if (s->settings_kind == CMD_APP_MODE) return s->settings_flags == row;
    if (s->settings_kind == CMD_LABEL) return s->settings_flags == row;
    if (s->settings_kind == CMD_PREFERENCES) return !!(s->settings_flags & (1 << row));
    row = view_row(s, row);
    if (row < 3) return s->settings_view == (row == 1 ? VIEW_BUTTONS : row == 2 ? VIEW_LIST : VIEW_ICONS);
    return s->settings_sort == row - 3;
}
static void choose(Session *s, int row) {
    s->settings_row = row;
    if (s->settings_kind == CMD_APP_MODE) { app_mode_choose(s, row); return; }
    if (s->settings_kind == CMD_PREFERENCES && row == 2) { finder_labels_preferences(s); return; }
    if (s->settings_kind == CMD_PREFERENCES && row == 4) {
        s->settings_delay = s->settings_delay < 600 ? 600 : s->settings_delay < 1200 ? 1200 : 300; return;
    }
    row = view_row(s, row);
    if (s->settings_kind == CMD_LABEL) s->settings_flags = row;
    else if (s->settings_kind == CMD_PREFERENCES) s->settings_flags ^= 1 << row;
    else if (row < 3) s->settings_view = row == 1 ? VIEW_BUTTONS : row == 2 ? VIEW_LIST : VIEW_ICONS;
    else s->settings_sort = row - 3;
}
int finder_settings_command(Session *s, int command) {
    if (command != CMD_LABEL && command != CMD_PREFERENCES && command != CMD_VIEW_OPTIONS && command != CMD_SETTINGS_APPLY) return 0;
    if (!s || s->dialog) return 1;
    Window *w = finder_current(s);
    if (command == CMD_SETTINGS_APPLY) {
        if (s->settings_kind == CMD_APP_MODE) { app_mode_apply(s); s->settings_kind = 0; return 1; }
        if (s->settings_kind == CMD_PREFERENCES) {
            int enabled = !!(s->settings_flags & 8);
            if ((enabled != !s->spring_disabled || s->settings_delay != (s->spring_delay_ms >= 100 && s->spring_delay_ms <= 2000 ? s->spring_delay_ms : 600)) &&
                finder_preferences_save(s, enabled, s->settings_delay)) {
                dialog_info(s, "Could not save Finder preferences", "");
                s->dialog = 4; s->dialog_cmd = CMD_SETTINGS_APPLY; return 1;
            }
            if (!!(s->settings_flags & 1) != !!s->host_sound_enabled) s->sound_toggle_request = 1;
            if (!!(s->settings_flags & 2) != !!s->host_startup_enabled) s->startup_toggle_request = 1;
        } else if (w && w->kind == WIN_FOLDER) {
            if (s->settings_kind == CMD_VIEW_OPTIONS) {
                int mode = s->settings_view, sort = s->settings_sort;
                session_command(s, mode == VIEW_LIST ? CMD_VIEW_LIST : mode == VIEW_BUTTONS ? CMD_VIEW_BUTTONS : CMD_VIEW_ICONS);
                const int commands[] = {CMD_SORT_NAME, CMD_SORT_KIND, CMD_SORT_DATE, CMD_SORT_SIZE, CMD_SORT_CREATED, CMD_SORT_LABEL};
                if (sort >= SORT_NAME && sort <= SORT_LABEL) session_command(s, commands[sort]);
            } else if (s->settings_kind == CMD_LABEL && !w->search_results) {
                int failure = 0;
                for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected)
                    failure |= finder_label(s->root, w->rel, window_items(w)[i].name, s->settings_flags) != 0;
                reload_folders(s);
                if (failure) dialog_info(s, "Label", "Some labels could not be saved. This volume may not support extended attributes.");
            }
        }
        s->settings_kind = 0; return 1;
    }
    if (command != CMD_PREFERENCES && (!w || w->kind != WIN_FOLDER)) return 1;
    if (command == CMD_LABEL && w->search_results) return 1;
    s->settings_kind = command; s->settings_row = 0; s->settings_flags = 0;
    if (command == CMD_PREFERENCES) {
        s->settings_flags = !!s->host_sound_enabled | (!!s->host_startup_enabled << 1) | (!s->spring_disabled << 3);
        s->settings_delay = s->spring_delay_ms >= 100 && s->spring_delay_ms <= 2000 ? s->spring_delay_ms : 600;
    }
    if (command == CMD_LABEL) for (int i = 0; i < w->nitems; i++)
        if (window_items(w)[i].selected) { s->settings_flags = window_items(w)[i].label; break; }
    if (command == CMD_VIEW_OPTIONS) { s->settings_view = w->view_mode; s->settings_sort = w->sort_mode; }
    dialog_info(s, command == CMD_LABEL ? "Label" : command == CMD_PREFERENCES ? "Finder Preferences" : "View Options", "");
    s->dialog = 4; s->dialog_cmd = CMD_SETTINGS_APPLY; return 1;
}
int finder_settings_key(Session *s, int key, int mods) {
    if (s->dialog != 4 || mods) return 0;
    if (s->settings_kind == CMD_PREFERENCES && s->settings_row == 4 && (key == MENU_KEY_LEFT || key == MENU_KEY_RIGHT)) {
        int delay = s->settings_delay + (key == MENU_KEY_LEFT ? -100 : 100);
        s->settings_delay = delay < 100 ? 100 : delay > 2000 ? 2000 : delay; return 1;
    }
    if (key == MENU_KEY_UP || key == MENU_KEY_DOWN) {
        s->settings_row = (s->settings_row + (key == MENU_KEY_UP ? count(s) - 1 : 1)) % count(s); return 1;
    }
    if (key == ' ') { choose(s, s->settings_row); return 1; }
    return 0;
}
static void slider_position(Session *s, int x) {
    Rect box, cancel, ok; dialog_layout(s, &box, &cancel, &ok);
    int pos = x - box.x - 150, width = box.w - 190;
    pos = pos < 0 ? 0 : pos > width ? width : pos;
    s->settings_delay = 100 + (pos * 1900 / width + 50) / 100 * 100;
}
int finder_settings_move(Session *s, int x, int y) {
    (void)y;
    if (s->dialog != 4 || s->settings_kind != CMD_PREFERENCES || !s->down || s->drag != 7) return 0;
    slider_position(s, x); return 1;
}
int finder_settings_down(Session *s, int x, int y) {
    if (s->dialog != 4) return 0;
    Rect box, cancel, ok; dialog_layout(s, &box, &cancel, &ok);
    int row = (y - box.y - top(s)) / 26;
    if (y < box.y + top(s) || row < 0 || row >= count(s) || x < box.x + 14 || x >= box.x + box.w - 14) return 0;
    if (s->settings_kind == CMD_PREFERENCES && row == 4) {
        slider_position(s, x);
        s->settings_row = row; s->down = 1; s->drag = 7; return 1;
    }
    choose(s, row); s->down = 0; return 1;
}
void finder_settings_paint(Fb *fb, const Session *s, Rect box) {
    if (s->dialog != 4) return;
    for (int i = 0; i < count(s); i++) {
        int x = box.x + 20, y = box.y + top(s) + i * 26;
        uint32_t ink = desktop_theme_color(THEME_FOREGROUND);
        if (s->settings_kind == CMD_PREFERENCES && i == 4) {
            int left = box.x + 150, width = box.w - 190;
            int knob = left + (s->settings_delay - 100) * width / 1900;
            fb_text(fb, x, y + 4, ink, name(s, i)); fb_hline(fb, left, y + 12, width, ink);
            fb_fill(fb, knob - 3, y + 5, 7, 15, desktop_theme_color(THEME_PANEL));
            fb_frame(fb, knob - 3, y + 5, 7, 15, ink);
            fb_text(fb, left, y + 24, ink, "Short"); fb_text(fb, left + width - fb_text_width("Long"), y + 24, ink, "Long");
            if (s->settings_row == i) fb_frame(fb, x - 4, y - 1, box.w - 34, 24, ink);
            continue;
        }
        if (s->settings_kind == CMD_PREFERENCES && i == 2) {
            fb_frame(fb, x, y, box.w - 40, 22, ink);
            fb_text(fb, x + 8, y + 4, ink, name(s, i));
            if (s->settings_row == i) fb_frame(fb, x - 3, y - 3, box.w - 34, 28, ink);
            continue;
        }
        if (s->settings_kind == CMD_LABEL) fb_fill(fb, x + 20, y, box.w - 62, 22, finder_label_color(i));
        fb_frame(fb, x, y + 5, 12, 12, ink);
        if (chosen(s, i)) fb_fill(fb, x + 3, y + 8, 6, 6, ink);
        fb_text(fb, x + 26, y + 4, s->settings_kind == CMD_LABEL ? finder_label_ink(i) : ink, name(s, i));
        if (s->settings_row == i) fb_frame(fb, x - 4, y - 1, box.w - 34, 24, ink);
    }
    app_mode_paint(fb, s, box);
}
