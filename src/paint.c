#include "desktop_info.h"
#include "notes.h"
#include "folder_view.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "desktop_gesture.h"
#include "session_int.h"
#include "desktop_theme.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "desktop_wallpaper.h"
#include "draw.h"
#include "finder_edit.h"
#include "desktop_assets.h"
#include "mac_icon.h"
#include "movie_window.h"
#include "font_book_window.h"
#include "os999_startup.h"
#include "ospi.h"
#include "term.h"
#include "finder_settings.h"
#include "finder_labels.h"
#include "finder_selection.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void menu_paint_popup(Fb *fb, int menu, int hot_row);
void menu_paint_flash(Fb *fb, const Session *s);
void menu_paint_dialog_caret(Fb *fb, const Session *s, int x, int y);

static void format_space(char *out, size_t n, uint64_t b) {
    if (b >= 10000000000ull) {
        snprintf(out, n, "%.0f GB", (double)b / 1000000000.0);
    } else if (b >= 1000000000ull) {
        snprintf(out, n, "%.1f GB", (double)b / 1000000000.0);
    } else if (b >= 1000000ull) {
        snprintf(out, n, "%.1f MB", (double)b / 1000000.0);
    } else if (b >= 1000ull) {
        snprintf(out, n, "%.1f KB", (double)b / 1000.0);
    } else {
        snprintf(out, n, "%llu bytes", (unsigned long long)b);
    }
}

static void icon_folder(Fb *fb, int x, int y, int system) {
    if (desktop_asset_draw(fb, x, y, system ? "system_folder" : "folder")) return;
    const uint32_t shadow = 0xFF252746u, light = 0xFFD8D5FFu;
    fb_fill(fb, x + 4, y + 27, 25, 3, shadow);
    fb_fill(fb, x + 3, y + 7, 16, 7, C_FOLDER_EDGE);
    fb_fill(fb, x + 4, y + 8, 13, 4, light);
    fb_fill(fb, x + 2, y + 12, 29, 16, C_FOLDER_EDGE);
    fb_fill(fb, x + 3, y + 13, 27, 13, C_FOLDER);
    fb_hline(fb, x + 4, y + 13, 25, light);
    fb_hline(fb, x + 3, y + 15, 27, 0xFFB8B5FFu);
    fb_fill(fb, x + 3, y + 26, 26, 1, 0xFF7776C4u);
    if (system) {
        fb_fill(fb, x + 10, y + 17, 12, 7, 0xFF49477Au);
        fb_text(fb, x + 12, y + 17, C_WHITE, "9");
    }
}

static void icon_doc(Fb *fb, int x, int y) {
    if (desktop_asset_draw(fb, x, y, "document")) return;
    fb_fill(fb, x + 10, y + 5, 16, 24, 0xFF777777u);
    fb_fill(fb, x + 8, y + 3, 16, 24, 0xFF353535u);
    fb_fill(fb, x + 9, y + 4, 14, 22, C_WHITE);
    fb_fill(fb, x + 18, y + 4, 5, 5, 0xFFD0D0D0u);
    fb_hline(fb, x + 18, y + 8, 5, 0xFF858585u);
    fb_fill(fb, x + 11, y + 12, 9, 2, 0xFF6589B9u);
    fb_hline(fb, x + 11, y + 17, 9, 0xFF9A9A9Au);
    fb_hline(fb, x + 11, y + 20, 9, 0xFF9A9A9Au);
    fb_hline(fb, x + 11, y + 23, 6, 0xFF9A9A9Au);
}

static void icon_disk(Fb *fb, int x, int y) {
    if (desktop_asset_draw(fb, x, y, "hard_disk")) return;
    fb_fill(fb, x + 4, y + 26, 25, 3, 0xFF272727u);
    fb_fill(fb, x + 3, y + 7, 26, 20, 0xFF33383Fu);
    fb_fill(fb, x + 4, y + 8, 24, 16, 0xFFD5DCE3u);
    fb_hline(fb, x + 5, y + 9, 22, C_WHITE);
    fb_fill(fb, x + 6, y + 12, 20, 7, 0xFFBAC5CEu);
    fb_fill(fb, x + 7, y + 20, 17, 2, 0xFF59636Du);
    fb_fill(fb, x + 8, y + 23, 4, 2, 0xFF777F87u);
    fb_fill(fb, x + 21, y + 23, 4, 2, 0xFF777F87u);
    fb_fill(fb, x + 23, y + 14, 2, 2, 0xFF5CCB78u);
}

