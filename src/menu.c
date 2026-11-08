#include "finder_apps.h"
#include "finder_recent.h"
#include "finder_labels.h"
#include "desktop_commands.h"
#include "desktop_finder.h"
#include "appearance_panel.h"
#include "draw.h"
#include "ospi.h"
#include "desktop_wallpaper.h"
#include <stdio.h>
#include <string.h>

#define ITEM(label, cmd) {label, cmd, 0, NULL, 0}
#define KEY(label, cmd, shortcut, key) {label, cmd, 0, shortcut, key}
#define SEP {NULL, 0, 0, NULL, 0}
static MenuItem k_pi[] = {
    ITEM("About This Mac", CMD_ABOUT), SEP,
    ITEM("Control Panels", CMD_MENU_PANELS),
    ITEM("Favorites", CMD_FAVORITES), ITEM("Recent Applications", CMD_MENU_RECENT_APPS),
    ITEM("Recent Documents", CMD_MENU_RECENT_DOCS), SEP,
    ITEM("Tailscale", CMD_TAILSCALE), ITEM("Terminal", CMD_TERMINAL_SHELL), ITEM("OS999 Console", CMD_TERMINAL_CONSOLE),
    KEY("Chromium Browser", CMD_BROWSER, "Cmd-B", 'b'), ITEM("OS999 Amp (Winamp)...", CMD_MEDIA_PLAYER),
    ITEM("Quake III Arena", CMD_QUAKE3),
    ITEM("Quake III Arena Demo", CMD_QUAKE3_DEMO),
};
static MenuItem k_file[] = {
    KEY("New Folder", CMD_NEW_FOLDER, "Cmd-N", 'n'),
    KEY("Open", CMD_OPEN, "Cmd-O", 'o'), ITEM("Print", CMD_PRINT),
    KEY("Close Window", CMD_CLOSE, "Cmd-W", 'w'), ITEM("Close All", CMD_CLOSE_ALL), SEP,
    KEY("Get Info", CMD_GET_INFO, "Cmd-I", 'i'), ITEM("Label", CMD_MENU_LABELS),
    KEY("Duplicate", CMD_DUPLICATE, "Cmd-D", 'd'),
    KEY("Make Alias", CMD_MAKE_ALIAS, "Cmd-M", 'm'),
    KEY("Put Away", CMD_PUT_AWAY, "Cmd-Y", 'y'),
    ITEM("Add to Favorites", CMD_FAVORITE), ITEM("Encrypt", CMD_ENCRYPT), SEP,
    KEY("Find...", CMD_FIND, "Cmd-F", 'f'), KEY("Find Again", CMD_FIND_AGAIN, "Cmd-G", 'g'),
    KEY("Show Original", CMD_SHOW_ORIGINAL, "Cmd-R", 'r'), SEP,
    ITEM("Page Setup...", CMD_PAGE_SETUP), KEY("Print Window...", CMD_PRINT_WINDOW, "Cmd-P", 'p'), SEP,
    ITEM("Rename...", CMD_RENAME), ITEM("Move to Trash", CMD_MOVE_TRASH),
    ITEM("Install from Garden", CMD_INSTALL), ITEM("New Terminal", CMD_TERMINAL_SHELL),
    ITEM("New OS999 Console", CMD_TERMINAL_CONSOLE), SEP, KEY("Quit OS999", CMD_QUIT, "Cmd-Q", 'q'),
};
static MenuItem k_edit[] = {
    KEY("Undo", CMD_UNDO, "Cmd-Z", 'z'), SEP,
    {"Cut", CMD_NONE, MI_DISABLED, "Cmd-X", 0},
    KEY("Copy Names", CMD_COPY, "Cmd-C", 'c'),
    KEY("Paste", CMD_PASTE, "Cmd-V", 'v'), ITEM("Clear", CMD_CLEAR),
    KEY("Select All", CMD_SELECT_ALL, "Cmd-A", 'a'), SEP,
    ITEM("Show Clipboard", CMD_SHOW_CLIPBOARD), SEP, ITEM("Preferences...", CMD_PREFERENCES), SEP,
    KEY("Clear Scrollback", CMD_TERM_CLEAR, "Cmd-K", 'k'), ITEM("Toggle Terminal Theme", CMD_TERM_THEME),
};
static MenuItem k_view[] = {
    ITEM("as Icons", CMD_VIEW_ICONS), ITEM("as Buttons", CMD_VIEW_BUTTONS), ITEM("as List", CMD_VIEW_LIST), SEP,
    ITEM("as Window", CMD_AS_WINDOW), ITEM("as Pop-up Window", CMD_AS_POPUP), SEP,
    ITEM("Clean Up", CMD_CLEANUP),
    ITEM("Arrange", CMD_MENU_ARRANGE), SEP,
    ITEM("Reset Column Positions", CMD_RESET_COLUMNS), ITEM("View Options...", CMD_VIEW_OPTIONS),
};
static MenuItem k_arrange[] = {
    ITEM("by Name", CMD_SORT_NAME), ITEM("by Kind", CMD_SORT_KIND),
    ITEM("by Date Modified", CMD_SORT_DATE), ITEM("by Date Created", CMD_SORT_CREATED),
    ITEM("by Size", CMD_SORT_SIZE), ITEM("by Label", CMD_SORT_LABEL),
};
static MenuItem k_labels[] = {
    ITEM("None", CMD_LABEL_BASE), ITEM("Essential", CMD_LABEL_BASE + 1),
    ITEM("Hot", CMD_LABEL_BASE + 2), ITEM("In Progress", CMD_LABEL_BASE + 3),
    ITEM("Cool", CMD_LABEL_BASE + 4), ITEM("Personal", CMD_LABEL_BASE + 5),
    ITEM("Project 1", CMD_LABEL_BASE + 6), ITEM("Project 2", CMD_LABEL_BASE + 7),
    SEP, ITEM("Edit Labels...", CMD_EDIT_LABELS),
};
static MenuItem k_panels[] = {
    ITEM("Open Control Panels", CMD_CONTROL_PANELS), SEP,
    ITEM("General Controls", CMD_CP_GENERAL),
    ITEM("Appearance", CMD_APPEARANCE_DESKTOP),
    ITEM("Sound & Startup", CMD_CP_SOUND),
    ITEM("Monitors & Display", CMD_CP_MONITORS),
    ITEM("Keyboard & Mouse", CMD_CP_KEYBOARD),
    ITEM("Energy Saver", CMD_CP_ENERGY),
    ITEM("Memory", CMD_CP_MEMORY),
    ITEM("TCP/IP & Network", CMD_CP_NETWORK), SEP,
    ITEM("Finder Preferences", CMD_PREFERENCES),
};
static MenuItem k_recent[2][18];
static char recent_labels[16][OSPI_PATH];
static int recent_count[2];
int menu_submenu(int command) {
    if (command == CMD_MENU_RECENT_APPS) return MENU_RECENT_APPS;
    if (command == CMD_MENU_RECENT_DOCS) return MENU_RECENT_DOCS;
    return command == CMD_MENU_ARRANGE ? MENU_ARRANGE :
        command == CMD_MENU_LABELS ? MENU_LABELS : command == CMD_MENU_PANELS ? MENU_PANELS : 0;
}
static MenuItem k_special[] = {
    ITEM("Empty Trash...", CMD_EMPTY_ASK), KEY("Eject", CMD_EJECT, "Cmd-E", 'e'),
    ITEM("Erase Disk...", CMD_ERASE), SEP, ITEM("Sleep", CMD_SLEEP),
    ITEM("Restart...", CMD_RESTART), ITEM("Shut Down", CMD_SHUTDOWN), SEP,
    KEY("Startup Animation", CMD_STARTUP_TOGGLE, "F9", 0),
    KEY("Preview Startup", CMD_STARTUP_PREVIEW, "F10", 0), ITEM("Interface Sounds", CMD_SOUND_TOGGLE),
    ITEM("Supported Audio Formats...", CMD_SUPPORTED_FORMATS),
};
static MenuItem k_help[] = {
    ITEM("OS999 Help", CMD_HELP), ITEM("Keyboard Shortcuts", CMD_HELP_KEYS), ITEM("What Works Today", CMD_HELP_STATUS),
};
static MenuItem k_windows[OSPI_MAX_WIN + 11];
static char active_app[OSPI_NAME] = "Finder", guest_name[OSPI_NAME], hide_app[OSPI_NAME + 8];
static char window_titles[OSPI_MAX_WIN][OSPI_NAME];
static char quit_guest[OSPI_NAME + 8];
static int window_count;
static int sound_enabled = 1;
void menu_set_sound_enabled(int enabled) { sound_enabled = !!enabled; }

