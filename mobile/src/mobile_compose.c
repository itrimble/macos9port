/* Desktop -> device-resolution canvas.
 *
 * The chrome (keyboard, strip, loupe, balloons) blends over real desktop
 * pixels, so the whole screen is composed on the CPU into one canvas and
 * uploaded as a single texture. Rows are scaled once and duplicated with
 * memcpy, and the host only recomposes when the desktop or the chrome changed,
 * so an idle desktop costs nothing.
 */
#include "mobile_compose.h"

#include <stdlib.h>
#include <string.h>

int mobile_canvas_resize(MobileCanvas *c, int w, int h) {
    if (c->w == w && c->h == h && c->px) return 0;
    uint32_t *px = realloc(c->px, (size_t)w * h * sizeof *px);
    int *map = realloc(c->xmap, (size_t)w * sizeof *map);
    if (!px || !map) {
        free(px ? px : c->px);
        free(map ? map : c->xmap);
        memset(c, 0, sizeof *c);
        return -1;
    }
    c->px = px;
    c->xmap = map;
    c->w = w;
    c->h = h;
    memset(c->px, 0, (size_t)w * h * sizeof *px);
    return 0;
}

void mobile_canvas_free(MobileCanvas *c) {
    free(c->px);
    free(c->xmap);
    memset(c, 0, sizeof *c);
}

void mobile_compose_desk(MobileCanvas *c, const MobileLayout *l, const MobileView *v,
                         const uint32_t *desk, uint32_t backdrop) {
    if (!c->px || !desk) return;
    Rect src, dst;
    mobile_view_rects(v, l, &src, &dst);
    int x0 = dst.x < 0 ? 0 : dst.x, x1 = dst.x + dst.w > c->w ? c->w : dst.x + dst.w;
    for (int x = x0; x < x1; x++) {
        long sx = src.x + (long)(x - dst.x) * src.w / dst.w;
        c->xmap[x] = (int)(sx < l->desk_w ? sx : l->desk_w - 1);
    }
    int last_sy = -1;
    for (int y = 0; y < c->h; y++) {
        uint32_t *row = c->px + (size_t)y * c->w;
        if (y < dst.y || y >= dst.y + dst.h || x1 <= x0) {
            for (int x = 0; x < c->w; x++) row[x] = backdrop;
            last_sy = -1;
            continue;
        }
        int sy = src.y + (int)((long)(y - dst.y) * src.h / dst.h);
        if (sy >= l->desk_h) sy = l->desk_h - 1;
        if (sy == last_sy) {
            memcpy(row, row - c->w, (size_t)c->w * sizeof *row);
            continue;
        }
        last_sy = sy;
        const uint32_t *srow = desk + (size_t)sy * l->desk_w;
        for (int x = 0; x < x0; x++) row[x] = backdrop;
        for (int x = x0; x < x1; x++) row[x] = srow[c->xmap[x]] | 0xFF000000u;
        for (int x = x1; x < c->w; x++) row[x] = backdrop;
    }
}

void mobile_compose_deck(MobileCanvas *c, Rect deck, uint32_t top, uint32_t bottom) {
    if (deck.w <= 0 || deck.h <= 0) return;
    for (int y = deck.y; y < deck.y + deck.h && y < c->h; y++) {
        if (y < 0) continue;
        int t = (y - deck.y) * 255 / deck.h;
        uint32_t col = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8) {
            int a = (top >> sh) & 255, b = (bottom >> sh) & 255;
            col |= (uint32_t)(a + (b - a) * t / 255) << sh;
        }
        uint32_t *row = c->px + (size_t)y * c->w;
        for (int x = deck.x < 0 ? 0 : deck.x; x < deck.x + deck.w && x < c->w; x++) row[x] = col;
    }
}
