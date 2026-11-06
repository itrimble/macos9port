#include "draw.h"
#include "ospi.h"
#include "desktop_wallpaper.h"
#include <stdio.h>

static const MenuItem k_pi[] = {
    {"About This Mac", CMD_ABOUT, 0, NULL, 0},
    {NULL, 0, 0, NULL, 0},
    {"Shut Down", CMD_SHUTDOWN, 0, NULL, 0},
};

static const MenuItem k_file[] = {
    {"New Folder", CMD_NEW_FOLDER, 0, "Cmd-N", 'n'},
    {"Open", CMD_OPEN, 0, "Cmd-O", 'o'},
    {"Close", CMD_CLOSE, 0, "Cmd-W", 'w'},
    {"Install from Garden", CMD_INSTALL, 0, NULL, 0},
    {NULL, 0, 0, NULL, 0},
    {"Quit", CMD_QUIT, 0, "Cmd-Q", 'q'},
};

static const MenuItem k_edit[] = {
    {"Undo", CMD_NONE, MI_DISABLED, "Cmd-Z", 0},
    {NULL, 0, 0, NULL, 0},
    {"Cut", CMD_NONE, MI_DISABLED, "Cmd-X", 0},
    {"Copy", CMD_NONE, MI_DISABLED, "Cmd-C", 0},
    {"Paste", CMD_NONE, MI_DISABLED, "Cmd-V", 0},
};

static const MenuItem k_view[] = {
    {"by Icon", CMD_NONE, MI_CHECKED, NULL, 0},
    {"Clean Up", CMD_CLEANUP, 0, NULL, 0},
};

static MenuItem k_special[] = {
    {"Empty Trash", CMD_EMPTY_ASK, 0, NULL, 0},
    {"Clean Up", CMD_CLEANUP, 0, NULL, 0},
    {"Startup Animation: Toggle", CMD_STARTUP_TOGGLE, 0, "F9", 0},
    {"Preview Startup", CMD_STARTUP_PREVIEW, 0, "F10", 0},
    {"Interface Sounds", CMD_SOUND_TOGGLE, MI_CHECKED, NULL, 0},
};

void menu_set_sound_enabled(int enabled) {
    k_special[4].flags = enabled ? MI_CHECKED : 0;
}

static const MenuItem k_help[] = {
    {"How to Use OS999 Stylized", CMD_HELP, 0, NULL, 0},
};

static MenuItem appearance[WALLPAPERS_PER_PAGE + 5];
static char wallpaper_labels[WALLPAPERS_PER_PAGE][OSPI_NAME];
static int wallpaper_page, appearance_count;

void menu_wallpaper_page(int page) {
    int count = desktop_wallpaper_count();
    int last = count > 0 ? (count - 1) / WALLPAPERS_PER_PAGE : 0;
    wallpaper_page = page < 0 ? 0 : page > last ? last : page;
    int current = desktop_wallpaper_current();
    int n = 0;
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
    {"999", k_pi, 3},
    {"File", k_file, 6},
    {"Edit", k_edit, 5},
    {"View", k_view, 2},
    {"Special", k_special, 5},
    {"Help", k_help, 1},
    {"Appearance", NULL, 0},
};

int menu_count(void) { return (int)(sizeof k_menus / sizeof k_menus[0]); }

const char *menu_title(int index) {
    if (index < 0 || index >= menu_count()) {
        return "";
    }
    return k_menus[index].title;
}

int menu_nitems(int menu) {
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
        int iw = 28 + fb_text_width(it->label);
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
    return w;
}

static int item_h(const MenuItem *it) {
    if (!it->label) {
        return 8;
    }
    int h = typeface_line_height(0) + 4;
    return h < 18 ? 18 : h;
}

void menu_title_rect(int index, Rect *r) {
    int x = 6;
    for (int i = 0; i < menu_count(); i++) {
        int tw = fb_text_width(menu_title(i));
        int w = i == 0 ? 34 : tw + 18;
        if (i == index) {
            *r = (Rect){x, 0, w, 20};
            return;
        }
        x += w;
    }
    *r = (Rect){0, 0, 0, 0};
}

void menu_item_rect(int menu, int item, Rect *r) {
    Rect title;
    menu_title_rect(menu, &title);
    int y = 20;
    int w = popup_width(menu);
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
