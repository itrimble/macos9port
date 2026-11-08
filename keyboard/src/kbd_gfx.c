/* Signed-distance rasterizing: every edge gets exact pixel coverage, so keycaps
 * stay crisp at 1x and on a 3x phone panel alike. */
#include "kbd_gfx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef OSPI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

uint32_t g_mix(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0, 1);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)(a >> s & 255), cb = (float)(b >> s & 255);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

uint32_t g_shade(uint32_t c, float k) {
    uint32_t a = c & 0xFF000000u;
    return k >= 1 ? g_mix(c, 0x00FFFFFFu | a, k - 1) : g_mix(c, a, 1 - k);
}

static inline void blend(Fb *fb, int x, int y, uint32_t c, float a) {
    if (a <= 0.002f) return;
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    if (fb->clip && (x < fb->cx || y < fb->cy || x >= fb->cx + fb->cw || y >= fb->cy + fb->ch)) return;
    a *= (float)(c >> 24) / 255.0f;
    uint32_t *p = &fb->px[(size_t)y * fb->w + x];
    if (a >= 0.998f) { *p = c | 0xFF000000u; return; }
    uint32_t d = *p, o = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        float cd = (float)(d >> s & 255), cs = (float)(c >> s & 255);
        o |= (uint32_t)(cd + (cs - cd) * a + 0.5f) << s;
    }
    *p = o;
}

static float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
}

void g_rrect(Fb *fb, float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom,
             float alpha) {
    if (w <= 0 || h <= 0 || alpha <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = clampf(r, 0, fminf(hw, hh));
    int x0 = (int)floorf(x), x1 = (int)ceilf(x + w), y0 = (int)floorf(y), y1 = (int)ceilf(y + h);
    for (int py = y0; py < y1; py++) {
        float fy = py + 0.5f;
        uint32_t c = top == bottom ? top : g_mix(top, bottom, (fy - y) / h);
        float ay = fabsf(fy - cy);
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, ax = fabsf(fx - cx), cov;
            if ((ax < hw - r - 1 && ay < hh - 1) || (ay < hh - r - 1 && ax < hw - 1)) cov = 1;
            else cov = clampf(0.5f - sd_rrect(fx, fy, cx, cy, hw, hh, r), 0, 1);
            blend(fb, px, py, c, cov * alpha);
        }
    }
}

void g_rrect_stroke(Fb *fb, float x, float y, float w, float h, float r, float t, uint32_t c, float alpha) {
    if (w <= 0 || h <= 0) return;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = clampf(r, 0, fminf(hw, hh));
    int x0 = (int)floorf(x) - 1, x1 = (int)ceilf(x + w) + 1, y0 = (int)floorf(y) - 1, y1 = (int)ceilf(y + h) + 1;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            if (fabsf(fx - cx) < hw - r - t - 1 && fabsf(fy - cy) < hh - t - 1) continue;
            float d = sd_rrect(fx, fy, cx, cy, hw, hh, r);
            float cov = clampf(0.5f - fabsf(d + t * 0.5f) + t * 0.5f, 0, 1);
            blend(fb, px, py, c, cov * alpha);
        }
}

void g_shadow(Fb *fb, float x, float y, float w, float h, float r, float blur, uint32_t c, float alpha) {
    if (blur < 0.5f) blur = 0.5f;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    r = clampf(r, 0, fminf(hw, hh));
    int x0 = (int)floorf(x - blur), x1 = (int)ceilf(x + w + blur);
    int y0 = (int)floorf(y - blur), y1 = (int)ceilf(y + h + blur);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float d = sd_rrect(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            float t = clampf(0.5f - d / (2 * blur), 0, 1);
            blend(fb, px, py, c, t * t * (3 - 2 * t) * alpha);
        }
}

