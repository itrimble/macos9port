#include "ospi.h"

#include <string.h>

int rect_has(Rect r, int x, int y) {
    return r.w > 0 && r.h > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void window_layout(const Window *w, Layout *lay) {
    memset(lay, 0, sizeof *lay);
    lay->outer = (Rect){w->x, w->y, w->w, w->h};
    lay->title = (Rect){w->x, w->y, w->w, TITLE_H};
    lay->closeb = (Rect){w->x + 7, w->y + 4, 12, 12};
    lay->zoom = (Rect){w->x + w->w - 20, w->y + 4, 12, 12};
    lay->shade = (Rect){lay->zoom.x - 16, w->y + 4, 12, 12};
    if (w->collapsed) {
        return;
    }
    int top = TITLE_H;
    if (w->kind == WIN_FOLDER) {
        lay->info = (Rect){w->x, w->y + TITLE_H, w->w, INFO_H};
        top = TITLE_H + INFO_H;
    }
    lay->content.x = w->x + 1;
    lay->content.y = w->y + top;
    lay->content.w = w->w - 2 - SB;
    lay->content.h = w->h - top - 1 - SB;
    if (lay->content.w < 0) {
        lay->content.w = 0;
    }
    if (lay->content.h < 0) {
        lay->content.h = 0;
    }
    lay->vbar = (Rect){lay->content.x + lay->content.w, lay->content.y, SB, lay->content.h};
    lay->hbar = (Rect){lay->content.x, lay->content.y + lay->content.h, lay->content.w, SB};
    lay->grow = (Rect){lay->vbar.x, lay->hbar.y, SB, SB};
}

int window_max_scroll(const Window *w) {
    if (!w || w->collapsed) {
        return 0;
    }
    Layout lay;
    window_layout(w, &lay);
    int doc = 0;
    if (w->kind == WIN_TEXT) {
        int mw = lay.content.w > 16 ? lay.content.w - 12 : 8;
        doc = text_height(w->text, mw) + 8;
    } else if (w->kind == WIN_FOLDER) {
        int cols = lay.content.w / CELL_W;
        if (cols < 1) {
            cols = 1;
        }
        int rows = w->nitems ? (w->nitems + cols - 1) / cols : 0;
        doc = rows * CELL_H + 4;
    }
    int maxs = doc - lay.content.h;
    return maxs > 0 ? maxs : 0;
}

int item_index_at(const Window *w, int x, int y) {
    if (!w || w->kind != WIN_FOLDER || w->collapsed) {
        return -1;
    }
    Layout lay;
    window_layout(w, &lay);
    if (!rect_has(lay.content, x, y)) {
        return -1;
    }
    int cols = lay.content.w / CELL_W;
    if (cols < 1) {
        cols = 1;
    }
    for (int i = 0; i < w->nitems; i++) {
        int col = i % cols;
        int row = i / cols;
        Rect cell = {lay.content.x + col * CELL_W, lay.content.y + row * CELL_H - w->scroll, CELL_W,
                     CELL_H};
        if (rect_has(cell, x, y)) {
            return i;
        }
    }
    return -1;
}

static Rect item_cell(const Window *w, int index) {
    Layout lay;
    window_layout(w, &lay);
    int cols = lay.content.w / CELL_W;
    if (cols < 1) {
        cols = 1;
    }
    int col = index % cols;
    int row = index / cols;
    Rect cell = {lay.content.x + col * CELL_W, lay.content.y + row * CELL_H - w->scroll, CELL_W, CELL_H};
    return cell;
}

void desktop_icon_rect(int sw, int sh, int which, Rect *icon, Rect *hit) {
    icon->w = 32;
    icon->h = 32;
    if (which == 0) {
        icon->x = sw - 72;
        icon->y = 28;
    } else {
        icon->x = sw - 68;
        icon->y = sh - 78;
    }
    hit->x = icon->x - 20;
    hit->y = icon->y;
    hit->w = 76;
    hit->h = 48;
    if (hit->x < 0) {
        hit->x = 0;
    }
    if (hit->x + hit->w > sw) {
        hit->w = sw - hit->x;
    }
}

int desktop_which(int sw, int sh, int x, int y) {
    for (int which = 0; which < 2; which++) {
        Rect icon, hit;
        desktop_icon_rect(sw, sh, which, &icon, &hit);
        (void)icon;
        if (rect_has(hit, x, y)) {
            return which;
        }
    }
    return -1;
}

void dialog_layout(const Session *s, Rect *box, Rect *cancel, Rect *ok) {
    box->w = 360;
    box->h = 220;
    box->x = (s->sw - box->w) / 2;
    box->y = (s->sh - box->h) / 2;
    ok->w = 88;
    ok->h = 22;
    ok->x = box->x + box->w - 104;
    ok->y = box->y + box->h - 36;
    if (s->dialog == 2) {
        cancel->w = 88;
        cancel->h = 22;
        cancel->x = ok->x - 96;
        cancel->y = ok->y;
    } else {
        *cancel = (Rect){0, 0, 0, 0};
    }
}

int ospi_item_center(const Session *s, int win, int index, int *x, int *y) {
    if (!s || win < 0 || win >= s->nwin || index < 0 || index >= s->win[win].nitems) {
        return -1;
    }
    Rect cell = item_cell(&s->win[win], index);
    *x = cell.x + cell.w / 2;
    *y = cell.y + 24;
    return 0;
}

int ospi_control_center(const Window *w, int which, int *x, int *y) {
    if (!w) {
        return -1;
    }
    Layout lay;
    window_layout(w, &lay);
    Rect r;
    if (which == 0) {
        r = lay.closeb;
    } else if (which == 1) {
        r = lay.shade;
    } else if (which == 2) {
        r = lay.zoom;
    } else if (which == 3) {
        r = lay.title;
        r.x += 80;
        r.w -= 120;
    } else {
        return -1;
    }
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
    return 0;
}

int ospi_desktop_center(const Session *s, int which, int *x, int *y) {
    if (!s || (which != 0 && which != 1)) {
        return -1;
    }
    Rect icon, hit;
    desktop_icon_rect(s->sw, s->sh, which, &icon, &hit);
    (void)hit;
    *x = icon.x + icon.w / 2;
    *y = icon.y + icon.h / 2;
    return 0;
}

int ospi_menu_center(int menu, int item, int *x, int *y) {
    Rect r;
    if (item < 0) {
        if (menu < 0 || menu >= menu_count()) {
            return -1;
        }
        menu_title_rect(menu, &r);
    } else {
        if (!menu_at(menu, item)) {
            return -1;
        }
        menu_item_rect(menu, item, &r);
    }
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
    return 0;
}

int ospi_dialog_center(const Session *s, int which, int *x, int *y) {
    if (!s || !s->dialog) {
        return -1;
    }
    Rect box, cancel, ok;
    dialog_layout(s, &box, &cancel, &ok);
    Rect r = which == 0 ? ok : cancel;
    if (r.w <= 0) {
        return -1;
    }
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
    return 0;
}
