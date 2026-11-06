#include "appearance_panel.h"
#include "desktop_wallpaper.h"
#include "desktop_assets.h"
#include "desktop_theme.h"

#include <stdio.h>

enum { PANEL_CLOSED, PANEL_CONTROLS, PANEL_APPEARANCE, ROW_HEIGHT = 21 };
static Session *owner;
static int mode, tab, page, theme_page;
static int context_kind;
static Rect context_box;

static uint32_t selection_color(void) {
    return desktop_theme_current() ? desktop_theme_color(THEME_ACCENT) : 0xFF344768u;
}

static uint32_t accent_text(void) {
    uint32_t c = selection_color();
    unsigned luma = ((c >> 16) & 255u) * 299u + ((c >> 8) & 255u) * 587u + (c & 255u) * 114u;
    return luma >= 150000u ? 0xFF000000u : 0xFFFFFFFFu;
}

static int theme_count(void) {
    int count = desktop_theme_count();
    int limit = CMD_THEME_LAST - CMD_THEME_BASE + 1;
    return count < 0 ? 0 : count > limit ? limit : count;
}

static int last_theme_page(void) {
    int count = theme_count();
    return count > 0 ? (count - 1) / 8 : 0;
}

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
    return (Rect){r.x + (r.w - 120) / 2, r.y + 47, 120, 66};
}
static Rect list_rect(Rect r) { return (Rect){r.x + 18, r.y + 96, r.w - 36, 233}; }
static Rect previous_rect(Rect r) { return (Rect){r.x + 18, r.y + 345, 105, 27}; }
static Rect next_rect(Rect r) { return (Rect){r.x + r.w - 123, r.y + 345, 105, 27}; }

void appearance_panel_close(void) {
    if (owner) owner->down = owner->drag = 0;
    owner = NULL;
    mode = PANEL_CLOSED;
    tab = page = theme_page = context_kind = 0;
}

int appearance_panel_active(void) { return mode != PANEL_CLOSED; }

void appearance_panel_open_controls(Session *s) {
    if (!s) return;
    owner = s;
    context_kind = 0;
    s->down = s->drag = 0;
    mode = PANEL_CONTROLS;
    s->menu = s->menu_hot = -1;
}

void appearance_panel_open(Session *s, int selected_tab) {
    if (!s) return;
    owner = s;
    context_kind = 0;
    s->down = s->drag = 0;
    mode = PANEL_APPEARANCE;
    tab = selected_tab == APPEARANCE_THEMES_TAB ? APPEARANCE_THEMES_TAB : APPEARANCE_DESKTOP_TAB;
    int current = desktop_wallpaper_current();
    page = current >= 0 ? current / WALLPAPERS_PER_PAGE : 0;
    if (page > last_page()) page = last_page();
    theme_page = desktop_theme_current() / 8;
    if (theme_page < 0 || theme_page > 1) theme_page = 0;
    s->menu = s->menu_hot = -1;
}

static void button(Fb *fb, Rect r, const char *title, int active, int enabled) {
    fb_fill(fb, r.x, r.y, r.w, r.h, active ? desktop_theme_color(THEME_PANEL) : desktop_theme_color(THEME_MENU));
    fb_frame(fb, r.x, r.y, r.w, r.h, enabled ? desktop_theme_color(THEME_FRAME) : (desktop_theme_current() ? desktop_theme_color(THEME_STRIPE_B) : C_GRAY));
    fb_hline(fb, r.x + 1, r.y + 1, r.w - 2, desktop_theme_color(THEME_PANEL));
    int x = r.x + (r.w - fb_text_width(title)) / 2;
    int y = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_text(fb, x, y, enabled ? desktop_theme_color(THEME_FOREGROUND) : desktop_theme_color(THEME_DISABLED), title);
}

static void frame(Fb *fb, Rect r, const char *title) {
    fb_fill(fb, r.x + 3, r.y + 3, r.w, r.h, 0xFF26304Au);
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FRAME));
    for (int y = 4; y < TITLE_H - 2; y += 2)
        fb_hline(fb, r.x + 3, r.y + y, r.w - 6, (desktop_theme_current() ? desktop_theme_color(THEME_STRIPE_B) : C_GRAY));
    int tw = fb_text_width(title);
    fb_fill(fb, r.x + (r.w - tw) / 2 - 5, r.y + 1, tw + 10, TITLE_H - 2, desktop_theme_color(THEME_MENU));
    fb_text(fb, r.x + (r.w - tw) / 2, r.y + 3, desktop_theme_color(THEME_FOREGROUND), title);
    fb_hline(fb, r.x, r.y + TITLE_H, r.w, desktop_theme_color(THEME_FRAME));
    Rect close = close_rect(r);
    fb_fill(fb, close.x, close.y, close.w, close.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, close.x, close.y, close.w, close.h, desktop_theme_color(THEME_FRAME));
}