static void icon_trash(Fb *fb, int x, int y) {
    if (desktop_asset_draw(fb, x, y, "trash_empty")) return;
    fb_fill(fb, x + 8, y + 27, 17, 2, 0xFF555555u);
    fb_fill(fb, x + 12, y + 4, 9, 3, 0xFFB7C1C9u);
    fb_frame(fb, x + 11, y + 3, 11, 5, 0xFF30343Au);
    fb_fill(fb, x + 6, y + 8, 21, 4, 0xFFE3E9EDu);
    fb_frame(fb, x + 5, y + 8, 22, 4, 0xFF30343Au);
    for (int row = 0; row < 14; row++) {
        int inset = row / 7;
        fb_fill(fb, x + 7 + inset, y + 12 + row, 18 - inset * 2, 1,
                (row & 1) ? 0xFFE6EBEFu : 0xFFF8FAFBu);
    }
    fb_fill(fb, x + 7, y + 12, 1, 14, 0xFF30343Au);
    fb_fill(fb, x + 24, y + 12, 1, 14, 0xFF30343Au);
    fb_fill(fb, x + 11, y + 15, 1, 9, 0xFF88949Du);
    fb_fill(fb, x + 15, y + 15, 1, 9, 0xFF88949Du);
    fb_fill(fb, x + 19, y + 15, 1, 9, 0xFF88949Du);
}

static void icon_application(Fb *fb, int x, int y) {
    if (desktop_asset_draw(fb, x, y, "application")) return;
    fb_fill(fb, x + 4, y + 27, 26, 3, 0xFF272746u);
    fb_fill(fb, x + 3, y + 3, 26, 21, 0xFF343343u);
    fb_fill(fb, x + 5, y + 5, 22, 17, 0xFFD4D7E2u);
    fb_fill(fb, x + 7, y + 7, 18, 12, 0xFF253D72u);
    fb_hline(fb, x + 8, y + 8, 16, 0xFF607FC2u);
    fb_fill(fb, x + 10, y + 11, 3, 3, 0xFFFFD45Du);
    fb_fill(fb, x + 17, y + 10, 5, 2, 0xFF8BE0D6u);
    fb_fill(fb, x + 15, y + 14, 7, 3, 0xFF8C91D9u);
    fb_fill(fb, x + 13, y + 24, 7, 3, 0xFF8E94A3u);
    fb_fill(fb, x + 10, y + 27, 13, 2, 0xFFC4CAD3u);
}

static void guest_icon(Fb *fb, int x, int y, const Item *item) {
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int mask_bit = row * 32 + col;
            if (!(item->guest_icon_mask[mask_bit >> 3] & (0x80u >> (mask_bit & 7)))) continue;
            fb_fill(fb, x + col, y + row, 1, 1,
                    mac_icon_color(item->guest_icon[mask_bit]));
        }
    }
}

static void icon_winamp(Fb *fb, int x, int y) {
    fb_fill(fb, x + 4, y + 27, 25, 3, 0xFF181C28u);
    fb_fill(fb, x + 3, y + 4, 26, 24, 0xFF232735u);
    fb_frame(fb, x + 3, y + 4, 26, 24, 0xFFD9A94Au);
    fb_fill(fb, x + 5, y + 6, 22, 7, 0xFF080C10u);
    fb_fill(fb, x + 7, y + 8, 4, 3, 0xFF00E84Au);
    fb_fill(fb, x + 13, y + 7, 3, 4, 0xFFF8E018u);
    fb_fill(fb, x + 18, y + 9, 3, 2, 0xFFFF2818u);
    /* Golden-orange Winamp lightning bolt */
    fb_fill(fb, x + 17, y + 13, 5, 3, 0xFFFFB820u);
    fb_fill(fb, x + 13, y + 16, 7, 3, 0xFFFF9810u);
    fb_fill(fb, x + 15, y + 19, 6, 3, 0xFFFFB820u);
    fb_fill(fb, x + 11, y + 22, 5, 3, 0xFFFF6810u);
}