static void update_items(Session *s, MenuItem *items, int count) {
    const Window *w = finder_current(s);
    int panel = appearance_panel_active() || s->media_focused || s->browser_focused;
    int folder = w && w->kind == WIN_FOLDER && !panel;
    int selected = 0;
    if (folder) for (int i = 0; i < w->nitems; i++) selected |= window_items(w)[i].selected;
    for (int i = 0; i < count; i++) {
        MenuItem *item = &items[i];
        if (!item->label) continue;
        int yes = 0, checked = 0;
        switch (item->cmd) {
        case CMD_MENU_ARRANGE: yes = folder; break;
        case CMD_MENU_LABELS: case CMD_EDIT_LABELS: yes = 1; break;
        case CMD_MENU_PANELS: case CMD_MENU_RECENT_APPS: case CMD_MENU_RECENT_DOCS: yes = 1; break;
        case CMD_NEW_FOLDER: yes = !w || !w->search_results; break;
        case CMD_ABOUT: case CMD_CONTROL_PANELS: case CMD_CP_GENERAL: case CMD_CP_SOUND:
        case CMD_CP_MONITORS: case CMD_CP_KEYBOARD: case CMD_CP_ENERGY:
        case CMD_CP_MEMORY: case CMD_CP_NETWORK:
        case CMD_APPEARANCE_DESKTOP: case CMD_INSTALL:
        case CMD_QUIT: case CMD_SHUTDOWN: case CMD_STARTUP_PREVIEW:
        case CMD_HELP: case CMD_HELP_KEYS: case CMD_HELP_STATUS: yes = 1; break;
        case CMD_STARTUP_TOGGLE: yes = 1; checked = s->host_startup_enabled; break;
        case CMD_SOUND_TOGGLE: yes = 1; checked = sound_enabled; break;
        case CMD_OPEN: yes = appearance_panel_can_open() || (!panel && (selected || s->desk_sel >= 0)); break;
        case CMD_CLOSE: yes = s->guest_focused || panel || (w && !w->desktop_surface); break;
        case CMD_CLOSE_ALL: yes = panel || s->nwin; break;
        case CMD_TERMINAL_SHELL: case CMD_TERMINAL_CONSOLE: case CMD_BROWSER:
        case CMD_MEDIA_PLAYER: case CMD_SUPPORTED_FORMATS: yes = 1; break;
        case CMD_PASTE: case CMD_TERM_CLEAR: case CMD_TERM_THEME:
            yes = w && w->kind == WIN_TERMINAL && !panel; break;
        case CMD_COPY:
            yes = !panel && ((w && w->kind == WIN_TERMINAL) || desktop_command_enabled(s, item->cmd)); break;
        case CMD_GET_INFO: case CMD_SELECT_ALL:
            yes = !panel && desktop_command_enabled(s, item->cmd); break;
        case CMD_CLEANUP: yes = folder; break;
        case CMD_EMPTY_ASK: yes = 1; break;
        case CMD_VIEW_ICONS: case CMD_VIEW_LIST: case CMD_VIEW_BUTTONS:
            yes = folder && (!w->desktop_surface || item->cmd == CMD_VIEW_ICONS);
            checked = folder && w->view_mode == (item->cmd == CMD_VIEW_LIST ? VIEW_LIST :
                       item->cmd == CMD_VIEW_BUTTONS ? VIEW_BUTTONS : VIEW_ICONS); break;
        case CMD_SORT_NAME: case CMD_SORT_KIND: case CMD_SORT_DATE: case CMD_SORT_SIZE:
        case CMD_SORT_CREATED: case CMD_SORT_LABEL:
            yes = folder;
            checked = folder && w->sort_mode == (item->cmd == CMD_SORT_KIND ? SORT_KIND :
                item->cmd == CMD_SORT_DATE ? SORT_DATE : item->cmd == CMD_SORT_SIZE ? SORT_SIZE :
                item->cmd == CMD_SORT_CREATED ? SORT_CREATED : item->cmd == CMD_SORT_LABEL ? SORT_LABEL : SORT_NAME); break;
        default: yes = finder_command_enabled(s, item->cmd); break;
        }
        if (item->cmd >= CMD_LABEL_BASE && item->cmd < CMD_LABEL_BASE + 8) {
            yes = finder_command_enabled(s, CMD_LABEL);
            int value = -1, mixed = 0;
            if (folder) for (int j = 0; j < w->nitems; j++) if (window_items(w)[j].selected) {
                if (value >= 0 && value != window_items(w)[j].label) mixed = 1;
                value = window_items(w)[j].label;
            }
            checked = !mixed && value == item->cmd - CMD_LABEL_BASE;
        }
        if (s->browser_focused && (item->cmd == CMD_COPY || item->cmd == CMD_PASTE ||
            item->cmd == CMD_SELECT_ALL || item->cmd == CMD_UNDO)) yes = 1;
        if (!finder_apps_command_allowed(s, item->cmd)) yes = checked = 0;
        if (item->cmd == CMD_NEW_FOLDER) {
            item->label = s->browser_focused ? "New Window" : "New Folder";
            if (s->browser_focused) yes = 1;
        }
        item->flags = (menu_submenu(item->cmd) ? MI_SUBMENU : 0) | (yes && !s->dialog ? 0 : MI_DISABLED) | (checked ? MI_CHECKED : 0);
        if (item->cmd == CMD_COPY) item->label = s->browser_focused ? "Copy" : w && w->kind == WIN_TERMINAL ? "Copy" : w && w->kind == WIN_TEXT ? "Copy Text" : "Copy Names";
        if (item->cmd == CMD_QUIT) item->label = s->browser_focused ? "Quit Browser" : s->media_focused ? "Quit OS999Amp" : s->guest_focused ? quit_guest : "Quit OS999";
    }
}
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
void menu_update(Session *s) {
    if (!s) return;
    snprintf(quit_guest, sizeof quit_guest, "Quit %s", s->guest_title[0] ? s->guest_title : "Application");
    for (int i = 0; i < 8; i++) k_labels[i].label = finder_label_name(i);
    update_items(s, k_pi, COUNT(k_pi)); update_items(s, k_file, COUNT(k_file));
    update_items(s, k_edit, COUNT(k_edit)); update_items(s, k_view, COUNT(k_view));
    update_items(s, k_special, COUNT(k_special)); update_items(s, k_help, COUNT(k_help));
    update_items(s, k_arrange, COUNT(k_arrange)); update_items(s, k_labels, COUNT(k_labels));
    update_items(s, k_panels, COUNT(k_panels));
    for (int kind = 0; kind < 2; kind++) {
        int n = 0;
        for (int i = 0; i < s->nrecent; i++) if (s->recent[i].application == kind) {
            snprintf(recent_labels[i], sizeof recent_labels[i], "%s", s->recent[i].path);
            k_recent[kind][n++] = (MenuItem){recent_labels[i], CMD_RECENT_BASE + i,
                s->dialog ? MI_DISABLED : 0, NULL, 0};
        }
        if (n) k_recent[kind][n++] = (MenuItem){0};
        int clear_disabled = n == 0 || s->dialog;
        k_recent[kind][n++] = (MenuItem){"Clear Menu", kind ? CMD_RECENT_CLEAR_APPS : CMD_RECENT_CLEAR_DOCS,
            clear_disabled ? MI_DISABLED : 0, NULL, 0};
        recent_count[kind] = n;
    }
    snprintf(active_app, sizeof active_app, "%s", s->browser_focused ? "Browser" : s->media_focused ? "OS999Amp" : s->guest_focused ? s->guest_title : "Finder");
    while (fb_text_width(active_app) > 160) {
        size_t n = strlen(active_app); if (!n) break;
        do { n--; } while (n && ((unsigned char)active_app[n] & 0xc0) == 0x80);
        active_app[n] = 0;
    }
    snprintf(guest_name, sizeof guest_name, "%s", s->guest_title);
    snprintf(hide_app, sizeof hide_app, "Hide %s", active_app);
    window_count = 0;
    int commands[] = {CMD_APP_HIDE, CMD_APP_HIDE_OTHERS, CMD_APP_SHOW_ALL};
    const char *names[] = {hide_app, "Hide Others", "Show All"};
    for (int i = 0; i < 3; i++) k_windows[window_count++] = (MenuItem){names[i], commands[i],
        finder_apps_enabled(s, commands[i]) ? 0 : MI_DISABLED, NULL, 0};
    k_windows[window_count++] = (MenuItem){"Application Switcher", CMD_APP_SWITCHER,
        s->dialog ? MI_DISABLED : 0, NULL, 0};
    k_windows[window_count++] = (MenuItem){0};
    k_windows[window_count++] = (MenuItem){"Finder", CMD_APP_FINDER,
        s->dialog ? MI_DISABLED : s->guest_focused || s->media_focused || s->browser_focused ? 0 : MI_CHECKED, NULL, 0};
    if (s->guest_active) k_windows[window_count++] = (MenuItem){guest_name, CMD_APP_GUEST,
        s->dialog ? MI_DISABLED : s->guest_focused ? MI_CHECKED : 0, NULL, 0};
    if (finder_apps_media_running(s)) k_windows[window_count++] = (MenuItem){"OS999Amp", CMD_APP_MEDIA,
        s->dialog ? MI_DISABLED : s->media_focused ? MI_CHECKED : 0, NULL, 0};
    if (s->browser_active) k_windows[window_count++] = (MenuItem){"Browser", CMD_APP_BROWSER,
        s->dialog ? MI_DISABLED : s->browser_focused ? MI_CHECKED : 0, NULL, 0};
    if (s->nwin) k_windows[window_count++] = (MenuItem){0};
    for (int i = 0; i < s->nwin; i++) {
        snprintf(window_titles[i], sizeof window_titles[i], "%s", s->win[i].title);
        k_windows[window_count++] = (MenuItem){window_titles[i], CMD_WINDOW_BASE + i,
            s->dialog ? MI_DISABLED : !s->guest_focused && !s->media_focused && !s->browser_focused && !s->desktop_focus && i == s->nwin - 1 ? MI_CHECKED : 0, NULL, 0};
    }
}

