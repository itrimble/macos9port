#include "mac_shape.h"
#include <stdint.h>
#include <stdlib.h>

void mac_shape_init(MacShape *s) { s->t = s->l = s->b = s->r = 0; s->lo = s->hi = NULL; }
void mac_shape_free(MacShape *s) { free(s->lo); free(s->hi); mac_shape_init(s); }

static uint64_t isqrt(uint64_t v) { uint64_t x = 0, bit = (uint64_t)1 << 62; while (bit > v) bit >>= 2; while (bit) { if (v >= x + bit) { v -= x + bit; x = (x >> 1) + bit; } else x >>= 1; bit >>= 2; } return x; }

/* Width of corner-ellipse row k (0 .. oh-1) of an ow x oh ellipse. */
static int ellipse_row(int ow, int oh, int k) {
    int64_t dx = 2 * (int64_t)k + 1 - oh; if (dx < 0) dx = -dx;
    int64_t num = (int64_t)ow * ow * ((int64_t)oh * oh - dx * dx), den = (int64_t)oh * oh;
    int64_t t = (int64_t)isqrt((uint64_t)(4 * (num / den)));              /* the full width in half pixels */
    int p = ow & 1;
    int64_t hh = t >= 2 * p ? p + 2 * ((t - 2 * p + 1) / 4) : p;           /* nearest width with the parity of ow, ties narrower */
    int64_t floor_w = p ? 1 : 2;
    if (hh < floor_w) hh = floor_w;
    if (hh > ow) hh = ow;
    return (int)hh;
}

int mac_shape_build(MacShape *s, int t, int l, int b, int r, int ow, int oh) {
    mac_shape_init(s);
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0) { s->t = t; s->l = l; s->b = t; s->r = r; return 0; }
    if (h > MAC_SHAPE_MAX_ROWS) return -1;
    if (ow < 0) ow = 0;
    if (oh < 0) oh = 0;
    if (ow > w) ow = w;
    if (oh > h) oh = h;
    s->lo = malloc((size_t)h * sizeof *s->lo); s->hi = malloc((size_t)h * sizeof *s->hi);
    if (!s->lo || !s->hi) { mac_shape_free(s); return -1; }
    s->t = t; s->l = l; s->b = b; s->r = r;
    for (int y = 0; y < h; y++) {
        int inset = 0;
        if (ow > 0 && oh > 0) {
            int k = -1;
            if (y < oh / 2) k = y; else if (y >= h - (oh - oh / 2)) k = oh - (h - y);
            if (k >= 0) inset = (ow - ellipse_row(ow, oh, k)) / 2;
        }
        s->lo[y] = l + inset; s->hi[y] = r - inset;
    }
    return 0;
}

int mac_shape_contains(const MacShape *s, int x, int y) {
    return y >= s->t && y < s->b && s->lo && x >= s->lo[y - s->t] && x < s->hi[y - s->t];
}