static void draw_kind(Fb *fb, int x, int y, const Item *item, const char *parent) {
    if (item->kind == ITEM_APPLICATION && item->has_guest_icon) {
        guest_icon(fb, x, y, item);
        return;
    }
    if (desktop_asset_draw(fb, x, y, desktop_asset_name(item, parent))) return;
    if (item->kind == ITEM_SYSTEM) {
        icon_folder(fb, x, y, 1);
    } else if (item->kind == ITEM_FOLDER) {
        icon_folder(fb, x, y, 0);
    } else if (strcmp(item->name, "OS999 Amp") == 0 ||
               (session_media_hooks() && session_media_hooks()->is_media_file &&
                strrchr(item->name, '.'))) {
        const char *ext = strrchr(item->name, '.');
        if (strcmp(item->name, "OS999 Amp") == 0 ||
            (ext && (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".flac") == 0 ||
                     strcmp(ext, ".m4a") == 0 || strcmp(ext, ".wav") == 0 ||
                     strcmp(ext, ".mid") == 0 || strcmp(ext, ".ogg") == 0 ||
                     strcmp(ext, ".opus") == 0))) {
            icon_winamp(fb, x, y);
        } else {
            icon_doc(fb, x, y);
        }
    } else if (item->kind == ITEM_APPLICATION) {
        icon_application(fb, x, y);
    } else {
        icon_doc(fb, x, y);
    }
}

static int text_y(int box_y, int box_h) {
    int lh = typeface_line_height(0);
    int y = box_y + (box_h - lh) / 2;
    return y < box_y ? box_y : y;
}

static void draw_label(Fb *fb, int cx, int y, const char *name, int selected, int desktop, int label) {
    char fitted[OSPI_NAME + 4];
    if (!desktop) name = finder_fit_label(name, fitted, sizeof fitted);
    int tw = fb_text_width(name);
    int x = cx - tw / 2;
    int lh = typeface_line_height(0);
    if (selected) {
        fb_fill(fb, x - 2, y - 1, tw + 4, lh + 2, C_BLACK);
        fb_text(fb, x, y, C_WHITE, name);
    } else if (desktop) {
        if (desktop_wallpaper_current() >= 0 || desktop_theme_current() > 0)
            fb_fill(fb, x - 2, y - 1, tw + 4, lh + 2, 0xFF26304Au);
        fb_text(fb, x + 1, y + 1, C_BLACK, name);
        fb_text(fb, x, y, C_WHITE, name);
    } else {
        fb_text(fb, x, y, label > 0 ? finder_label_ink(label) : desktop_theme_color(THEME_FOREGROUND), name);
    }
}

void session_desktop_icons_paint(Fb *fb, const Session *s) {
    for (int i = 0; i < s->desktop.nitems; i++) {
        int x, y; if (ospi_item_center(s, OSPI_DESKTOP_WINDOW, i, &x, &y)) continue;
        const Item *item = &window_items(&s->desktop)[i];
        if (!desktop_item_visible(&s->desktop, i)) continue;
        draw_kind(fb, x - 16, y - 16, item, s->desktop.rel);
        if (item->selected) fb_frame(fb, x - 18, y - 18, 36, 36, C_BLACK);
        draw_label(fb, x, y + 18, item->name, item->selected, 1, item->label);
    }
    const char *names[2] = {"OS999 HD", "Trash"};
    for (int which = 0; which < 2; which++) {
        Rect icon, hit;
        session_desktop_icon_rect(s, which, &icon, &hit);
        (void)hit;
        if (which == 0) {
            icon_disk(fb, icon.x, icon.y);
        } else {
            icon_trash(fb, icon.x, icon.y);
        }
        if (s->desk_sel == which) {
            fb_frame(fb, icon.x - 2, icon.y - 2, icon.w + 4, icon.h + 4, C_BLACK);
        }
        draw_label(fb, icon.x + 16, icon.y + 34, names[which], s->desk_sel == which, 1, 0);
    }
}