static void desktop_tab(Fb *fb, Rect r) {
    fb_text(fb, r.x + 20, r.y + 74, desktop_theme_color(THEME_FOREGROUND), "Choose a desktop picture. Your choice is saved automatically.");
    Rect list = list_rect(r);
    fb_fill(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_FRAME));
    int current = desktop_wallpaper_current();
    for (int row = 0; row <= WALLPAPERS_PER_PAGE; row++) {
        int index = row ? page * WALLPAPERS_PER_PAGE + row - 1 : -1;
        if (index >= picture_count()) break;
        int y = list.y + 1 + row * ROW_HEIGHT;
        int selected = current == index;
        if (selected) fb_fill(fb, list.x + 1, y, list.w - 2, ROW_HEIGHT, selection_color());
        uint32_t color = selected ? accent_text() : desktop_theme_color(THEME_FOREGROUND);
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
    fb_text(fb, r.x + (r.w - fb_text_width(position)) / 2, r.y + 352, desktop_theme_color(THEME_FOREGROUND), position);
    fb_text(fb, r.x + 20, r.y + 389, (desktop_theme_current() ? desktop_theme_color(THEME_FOREGROUND) : 0xFF444444u), "Changes apply immediately.");
}

static void themes_tab(Fb *fb, Rect r) {
    uint32_t foreground = desktop_theme_color(THEME_FOREGROUND);
    fb_text(fb, r.x + 20, r.y + 74, foreground, "Color themes");
    Rect list = list_rect(r);
    fb_fill(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_FRAME));
    for (int row = 0; row < 8; row++) {
        int index = theme_page * 8 + row;
        if (index >= theme_count()) break;
        int y = list.y + 1 + row * 29;
        int selected = desktop_theme_current() == index;
        if (selected) fb_fill(fb, list.x + 1, y, list.w - 2, 29, selection_color());
        uint32_t ink = selected ? accent_text() : foreground;
        fb_clip(fb, list.x + 6, y, list.w - 12, 29);
        fb_text(fb, list.x + 8, y + 7, ink, selected ? "*" : " ");
        fb_text(fb, list.x + 26, y + 7, ink, desktop_theme_name(index));
        fb_unclip(fb);
    }
    button(fb, previous_rect(r), "Previous", 0, theme_page > 0);
    button(fb, next_rect(r), "Next", 0, theme_page < last_theme_page());
    char position[64];
    snprintf(position, sizeof position, "Page %d of %d", theme_page + 1, last_theme_page() + 1);
    fb_text(fb, r.x + (r.w - fb_text_width(position)) / 2, r.y + 352, foreground, position);
    fb_text(fb, r.x + 20, r.y + 389, foreground, "Color palettes only. Window shapes and controls stay the same.");
}

static void context_action(Session *s, int row) {
    int kind = context_kind;
    context_kind = 0;
    if (kind == 1 && row == 0) appearance_panel_open(s, APPEARANCE_DESKTOP_TAB);
    else if (kind == 2 && row >= 0 && row < 2) appearance_panel_open(s, row);
}

static void context_draw(Session *s, Fb *fb) {
    if (!context_kind) return;
    Rect r = context_box;
    fb_fill(fb, r.x + 3, r.y + 3, r.w, r.h, 0xFF26304Au);
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FRAME));
    int count = context_kind == 1 ? 1 : 2;
    for (int i = 0; i < count; i++) {
        Rect row = {r.x + 1, r.y + 1 + i * 24, r.w - 2, 24};
        int hot = rect_has(row, s->mx, s->my);
        if (hot) fb_fill(fb, row.x, row.y, row.w, row.h, desktop_theme_color(THEME_FOREGROUND));
        const char *label = context_kind == 1 ? "Open Appearance" : i ? "Themes" : "Desktop Pictures";
        fb_text(fb, row.x + 12, row.y + 4, hot ? desktop_theme_color(THEME_PANEL) : desktop_theme_color(THEME_FOREGROUND), label);
    }
}

