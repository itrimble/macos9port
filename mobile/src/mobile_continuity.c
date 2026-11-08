/* Window continuity across fold, unfold and Display-size changes.
 *
 * session_clamp() handles a smaller desktop by cutting windows down and
 * pushing them into the corner. On a phone that changes size every time it is
 * folded, that would leave a pile of shrunken windows by lunchtime. Here each
 * window keeps the size the user wanted; on every resize its centre moves
 * proportionally and its size is the wanted size, cut to fit. Unfold again and
 * the windows grow back.
 */
#include "mobile.h"

#include <stdio.h>
#include <string.h>

enum { MENU_BAR = 20, MIN_W = 260, MIN_H = 140 };

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int slot_for(MobileContinuity *c, const Window *w) {
    for (int i = 0; i < c->n; i++)
        if (strcmp(c->rel[i], w->rel) == 0) return i;
    if (c->n >= OSPI_MAX_WIN) return -1;
    int i = c->n++;
    snprintf(c->rel[i], sizeof c->rel[i], "%s", w->rel);
    c->want_w[i] = w->w;
    c->want_h[i] = w->h;
    return i;
}

/* Forget windows that have closed so a reopened one starts fresh. */
static void prune(MobileContinuity *c, const Session *s) {
    int k = 0;
    for (int i = 0; i < c->n; i++) {
        int open = 0;
        for (int j = 0; j < s->nwin && !open; j++)
            open = strcmp(s->win[j].rel, c->rel[i]) == 0;
        if (!open) continue;
        if (k != i) {
            memcpy(c->rel[k], c->rel[i], sizeof c->rel[k]);
            c->want_w[k] = c->want_w[i];
            c->want_h[k] = c->want_h[i];
        }
        k++;
    }
    c->n = k;
}

void mobile_continuity_resize(MobileContinuity *c, Session *s, int new_w, int new_h) {
    if (!c || !s || new_w <= 0 || new_h <= 0) return;
    int old_w = s->sw, old_h = s->sh;
    prune(c, s);
    if (old_w <= 0 || old_h <= 0 || (old_w == new_w && old_h == new_h)) {
        /* No resize: remember any size the user set by hand meanwhile. */
        for (int i = 0; i < s->nwin; i++) {
            Window *w = &s->win[i];
            int k = slot_for(c, w);
            if (k < 0 || w->zoomed || w->collapsed || old_w <= 0) continue;
            /* The size we would have given it; anything else is the user's. */
            int exp_w = imax(MIN_W, imin(c->want_w[k], old_w - 16));
            int exp_h = imax(MIN_H, imin(c->want_h[k], old_h - MENU_BAR - 12));
            if (w->w != exp_w) c->want_w[k] = w->w;
            if (w->h != exp_h) c->want_h[k] = w->h;
        }
        return;
    }
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        int k = slot_for(c, w);
        if (w->zoomed) {   /* a zoomed window stays zoomed to the new size */
            w->x = 8; w->y = 26; w->w = new_w - 16; w->h = new_h - 34;
            continue;
        }
        int ww = k >= 0 ? c->want_w[k] : w->w, wh = k >= 0 ? c->want_h[k] : w->h;
        ww = imax(MIN_W, imin(ww, new_w - 16));
        if (!w->collapsed) wh = imax(MIN_H, imin(wh, new_h - MENU_BAR - 12));
        long cx = (long)(w->x + w->w / 2) * new_w / old_w;
        long cy = (long)(w->y + TITLE_H / 2 - MENU_BAR) * (new_h - MENU_BAR) /
                  imax(1, old_h - MENU_BAR) + MENU_BAR;
        int x = (int)cx - ww / 2, y = (int)cy - TITLE_H / 2;
        int h_now = w->collapsed ? TITLE_H : wh;
        x = imax(0, imin(x, new_w - ww));
        y = imax(MENU_BAR + 2, imin(y, new_h - h_now));
        w->x = x; w->y = y; w->w = ww;
        if (!w->collapsed) w->h = wh;
    }
}