static void stripes(Fb *fb, Rect title) {
    fb_fill(fb, title.x, title.y, title.w, title.h, desktop_theme_color(THEME_FOREGROUND));
    for (int i = 1; i < title.h - 1; i++) {
        uint32_t c = (i & 1) ? desktop_theme_color(THEME_STRIPE_B) : desktop_theme_color(THEME_STRIPE_A);
        if (i == 1) {
            c = desktop_theme_color(THEME_PANEL);
        } else if (i == title.h - 2) {
            c = 0xFFDCDCDCu;
        }
        fb_hline(fb, title.x + 1, title.y + i, title.w - 2, c);
    }
}

static int held(const Session *s, int win, int zone, Rect r) {
    return s->down && s->press_win == win && s->press_zone == zone && rect_has(r, s->mx, s->my);
}

static void rbox(Fb *fb, Rect r, int invert) {
    fb_fill(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, r.x, r.y, r.w, r.h, desktop_theme_color(THEME_FOREGROUND));
    if (invert) {
        fb_invert(fb, r.x + 1, r.y + 1, r.w - 2, r.h - 2);
    }
}

static void paint_scroll(Fb *fb, const Window *w, const Layout *lay) {
    fb_fill(fb, lay->vbar.x, lay->vbar.y, lay->vbar.w, lay->vbar.h, desktop_theme_color(THEME_TRACK));
    fb_fill(fb, lay->hbar.x, lay->hbar.y, lay->hbar.w, lay->hbar.h, desktop_theme_color(THEME_TRACK));
    if (lay->vbar.h > 36) {
        rbox(fb, (Rect){lay->vbar.x, lay->vbar.y, lay->vbar.w, 16}, 0);
        rbox(fb, (Rect){lay->vbar.x, lay->vbar.y + lay->vbar.h - 16, lay->vbar.w, 16}, 0);
        for (int i = 0; i < 4; i++) {
            fb_hline(fb, lay->vbar.x + 8 - i, lay->vbar.y + 6 + i, i * 2 + 1, desktop_theme_color(THEME_FOREGROUND));
            fb_hline(fb, lay->vbar.x + 8 - i, lay->vbar.y + lay->vbar.h - 7 - i, i * 2 + 1, desktop_theme_color(THEME_FOREGROUND));
        }
        int maxs = window_max_scroll(w);
        if (maxs > 0) {
            int track = lay->vbar.h - 32;
            int thumb = track * lay->vbar.h / (lay->vbar.h + maxs);
            if (thumb < 14) {
                thumb = 14;
            }
            if (thumb > track) {
                thumb = track;
            }
            int travel = track - thumb;
            int ty = lay->vbar.y + 16;
            if (maxs > 0 && travel > 0) {
                ty += w->scroll * travel / maxs;
            }
            rbox(fb, (Rect){lay->vbar.x + 1, ty, lay->vbar.w - 2, thumb}, 0);
        }
    }
    fb_fill(fb, lay->grow.x, lay->grow.y, lay->grow.w, lay->grow.h, desktop_theme_color(THEME_MENU));
    fb_frame(fb, lay->grow.x, lay->grow.y, lay->grow.w, lay->grow.h, desktop_theme_color(THEME_FOREGROUND));
    for (int i = 3; i <= 11; i += 3) {
        for (int k = 0; k <= i; k++) {
            fb_fill(fb, lay->grow.x + lay->grow.w - 4 - k, lay->grow.y + 3 + i - k, 1, 1, 0xFF6A6A6Au);
        }
    }
}

static void paint_items(Fb *fb, const Session *s, const Window *w, const Layout *lay) {
    if (w->view_mode == VIEW_LIST) {
        folder_view_draw_list(fb, w, lay, draw_kind);
        return;
    }
    if (w->nitems == 0) {
        fb_text(fb, lay->content.x + 12, lay->content.y + 12, desktop_theme_color(THEME_DISABLED), "This folder is empty.");
        return;
    }
    int cols = lay->content.w / CELL_W;
    if (cols < 1) {
        cols = 1;
    }
    for (int i = 0; i < w->nitems; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = lay->content.x + col * CELL_W + (CELL_W - 32) / 2;
        int y = lay->content.y + row * CELL_H + 4 - w->scroll;
        if (w->view_mode == VIEW_BUTTONS) {
            fb_fill(fb, x - 34, y - 2, CELL_W - 8, CELL_H - 6, desktop_theme_color(THEME_MENU));
            fb_frame(fb, x - 34, y - 2, CELL_W - 8, CELL_H - 6, desktop_theme_color(THEME_FOREGROUND));
        }
        draw_kind(fb, x, y, &window_items(w)[i], w->rel);
        if (window_items(w)[i].selected) {
            fb_frame(fb, x - 2, y - 2, 36, 36, desktop_theme_color(THEME_FOREGROUND));
        }
        if (window_items(w)[i].label > 0) {
            fb_fill(fb, x - 32, y + 33, CELL_W - 8, 20, finder_label_color(window_items(w)[i].label));
        }
        if (!finder_edit_paint(fb, s, w, i, x + 16, y + 34))
            draw_label(fb, x + 16, y + 34, window_items(w)[i].name, window_items(w)[i].selected, 0, window_items(w)[i].label);
    }
}