int appearance_panel_context(Session *s, int x, int y) {
    if (!s || owner != s || mode == PANEL_CLOSED) return 0;
    s->down = s->drag = 0;
    s->menu = s->menu_hot = -1;
    context_kind = 0;
    Rect r = panel_rect(s);
    if (!rect_has(r, x, y) || y <= r.y + TITLE_H) return 1;
    context_kind = mode == PANEL_CONTROLS && rect_has(controls_appearance(r), x, y) ? 1 : 2;
    int count = context_kind == 1 ? 1 : 2;
    context_box = (Rect){x, y, 200, count * 24 + 2};
    int sw = s->sw > 0 ? s->sw : OSPI_W, sh = s->sh > 0 ? s->sh : OSPI_H;
    if (context_box.x + context_box.w > sw) context_box.x = sw - context_box.w;
    if (context_box.y + context_box.h > sh) context_box.y = sh - context_box.h;
    if (context_box.x < 0) context_box.x = 0;
    if (context_box.y < TITLE_H) context_box.y = TITLE_H;
    return 1;
}

void appearance_panel_draw(Session *s, Fb *fb) {
    if (!s || owner != s || mode == PANEL_CLOSED || !fb) return;
    Rect r = panel_rect(s);
    frame(fb, r, mode == PANEL_CONTROLS ? "Control Panels" : "Appearance");
    if (mode == PANEL_CONTROLS) {
        Rect icon = controls_appearance(r);
        int ix = icon.x + (icon.w - 32) / 2;
        if (!desktop_asset_draw(fb, ix, icon.y, "control_panel")) {
            fb_fill(fb, ix, icon.y, 32, 32, desktop_theme_color(THEME_PANEL));
            fb_frame(fb, ix, icon.y, 32, 32, desktop_theme_color(THEME_FRAME));
            for (int i = 0; i < 3; i++) {
                fb_fill(fb, ix + 6 + i * 9, icon.y + 5, 1, 22, desktop_theme_color(THEME_FOREGROUND));
                fb_fill(fb, ix + 3 + i * 9, icon.y + 9 + i * 4, 7, 5, desktop_theme_color(THEME_FOREGROUND));
            }
        }
        fb_text(fb, icon.x + (icon.w - fb_text_width("Appearance")) / 2,
                icon.y + 38, desktop_theme_color(THEME_FOREGROUND), "Appearance");
        fb_text(fb, r.x + 24, r.y + 131, desktop_theme_color(THEME_FOREGROUND), "Desktop pictures and visual settings");
        context_draw(s, fb);
        return;
    }
    button(fb, tab_rect(r, 0), "Desktop", tab == 0, 1);
    button(fb, tab_rect(r, 1), "Themes", tab == 1, 1);
    if (tab == APPEARANCE_DESKTOP_TAB) desktop_tab(fb, r);
    else themes_tab(fb, r);
    context_draw(s, fb);
}

int appearance_panel_down(Session *s, int x, int y) {
    if (!s || owner != s || mode == PANEL_CLOSED) return 0;
    if (context_kind) {
        s->down = s->drag = 0;
        if (x > context_box.x && x < context_box.x + context_box.w - 1 &&
            y > context_box.y && y < context_box.y + context_box.h - 1)
            context_action(s, (y - context_box.y - 1) / 24);
        else context_kind = 0;
        return 1;
    }
    if (y < TITLE_H) return 0;
    s->down = s->drag = 0;
    Rect r = panel_rect(s);
    if (rect_has(close_rect(r), x, y)) { appearance_panel_close(); return 1; }
    if (mode == PANEL_CONTROLS) {
        if (rect_has(controls_appearance(r), x, y)) appearance_panel_open(s, APPEARANCE_DESKTOP_TAB);
        return 1;
    }
    for (int i = 0; i < 2; i++)
        if (rect_has(tab_rect(r, i), x, y)) { tab = i; return 1; }
    if (tab == APPEARANCE_THEMES_TAB) {
        if (rect_has(previous_rect(r), x, y) && theme_page > 0) theme_page--;
        else if (rect_has(next_rect(r), x, y) && theme_page < last_theme_page()) theme_page++;
        else {
            Rect list = list_rect(r);
            if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
                int row = (y - list.y - 1) / 29;
                int index = theme_page * 8 + row;
                if (row < 8 && index < theme_count()) session_command(s, CMD_THEME_BASE + index);
            }
        }
        return 1;
    }
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
    if ((mods & MOD_CMD) && (key == 'q' || key == 'Q' || key == 't' || key == 'T')) return 0;
    if (context_kind && key == 27) { context_kind = 0; return 1; }
    if (context_kind && (key == '\r' || key == '\n')) { context_action(s, 0); return 1; }
    if (key == 27 || ((mods & MOD_CMD) && (key == 'w' || key == 'W')))
        appearance_panel_close();
    else if (mode == PANEL_CONTROLS && (key == '\r' || key == '\n'))
        appearance_panel_open(s, APPEARANCE_DESKTOP_TAB);
    else if (mode == PANEL_APPEARANCE && key == '\t') tab = !tab;
    return 1;
}