void g_line(Fb *fb, float x0, float y0, float x1, float y1, float th, uint32_t c, float alpha) {
    float minx = fminf(x0, x1) - th, maxx = fmaxf(x0, x1) + th;
    float miny = fminf(y0, y1) - th, maxy = fmaxf(y0, y1) + th;
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int py = (int)floorf(miny); py < (int)ceilf(maxy); py++)
        for (int px = (int)floorf(minx); px < (int)ceilf(maxx); px++) {
            float fx = px + 0.5f - x0, fy = py + 0.5f - y0;
            float t = len2 > 0 ? clampf((fx * dx + fy * dy) / len2, 0, 1) : 0;
            float ex = fx - dx * t, ey = fy - dy * t;
            float d = sqrtf(ex * ex + ey * ey) - th * 0.5f;
            blend(fb, px, py, c, clampf(0.5f - d, 0, 1) * alpha);
        }
}

void g_disc(Fb *fb, float cx, float cy, float r, uint32_t c, float alpha) {
    for (int py = (int)floorf(cy - r - 1); py <= (int)ceilf(cy + r + 1); py++)
        for (int px = (int)floorf(cx - r - 1); px <= (int)ceilf(cx + r + 1); px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            blend(fb, px, py, c, clampf(r + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1) * alpha);
        }
}

void g_arc(Fb *fb, float cx, float cy, float r, float th, float a0, float a1, uint32_t c, float alpha) {
    float R = r + th;
    for (int py = (int)floorf(cy - R); py <= (int)ceilf(cy + R); py++)
        for (int px = (int)floorf(cx - R); px <= (int)ceilf(cx + R); px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float a = atan2f(dy, dx);
            while (a < a0) a += 6.2831853f;
            while (a > a0 + 6.2831853f) a -= 6.2831853f;
            if (a > a1) continue;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - r) - th * 0.5f;
            blend(fb, px, py, c, clampf(0.5f - d, 0, 1) * alpha);
        }
}

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    float ex = bx - ax, ey = by - ay, l = sqrtf(ex * ex + ey * ey);
    return l > 0 ? ((px - ax) * ey - (py - ay) * ex) / l : 0;
}

void g_triangle(Fb *fb, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, float alpha) {
    if (edge(x0, y0, x1, y1, x2, y2) > 0) { float tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    int minx = (int)floorf(fminf(x0, fminf(x1, x2))) - 1, maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1;
    int miny = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
    for (int py = miny; py < maxy; py++)
        for (int px = minx; px < maxx; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float d = fmaxf(edge(x0, y0, x1, y1, fx, fy),
                            fmaxf(edge(x1, y1, x2, y2, fx, fy), edge(x2, y2, x0, y0, fx, fy)));
            blend(fb, px, py, c, clampf(0.5f - d, 0, 1) * alpha);
        }
}

/* ---- Text ------------------------------------------------------------- */
int font_next(const char *s, int *advance, const uint8_t **rows);

static unsigned utf8_cp(const char *s, int *n) {
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) { *n = 1; return u[0]; }
    if ((u[0] & 0xE0) == 0xC0 && u[1]) { *n = 2; return (unsigned)(u[0] & 31) << 6 | (u[1] & 63); }
    if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) {
        *n = 3;
        return (unsigned)(u[0] & 15) << 12 | (unsigned)(u[1] & 63) << 6 | (u[2] & 63);
    }
    *n = 1;
    return '?';
}

#ifdef OSPI_FREETYPE
typedef struct { unsigned cp; int size, w, h, left, top, adv; uint8_t *bm; } GCache;
enum { GC_N = 2048 };
static FT_Library ft;
static FT_Face faces[2];
static GCache gc[GC_N];

int g_font_init(const char *primary, const char *fallback) {
    if (!ft && FT_Init_FreeType(&ft)) return 0;
    const char *p[2] = { primary, fallback };
    for (int i = 0; i < 2; i++)
        if (p[i] && !faces[i] && FT_New_Face(ft, p[i], 0, &faces[i])) faces[i] = NULL;
    return faces[0] || faces[1];
}

