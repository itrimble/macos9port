#include "cdev_ui.h"
#include "desktop_assets.h"
#include "desktop_theme.h"

#include <string.h>

uint32_t cdev_ui_selection_color(void) {
    return desktop_theme_current() ? desktop_theme_color(THEME_ACCENT) : 0xFF344768u;
}

uint32_t cdev_ui_accent_text(void) {
    uint32_t c = cdev_ui_selection_color();
    unsigned luma = ((c >> 16) & 255u) * 299u + ((c >> 8) & 255u) * 587u + (c & 255u) * 114u;
    return luma >= 150000u ? 0xFF000000u : 0xFFFFFFFFu;
}

void cdev_ui_group(Fb *fb, Rect r, const char *title) {
    fb_fill(fb, r.x, r.y + 6, r.w, r.h - 6, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, r.x, r.y + 6, r.w, r.h - 6, desktop_theme_color(THEME_FRAME));
    if (title && title[0]) {
        int tw = fb_text_width(title);
        fb_fill(fb, r.x + 10, r.y, tw + 8, 14, desktop_theme_color(THEME_PANEL));
        fb_text(fb, r.x + 14, r.y, desktop_theme_color(THEME_FOREGROUND), title);
    }
}

void cdev_ui_button(Fb *fb, Rect r, const char *label, int active, int enabled) {
    uint32_t bg = active ? cdev_ui_selection_color() : desktop_theme_color(THEME_MENU);
    uint32_t border = enabled ? desktop_theme_color(THEME_FRAME) : desktop_theme_color(THEME_DISABLED);
    uint32_t ink = !enabled ? desktop_theme_color(THEME_DISABLED)
                            : active ? cdev_ui_accent_text() : desktop_theme_color(THEME_FOREGROUND);
    fb_fill(fb, r.x, r.y, r.w, r.h, bg);
    fb_frame(fb, r.x, r.y, r.w, r.h, border);
    if (!active && r.w > 4) fb_hline(fb, r.x + 1, r.y + 1, r.w - 2, desktop_theme_color(THEME_PANEL));
    int tx = r.x + (r.w - fb_text_width(label)) / 2;
    int ty = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_clip(fb, r.x + 2, r.y + 1, r.w - 4, r.h - 2);
    fb_text(fb, tx, ty, ink, label);
    fb_unclip(fb);
}

void cdev_ui_checkbox(Fb *fb, Rect r, const char *label, int checked) {
    int by = r.y + (r.h - 13) / 2;
    fb_fill(fb, r.x, by, 13, 13, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, r.x, by, 13, 13, desktop_theme_color(THEME_FRAME));
    if (checked) {
        uint32_t ink = desktop_theme_color(THEME_FOREGROUND);
        for (int i = 0; i < 7; i++) {
            fb_fill(fb, r.x + 3 + i, by + 3 + i, 2, 1, ink);
            fb_fill(fb, r.x + 9 - i, by + 3 + i, 2, 1, ink);
        }
    }
    int ty = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_text(fb, r.x + 20, ty, desktop_theme_color(THEME_FOREGROUND), label);
}

void cdev_ui_slider(Fb *fb, Rect track, int value, int max_value, int muted) {
    if (max_value < 1) max_value = 1;
    if (value < 0) value = 0;
    if (value > max_value) value = max_value;
    int bar_y = track.y + (track.h - 6) / 2;
    fb_fill(fb, track.x, bar_y, track.w, 6, desktop_theme_color(THEME_TRACK));
    fb_frame(fb, track.x, bar_y, track.w, 6, desktop_theme_color(THEME_FRAME));
    int fill_w = (track.w - 12) * value / max_value;
    if (fill_w > 0 && !muted)
        fb_fill(fb, track.x + 1, bar_y + 1, fill_w + 5, 4, cdev_ui_selection_color());
    int thumb_x = track.x + fill_w;
    fb_fill(fb, thumb_x, track.y + 2, 12, track.h - 4, desktop_theme_color(THEME_MENU));
    fb_frame(fb, thumb_x, track.y + 2, 12, track.h - 4, desktop_theme_color(THEME_FRAME));
    fb_fill(fb, thumb_x + 5, track.y + 5, 2, track.h - 10, desktop_theme_color(THEME_FOREGROUND));
}

