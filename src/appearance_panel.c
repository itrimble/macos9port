#include "appearance_panel.h"
#include "desktop_wallpaper.h"

#include <stdio.h>

enum { PANEL_CLOSED, PANEL_CONTROLS, PANEL_APPEARANCE, ROW_HEIGHT = 21 };
static Session *owner;
static int mode, tab, page;

static int picture_count(void) {
    int count = desktop_wallpaper_count();
    int limit = CMD_WALLPAPER_LAST - CMD_WALLPAPER_BASE;
    return count < 0 ? 0 : count > limit ? limit : count;
}

static int last_page(void) {
    int count = picture_count();
    return count > 0 ? (count - 1) / WALLPAPERS_PER_PAGE : 0;
}

static Rect panel_rect(const Session *s) {
    int sw = s->sw > 0 ? s->sw : OSPI_W;
    int sh = s->sh > 0 ? s->sh : OSPI_H;
    int w = mode == PANEL_CONTROLS ? 340 : 600;
    int h = mode == PANEL_CONTROLS ? 170 : 420;
    int x = (sw - w) / 2, y = (sh - h) / 2;
    if (x < 0) x = 0;
    if (y < TITLE_H) y = TITLE_H;
    return (Rect){x, y, w, h};
}

static Rect close_rect(Rect r) { return (Rect){r.x + 8, r.y + 5, 12, 12}; }
static Rect tab_rect(Rect r, int index) {
    return (Rect){r.x + 18 + index * 116, r.y + 35, 112, 27};
}
static Rect controls_appearance(Rect r) {
    return (Rect){r.x + 24, r.y + 47, r.w - 48, 66};
}
static Rect list_rect(Rect r) { return (Rect){r.x + 18, r.y + 96, r.w - 36, 233}; }
static Rect previous_rect(Rect r) { return (Rect){r.x + 18, r.y + 345, 105, 27}; }
static Rect next_rect(Rect r) { return (Rect){r.x + r.w - 123, r.y + 345, 105, 27}; }

void appearance_panel_close(void) {
    if (owner) owner->down = owner->drag = 0;
    owner = NULL;
    mode = PANEL_CLOSED;
    tab = page = 0;
}

int appearance_panel_active(void) { return mode != PANEL_CLOSED; }

void appearance_panel_open_controls(Session *s) {
    if (!s) return;
    owner = s;
    s->down = s->drag = 0;
    mode = PANEL_CONTROLS;
    s->menu = s->menu_hot = -1;
}

void appearance_panel_open(Session *s, int selected_tab) {
    if (!s) return;
    owner = s;
    s->down = s->drag = 0;
    mode = PANEL_APPEARANCE;
    tab = selected_tab == APPEARANCE_THEMES_TAB ? APPEARANCE_THEMES_TAB : APPEARANCE_DESKTOP_TAB;
    int current = desktop_wallpaper_current();
    page = current >= 0 ? current / WALLPAPERS_PER_PAGE : 0;
    if (page > last_page()) page = last_page();
    s->menu = s->menu_hot = -1;
}

static void button(Fb *fb, Rect r, const char *title, int active, int enabled) {
    fb_fill(fb, r.x, r.y, r.w, r.h, active ? C_WHITE : C_MENU);
    fb_frame(fb, r.x, r.y, r.w, r.h, enabled ? C_BLACK : C_GRAY);
    fb_hline(fb, r.x + 1, r.y + 1, r.w - 2, C_WHITE);
    int x = r.x + (r.w - fb_text_width(title)) / 2;
    int y = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_text(fb, x, y, enabled ? C_BLACK : C_DISABLED, title);
}

static void frame(Fb *fb, Rect r, const char *title) {
    fb_fill(fb, r.x + 3, r.y + 3, r.w, r.h, 0xFF26304Au);
    fb_fill(fb, r.x, r.y, r.w, r.h, C_MENU);
    fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
    for (int y = 4; y < TITLE_H - 2; y += 2)
        fb_hline(fb, r.x + 3, r.y + y, r.w - 6, C_GRAY);
    int tw = fb_text_width(title);
    fb_fill(fb, r.x + (r.w - tw) / 2 - 5, r.y + 1, tw + 10, TITLE_H - 2, C_MENU);
    fb_text(fb, r.x + (r.w - tw) / 2, r.y + 3, C_BLACK, title);
    fb_hline(fb, r.x, r.y + TITLE_H, r.w, C_BLACK);
    Rect close = close_rect(r);
    fb_fill(fb, close.x, close.y, close.w, close.h, C_WHITE);
    fb_frame(fb, close.x, close.y, close.w, close.h, C_BLACK);
}

