#include "draw.h"

#include <string.h>

void fb_init(Fb *fb, uint32_t *px, int w, int h) {
    memset(fb, 0, sizeof *fb);
    fb->px = px;
    fb->w = w;
    fb->h = h;
}

void fb_clip(Fb *fb, int x, int y, int w, int h) {
    if (w < 0) {
        w = 0;
    }
    if (h < 0) {
        h = 0;
    }
    fb->clip = 1;
    fb->cx = x;
    fb->cy = y;
    fb->cw = w;
    fb->ch = h;
}

void fb_unclip(Fb *fb) { fb->clip = 0; }

static void bounds(const Fb *fb, int *x0, int *y0, int *x1, int *y1) {
    if (*x0 < 0) {
        *x0 = 0;
    }
    if (*y0 < 0) {
        *y0 = 0;
    }
    if (*x1 > fb->w) {
        *x1 = fb->w;
    }
    if (*y1 > fb->h) {
        *y1 = fb->h;
    }
    if (fb->clip) {
        int cx1 = fb->cx + fb->cw;
        int cy1 = fb->cy + fb->ch;
        if (*x0 < fb->cx) {
            *x0 = fb->cx;
        }
        if (*y0 < fb->cy) {
            *y0 = fb->cy;
        }
        if (*x1 > cx1) {
            *x1 = cx1;
        }
        if (*y1 > cy1) {
            *y1 = cy1;
        }
    }
}

void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0 || !fb->px) {
        return;
    }
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    bounds(fb, &x0, &y0, &x1, &y1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = fb->px + yy * fb->w;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = c;
        }
    }
}

void fb_hline(Fb *fb, int x, int y, int w, uint32_t c) { fb_fill(fb, x, y, w, 1, c); }

void fb_frame(Fb *fb, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    fb_hline(fb, x, y, w, c);
    fb_hline(fb, x, y + h - 1, w, c);
    fb_fill(fb, x, y, 1, h, c);
    fb_fill(fb, x + w - 1, y, 1, h, c);
}

void fb_invert(Fb *fb, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0 || !fb->px) {
        return;
    }
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    bounds(fb, &x0, &y0, &x1, &y1);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = fb->px + yy * fb->w;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = (row[xx] ^ 0x00FFFFFFu) | 0xFF000000u;
        }
    }
}

static int glyph_width(const char *s, int nbytes, int *glyphs) {
    int used = 0;
    int n = 0;
    const char *p = s;
    while (*p && (nbytes < 0 || used < nbytes)) {
        int adv = 0;
        const uint8_t *rows = 0;
        if (!font_next(p, &adv, &rows) || adv <= 0) {
            break;
        }
        if (nbytes >= 0 && used + adv > nbytes) {
            break;
        }
        used += adv;
        p += adv;
        n++;
    }
    if (glyphs) {
        *glyphs = n;
    }
    return used;
}

int fb_text_width_n(const char *s, int nbytes) {
    int n = 0;
    if (!s) {
        return 0;
    }
    glyph_width(s, nbytes, &n);
    return n * 8;
}

int fb_text_width(const char *s) { return fb_text_width_n(s, -1); }

void fb_text_n(Fb *fb, int x, int y, uint32_t c, const char *s, int nbytes) {
    if (!s) {
        return;
    }
    int used = 0;
    int cx = x;
    while (*s && (nbytes < 0 || used < nbytes)) {
        int adv = 0;
        const uint8_t *rows = 0;
        if (!font_next(s, &adv, &rows) || adv <= 0 || !rows) {
            break;
        }
        if (nbytes >= 0 && used + adv > nbytes) {
            break;
        }
        for (int row = 0; row < 8; row++) {
            uint8_t bits = rows[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (uint8_t)(1u << col)) {
                    fb_fill(fb, cx + col, y + row, 1, 1, c);
                }
            }
        }
        s += adv;
        used += adv;
        cx += 8;
    }
}

void fb_text(Fb *fb, int x, int y, uint32_t c, const char *s) { fb_text_n(fb, x, y, c, s, -1); }

void fb_desktop(Fb *fb) {
    static const uint8_t pat[8] = {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00};
    for (int y = 0; y < fb->h; y++) {
        uint32_t *row = fb->px + y * fb->w;
        uint8_t bits = pat[y & 7];
        for (int x = 0; x < fb->w; x++) {
            int on = (bits >> (7 - (x & 7))) & 1;
            row[x] = on ? C_DESK2 : C_DESK;
        }
    }
}
