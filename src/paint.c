#include "draw.h"
#include "ospi.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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
    fb_fill(fb, x + 2, y + 7, 14, 6, C_FOLDER_TAB);
    fb_frame(fb, x + 2, y + 7, 14, 6, C_FOLDER_EDGE);
    fb_fill(fb, x + 1, y + 12, 30, 17, C_FOLDER);
    fb_frame(fb, x + 1, y + 12, 30, 17, C_FOLDER_EDGE);
    fb_hline(fb, x + 2, y + 13, 28, C_FOLDER_TAB);
    if (system) {
        fb_text(fb, x + 12, y + 16, C_FOLDER_EDGE, "π");
    }
}

static void icon_doc(Fb *fb, int x, int y) {
    fb_fill(fb, x + 8, y + 3, 16, 24, C_WHITE);
    fb_fill(fb, x + 18, y + 3, 7, 7, 0xFFD0D0D0u);
    fb_frame(fb, x + 8, y + 3, 17, 24, C_BLACK);
    fb_hline(fb, x + 11, y + 14, 10, 0xFF9A9A9Au);
    fb_hline(fb, x + 11, y + 18, 10, 0xFF9A9A9Au);
    fb_hline(fb, x + 11, y + 22, 7, 0xFF9A9A9Au);
}

static void icon_disk(Fb *fb, int x, int y) {
    fb_fill(fb, x + 4, y + 8, 24, 16, 0xFFE6E6E6u);
    fb_frame(fb, x + 4, y + 8, 24, 16, C_BLACK);
    fb_fill(fb, x + 7, y + 18, 18, 2, 0xFF8E8E8Eu);
    fb_fill(fb, x + 8, y + 11, 5, 3, 0xFF3CAA55u);
}

static void icon_trash(Fb *fb, int x, int y) {
    fb_frame(fb, x + 11, y + 4, 10, 4, C_BLACK);
    fb_fill(fb, x + 6, y + 8, 20, 3, 0xFFF2F2F2u);
    fb_frame(fb, x + 6, y + 8, 20, 3, C_BLACK);
    fb_fill(fb, x + 8, y + 12, 16, 15, 0xFFF7F7F7u);
    fb_frame(fb, x + 8, y + 12, 16, 15, C_BLACK);
    for (int i = 0; i < 3; i++) {
        fb_fill(fb, x + 12 + i * 4, y + 15, 1, 9, 0xFF8A8A8Au);
    }
}

static void draw_kind(Fb *fb, int x, int y, int kind) {
    if (kind == ITEM_SYSTEM) {
        icon_folder(fb, x, y, 1);
    } else if (kind == ITEM_FOLDER) {
        icon_folder(fb, x, y, 0);
    } else {
        icon_doc(fb, x, y);
    }
}

static int text_y(int box_y, int box_h) {
    int lh = typeface_line_height(0);
    int y = box_y + (box_h - lh) / 2;
    return y < box_y ? box_y : y;
}

static void draw_label(Fb *fb, int cx, int y, const char *name, int selected, int desktop) {
    int tw = fb_text_width(name);
    int x = cx - tw / 2;
    int lh = typeface_line_height(0);
    if (selected) {
        fb_fill(fb, x - 2, y - 1, tw + 4, lh + 2, C_BLACK);
        fb_text(fb, x, y, C_WHITE, name);
    } else if (desktop) {
        fb_text(fb, x + 1, y + 1, C_BLACK, name);
        fb_text(fb, x, y, C_WHITE, name);
    } else {
        fb_text(fb, x, y, C_BLACK, name);
    }
}

static void paint_desktop_icons(Fb *fb, const Session *s) {
    const char *names[2] = {"OS π HD", "Trash"};
    for (int which = 0; which < 2; which++) {
        Rect icon, hit;
        desktop_icon_rect(s->sw, s->sh, which, &icon, &hit);
        (void)hit;
        if (which == 0) {
            icon_disk(fb, icon.x, icon.y);
        } else {
            icon_trash(fb, icon.x, icon.y);
        }
        if (s->desk_sel == which) {
            fb_frame(fb, icon.x - 2, icon.y - 2, icon.w + 4, icon.h + 4, C_BLACK);
        }
        draw_label(fb, icon.x + 16, icon.y + 34, names[which], s->desk_sel == which, 1);
    }
}

