#include "volume_documents.h"
#include "notes.h"
#include "finder_desktop_positions.h"
#include "finder_apps.h"
#include "finder_print.h"
#include "finder_recent.h"
#include "desktop_commands.h"
#include "desktop_finder.h"
#include "finder_files.h"
#include "finder_search.h"
#include "finder_settings.h"
#include "finder_labels.h"
#include "finder_state.h"
#include "finder_popup.h"
#include "folder_view.h"
#include "desktop_help.h"
#include "desktop_menu_status.h"
#include "desktop_theme.h"
#include "appearance_panel.h"
#include "control_panels.h"
#include "font_book.h"
#include "font_book_window.h"
#include "desktop_context.h"
#include "desktop_wallpaper.h"
#include "ospi.h"
#include "native_apps.h"
#include "draw.h"
#include "movie_window.h"
#include "native_launch.h"
#include "finder_edit.h"
#include "session_int.h"
#include "quake_pick.h"
#include "term.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
void bring_front(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin) return;
    finder_apps_command(s, CMD_APP_FINDER); appearance_panel_blur();
    s->desk_sel = -1;
    s->desktop_focus = 0;
    finder_popup_collapse_others(s, idx);
    if (idx == s->nwin - 1) return;
    Window tmp = s->win[idx];
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->win[s->nwin - 1] = tmp;
}
void close_index(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin) {
        return;
    }
    if (s->win[idx].kind == WIN_TERMINAL) {
        terminal_window_close(s, &s->win[idx]);
    }
    font_book_window_release(&s->win[idx]);
    movie_window_release(&s->win[idx]);
    finder_state_remember(s, &s->win[idx], 0);
    free(s->win[idx].expanded_items);
    free(s->win[idx].expanded_text);
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->nwin--;
    if (s->nwin < OSPI_MAX_WIN) {
        s->win[s->nwin].movie = NULL;
        s->win[s->nwin].expanded_items = NULL;
        s->win[s->nwin].expanded_text = NULL;
    }
    snprintf(s->sound_event, sizeof s->sound_event, "window.close");
}
void reload_folders(Session *s) {
    if (s->desktop.kind == WIN_FOLDER) reload_folder_window(s, &s->desktop);
    for (int i = 0; i < s->nwin; i++) {
        reload_folder_window(s, &s->win[i]);
    }
}
static const char *base_name(const char *rel) {
    if (!rel || !rel[0]) {
        return "OS999 HD";
    }
    const char *sl = strrchr(rel, '/');
    return sl ? sl + 1 : rel;
}
static int find_folder(const Session *s, const char *rel) {
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_FOLDER && strcmp(s->win[i].rel, rel ? rel : "") == 0) {
            return i;
        }
    }
    return -1;
}
static void place_new(Session *s, Window *w, int text) {
    int n = s->nwin;
    w->selection_anchor = w->selection_cursor = -1;
    w->x = 36 + (n % 5) * 22;
    w->y = 42 + (n % 5) * 22;
    w->w = text ? 500 : 524;
    w->h = text ? 320 : 214;
    w->shade_h = w->h;
    if (w->y + 80 > s->sh) {
        w->y = 36;
    }
}
int open_folder(Session *s, const char *rel) {
    finder_apps_command(s, CMD_APP_FINDER); appearance_panel_blur();
    if (!rel) {
        rel = "";
    }
    if (!finder_directory_exists(s->root, rel)) return -1;
    int existing = find_folder(s, rel);
    if (existing >= 0) {
        bring_front(s, existing);
        if (s->win[s->nwin - 1].popup && s->win[s->nwin - 1].collapsed)
            finder_popup_toggle(s, &s->win[s->nwin - 1]);
        return reload_folder_window(s, &s->win[s->nwin - 1]);
    }
    if (s->nwin >= OSPI_MAX_WIN) {
        return -1;
    }
    Window *w = &s->win[s->nwin];
    memset(w, 0, sizeof *w);
    w->kind = WIN_FOLDER;
    snprintf(w->rel, sizeof w->rel, "%s", rel);
    snprintf(w->title, sizeof w->title, "%s", base_name(rel));
    place_new(s, w, 0);
    finder_state_apply(s, w);
    s->nwin++;
    s->desk_sel = -1;
    s->desktop_focus = 0;
    if (reload_folder_window(s, w)) { s->nwin--; return -1; }
    snprintf(s->sound_event, sizeof s->sound_event, "folder.open");
    return 0;
}
int session_open_text(Session *s, const char *title, const char *rel, const char *body) {
    if (body && (!strcmp(body, OS999_LEGACY_README) || !strcmp(body, OS999_PREVIOUS_README))) body = OS999_CURRENT_README;
    finder_apps_command(s, CMD_APP_FINDER); appearance_panel_blur();
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_TEXT && strcmp(s->win[i].rel, rel ? rel : "") == 0) {
            if (window_text_set(&s->win[i], body)) return -1;
            bring_front(s, i);
            return 0;
        }
    }
    if (s->nwin >= OSPI_MAX_WIN) {
        return -1;
    }
    Window *w = &s->win[s->nwin];
    memset(w, 0, sizeof *w);
    w->kind = WIN_TEXT;
    size_t path_len = rel ? strlen(rel) : 0;
    w->note_editable = rel && (!strcmp(rel,"Documents/Notes") ||
        (!strncmp(rel,"Documents/",10) && path_len > 4 && !strcmp(rel+path_len-4,".txt")));
    w->note_caret = body ? strlen(body) : 0;
    snprintf(w->title, sizeof w->title, "%s", title);
    snprintf(w->rel, sizeof w->rel, "%s", rel ? rel : "");
    if (window_text_set(w, body)) return -1;
    place_new(s, w, 1);
    if (strcmp(title, "About This Mac") == 0) {
        w->w = 460;
        w->h = 440;
        w->x = (s->sw - w->w) / 2;
        w->y = (s->sh - w->h) / 2;
        w->shade_h = w->h;
    }
    s->nwin++;
    s->desk_sel = -1;
    s->desktop_focus = 0;
    return 0;
}
void dialog_info(Session *s, const char *title, const char *body) {
    s->dialog = 1;
    s->dialog_cmd = 0;
    s->dialog_down = 0;
    snprintf(s->dialog_title, sizeof s->dialog_title, "%s", title);
    snprintf(s->dialog_body, sizeof s->dialog_body, "%s", body);
}
static void dialog_confirm(Session *s, const char *title, const char *body, int cmd) {
    dialog_info(s, title, body);
    s->dialog = 2;
    s->dialog_cmd = cmd;
}
void dialog_accept(Session *s) {
    int cmd = s->dialog_cmd;
    s->dialog = 0;
    s->dialog_cmd = 0;
    s->dialog_down = 0;
    if (cmd) {
        session_command(s, cmd);
    }
}
void select_only(Window *w, int index) {
    w->selection_anchor = w->selection_cursor = index;
    for (int i = 0; i < w->nitems; i++) {
        window_items(w)[i].selected = i == index;
    }
}
void toggle_shade(Window *w) {
    if (!w->collapsed) {
        w->shade_h = w->h;
        w->h = TITLE_H;
        w->collapsed = 1;
    } else {
        w->h = w->shade_h > TITLE_H ? w->shade_h : 214;
        w->collapsed = 0;
    }
}
void toggle_zoom(Session *s, Window *w) {
    if (w->collapsed) {
        toggle_shade(w);
    }
    if (!w->zoomed) {
        w->zoom_x = w->x; w->zoom_y = w->y; w->zoom_w = w->w; w->zoom_h = w->h;
        w->x = 8; w->y = 26; w->w = s->sw - 16; w->h = s->sh - 34;
        w->zoomed = 1;
    } else {
        w->x = w->zoom_x; w->y = w->zoom_y; w->w = w->zoom_w; w->h = w->zoom_h;
        w->zoomed = 0;
    }
    snprintf(s->sound_event, sizeof s->sound_event, "window.zoom");
}
void session_command(Session *s, int cmd) {
    telemetry_command(s, cmd);
    s->menu = -1; s->menu_child = 0;
    if (!s->dialog && !finder_apps_command_allowed(s, cmd)) return;
    if (s->media_focused && !s->dialog && cmd == CMD_QUIT && session_media_hooks() && session_media_hooks()->quit) {
        session_media_hooks()->quit(s); return;
    }
    if (s->media_focused && !s->dialog && (cmd == CMD_CLOSE || cmd == CMD_CLOSE_ALL)) {
        if (session_media_hooks() && session_media_hooks()->close) session_media_hooks()->close();
        return;
    }
    if (notes_active(s) && (cmd == CMD_SELECT_ALL || cmd == CMD_PASTE)) {
        if (cmd == CMD_PASTE && !session_clipboard_input(s)[0] && !s->clipboard_read_error)
            dialog_info(s, "Paste", "The clipboard is empty. The note was not changed.");
        else notes_key(s, cmd == CMD_PASTE ? 'v' : 'a', MOD_CMD);
        return;
    }
    if (session_sit_command(s, cmd) || finder_labels_command(s, cmd) || finder_apps_command(s, cmd) || finder_print_command(s, cmd) || finder_recent_command(s, cmd) || finder_settings_command(s, cmd) || finder_search_command(s, cmd) || desktop_menu_command(s, cmd) || finder_command(s, cmd) || desktop_command(s, cmd) || desktop_help_command(s, cmd)) return;
    if (folder_view_command(s, cmd)) {
        Window *w = finder_current(s);
        if (w && w->desktop_surface && cmd != CMD_VIEW_ICONS && cmd != CMD_VIEW_LIST && cmd != CMD_VIEW_BUTTONS)
            finder_desktop_positions_clean(s);
        return;
    }
    if ((cmd >= CMD_THEME_BASE && cmd <= CMD_THEME_LAST) || cmd == CMD_THEME_NEXT) {
        int index = cmd == CMD_THEME_NEXT ? (desktop_theme_current() + 1) % desktop_theme_count() : cmd - CMD_THEME_BASE;
        if (desktop_theme_select(index) < 0)
            dialog_info(s, "Appearance", "Could not save the color theme selection.");
        return;
    }
    if (cmd == CMD_WALLPAPER_PREVIOUS || cmd == CMD_WALLPAPER_NEXT) {
        int step = cmd == CMD_WALLPAPER_NEXT ? 1 : -1;
        menu_wallpaper_page(menu_wallpaper_current_page() + step);
        s->menu = MENU_APPEARANCE;
        s->menu_hot = -1;
        return;
    }
    if (cmd >= CMD_WALLPAPER_BASE && cmd <= CMD_WALLPAPER_LAST) {
        int index = cmd - CMD_WALLPAPER_BASE - 1;
        if (index >= desktop_wallpaper_count()) return;
        int selected = desktop_wallpaper_select(index);
        if (selected < 0)
            dialog_info(s, "Appearance", "Could not save the desktop picture selection.");
        else if (!selected && index >= 0)
            dialog_info(s, "Appearance", "Desktop picture unavailable. Using the plain desktop.");
        menu_wallpaper_page(menu_wallpaper_current_page());
        return;
    }
    switch (cmd) {
    case CMD_STANDARD_ICON: finder_standard_icon(s); break;
    case CMD_CONTROL_PANELS:
        context_close();
        finder_apps_command(s, CMD_APP_FINDER);
        control_panels_open(s); native_app_note(s, NATIVE_CONTROLS); break;
    case CMD_CP_GENERAL:
    case CMD_CP_SOUND:
    case CMD_CP_MONITORS:
    case CMD_CP_KEYBOARD:
    case CMD_CP_ENERGY:
    case CMD_CP_MEMORY: case CMD_CP_NETWORK: case CMD_CP_BLUETOOTH: case CMD_CP_EXTENSIONS: {
        static const char *const ids[] = {"general", "sound", "monitors", "keyboard_mouse",
                                          "energy_saver", "memory", "network"};
        const char *id = cmd == CMD_CP_BLUETOOTH ? "bluetooth" : cmd == CMD_CP_EXTENSIONS ? "extensions" : ids[cmd - CMD_CP_GENERAL];
        context_close();
        finder_apps_command(s, CMD_APP_FINDER);
        control_panels_open_module(s, id); native_app_note(s, NATIVE_CONTROLS);
        break;
    }
    case CMD_APPEARANCE_DESKTOP:
    case CMD_APPEARANCE_THEMES:
        context_close();
        finder_apps_command(s, CMD_APP_FINDER);
        appearance_panel_open(s, cmd == CMD_APPEARANCE_THEMES); break;
    case CMD_FONT_BOOK:
        context_close(); finder_apps_command(s, CMD_APP_FINDER);
        if (font_book_window_open(s) != 0) dialog_info(s, "Font Book 999", "Close a window first.");
        break;
    case CMD_MEDIA_PLAYER:
    case CMD_SUPPORTED_FORMATS:
        context_close();
        if (session_media_hooks() && session_media_hooks()->open_panel)
            session_media_hooks()->open_panel(s, cmd == CMD_SUPPORTED_FORMATS ? 1 : 0);
        break;
    case CMD_QUIT:
    case CMD_SHUTDOWN:
        s->quit = 1;
        break;
    case CMD_SOUND_TOGGLE:
        s->sound_toggle_request = 1;
        break;
    case CMD_BROWSER:
        session_note_network_activity(s);
        if (s->net_connect_via == 2) {
            s->browser_request = 0; s->browser_open_url[0] = 0;
            dialog_info(s, "Network Offline",
                        "TCP/IP & Network is set to Offline.\n\n"
                        "Choose Host HTTPS or Tailscale in\n"
                        "TCP/IP & Network to browse online.");
            break;
        }
        s->browser_request = 1;
        break;
    case CMD_STARTUP_TOGGLE:
        s->startup_toggle_request = 1;
        break;
    case CMD_STARTUP_PREVIEW:
        s->startup_preview_request = 1;
        break;
    case CMD_CLOSE_ALL:
        context_close(); appearance_panel_close();
        while (s->nwin) close_index(s, s->nwin - 1); break;
    case CMD_TAILSCALE:
        session_note_network_activity(s);
        terminal_tailnet_open(s);
        break;
    case CMD_QUAKE3_DEMO: session_open_quake_demo(s); break;
    case CMD_QUAKE3: session_open_quake(s); break;
    case CMD_QUAKE_CHOOSE: quake_pick_open(s); break;
    case CMD_TERMINAL_SHELL:
        terminal_open(s, TERM_MODE_SHELL);
        break;
    case CMD_TERMINAL_CONSOLE:
        terminal_open(s, TERM_MODE_CONSOLE);
        break;
    case CMD_COPY:
        if (terminal_front(s) && terminal_copy(s, terminal_front(s)) < 0)
            dialog_info(s, "Copy", "Select some text in the terminal first.");
        break;
    case CMD_PASTE:
        if (terminal_front(s) && terminal_paste(s, terminal_front(s)) < 0)
            dialog_info(s, "Paste", "The clipboard is empty.");
        break;
    case CMD_TERM_CLEAR:
        if (terminal_front(s)) terminal_clear_scrollback(terminal_front(s));
        break;
    case CMD_TERM_THEME:
        if (terminal_front(s)) terminal_toggle_theme(terminal_front(s));
        break;
    case CMD_TERM_PASTE_DO:
        if (terminal_front(s)) terminal_confirm_paste(s, terminal_front(s));
        break;
    case CMD_CLOSE:
        if (appearance_panel_active()) { appearance_panel_close(); break; }
        if (!s->desktop_focus && s->nwin > 0) {
            close_index(s, s->nwin - 1);
        }
        break;
    case CMD_CLEANUP:
        if (finder_current(s) && finder_current(s)->kind == WIN_FOLDER) {
            finder_current(s)->scroll = 0; reload_folder_window(s, finder_current(s));
            if (finder_current(s)->desktop_surface) finder_desktop_positions_clean(s);
        }
        break;
    case CMD_NEW_NOTE: notes_new(s); break;
    case CMD_NEW_FOLDER: {
        if (s->desk_sel >= 0 && open_folder(s, s->desk_sel == 1 ? "Trash" : "")) {
            dialog_info(s, "New Folder", "Close a window first."); break;
        }
        Window *w = finder_current(s);
        if (!w || w->kind != WIN_FOLDER) {
            if (open_folder(s, "")) { dialog_info(s, "New Folder", "Close a window first."); break; }
            w = finder_current(s);
        }
        char created[OSPI_NAME];
        if (volume_mkdir_unique(s->root, w->rel, "untitled folder", created, sizeof created) != 0) {
            dialog_info(s, "New Folder", "Could not create the folder.");
            break;
        }
        reload_folder_window(s, w);
        select_only(w, -1);
        for (int i = 0; i < w->nitems; i++) {
            window_items(w)[i].selected = strcmp(window_items(w)[i].name, created) == 0;
        }
        finder_edit_return(s);   /* like Mac OS 8 and 9: type the new folder's name */
        break;
    }
    case CMD_OPEN: {
        if (appearance_panel_active()) { appearance_panel_key(s, '\r', 0); break; }
        if (s->desk_sel >= 0) { open_folder(s, s->desk_sel == 0 ? "" : "Trash"); break; }
        Window *w = finder_current(s);
        if (w && w->kind == WIN_FOLDER) {
            for (int i = 0; i < w->nitems; i++) {
                if (window_items(w)[i].selected) {
                    open_item(s, w, i);
                    return;
                }
            }
        }
        if (s->desk_sel == 0) {
            open_folder(s, "");
        } else if (s->desk_sel == 1) {
            open_folder(s, "Trash");
        }
        break;
    }
    case CMD_EMPTY_ASK: {
        int n = volume_count(s->root, "Trash");
        if (n <= 0) {
            dialog_info(s, "Trash", "The Trash is empty.");
        } else {
            char body[160];
            snprintf(body, sizeof body, "Empty the Trash permanently?\n\n%d item%s will be deleted.", n,
                     n == 1 ? "" : "s");
            dialog_confirm(s, "Trash", body, CMD_EMPTY_DO);
        }
        break;
    }
    case CMD_EMPTY_DO:
        if (volume_empty_trash(s->root) != 0) {
            dialog_info(s, "Trash", "Some items could not be deleted.");
        } else {
            snprintf(s->sound_event, sizeof s->sound_event, "trash.empty");
        }
        reload_folders(s);
        break;
    case CMD_ABOUT: {
        char body[OSPI_TEXT];
        snprintf(body, sizeof body,
                 "Created by Ian Trimble\n"
                 "Independent software creator\n"
                 "Games, tools, and classic computing\n\n"
                 "Explore Ian's apps using the link below.\n\n"
                 "OS999 is a clean-room desktop inspired\n"
                 "by classic Macintosh computing.\n"
                 "Compatibility is a work in progress.\n");
        if (session_open_text(s, "About This Mac", ":about", body) != 0) {
            dialog_info(s, "About This Mac", "Close a window first.");
        }
        break;
    }
    case CMD_INSTALL: {
        session_note_network_activity(s);
        if (s->net_connect_via == 2) {
            dialog_info(s, "Network Offline",
                        "TCP/IP & Network is set to Offline.\n\n"
                        "Choose Host HTTPS or Tailscale first.");
            break;
        }
        if (!garden_page_ok(s->clip)) {
            dialog_info(s, "Install",
                        "Copy an app or game link from\n"
                        "macintoshgarden.org, then choose\n"
                        "Install from Garden again.");
            break;
        }
        snprintf(s->install_url, sizeof s->install_url, "%s", s->clip);
        char body[384];
        snprintf(body, sizeof body,
                 "Install this page into Applications?\n\n%s\n\n"
                 "Double-click the archive to open it.\n"
                 "Many classic programs still fail.",
                 s->install_url);
        dialog_confirm(s, "Install", body, CMD_INSTALL_DO);
        break;
    }
    case CMD_INSTALL_DO: {
        char err[240];
        char rel[OSPI_PATH];
        err[0] = 0;
        if (install_garden(s->root, s->install_url, rel, sizeof rel, err, sizeof err) != 0) {
            if (rel[0]) open_folder(s, rel);
            dialog_info(s, "Install", err[0] ? err : "Could not install that page.");
            break;
        }
        open_folder(s, rel);
        dialog_info(s, "Install", "Archive downloaded and expanded.\n\nOpen the app inside its folder.");
        break;
    }
    default:
        break;
    }
}