static void desktop_tab(Fb *fb, Rect r) {
    fb_text(fb, r.x + 20, r.y + 74, C_BLACK, "Choose a desktop picture. Your choice is saved automatically.");
    Rect list = list_rect(r);
    fb_fill(fb, list.x, list.y, list.w, list.h, C_WHITE);
    fb_frame(fb, list.x, list.y, list.w, list.h, C_BLACK);
    int current = desktop_wallpaper_current();
    for (int row = 0; row <= WALLPAPERS_PER_PAGE; row++) {
        int index = row ? page * WALLPAPERS_PER_PAGE + row - 1 : -1;
        if (index >= picture_count()) break;
        int y = list.y + 1 + row * ROW_HEIGHT;
        int selected = current == index;
        if (selected) fb_fill(fb, list.x + 1, y, list.w - 2, ROW_HEIGHT, 0xFF344768u);
        uint32_t color = selected ? C_WHITE : C_BLACK;
        char label[OSPI_NAME + 12];
        if (index < 0) snprintf(label, sizeof label, "Plain Desktop");
        else snprintf(label, sizeof label, "%02d  %s", index + 1, desktop_wallpaper_name(index));
        fb_clip(fb, list.x + 6, y, list.w - 12, ROW_HEIGHT);
        fb_text(fb, list.x + 8, y + 3, color, selected ? "*" : " ");
        fb_text(fb, list.x + 26, y + 3, color, label);
        fb_unclip(fb);
    }
    button(fb, previous_rect(r), "Previous", 0, page > 0);
    button(fb, next_rect(r), "Next", 0, page < last_page());
    char position[64];
    snprintf(position, sizeof position, "Page %d of %d", page + 1, last_page() + 1);
    fb_text(fb, r.x + (r.w - fb_text_width(position)) / 2, r.y + 352, C_BLACK, position);
    fb_text(fb, r.x + 20, r.y + 389, 0xFF444444u, "Changes apply immediately.");
}

void appearance_panel_draw(Session *s, Fb *fb) {
    if (!s || owner != s || mode == PANEL_CLOSED || !fb) return;
    Rect r = panel_rect(s);
    frame(fb, r, mode == PANEL_CONTROLS ? "Control Panels" : "Appearance");
    if (mode == PANEL_CONTROLS) {
        button(fb, controls_appearance(r), "Appearance", 0, 1);
        fb_text(fb, r.x + 24, r.y + 131, C_BLACK, "Desktop pictures and visual settings");
        return;
    }
    button(fb, tab_rect(r, 0), "Desktop", tab == 0, 1);
    button(fb, tab_rect(r, 1), "Themes", tab == 1, 1);
    if (tab == APPEARANCE_DESKTOP_TAB) desktop_tab(fb, r);
    else {
        fb_text(fb, r.x + 24, r.y + 98, C_BLACK, "Themes are not installed yet.");
        fb_text(fb, r.x + 24, r.y + 128, C_BLACK, "No additional themes are available.");
        fb_text(fb, r.x + 24, r.y + 158, C_BLACK, "Your current window style is unchanged.");
        fb_text(fb, r.x + 24, r.y + 198, C_BLACK, "Use Desktop to choose from the installed pictures.");
    }
}

int appearance_panel_down(Session *s, int x, int y) {
    if (!s || owner != s || mode == PANEL_CLOSED || y < TITLE_H) return 0;
    s->down = s->drag = 0;
    Rect r = panel_rect(s);
    if (rect_has(close_rect(r), x, y)) { appearance_panel_close(); return 1; }
    if (mode == PANEL_CONTROLS) {
        if (rect_has(controls_appearance(r), x, y)) appearance_panel_open(s, APPEARANCE_DESKTOP_TAB);
        return 1;
    }
    for (int i = 0; i < 2; i++)
        if (rect_has(tab_rect(r, i), x, y)) { tab = i; return 1; }
    if (tab == APPEARANCE_THEMES_TAB) return 1;
    if (rect_has(previous_rect(r), x, y) && page > 0) page--;
    else if (rect_has(next_rect(r), x, y) && page < last_page()) page++;
    else {
        Rect list = list_rect(r);
        if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
            int row = (y - list.y - 1) / ROW_HEIGHT;
            int index = row ? page * WALLPAPERS_PER_PAGE + row - 1 : -1;
            if (row <= WALLPAPERS_PER_PAGE && index < picture_count())
                session_command(s, CMD_WALLPAPER_BASE + index + 1);
        }
    }
    return 1;
}

int appearance_panel_key(Session *s, int key, int mods) {
    if (!s || owner != s || mode == PANEL_CLOSED) return 0;
    if ((mods & MOD_CMD) && (key == 'q' || key == 'Q')) return 0;
    if (key == 27 || ((mods & MOD_CMD) && (key == 'w' || key == 'W')))
        appearance_panel_close();
    else if (mode == PANEL_CONTROLS && (key == '\r' || key == '\n'))
        appearance_panel_open(s, APPEARANCE_DESKTOP_TAB);
    else if (mode == PANEL_APPEARANCE && key == '\t') tab = !tab;
    return 1;
}