static MenuItem appearance[WALLPAPERS_PER_PAGE + 8];
static char wallpaper_labels[WALLPAPERS_PER_PAGE][OSPI_NAME];
static int wallpaper_page, appearance_count;

void menu_wallpaper_page(int page) {
    int count = desktop_wallpaper_count();
    int last = count > 0 ? (count - 1) / WALLPAPERS_PER_PAGE : 0;
    wallpaper_page = page < 0 ? 0 : page > last ? last : page;
    int current = desktop_wallpaper_current();
    int n = 0;
    appearance[n++] = (MenuItem){"Color Themes...", CMD_APPEARANCE_THEMES, 0, NULL, 0};
    appearance[n++] = (MenuItem){"Next Color Theme", CMD_THEME_NEXT, 0, "Cmd-T", 't'};
    appearance[n++] = (MenuItem){0};
    appearance[n++] = (MenuItem){"Plain Desktop", CMD_WALLPAPER_BASE,
                                current < 0 ? MI_CHECKED : 0, NULL, 0};
    appearance[n++] = (MenuItem){0};
    int start = wallpaper_page * WALLPAPERS_PER_PAGE;
    for (int i = 0; i < WALLPAPERS_PER_PAGE && start + i < count; i++) {
        int index = start + i;
        snprintf(wallpaper_labels[i], sizeof wallpaper_labels[i], "%02d %s",
                 index + 1, desktop_wallpaper_name(index));
        appearance[n++] = (MenuItem){wallpaper_labels[i], CMD_WALLPAPER_BASE + index + 1,
                                    index == current ? MI_CHECKED : 0, NULL, 0};
    }
    appearance[n++] = (MenuItem){0};
    appearance[n++] = (MenuItem){"Previous Wallpapers", CMD_WALLPAPER_PREVIOUS,
                                wallpaper_page ? 0 : MI_DISABLED, NULL, 0};
    appearance[n++] = (MenuItem){"Next Wallpapers", CMD_WALLPAPER_NEXT,
                                wallpaper_page < last ? 0 : MI_DISABLED, NULL, 0};
    appearance_count = n;
}