static GCache *glyph(unsigned cp, int size) {
    unsigned hsh = (cp * 2654435761u ^ (unsigned)size * 40503u) % GC_N;
    GCache *g = &gc[hsh];
    if (g->bm && g->cp == cp && g->size == size) return g;
    if (g->cp == cp && g->size == size && g->adv) return g;
    FT_Face f = NULL;
    for (int i = 0; i < 2 && !f; i++)
        if (faces[i] && FT_Get_Char_Index(faces[i], cp)) f = faces[i];
    if (!f) f = faces[0] ? faces[0] : faces[1];
    if (!f) return NULL;
    FT_Set_Pixel_Sizes(f, 0, (FT_UInt)size);
    if (FT_Load_Char(f, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return NULL;
    FT_Bitmap *b = &f->glyph->bitmap;
    free(g->bm);
    *g = (GCache){ cp, size, (int)b->width, (int)b->rows, f->glyph->bitmap_left, f->glyph->bitmap_top,
                   (int)(f->glyph->advance.x >> 6), NULL };
    if (g->w * g->h > 0 && (g->bm = malloc((size_t)g->w * g->h))) {
        for (int y = 0; y < g->h; y++) memcpy(g->bm + y * g->w, b->buffer + y * b->pitch, (size_t)g->w);
    }
    return g;
}

static int ascender(int size) {
    FT_Face f = faces[0] ? faces[0] : faces[1];
    if (!f) return size;
    FT_Set_Pixel_Sizes(f, 0, (FT_UInt)size);
    return (int)(f->size->metrics.ascender >> 6);
}
#else
int g_font_init(const char *primary, const char *fallback) { (void)primary; (void)fallback; return 0; }
#endif

static int have_ft(void) {
#ifdef OSPI_FREETYPE
    return faces[0] || faces[1];
#else
    return 0;
#endif
}

float g_text_width(const char *s, float px) {
    float w = 0;
    int size = (int)(px + 0.5f);
    while (s && *s) {
        int n;
        unsigned cp = utf8_cp(s, &n);
#ifdef OSPI_FREETYPE
        if (have_ft()) { GCache *g = glyph(cp, size); w += g ? g->adv : size * 0.5f; s += n; continue; }
#endif
        (void)cp; (void)size;
        w += px;  /* 8x8 cell scaled */
        s += n;
    }
    return w;
}

void g_text(Fb *fb, float x, float y, float px, uint32_t c, float alpha, const char *s) {
    int size = (int)(px + 0.5f);
    if (size < 4 || !s) return;
#ifdef OSPI_FREETYPE
    if (have_ft()) {
        int base = (int)floorf(y + 0.5f) + ascender(size);
        float pen = x;
        while (*s) {
            int n;
            GCache *g = glyph(utf8_cp(s, &n), size);
            s += n;
            if (!g) continue;
            int gx = (int)floorf(pen + 0.5f) + g->left, gy = base - g->top;
            for (int yy = 0; yy < g->h && g->bm; yy++)
                for (int xx = 0; xx < g->w; xx++)
                    blend(fb, gx + xx, gy + yy, c, g->bm[yy * g->w + xx] / 255.0f * alpha);
            pen += g->adv;
        }
        return;
    }
#endif
    float scale = px / 8.0f, pen = x;
    while (*s) {
        int adv = 1;
        const uint8_t *rows = NULL;
        if (!font_next(s, &adv, &rows) || adv <= 0) break;
        for (int yy = 0; yy < 8 && rows; yy++)
            for (int xx = 0; xx < 8; xx++)
                if (rows[yy] >> xx & 1)
                    g_rrect(fb, pen + xx * scale, y + yy * scale, scale, scale, 0, c, c, alpha);
        pen += px;
        s += adv;
    }
}

void g_text_center(Fb *fb, float cx, float cy, float px, uint32_t c, float alpha, const char *s) {
    float w = g_text_width(s, px);
    float cap = have_ft() ? px * 0.36f : px * 0.5f;
    float top = have_ft() ? cy + cap - (float)px * 0.0f : cy - cap;
#ifdef OSPI_FREETYPE
    if (have_ft()) top = cy + cap - (float)ascender((int)(px + 0.5f));
#endif
    g_text(fb, cx - w * 0.5f, top, px, c, alpha, s);
}

float g_fit(const char *s, float px, float max_w) {
    float w = g_text_width(s, px);
    if (w <= max_w || w <= 0) return px;
    float f = px * max_w / w;
    while (f > 5 && g_text_width(s, f) > max_w) f -= 0.5f;
    return f;
}
