#include "ospi.h"
#include "draw.h"
#include "desktop_theme.h"
#include "finder_labels.h"

void menu_paint_popup(Fb *fb, int menu, int hot_row) {
    int n = menu_nitems(menu);
    if (n <= 0) {
        return;
    }
    Rect last;
    menu_item_rect(menu, n - 1, &last);
    Rect first;
    menu_item_rect(menu, 0, &first);
    int ph = last.y + last.h - first.y;
    fb_fill(fb, first.x + 2, first.y + 2, first.w, ph, 0xFF8A8A8Au);
    fb_fill(fb, first.x, first.y, first.w, ph, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, first.x, first.y, first.w, ph, desktop_theme_color(THEME_FOREGROUND));
    for (int i = 0; i < n; i++) {
        const MenuItem *it = menu_at(menu, i);
        Rect r;
        menu_item_rect(menu, i, &r);
        int hot = hot_row == i && it->label && !(it->flags & MI_DISABLED);
        if (!it->label) {
            fb_hline(fb, r.x + 1, r.y + r.h / 2, r.w - 2, C_GRAY);
            continue;
        }
        if (hot) {
            fb_fill(fb, r.x + 1, r.y, r.w - 2, r.h, desktop_theme_color(THEME_FOREGROUND));
        }
        uint32_t color = hot ? desktop_theme_color(THEME_PANEL) : ((it->flags & MI_DISABLED) ? desktop_theme_color(THEME_DISABLED) : desktop_theme_color(THEME_FOREGROUND));
        int tx = r.x + 8;
        if (it->flags & MI_CHECKED) {
            fb_fill(fb, r.x + 6, r.y + 6, 6, 6, color);
            tx = r.x + 18;
        }
        int iy = r.y + (r.h - typeface_line_height(0)) / 2;
        fb_clip(fb, tx, r.y, r.w - (tx - r.x) - 8, r.h);
        fb_text(fb, tx, iy, color, it->label);
        fb_unclip(fb);
        if (it->cmd > CMD_LABEL_BASE && it->cmd < CMD_LABEL_BASE + 8) {
            fb_fill(fb, r.x + r.w - 18, r.y + 5, 10, r.h - 10, finder_label_color(it->cmd - CMD_LABEL_BASE));
            fb_frame(fb, r.x + r.w - 18, r.y + 5, 10, r.h - 10, color);
        }
        if (it->flags & MI_SUBMENU) fb_text(fb, r.x + r.w - 14, iy, color, ">");
        if (it->shortcut) {
            int sw = fb_text_width(it->shortcut);
            fb_text(fb, r.x + r.w - sw - 8, iy, color, it->shortcut);
        }
    }
}