int menu_wallpaper_current_page(void) { return wallpaper_page; }

static const struct {
    const char *title;
    const MenuItem *items;
    int n;
} k_menus[] = {
    {"999", k_pi, COUNT(k_pi)},
    {"File", k_file, COUNT(k_file)},
    {"Edit", k_edit, COUNT(k_edit)},
    {"View", k_view, COUNT(k_view)},
    {"Special", k_special, COUNT(k_special)},
    {"Help", k_help, COUNT(k_help)},
    {"Appearance", NULL, 0},
    {"Finder", k_windows, 0},
};

int menu_count(void) { return (int)(sizeof k_menus / sizeof k_menus[0]); }

const char *menu_title(int index) {
    if (index == 7) return active_app;
    if (index < 0 || index >= menu_count()) {
        return "";
    }
    return k_menus[index].title;
}

int menu_nitems(int menu) {
    if (menu == MENU_RECENT_APPS || menu == MENU_RECENT_DOCS) return recent_count[menu == MENU_RECENT_APPS];
    if (menu == MENU_ARRANGE) return COUNT(k_arrange);
    if (menu == MENU_LABELS) return COUNT(k_labels);
    if (menu == MENU_PANELS) return COUNT(k_panels);
    if (menu == 7) return window_count;
    if (menu == MENU_APPEARANCE) {
        menu_wallpaper_page(wallpaper_page);
        return appearance_count;
    }
    if (menu < 0 || menu >= menu_count()) {
        return 0;
    }
    return k_menus[menu].n;
}

