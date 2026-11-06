#include "desktop_theme.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "desktop_wallpaper.h"
#include "ospi.h"
#include "draw.h"
#include "native_launch.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void bring_front(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin || idx == s->nwin - 1) {
        return;
    }
    Window tmp = s->win[idx];
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->win[s->nwin - 1] = tmp;
}

void close_index(Session *s, int idx) {
    if (idx < 0 || idx >= s->nwin) {
        return;
    }
    memmove(&s->win[idx], &s->win[idx + 1], (size_t)(s->nwin - idx - 1) * sizeof(Window));
    s->nwin--;
}

static void reload_window(Session *s, Window *w) {
    if (!w || w->kind != WIN_FOLDER) {
        return;
    }
    char keep[OSPI_NAME];
    keep[0] = 0;
    for (int i = 0; i < w->nitems; i++) {
        if (w->items[i].selected) {
            snprintf(keep, sizeof keep, "%s", w->items[i].name);
        }
    }
    int n = volume_list(s->root, w->rel, w->items, OSPI_MAX_ITEMS);
    w->nitems = n < 0 ? 0 : n;
    for (int i = 0; i < w->nitems; i++) {
        w->items[i].selected = keep[0] && strcmp(w->items[i].name, keep) == 0;
    }
}

void reload_folders(Session *s) {
    for (int i = 0; i < s->nwin; i++) {
        reload_window(s, &s->win[i]);
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
    if (!rel) {
        rel = "";
    }
    int existing = find_folder(s, rel);
    if (existing >= 0) {
        bring_front(s, existing);
        reload_window(s, &s->win[s->nwin - 1]);
        return 0;
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
    s->nwin++;
    reload_window(s, w);
    return 0;
}

static int open_text(Session *s, const char *title, const char *rel, const char *body) {
    for (int i = 0; i < s->nwin; i++) {
        if (s->win[i].kind == WIN_TEXT && strcmp(s->win[i].title, title) == 0) {
            snprintf(s->win[i].text, sizeof s->win[i].text, "%s", body ? body : "");
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
    snprintf(w->title, sizeof w->title, "%s", title);
    snprintf(w->rel, sizeof w->rel, "%s", rel ? rel : "");
    snprintf(w->text, sizeof w->text, "%s", body ? body : "");
    place_new(s, w, 1);
    if (strcmp(title, "About This Mac") == 0) {
        w->w = 460;
        w->h = 350;
        w->x = (s->sw - w->w) / 2;
        w->y = (s->sh - w->h) / 2;
        w->shade_h = w->h;
    }
    s->nwin++;
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

static int join_rel(char *out, size_t n, const char *rel, const char *name) {
    if (!name || strchr(name, '/')) {
        return -1;
    }
    int k = (!rel || !rel[0]) ? snprintf(out, n, "%s", name) : snprintf(out, n, "%s/%s", rel, name);
    if (k < 0 || (size_t)k >= n) {
        return -1;
    }
    return 0;
}

void open_item(Session *s, Window *w, int index) {
    if (index < 0 || index >= w->nitems) {
        return;
    }
    char rel[OSPI_PATH];
    if (join_rel(rel, sizeof rel, w->rel, w->items[index].name) != 0) {
        return;
    }
    if (session_is_sit(w->items[index].name)) {
        session_open_sit(s, rel);
        return;
    }
    if (w->items[index].kind == ITEM_APPLICATION) {
        char full[OSPI_PATH], err[240] = {0};
        int opened = volume_join(full, sizeof full, s->root, rel) == 0 ? native_open(s->root, full, err, sizeof err) : -1;
        if (opened != 0) dialog_info(s, "Open", opened < 0 && err[0] ? err : "This application has no supported CODE resource.");
        return;
    }
    if (w->items[index].kind == ITEM_FILE) {
        char full[OSPI_PATH];
        char err[240];
        int opened = 1;
        err[0] = 0;
        if (volume_join(full, sizeof full, s->root, rel) == 0) {
            opened = classic_open(s->root, full, w->items[index].name, err, sizeof err);
        }
        if (opened <= 0) {
            if (opened < 0) {
                dialog_info(s, "Open", err[0] ? err : "Could not open that program.");
            }
            return;
        }
        char buf[OSPI_TEXT];
        if (volume_read(s->root, rel, buf, sizeof buf) != 0) {
            dialog_info(s, "Open", "Could not read that file.");
            return;
        }
        if (open_text(s, w->items[index].name, rel, buf) != 0) {
            dialog_info(s, "Open", "Close a window first.");
        }
        return;
    }
    if (open_folder(s, rel) != 0) {
        dialog_info(s, "Open", "Close a window first.");
    }
}
void select_only(Window *w, int index) {
    for (int i = 0; i < w->nitems; i++) {
        w->items[i].selected = i == index;
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
        w->zoom_x = w->x;
        w->zoom_y = w->y;
        w->zoom_w = w->w;
        w->zoom_h = w->h;
        w->x = 8;
        w->y = 26;
        w->w = s->sw - 16;
        w->h = s->sh - 34;
        w->zoomed = 1;
    } else {
        w->x = w->zoom_x;
        w->y = w->zoom_y;
        w->w = w->zoom_w;
        w->h = w->zoom_h;
        w->zoomed = 0;
    }
}
static void format_ram(char *out, size_t n, uint64_t b) {
    uint64_t g = 1024ull * 1024ull * 1024ull;
    if (g != 0 && b >= g && b % g == 0) {
        snprintf(out, n, "%llu GB", (unsigned long long)(b / g));
    } else {
        snprintf(out, n, "%.1f GB", (double)b / (double)g);
    }
}
void session_command(Session *s, int cmd) {
    s->menu = -1;
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
    case CMD_CONTROL_PANELS:
        context_close(); appearance_panel_open_controls(s); break;
    case CMD_APPEARANCE_DESKTOP:
    case CMD_APPEARANCE_THEMES:
        context_close(); appearance_panel_open(s, cmd == CMD_APPEARANCE_THEMES); break;
    case CMD_QUIT:
    case CMD_SHUTDOWN:
        s->quit = 1;
        break;
    case CMD_SOUND_TOGGLE:
        s->sound_toggle_request = 1;
        break;
    case CMD_STARTUP_TOGGLE:
        s->startup_toggle_request = 1;
        break;
    case CMD_STARTUP_PREVIEW:
        s->startup_preview_request = 1;
        break;
    case CMD_CLOSE:
        if (s->nwin > 0) {
            close_index(s, s->nwin - 1);
        }
        break;
    case CMD_CLEANUP:
        if (s->nwin > 0 && s->win[s->nwin - 1].kind == WIN_FOLDER) {
            s->win[s->nwin - 1].scroll = 0;
            reload_window(s, &s->win[s->nwin - 1]);
        }
        break;
    case CMD_NEW_FOLDER: {
        if (s->nwin == 0 || s->win[s->nwin - 1].kind != WIN_FOLDER) {
            open_folder(s, "");
        }
        Window *w = &s->win[s->nwin - 1];
        char created[OSPI_NAME];
        if (volume_mkdir_unique(s->root, w->rel, "untitled folder", created, sizeof created) != 0) {
            dialog_info(s, "New Folder", "Could not create the folder.");
            break;
        }
        reload_window(s, w);
        select_only(w, -1);
        for (int i = 0; i < w->nitems; i++) {
            w->items[i].selected = strcmp(w->items[i].name, created) == 0;
        }
        break;
    }
    case CMD_OPEN: {
        Window *w = s->nwin ? &s->win[s->nwin - 1] : NULL;
        if (w && w->kind == WIN_FOLDER) {
            for (int i = 0; i < w->nitems; i++) {
                if (w->items[i].selected) {
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
        }
        reload_folders(s);
        break;
    case CMD_ABOUT: {
        char ram[32];
        char body[OSPI_TEXT];
        format_ram(ram, sizeof ram, s->ram);
        snprintf(body, sizeof body,
                 "OS999 Stylized 0.1\n%s, %s memory\n"
                 "Letters: %s\n\n"
                 "Created by Ian Trimble\n"
                 "Independent software creator\n"
                 "Games, tools, and classic computing\n\n"
                 "remnantsecurity.com\n"
                 "Ian's home for the things he creates\n"
                 "and shares publicly.\n\n"
                 "OS999 is a clean-room desktop inspired\n"
                 "by classic Macintosh computing.\n"
                 "Compatibility is a work in progress.\n",
                 s->arch, ram, typeface_label());
        if (open_text(s, "About This Mac", ":about", body) != 0) {
            dialog_info(s, "About This Mac", "Close a window first.");
        }
        break;
    }
    case CMD_HELP: {
        char body[OSPI_TEXT];
        snprintf(body, sizeof body,
                 "Click a menu, then a command.\n"
                 "Double-click a folder or a file.\n"
                 "Drag a file onto the Trash.\n"
                 "Special, Empty Trash, deletes it.\n\n"
                 "Cmd-N  New Folder\n"
                 "Cmd-O  Open\n"
                 "Cmd-W  Close\n"
                 "Cmd-Q  Quit\n\n"
                 "File, Install from Garden, copies\n"
                 "an app or game you copied from\n"
                 "macintoshgarden.org. Supported 68k\n"
                 "apps open in OS999's own runtime.\n"
                 "Some archives use a legacy launcher;\n"
                 "many classic programs still fail.\n\n"
                 "The volume on disk:\n%s\n\n"
                 "Quit, then delete that folder,\n"
                 "to get the original files back.\n",
                 s->root);
        if (open_text(s, "How to Use OS999 Stylized", ":help", body) != 0) {
            dialog_info(s, "Help", "Close a window first.");
        }
        break;
    }
    case CMD_INSTALL: {
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