void cdev_ui_icon(Fb *fb, int x, int y, const char *icon_name) {
    const char *name = (icon_name && icon_name[0]) ? icon_name : "control_panel";
    if (desktop_asset_draw(fb, x, y, name)) return;
    /* Clean geometric 32x32 fallback when asset pack is absent in unit tests. */
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    fb_fill(fb, x + 2, y + 2, 28, 28, desktop_theme_color(THEME_MENU));
    fb_frame(fb, x + 1, y + 1, 30, 30, desktop_theme_color(THEME_FRAME));
    if (strcmp(name, "speaker") == 0) {
        fb_fill(fb, x + 6, y + 12, 5, 8, fg);
        for (int i = 0; i < 6; i++)
            fb_fill(fb, x + 11 + i, y + 11 - i, 1, 10 + i * 2, fg);
        fb_fill(fb, x + 20, y + 10, 2, 12, cdev_ui_selection_color());
        fb_fill(fb, x + 24, y + 7, 2, 18, cdev_ui_selection_color());
    } else if (strcmp(name, "settings") == 0) {
        fb_frame(fb, x + 7, y + 7, 18, 18, fg);
        fb_fill(fb, x + 11, y + 11, 10, 10, cdev_ui_selection_color());
        fb_fill(fb, x + 14, y + 4, 4, 3, fg);
        fb_fill(fb, x + 14, y + 25, 4, 3, fg);
        fb_fill(fb, x + 4, y + 14, 3, 4, fg);
        fb_fill(fb, x + 25, y + 14, 3, 4, fg);
    } else if (strcmp(name, "monitor") == 0) {
        fb_frame(fb, x + 4, y + 5, 24, 17, fg);
        fb_fill(fb, x + 6, y + 7, 20, 13, cdev_ui_selection_color());
        fb_fill(fb, x + 13, y + 22, 6, 3, fg);
        fb_fill(fb, x + 9, y + 25, 14, 2, fg);
    } else if (strcmp(name, "keyboard") == 0 || strcmp(name, "mouse") == 0) {
        fb_frame(fb, x + 4, y + 9, 24, 14, fg);
        for (int r = 0; r < 2; r++)
            for (int k = 0; k < 5; k++)
                fb_fill(fb, x + 6 + k * 4, y + 12 + r * 4, 3, 2, fg);
    } else if (strcmp(name, "battery") == 0) {
        fb_frame(fb, x + 5, y + 9, 20, 14, fg);
        fb_fill(fb, x + 25, y + 13, 2, 6, fg);
        fb_fill(fb, x + 7, y + 11, 14, 10, 0xFF28A448u);
    } else if (strcmp(name, "cpu") == 0) {
        fb_frame(fb, x + 7, y + 7, 18, 18, fg);
        fb_fill(fb, x + 10, y + 10, 12, 12, cdev_ui_selection_color());
    } else if (strcmp(name, "network") == 0 || strcmp(name, "wireless") == 0) {
        fb_frame(fb, x + 11, y + 5, 10, 8, fg);
        fb_frame(fb, x + 4, y + 19, 10, 8, fg);
        fb_frame(fb, x + 18, y + 19, 10, 8, fg);
        fb_hline(fb, x + 9, y + 16, 14, cdev_ui_selection_color());
    } else {
        for (int i = 0; i < 3; i++) {
            fb_fill(fb, x + 7 + i * 8, y + 6, 2, 20, fg);
            fb_fill(fb, x + 4 + i * 8, y + 9 + i * 4, 8, 4, cdev_ui_selection_color());
        }
    }
}

void cdev_ui_pattern_tile(Fb *fb, Rect r, int pattern_id, int selected) {
    uint32_t a = desktop_theme_current() ? desktop_theme_color(THEME_DESKTOP_TOP) : C_DESK;
    uint32_t b = desktop_theme_current() ? desktop_theme_color(THEME_DESKTOP_BOTTOM) : C_DESK2;
    for (int py = 1; py < r.h - 1; py++) {
        for (int px = 1; px < r.w - 1; px++) {
            int alt = 0;
            if (pattern_id == 0) alt = (px ^ py) & 1;
            else if (pattern_id == 1) alt = (px % 4 == 0) || (py % 4 == 0);
            else if (pattern_id == 2) alt = ((px + py) % 4) < 2;
            else alt = 0;
            fb_fill(fb, r.x + px, r.y + py, 1, 1, alt ? a : b);
        }
    }
    fb_frame(fb, r.x, r.y, r.w, r.h,
             selected ? cdev_ui_selection_color() : desktop_theme_color(THEME_FRAME));
    if (selected)
        fb_frame(fb, r.x - 1, r.y - 1, r.w + 2, r.h + 2, desktop_theme_color(THEME_FOREGROUND));
}