static void paint_window(Fb *fb, const Session *s, int index) {
    const Window *w = &s->win[index];
    Layout lay;
    window_layout(w, &lay);
    fb_fill(fb, lay.outer.x, lay.outer.y, lay.outer.w, lay.outer.h, desktop_theme_color(THEME_PANEL));
    if (w->popup && w->collapsed) {
        fb_frame(fb, lay.outer.x, lay.outer.y, lay.outer.w, lay.outer.h, desktop_theme_color(THEME_FOREGROUND));
        fb_clip(fb, w->x + 6, w->y + 1, w->w - 12, TITLE_H - 2);
        fb_text(fb, w->x + 16, text_y(w->y, TITLE_H), desktop_theme_color(THEME_FOREGROUND), w->title);
        fb_unclip(fb); return;
    }
    if (finder_apps_front(s) == 1 && !s->desktop_focus && index == s->nwin - 1) stripes(fb, lay.title);
    else fb_fill(fb, lay.title.x, lay.title.y, lay.title.w, lay.title.h, desktop_theme_color(THEME_MENU));
    if (lay.info.h > 0 && w->kind == WIN_FOLDER) {
        fb_fill(fb, lay.info.x, lay.info.y, lay.info.w, lay.info.h, desktop_theme_color(THEME_MENU));
        fb_hline(fb, lay.info.x, lay.info.y + lay.info.h - 1, lay.info.w, desktop_theme_color(THEME_FOREGROUND));
        char info[80];
        char space[32];
        format_space(space, sizeof space, s->free_bytes);
        snprintf(info, sizeof info, "%d%s item%s, %s available", w->nitems, w->listing_limited ? "+" : "", w->nitems == 1 ? "" : "s",
                 space);
        int tw = fb_text_width(info);
        fb_clip(fb, lay.info.x + 4, lay.info.y, lay.info.w - 8, lay.info.h);
        fb_text(fb, lay.info.x + (lay.info.w - tw) / 2, text_y(lay.info.y, lay.info.h), desktop_theme_color(THEME_FOREGROUND), info);
        fb_unclip(fb);
    }
    rbox(fb, lay.closeb, held(s, index, Z_CLOSE, lay.closeb));
    rbox(fb, lay.shade, 0);
    fb_hline(fb, lay.shade.x + 3, lay.shade.y + 6, lay.shade.w - 6, desktop_theme_color(THEME_FOREGROUND));
    if (held(s, index, Z_SHADE, lay.shade)) {
        fb_invert(fb, lay.shade.x + 1, lay.shade.y + 1, lay.shade.w - 2, lay.shade.h - 2);
    }
    rbox(fb, lay.zoom, 0);
    fb_frame(fb, lay.zoom.x + 3, lay.zoom.y + 3, 6, 6, desktop_theme_color(THEME_FOREGROUND));
    if (held(s, index, Z_ZOOM, lay.zoom)) {
        fb_invert(fb, lay.zoom.x + 1, lay.zoom.y + 1, lay.zoom.w - 2, lay.zoom.h - 2);
    }
    fb_clip(fb, lay.title.x + 40, lay.title.y + 1, lay.title.w - 80, lay.title.h - 2);
    int tw = fb_text_width(w->title);
    int tx = lay.title.x + (lay.title.w - tw) / 2;
    int ty = text_y(lay.title.y, lay.title.h);
    if (desktop_theme_current() > 0)
        fb_fill(fb, tx - 4, lay.title.y + 1, tw + 8, lay.title.h - 2, desktop_theme_color(THEME_MENU));
    fb_text(fb, tx + 1, ty + 1, desktop_theme_color(THEME_PANEL), w->title);
    fb_text(fb, tx, ty, desktop_theme_color(THEME_FOREGROUND), w->title);
    fb_unclip(fb);
    if (!w->collapsed && lay.content.w > 0 && lay.content.h > 0) {
        if (w->kind == WIN_TERMINAL) {
            terminal_paint_window(fb, s, w, &lay);
        } else {
            fb_clip(fb, lay.content.x, lay.content.y, lay.content.w, lay.content.h);
            if (w->kind == WIN_TEXT) {
                if (w->note_select_all) fb_fill(fb,lay.content.x,lay.content.y,lay.content.w,lay.content.h,0xffc8dcf5);
                if (!about_panel_paint(fb, s, w, &lay) && !desktop_info_paint(fb, w, &lay)) text_draw(fb, lay.content.x + 8, lay.content.y + 6 - w->scroll, lay.content.w - 12, desktop_theme_color(THEME_FOREGROUND),
                          window_text(w));
                notes_paint(fb, s, w, &lay);
            } else if (w->kind == WIN_MOVIE) { movie_window_paint(fb, w);
            } else if (w->kind == WIN_FONT_BOOK) { font_book_window_paint(fb, s, w, &lay);
            } else {
                paint_items(fb, s, w, &lay);
            }
            fb_unclip(fb);
        }
        paint_scroll(fb, w, &lay);
    }
    fb_frame(fb, lay.outer.x, lay.outer.y, lay.outer.w, lay.outer.h, desktop_theme_color(THEME_FOREGROUND));
}

