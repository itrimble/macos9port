#include "finder_apps.h"
#include "finder_print.h"
#include "desktop_finder.h"
#include "finder_files.h"
#include "finder_batch.h"
#include "finder_state.h"
#include "finder_popup.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "session_int.h"
#include <stdio.h>
#include <string.h>

static Window *front(Session *s) {
    return finder_current(s);
}

static const Item *single(const Window *w) {
    const Item *item = NULL;
    if (!w || w->kind != WIN_FOLDER || w->search_results) return NULL;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) {
        if (item) return NULL;
        item = &window_items(w)[i];
    }
    return item;
}
static void select_name(Session *s, const char *name) {
    reload_folders(s);
    Window *w = front(s);
    if (w) for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = !strcmp(window_items(w)[i].name, name);
}
static void retarget(Session *s, const char *parent, const char *from, const char *to) {
    finder_state_retarget(s, parent, from, parent, to);
}
int finder_command_enabled(const Session *s, int command) {
    if (command == CMD_PRINT || command == CMD_PRINT_WINDOW || command == CMD_PAGE_SETUP)
        return finder_print_enabled(s, command);
    if (!s || s->dialog) return 0;
    Window *active = finder_current(s);
    int folder = active && active->kind == WIN_FOLDER && !appearance_panel_active();
    if (command >= CMD_WINDOW_BASE && command < CMD_WINDOW_BASE + OSPI_MAX_WIN)
        return command - CMD_WINDOW_BASE < s->nwin;
    const Item *item = folder ? single(active) : NULL;
    int selected = 0;
    if (folder && !active->search_results)
        for (int i = 0; i < active->nitems; i++) selected += !!window_items(active)[i].selected;
    switch (command) {
    case CMD_LABEL: case CMD_MAKE_ALIAS: case CMD_FAVORITE: return selected > 0;
    case CMD_SHOW_ORIGINAL: return item && item->kind == ITEM_ALIAS;
    case CMD_FIND: case CMD_FAVORITES: case CMD_DESKTOP_ITEMS: return 1;
    case CMD_FIND_AGAIN: return s->find_query[0] != 0;
    case CMD_DUPLICATE: return selected > 0;
    case CMD_MOVE_TRASH: case CMD_CLEAR:
        return selected && strcmp(active->rel, "Trash");
    case CMD_PUT_AWAY: return selected && !strcmp(active->rel, "Trash");
    case CMD_RENAME: return item && strcmp(item->name, "Trash") && strcmp(item->name, "System Folder");
    case CMD_UNDO: return s->undo_count > 0;
    case CMD_SHOW_CLIPBOARD: case CMD_PREFERENCES: case CMD_RESTART: return 1;
    case CMD_VIEW_OPTIONS: return folder;
    case CMD_AS_WINDOW: case CMD_AS_POPUP: return folder && !active->desktop_surface;
    case CMD_RESET_COLUMNS: return folder && active->view_mode == VIEW_LIST;
    default: return 0;
    }
}

