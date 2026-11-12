#include "movie_window.h"
#include "finder_apps.h"
#include "appearance_panel.h"
#include "finder_desktop_positions.h"
#include "finder_drag.h"
#include "app_switcher.h"
#include "finder_files.h"
#include "volume_capacity.h"
#include "finder_batch.h"
#include "finder_state.h"
#include "finder_recent.h"
#include "session_int.h"
#include <stdio.h>
#include <string.h>

static void switcher_drop(Session *s, int source_window, int x, int y) {
    Window *source = finder_window(s, source_window);
    if (!source) return;
    if (source->kind != WIN_FOLDER || source->search_results) return;
    int app = app_switcher_application_at(s, x, y);
    if (!app) return; /* Dropping onto palette chrome never transfers a file. */
    const SessionMediaHooks *media = session_media_hooks();
    const char *error = "This application does not accept document drops yet. Your files have not been moved.";
    int selected = 0, index = -1;
    for (int i = 0; i < source->nitems; i++) if (window_items(source)[i].selected) { selected++; index = i; }
    if (!selected) return;
    if (app == 3 && media && media->is_media_file && media->try_open_file) {
        error = "Drop one supported media file onto OS999Amp. Your files have not been moved.";
        if (selected == 1 && window_items(source)[index].kind == ITEM_FILE) {
            const char *name = window_items(source)[index].name;
            char rel[OSPI_PATH], full[OSPI_PATH]; FinderUndo identity;
            int n = snprintf(rel, sizeof rel, "%s%s%s", source->rel, source->rel[0] ? "/" : "", name);
            if (n > 0 && (size_t)n < sizeof rel && !finder_identity(s->root, source->rel, name, &identity) &&
                identity.file_kind == 1 && !volume_join(full, sizeof full, s->root, rel) && media->is_media_file(full)) {
                if (!media->try_open_file(s, full)) { finder_recent_note(s, rel, 0); return; }
                error = "OS999Amp could not open this media file. Your files have not been moved.";
            }
        }
    }
    dialog_info(s, "Open in Application", error);
}