const MenuItem *menu_at(int menu, int item) {
    if (menu == MENU_RECENT_APPS || menu == MENU_RECENT_DOCS)
        return item >= 0 && item < menu_nitems(menu) ? &k_recent[menu == MENU_RECENT_APPS][item] : NULL;
    if (menu >= MENU_ARRANGE && menu <= MENU_PANELS) {
        if (item < 0 || item >= menu_nitems(menu)) return NULL;
        return &(menu == MENU_ARRANGE ? k_arrange : menu == MENU_LABELS ? k_labels : k_panels)[item];
    }
    if (menu == 7) return item >= 0 && item < window_count ? &k_windows[item] : NULL;
    if (menu == MENU_APPEARANCE) {
        menu_wallpaper_page(wallpaper_page);
        return item >= 0 && item < appearance_count ? &appearance[item] : NULL;
    }
    if (menu < 0 || menu >= menu_count() || item < 0 || item >= k_menus[menu].n) {
        return NULL;
    }
    return &k_menus[menu].items[item];
}

static int popup_width(int menu) {
    int w = fb_text_width(menu_title(menu)) + 24;
    for (int i = 0; i < menu_nitems(menu); i++) {
        const MenuItem *it = menu_at(menu, i);
        if (!it->label) {
            continue;
        }
        int iw = 28 + fb_text_width(it->label) + ((it->flags & MI_SUBMENU) ? 16 : 0);
        if (it->flags & MI_CHECKED) {
            iw += 16;
        }
        if (it->shortcut) {
            iw += 16 + fb_text_width(it->shortcut);
        }
        if (iw > w) {
            w = iw;
        }
    }
    return w > OSPI_W - 8 ? OSPI_W - 8 : w;
}