static void paint_menus(Fb *fb, Session *s) {
    fb_fill(fb, 0, 0, s->sw, 19, desktop_theme_color(THEME_MENU));
    fb_hline(fb, 0, 19, s->sw, desktop_theme_color(THEME_FOREGROUND));
    int menus_right = 0;
    for (int i = 0; i < menu_count(); i++) {
        Rect r;
        menu_title_rect(i, &r);
        if (r.w <= 0) continue;   /* left the bar on a narrow desktop */
        if (i != 7 && r.x + r.w > menus_right) menus_right = r.x + r.w;
        int open = s->menu == i;
        if (open) fb_fill(fb, r.x, 1, r.w, 18, desktop_theme_color(THEME_FOREGROUND));
        if (i == 0) os999_draw_menu_apple(fb, r.x + 5, 0);
        else fb_text(fb, r.x + (r.w - fb_text_width(menu_title(i))) / 2 - 1, text_y(0, 19), open ? desktop_theme_color(THEME_PANEL) : desktop_theme_color(THEME_FOREGROUND), menu_title(i));
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char clock[32];
    strftime(clock, sizeof clock, "%a %l:%M %p", &tm);
    const char *clk = clock[0] == ' ' ? clock + 1 : clock;
    int finder_w = fb_text_width(menu_title(7));
    int clock_w = fb_text_width(clk);
    int my = text_y(0, 19);
    if (s->sw - finder_w - 12 < menus_right + 8) finder_w = -16;   /* no room: drop the label */
    /* Narrow desktops (phone cover screens): shorten the clock, then drop it,
     * rather than drawing it over the menu titles. */
    if (s->sw - finder_w - 28 - clock_w < menus_right + 8) {
        strftime(clock, sizeof clock, "%l:%M", &tm);
        clk = clock[0] == ' ' ? clock + 1 : clock;
        clock_w = fb_text_width(clk);
    }
    if (s->sw - finder_w - 28 - clock_w >= menus_right + 8)
        fb_text(fb, s->sw - finder_w - 28 - clock_w, my, desktop_theme_color(THEME_FOREGROUND), clk);
    if (s->menu < 0) { menu_paint_flash(fb, s); return; }
    menu_paint_popup(fb, s->menu, s->menu_hot);
    if (s->menu_child) menu_paint_popup(fb, s->menu_child, s->menu_child_hot);
}

static void paint_dialog(Fb *fb, const Session *s) {
    if (!s->dialog) return;
    Rect box, cancel, ok;
    dialog_layout(s, &box, &cancel, &ok);
    fb_fill(fb, box.x + 3, box.y + 3, box.w, box.h, 0xFF6A6A6Au);
    fb_fill(fb, box.x, box.y, box.w, box.h, desktop_theme_color(THEME_PANEL));
    stripes(fb, (Rect){box.x, box.y, box.w, TITLE_H});
    fb_frame(fb, box.x, box.y, box.w, box.h, desktop_theme_color(THEME_FOREGROUND));
    int tw = fb_text_width(s->dialog_title);
    fb_text(fb, box.x + (box.w - tw) / 2, text_y(box.y, TITLE_H), desktop_theme_color(THEME_FOREGROUND), s->dialog_title);
    fb_clip(fb, box.x + 12, box.y + TITLE_H, box.w - 24, box.h - TITLE_H - 48);
    text_draw(fb, box.x + 16, box.y + 32, box.w - 32, desktop_theme_color(THEME_FOREGROUND), s->dialog_body);
    fb_unclip(fb);
    if (s->dialog == 3) {
        int x = box.x + 16, y = box.y + 80, width = box.w - 32;
        fb_fill(fb, x, y, width, 28, C_WHITE);
        fb_frame(fb, x, y, width, 28, C_BLACK);
        fb_clip(fb, x + 4, y + 2, width - 8, 24);
        int offset = fb_text_width(s->edit_text) - width + 12;
        if (offset < 0) offset = 0;
        fb_text(fb, x + 5 - offset, y + 7, C_BLACK, s->edit_text);
        if (s->edit_selected) fb_invert(fb, x + 3, y + 3, width - 6, 22);
        else menu_paint_dialog_caret(fb, s, x + 5 - offset + fb_text_width(s->edit_text), y + 6);
        fb_unclip(fb);
    }
    finder_settings_paint(fb, s, box);
    finder_labels_paint(fb, s, box);
    int ok_hot = s->dialog_down == 1 && rect_has(ok, s->mx, s->my);
    int cancel_hot = s->dialog_down == 2 && rect_has(cancel, s->mx, s->my);
    rbox(fb, ok, ok_hot);
    const char *ok_label = s->dialog_cmd == CMD_EXPAND_ARCHIVE_DO ? "Expand" : "OK";
    fb_text(fb, ok.x + (ok.w - fb_text_width(ok_label)) / 2, text_y(ok.y, ok.h), desktop_theme_color(THEME_FOREGROUND), ok_label);
    if (cancel.w > 0) {
        rbox(fb, cancel, cancel_hot);
        int cw = fb_text_width("Cancel");
        fb_text(fb, cancel.x + (cancel.w - cw) / 2, text_y(cancel.y, cancel.h), desktop_theme_color(THEME_FOREGROUND), "Cancel");
    }
}

void session_windows_paint_above(Session *s, uint32_t *px, int w, int h, int app) {
    Fb fb; fb_init(&fb, px, w, h);
    int cutoff = app ? finder_apps_rank(s, app) : 4;
    const SessionMediaHooks *media = session_media_hooks();
    for (int rank = cutoff - 1; rank >= 0; rank--) {
        if (finder_apps_rank(s, 3) == rank && !s->media_hidden && media && media->draw) media->draw(s, &fb);
        if (finder_apps_rank(s, 1) == rank && !s->finder_hidden) {
            for (int i = 0; i < s->nwin; i++) paint_window(&fb, s, i);
            appearance_panel_draw(s, &fb);
        }
    }
}
void session_windows_paint(Session *s, uint32_t *px, int w, int h) {
    session_windows_paint_above(s, px, w, h, 0);
}

void session_overlay_paint(Session *s, uint32_t *px, int w, int h) {
    menu_update(s);
    Fb fb;
    fb_init(&fb, px, w, h);
    if (appearance_panel_active()) appearance_panel_draw(s, &fb);
    if (s->media_focused && session_media_hooks() && session_media_hooks()->draw)
        session_media_hooks()->draw(s, &fb);
    session_gesture_paint(&fb, s);
    app_switcher_paint(s, &fb);
    paint_menus(&fb, s);
    context_draw(s, &fb);
    paint_dialog(&fb, s);
}