void finder_drag_drop(Session *s, int source_window, int x, int y) {
    if (app_switcher_hit(s,x,y)) {
        switcher_drop(s, source_window, x, y);
        return;
    }
    if (finder_apps_modal(s) || finder_apps_occluded(s, 1, x, y)) return;
    Rect panels[2]; int panel_count = appearance_panel_regions(s, panels);
    for (int i = 0; i < panel_count; i++) if (rect_has(panels[i], x, y)) return;
    Window *source = finder_window(s, source_window);
    if (!source) return;
    if (source->kind != WIN_FOLDER || source->search_results) return;
    char target[OSPI_PATH] = {0}; int found = 0, trash = 0;
    for (int i = s->nwin - 1; i >= 0; i--) {
        Window *w = &s->win[i]; Layout lay; window_layout(w, &lay);
        if (!rect_has(lay.outer, x, y)) continue;
        if (w->kind == WIN_MOVIE && !w->collapsed && rect_has(lay.content, x, y)) {
            int selected = 0, item = -1;
            for (int j = 0; j < source->nitems; j++) if (window_items(source)[j].selected) { selected++; item = j; }
            if (selected == 1 && window_items(source)[item].kind == ITEM_FILE && movie_is_name(window_items(source)[item].name)) {
                char rel[OSPI_PATH];
                int n = snprintf(rel, sizeof rel, "%s%s%s", source->rel, source->rel[0] ? "/" : "", window_items(source)[item].name);
                if (n > 0 && (size_t)n < sizeof rel) (void)movie_window_open(s, window_items(source)[item].name, rel);
            }
            return; /* Document drop opens in a player; never moves source files. */
        }
        if (w->kind != WIN_FOLDER || w->search_results || w->collapsed || !rect_has(lay.content, x, y)) return;
        int item = item_index_at(w, x, y);
        if (item >= 0 && (window_items(w)[item].kind == ITEM_FOLDER || window_items(w)[item].kind == ITEM_SYSTEM)) {
            if (w == source && window_items(w)[item].selected) return;
            int n = snprintf(target, sizeof target, "%s%s%s", w->rel, w->rel[0] ? "/" : "", window_items(w)[item].name);
            if (n < 0 || (size_t)n >= sizeof target) return;
        } else if (item >= 0 && window_items(w)[item].kind == ITEM_ALIAS) {
            if (w == source && window_items(w)[item].selected) return;
            if (finder_alias_target(s->root, w->rel, window_items(w)[item].name, target) != 1 ||
                !finder_directory_exists(s->root, target)) {
                dialog_info(s, "File Transfer", "This alias does not resolve to an available folder. Your files have not been moved.");
                return;
            }
        } else {
            snprintf(target, sizeof target, "%s", w->rel);
        }
        found = 1; break;
    }
    if (!found) {
        int which = session_desktop_which(s, x, y);
        if (which < 0) {
            if (y < TITLE_H || x < 0 || x >= s->sw || y >= s->sh || !s->desktop.kind) return;
            strcpy(target, s->desktop.rel);
            int item = item_index_at(&s->desktop, x, y);
            if (item >= 0) {
                const Item *entry = &window_items(&s->desktop)[item];
                if (source == &s->desktop && entry->selected) {
                    if (!(s->mouse_mods & MOD_OPTION)) {
                        finder_desktop_positions_move(s, x - s->press_x, y - s->press_y); return;
                    }
                } else if (entry->kind == ITEM_FOLDER || entry->kind == ITEM_SYSTEM) {
                    int n = snprintf(target, sizeof target, "%s/%s", s->desktop.rel, entry->name);
                    if (n < 0 || n >= (int)sizeof target) return;
                } else if (entry->kind == ITEM_ALIAS &&
                    (finder_alias_target(s->root, s->desktop.rel, entry->name, target) != 1 || !finder_directory_exists(s->root, target))) {
                    dialog_info(s, "File Transfer", "This alias does not resolve to an available folder. Your files have not been moved."); return;
                }
            }
        }
        if (which == 1) strcpy(target, "Trash");
        found = 1;
    }
    trash = !strcmp(target, "Trash");
    if (!strcmp(source->rel, target) && !(s->mouse_mods & MOD_OPTION)) {
        if (source == &s->desktop) finder_desktop_positions_move(s, x - s->press_x, y - s->press_y);
        return;
    }
    char parent[OSPI_PATH]; snprintf(parent, sizeof parent, "%s", source->rel);
    int succeeded = 0, failed = 0, into_self = 0;
    int selected = 0;
    for (int i = 0; i < source->nitems; i++) selected += !!window_items(source)[i].selected;
    if (finder_undo_reserve(s, selected)) return;
    finder_undo_begin(s);
    for (int i = 0; i < source->nitems; i++) {
        if (!window_items(source)[i].selected) continue;
        char name[OSPI_NAME], moved[OSPI_NAME]; strcpy(name, window_items(source)[i].name);
        char self[OSPI_PATH * 2]; size_t self_n = (size_t)snprintf(self, sizeof self, "%s%s%s", parent, parent[0] ? "/" : "", name);
        if (!trash && !strncmp(target, self, self_n) && (target[self_n] == 0 || target[self_n] == '/')) { into_self++; continue; }
        int result = trash ? finder_trash(s->root, parent, name, moved) :
            (s->mouse_mods & MOD_OPTION) ? finder_copy_to(s->root, parent, name, target, moved) :
                                         finder_move(s->root, parent, name, target);
        if (result) { failed++; continue; }
        succeeded++;
        if (trash) snprintf(s->sound_event, sizeof s->sound_event, "trash.drop");
        if (trash || !(s->mouse_mods & MOD_OPTION))
            finder_state_retarget(s, parent, name, target, trash ? moved : name);
        finder_undo_record(s, trash ? 2 : (s->mouse_mods & MOD_OPTION) ? 4 : 3,
            parent, target, trash || (s->mouse_mods & MOD_OPTION) ? moved : name);
    }
    reload_folders(s);
    if (!strcmp(target, s->desktop.rel)) {
        int position_failed = 0;
        for (int i = 0; i < s->undo_count; i++) {
            FinderUndo *entry = &finder_undo_entries(s)[i];
            position_failed += finder_desktop_position_item(s, entry->name, x + (i % 5) * 12, y + (i % 5) * 12) != 0;
        }
        if (position_failed) dialog_info(s, "Desktop Icons", "Files were transferred, but some desktop icon positions could not be saved.");
    }
    if (into_self && !failed) dialog_info(s, "File Transfer", "A folder cannot be moved or copied into itself. Nothing was changed.");
    if (failed) {
        char body[200]; snprintf(body, sizeof body,
            "%d item%s moved or copied; %d could not be transferred. Existing destination files were not replaced.",
            succeeded, succeeded == 1 ? "" : "s", failed);
        const char *full = volume_disk_full_notice(1);
        dialog_info(s, "File Transfer", full ? full : body);
    }
}