static int item_h(const MenuItem *it) {
    if (!it->label) {
        return 8;
    }
    int h = typeface_line_height(0) + 4;
    return h < 18 ? 18 : h;
}

void menu_title_rect(int index, Rect *r) {
    static const int order[] = {0, 1, 2, 3, 4, 6, 5, 7};
    int x = 6;
    for (int position = 0; position < menu_count(); position++) {
        int i = order[position];
        int width = i == 0 ? 34 : fb_text_width(menu_title(i)) + 18;
        if (i == index) {
            *r = (Rect){i == 7 ? OSPI_W - width - 8 : x, 0, width, 20}; return;
        }
        x += width;
    }
    *r = (Rect){0};
}

void menu_item_rect(int menu, int item, Rect *r) {
    Rect title;
    menu_title_rect(menu, &title);
    int y = 20;
    int w = popup_width(menu);
    if (menu >= MENU_ARRANGE && menu <= MENU_RECENT_DOCS) {
        int parent = menu == MENU_ARRANGE ? 3 : menu == MENU_LABELS ? 1 : 0;
        for (int i = 0; i < menu_nitems(parent); i++) if (menu_submenu(menu_at(parent, i)->cmd) == menu) {
            Rect row; menu_item_rect(parent, i, &row);
            title.x = row.x + row.w - 2; y = row.y;
            if (title.x + w > OSPI_W) title.x = row.x - w + 2;
            int height = 0;
            for (int j = 0; j < menu_nitems(menu); j++) height += item_h(menu_at(menu, j));
            if (y + height > OSPI_H) y = OSPI_H - height;
            if (y < 20) y = 20;
            break;
        }
    }
    if (title.x + w > OSPI_W) title.x = OSPI_W - w;
    if (title.x < 0) title.x = 0;
    for (int i = 0; i < menu_nitems(menu); i++) {
        const MenuItem *it = menu_at(menu, i);
        int h = item_h(it);
        if (i == item) {
            *r = (Rect){title.x, y, w, h};
            return;
        }
        y += h;
    }
    *r = (Rect){0, 0, 0, 0};
}

int menu_title_at(int x, int y) {
    if (y < 0 || y >= 20) {
        return -1;
    }
    for (int i = 0; i < menu_count(); i++) {
        Rect r;
        menu_title_rect(i, &r);
        if (rect_has(r, x, y)) {
            return i;
        }
    }
    return -1;
}

int menu_item_at(int menu, int x, int y) {
    if (menu < 0) {
        return -1;
    }
    for (int i = 0; i < menu_nitems(menu); i++) {
        Rect r;
        menu_item_rect(menu, i, &r);
        if (rect_has(r, x, y)) {
            return i;
        }
    }
    return -1;
}

int menu_cmd_for_key(int key) {
    for (int m = 0; m < menu_count(); m++) {
        for (int i = 0; i < menu_nitems(m); i++) {
            const MenuItem *it = menu_at(m, i);
            if (it->key && it->key == key && !(it->flags & MI_DISABLED)) {
                return it->cmd;
            }
        }
    }
    return CMD_NONE;
}
