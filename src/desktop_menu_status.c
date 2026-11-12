#include "desktop_menu_status.h"
#include "session_int.h"
#include "notes.h"
#include "term.h"
#include <string.h>

static Session *tracked;
static int pending_command;
void desktop_menu_track(Session *s) { tracked = s; }
Session *desktop_menu_tracked(void) { return tracked; }
void desktop_menu_note_unavailable(int command) { pending_command = command; }

static const Window *front_window(const Session *s) {
    return s && s->nwin > 0 ? &s->win[s->nwin - 1] : NULL;
}
static int in_trash(const Window *w) { return w && w->kind == WIN_FOLDER && !strcmp(w->rel, "Trash"); }

const char *desktop_menu_reason(const Session *s, int command) {
    const Window *w = front_window(s);
    int folder = w && w->kind == WIN_FOLDER;
    int foreign = s && (s->guest_focused || s->media_focused || s->browser_focused);
    switch (command) {
    case CMD_NEW_FOLDER: return "Search results are not a real folder. Open a folder window to make a new folder.";
    case CMD_NEW_NOTE: return "Notes are made from the Finder. Switch to the Finder first.";
    case CMD_OPEN: return "Select an item or an icon first.";
    case CMD_PRINT: return "Nothing printable is selected, or no printer connection is available. Select a text file or open a text window.";
    case CMD_PRINT_WINDOW: case CMD_PAGE_SETUP: return "Printing needs a host printer connection, and none is available right now.";
    case CMD_CLOSE: return "There is no window to close.";
    case CMD_CLOSE_ALL: return "There are no windows to close.";
    case CMD_GET_INFO: return "Select one item, or the disk or Trash icon.";
    case CMD_MENU_LABELS: case CMD_LABEL: return "Select items in a folder window first.";
    case CMD_DUPLICATE: case CMD_MAKE_ALIAS: case CMD_FAVORITE:
        return "Select one or more items in a folder window first.";
    case CMD_PUT_AWAY: return "Put Away works on items selected inside the Trash window.";
    case CMD_ENCRYPT: return "Encryption is not built into OS999 yet, so there is nothing for this item to do.";
    case CMD_FIND_AGAIN: return "Use Find first; there is no search to repeat.";
    case CMD_SHOW_ORIGINAL: return "Select a single alias to find its original.";
    case CMD_RENAME: return "Select one item. The Trash and System Folder cannot be renamed.";
    case CMD_MOVE_TRASH: case CMD_CLEAR:
        return in_trash(w) ? "Items already in the Trash cannot be moved there again. Use Empty Trash."
                           : "Select items in a folder window first.";
    case CMD_UNDO: return "There is nothing to undo.";
    case CMD_CUT: return "Cut is not offered: OS999 copies names and text but never moves files through the clipboard. Use Copy, or drag items to the Trash.";
    case CMD_COPY:
        return "Select items in a folder window, or open a text or Terminal window, to copy from.";
    case CMD_PASTE:
        return "Paste works in notes and the Terminal. Open one and click inside it.";
    case CMD_SELECT_ALL: return "Select All needs a folder window with items, or an editable note.";
    case CMD_TERM_CLEAR: case CMD_TERM_THEME: return "This only works with a Terminal or Console window in front.";
    case CMD_VIEW_ICONS: case CMD_VIEW_LIST: case CMD_VIEW_BUTTONS:
        return folder ? "The desktop can only be viewed as icons." : "Open a folder window first.";
    case CMD_AS_WINDOW: case CMD_AS_POPUP: return "Open a folder window (not the desktop) first.";
    case CMD_CLEANUP: case CMD_MENU_ARRANGE: case CMD_VIEW_OPTIONS:
    case CMD_SORT_NAME: case CMD_SORT_KIND: case CMD_SORT_DATE: case CMD_SORT_CREATED:
    case CMD_SORT_SIZE: case CMD_SORT_LABEL:
        return "Open a folder window first.";
    case CMD_RESET_COLUMNS: return folder && w->view_mode == VIEW_LIST ? "The columns are already in their standard positions."
                                                                     : "Column positions only apply to a folder shown as a list.";
    case CMD_EJECT: return "OS999 HD is the startup disk and cannot be ejected.";
    case CMD_ERASE: return "Erasing the disk is not offered. It would delete everything on OS999 HD.";
    case CMD_APP_HIDE: case CMD_APP_HIDE_OTHERS: return "No other application is running.";
    case CMD_APP_SHOW_ALL: return "Nothing is hidden.";
    default: break;
    }
    if (command >= CMD_LABEL_BASE && command < CMD_LABEL_BASE + 8) return "Select items in a folder window first.";
    if (foreign) return "Switch to the Finder to use this command.";
    return NULL;
}

void desktop_menu_untrack(const Session *s) { if (tracked == s) tracked = NULL; }

int desktop_menu_command(Session *s, int command) {
    if (command == CMD_SLEEP) {
        s->display_dimmed = 1;
        return 1;
    }
    if (command == CMD_MENU_UNAVAILABLE) {
        int pending = pending_command; pending_command = 0;
        const char *why = desktop_menu_reason(s, pending);
        if (why && !s->dialog) dialog_info(s, "Not Available", why);
        return 1;
    }
    return 0;
}