static void stripes(Fb *fb, Rect title) {
    fb_fill(fb, title.x, title.y, title.w, title.h, C_BLACK);
    for (int i = 1; i < title.h - 1; i++) {
        uint32_t c = (i & 1) ? C_STRIPE_B : C_STRIPE_A;
        if (i == 1) {
            c = C_WHITE;
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
    fb_fill(fb, r.x, r.y, r.w, r.h, C_WHITE);
    fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
    if (invert) {
        fb_invert(fb, r.x + 1, r.y + 1, r.w - 2, r.h - 2);
    }
}

static void paint_scroll(Fb *fb, const Window *w, const Layout *lay) {
    fb_fill(fb, lay->vbar.x, lay->vbar.y, lay->vbar.w, lay->vbar.h, C_TRACK);
    fb_fill(fb, lay->hbar.x, lay->hbar.y, lay->hbar.w, lay->hbar.h, C_TRACK);
    if (lay->vbar.h > 36) {
        rbox(fb, (Rect){lay->vbar.x, lay->vbar.y, lay->vbar.w, 16}, 0);
        rbox(fb, (Rect){lay->vbar.x, lay->vbar.y + lay->vbar.h - 16, lay->vbar.w, 16}, 0);
        for (int i = 0; i < 4; i++) {
            fb_hline(fb, lay->vbar.x + 8 - i, lay->vbar.y + 6 + i, i * 2 + 1, C_BLACK);
            fb_hline(fb, lay->vbar.x + 8 - i, lay->vbar.y + lay->vbar.h - 7 - i, i * 2 + 1, C_BLACK);
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
    fb_fill(fb, lay->grow.x, lay->grow.y, lay->grow.w, lay->grow.h, C_MENU);
    fb_frame(fb, lay->grow.x, lay->grow.y, lay->grow.w, lay->grow.h, C_BLACK);
    for (int i = 3; i <= 11; i += 3) {
        for (int k = 0; k <= i; k++) {
            fb_fill(fb, lay->grow.x + lay->grow.w - 4 - k, lay->grow.y + 3 + i - k, 1, 1, 0xFF6A6A6Au);
        }
    }
}

static void paint_items(Fb *fb, const Window *w, const Layout *lay) {
    if (w->nitems == 0) {
        fb_text(fb, lay->content.x + 12, lay->content.y + 12, C_DISABLED, "This folder is empty.");
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
        draw_kind(fb, x, y, w->items[i].kind);
        if (w->items[i].selected) {
            fb_frame(fb, x - 2, y - 2, 36, 36, C_BLACK);
        }
        draw_label(fb, x + 16, y + 34, w->items[i].name, w->items[i].selected, 0);
    }
}

static void paint_window(Fb *fb, const Session *s, int index) {
    const Window *w = &s->win[index];
    Layout lay;
    window_layout(w, &lay);
    fb_fill(fb, lay.outer.x, lay.outer.y, lay.outer.w, lay.outer.h, C_WHITE);
    stripes(fb, lay.title);
    if (lay.info.h > 0) {
        fb_fill(fb, lay.info.x, lay.info.y, lay.info.w, lay.info.h, C_MENU);
        fb_hline(fb, lay.info.x, lay.info.y + lay.info.h - 1, lay.info.w, C_BLACK);
        char info[80];
        char space[32];
        format_space(space, sizeof space, s->free_bytes);
        snprintf(info, sizeof info, "%d item%s, %s available", w->nitems, w->nitems == 1 ? "" : "s",
                 space);
        int tw = fb_text_width(info);
        fb_clip(fb, lay.info.x + 4, lay.info.y, lay.info.w - 8, lay.info.h);
        fb_text(fb, lay.info.x + (lay.info.w - tw) / 2, text_y(lay.info.y, lay.info.h), C_BLACK, info);
        fb_unclip(fb);
    }
    rbox(fb, lay.closeb, held(s, index, Z_CLOSE, lay.closeb));
    rbox(fb, lay.shade, 0);
    fb_hline(fb, lay.shade.x + 3, lay.shade.y + 6, lay.shade.w - 6, C_BLACK);
    if (held(s, index, Z_SHADE, lay.shade)) {
        fb_invert(fb, lay.shade.x + 1, lay.shade.y + 1, lay.shade.w - 2, lay.shade.h - 2);
    }
    rbox(fb, lay.zoom, 0);
    fb_frame(fb, lay.zoom.x + 3, lay.zoom.y + 3, 6, 6, C_BLACK);
    if (held(s, index, Z_ZOOM, lay.zoom)) {
        fb_invert(fb, lay.zoom.x + 1, lay.zoom.y + 1, lay.zoom.w - 2, lay.zoom.h - 2);
    }
    fb_clip(fb, lay.title.x + 40, lay.title.y + 1, lay.title.w - 80, lay.title.h - 2);
    int tw = fb_text_width(w->title);
    int tx = lay.title.x + (lay.title.w - tw) / 2;
    int ty = text_y(lay.title.y, lay.title.h);
    fb_text(fb, tx + 1, ty + 1, C_WHITE, w->title);
    fb_text(fb, tx, ty, C_BLACK, w->title);
    fb_unclip(fb);
    if (!w->collapsed && lay.content.w > 0 && lay.content.h > 0) {
        fb_clip(fb, lay.content.x, lay.content.y, lay.content.w, lay.content.h);
        if (w->kind == WIN_TEXT) {
            text_draw(fb, lay.content.x + 8, lay.content.y + 6 - w->scroll, lay.content.w - 12, C_BLACK,
                      w->text);
        } else {
            paint_items(fb, w, &lay);
        }
        fb_unclip(fb);
        paint_scroll(fb, w, &lay);
    }
    fb_frame(fb, lay.outer.x, lay.outer.y, lay.outer.w, lay.outer.h, C_BLACK);
}

static void paint_menus(Fb *fb, Session *s) {
    fb_fill(fb, 0, 0, s->sw, 19, C_MENU);
    fb_hline(fb, 0, 19, s->sw, C_BLACK);
    for (int i = 0; i < menu_count(); i++) {
        Rect r;
        menu_title_rect(i, &r);
        int open = s->menu == i;
        if (open) {
            fb_fill(fb, r.x, 1, r.w, 18, C_BLACK);
        }
        fb_text(fb, r.x + 8, text_y(0, 19), open ? C_WHITE : C_BLACK, menu_title(i));
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char clock[32];
    strftime(clock, sizeof clock, "%a %l:%M %p", &tm);
    const char *clk = clock[0] == ' ' ? clock + 1 : clock;
    int finder_w = fb_text_width("Finder");
    int clock_w = fb_text_width(clk);
    int my = text_y(0, 19);
    fb_text(fb, s->sw - finder_w - 12, my, C_BLACK, "Finder");
    fb_text(fb, s->sw - finder_w - 28 - clock_w, my, C_BLACK, clk);
    if (s->menu < 0) {
        return;
    }
    int n = menu_nitems(s->menu);
    if (n <= 0) {
        return;
    }
    Rect last;
    menu_item_rect(s->menu, n - 1, &last);
    Rect first;
    menu_item_rect(s->menu, 0, &first);
    int ph = last.y + last.h - first.y;
    fb_fill(fb, first.x + 2, first.y + 2, first.w, ph, 0xFF8A8A8Au);
    fb_fill(fb, first.x, first.y, first.w, ph, C_WHITE);
    fb_frame(fb, first.x, first.y, first.w, ph, C_BLACK);
    for (int i = 0; i < n; i++) {
        const MenuItem *it = menu_at(s->menu, i);
        Rect r;
        menu_item_rect(s->menu, i, &r);
        int hot = s->menu_hot == i && it->label && !(it->flags & MI_DISABLED);
        if (!it->label) {
            fb_hline(fb, r.x + 1, r.y + r.h / 2, r.w - 2, C_GRAY);
            continue;
        }
        if (hot) {
            fb_fill(fb, r.x + 1, r.y, r.w - 2, r.h, C_BLACK);
        }
        uint32_t color = hot ? C_WHITE : ((it->flags & MI_DISABLED) ? C_DISABLED : C_BLACK);
        int tx = r.x + 8;
        if (it->flags & MI_CHECKED) {
            fb_fill(fb, r.x + 6, r.y + 6, 6, 6, color);
            tx = r.x + 18;
        }
        int iy = text_y(r.y, r.h);
        fb_text(fb, tx, iy, color, it->label);
        if (it->shortcut) {
            int sw = fb_text_width(it->shortcut);
            fb_text(fb, r.x + r.w - sw - 8, iy, color, it->shortcut);
        }
    }
}

static void paint_dialog(Fb *fb, const Session *s) {
    if (!s->dialog) {
        return;
    }
    Rect box, cancel, ok;
    dialog_layout(s, &box, &cancel, &ok);
    fb_fill(fb, box.x + 3, box.y + 3, box.w, box.h, 0xFF6A6A6Au);
    fb_fill(fb, box.x, box.y, box.w, box.h, C_WHITE);
    stripes(fb, (Rect){box.x, box.y, box.w, TITLE_H});
    fb_frame(fb, box.x, box.y, box.w, box.h, C_BLACK);
    int tw = fb_text_width(s->dialog_title);
    fb_text(fb, box.x + (box.w - tw) / 2, text_y(box.y, TITLE_H), C_BLACK, s->dialog_title);
    fb_clip(fb, box.x + 12, box.y + TITLE_H, box.w - 24, box.h - TITLE_H - 48);
    text_draw(fb, box.x + 16, box.y + 32, box.w - 32, C_BLACK, s->dialog_body);
    fb_unclip(fb);
    int ok_hot = s->dialog_down == 1 && rect_has(ok, s->mx, s->my);
    int cancel_hot = s->dialog_down == 2 && rect_has(cancel, s->mx, s->my);
    rbox(fb, ok, ok_hot);
    int ow = fb_text_width("OK");
    fb_text(fb, ok.x + (ok.w - ow) / 2, text_y(ok.y, ok.h), C_BLACK, "OK");
    if (cancel.w > 0) {
        rbox(fb, cancel, cancel_hot);
        int cw = fb_text_width("Cancel");
        fb_text(fb, cancel.x + (cancel.w - cw) / 2, text_y(cancel.y, cancel.h), C_BLACK, "Cancel");
    }
}

void session_paint(Session *s, uint32_t *px, int w, int h) {
    s->sw = w;
    s->sh = h;
    session_clamp(s);
    Fb fb;
    fb_init(&fb, px, w, h);
    fb_desktop(&fb);
    paint_desktop_icons(&fb, s);
    for (int i = 0; i < s->nwin; i++) {
        paint_window(&fb, s, i);
    }
    if (s->drag == 3) {
        fb_frame(&fb, s->mx - 16, s->my - 12, 32, 28, C_BLACK);
        fb_frame(&fb, s->mx - 15, s->my - 11, 30, 26, C_WHITE);
        if (desktop_which(s->sw, s->sh, s->mx, s->my) == 1 && s->desk_sel != 1) {
            Rect icon, hit;
            desktop_icon_rect(s->sw, s->sh, 1, &icon, &hit);
            (void)hit;
            fb_invert(&fb, icon.x, icon.y, icon.w, icon.h);
        }
    }
    paint_menus(&fb, s);
    paint_dialog(&fb, s);
}