int finder_command(Session *s, int command) {
    if (command >= CMD_LABEL_BASE && command < CMD_LABEL_BASE + 8) {
        if (finder_command_enabled(s, CMD_LABEL)) {
            s->settings_kind = CMD_LABEL;
            s->settings_flags = command - CMD_LABEL_BASE;
            session_command(s, CMD_SETTINGS_APPLY);
        }
        return 1;
    }
    if (command >= CMD_WINDOW_BASE && command < CMD_WINDOW_BASE + OSPI_MAX_WIN) {
        if (finder_command_enabled(s, command)) {
            appearance_panel_close(); context_close();
            finder_apps_command(s, CMD_APP_FINDER);
            bring_front(s, command - CMD_WINDOW_BASE);
            Window *w = front(s);
            if (w && w->collapsed) {
                if (w->popup) finder_popup_toggle(s, w); else toggle_shade(w);
            }
        }
        return 1;
    }
    if (command < CMD_SHOW_CLIPBOARD || command >= CMD_WINDOW_BASE) return 0;
    /* Sorting and button view are handled by folder_view_command. */
    if (command == CMD_SORT_DATE || command == CMD_SORT_SIZE || command == CMD_SORT_CREATED || command == CMD_SORT_LABEL || command == CMD_VIEW_BUTTONS) return 0;
    if (command == CMD_RESTART_DO) {
        appearance_panel_close(); context_close();
        while (s->nwin) close_index(s, s->nwin - 1);
        s->desk_sel = -1; s->desktop_focus = 1;
        open_folder(s, ""); return 1;
    }
    if (command == CMD_RENAME_DO) {
        if (!s->edit_name[0]) return 1;
        if (finder_rename(s->root, s->edit_parent, s->edit_name, s->edit_text)) {
            dialog_info(s, "Rename", "Could not rename the item. Choose an unused name without slashes. The source and destination must stay inside OS999 HD.");
        } else {
            finder_undo_begin(s);
            finder_undo_record(s, 1, s->edit_parent, s->edit_parent, s->edit_text);
            snprintf(finder_undo_entries(s)[0].original, sizeof finder_undo_entries(s)[0].original, "%s", s->edit_name);
            retarget(s, s->edit_parent, s->edit_name, s->edit_text);
            select_name(s, s->edit_text);
        }
        s->edit_name[0] = 0; return 1;
    }
    if (!finder_command_enabled(s, command)) return 1;
    if (finder_batch_command(s, command)) return 1;
    Window *w = front(s);
    switch (command) {
    case CMD_DESKTOP_ITEMS:
        if (open_folder(s, "Desktop Folder")) dialog_info(s, "Desktop Items", "Close a window or reconnect the desktop folder and try again.");
        break;
    case CMD_FAVORITES:
        if (open_folder(s, "Favorites")) dialog_info(s, "Favorites", "Close a window first.");
        break;
    case CMD_SHOW_ORIGINAL: {
        const Item *item = single(w); char target[OSPI_PATH], name[OSPI_NAME];
        if (finder_alias_target(s->root, w->rel, item->name, target) != 1) {
            dialog_info(s, "Show Original", "The original item could not be found."); break;
        }
        char *leaf = strrchr(target, '/');
        snprintf(name, sizeof name, "%s", leaf ? leaf + 1 : target);
        if (leaf) *leaf = 0; else target[0] = 0;
        if (open_folder(s, target)) dialog_info(s, "Show Original", "Close a window first.");
        else select_name(s, name);
        break;
    }
    case CMD_RENAME: {
        const Item *item = single(w);
        snprintf(s->edit_parent, sizeof s->edit_parent, "%s", w->rel);
        snprintf(s->edit_name, sizeof s->edit_name, "%s", item->name);
        snprintf(s->edit_text, sizeof s->edit_text, "%s", item->name);
        s->edit_selected = 1;
        dialog_info(s, "Rename", "Enter a new name:");
        s->dialog = 3; s->dialog_cmd = CMD_RENAME_DO; break;
    }
    case CMD_SHOW_CLIPBOARD:
        if (!s->clipboard_write_request && s->clipboard_read_error) {
            dialog_info(s, "Clipboard", "The clipboard is too large or could not be read completely. Nothing was displayed."); break;
        }
        if (session_open_text(s, "Clipboard", ":clipboard",
            s->clipboard_write_request ? session_clipboard_output(s) : session_clipboard_input(s)))
            dialog_info(s, "Clipboard", "The clipboard window could not be opened. Close a window or free memory and try again.");
        break;
    case CMD_PREFERENCES:
        appearance_panel_open(s, 0); break;
    case CMD_VIEW_OPTIONS:
        dialog_info(s, "View Options", "Choose Icons, Buttons or List from View.\n\nArrange items by name, kind, date modified or size. Buttons open with one click.\n\nEach open folder keeps its own view and ordering."); break;
    case CMD_RESET_COLUMNS:
        memset(w->list_columns, 0, sizeof w->list_columns); break;
    case CMD_AS_WINDOW:
        finder_popup_set(s, w, 0); session_clamp(s); break;
    case CMD_AS_POPUP:
        finder_popup_set(s, w, 1); session_clamp(s); break;
    case CMD_RESTART:
        dialog_info(s, "Restart OS999", "Close desktop windows and reopen OS999 HD?\n\nFiles and preferences are kept. This restarts the desktop view only; the host computer and classic applications keep running.");
        s->dialog = 2; s->dialog_cmd = CMD_RESTART_DO; break;
    default: break;
    }
    return 1;
}
