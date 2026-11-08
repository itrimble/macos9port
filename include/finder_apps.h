#ifndef OS999_FINDER_APPS_H
#define OS999_FINDER_APPS_H
#include "ospi.h"
/* Finder-only commands must never act on background Finder selections. Global
 * launch, appearance and application-switching commands remain available. */
static inline int finder_apps_command_allowed(const Session *s, int command) {
    if (!s || !(s->media_focused || s->browser_focused || s->guest_focused)) return 1;
    if (s->guest_focused && (command == CMD_CLOSE || command == CMD_QUIT || command == CMD_CLOSE_ALL))
        return command != CMD_CLOSE_ALL && s->guest_active && s->guest_menu_command;
    if (command >= CMD_LABEL_BASE && command < CMD_LABEL_BASE + 8) return 0;
    switch (command) {
    case CMD_NEW_FOLDER: return s->browser_focused;
    case CMD_COPY: return s->browser_focused && (s->browser_edit_flags & BROWSER_CAN_COPY);
    case CMD_PASTE: return s->browser_focused && (s->browser_edit_flags & BROWSER_CAN_PASTE);
    case CMD_SELECT_ALL: return s->browser_focused && (s->browser_edit_flags & BROWSER_CAN_SELECT);
    case CMD_UNDO: return s->browser_focused && (s->browser_edit_flags & BROWSER_CAN_UNDO);
    case CMD_CLOSE: case CMD_CLOSE_ALL: return !s->browser_focused || s->browser_windows > 0;
    case CMD_OPEN: case CMD_GET_INFO: case CMD_DUPLICATE: case CMD_MAKE_ALIAS:
    case CMD_PUT_AWAY: case CMD_FAVORITE: case CMD_ENCRYPT: case CMD_FIND:
    case CMD_FIND_AGAIN: case CMD_SHOW_ORIGINAL: case CMD_PAGE_SETUP:
    case CMD_PRINT: case CMD_PRINT_WINDOW: case CMD_RENAME: case CMD_MOVE_TRASH:
    case CMD_CLEAR: case CMD_DESKTOP_ITEMS:
    case CMD_TERM_CLEAR: case CMD_TERM_THEME: case CMD_VIEW_ICONS: case CMD_VIEW_LIST:
    case CMD_VIEW_BUTTONS: case CMD_AS_WINDOW: case CMD_AS_POPUP: case CMD_CLEANUP:
    case CMD_MENU_ARRANGE: case CMD_MENU_LABELS: case CMD_LABEL:
    case CMD_SORT_NAME: case CMD_SORT_KIND: case CMD_SORT_DATE: case CMD_SORT_SIZE:
    case CMD_SORT_CREATED: case CMD_SORT_LABEL:
    case CMD_RESET_COLUMNS: case CMD_VIEW_OPTIONS: case CMD_EMPTY_ASK:
    case CMD_EJECT: case CMD_ERASE: return 0;
    default: return 1;
    }
}
int finder_apps_front(const Session *s);
void finder_apps_note(Session *s, int app);
int finder_apps_rank(const Session *s, int app);
int finder_apps_above(const Session *s, int app, int other);
void finder_apps_return(Session *s, int departing);
int finder_apps_modal(const Session *s);
int finder_apps_pointer_capture(const Session *s);
int finder_apps_occluded(const Session *s, int app, int x, int y);
int finder_apps_key(Session *s, int key, int mods, int down, int repeat);
void finder_apps_close_guest_window(Session *s);
int finder_apps_enabled(const Session *s, int command);
int finder_apps_media_running(const Session *s);
int finder_apps_command(Session *s, int command);
int finder_apps_sync(Session *s, int active, const char *title, int *focus);
int finder_apps_guest_hit(const Session *s, int focus, int x, int y);
#endif
