/* App icons, launch marks and the Control Strip glyph sheet, drawn in code.
 *
 *   build/mobile/mobile_icons mobile/assets
 *
 * The artwork is original: a folding phone half-closed like a laptop, its top
 * screen showing a tiny Platinum desktop, on the periwinkle OS999 desktop
 * colour. No Apple artwork, logos or fonts. Writes:
 *   ios/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png
 *   ios/Assets.xcassets/LaunchMark.imageset/LaunchMark@{2,3}x.png
 *   android/app/src/main/res/mipmap-<density>/ic_launcher{,_round,_foreground,_monochrome}.png
 *   assets/store/play-icon-512.png, assets/glyphs.png
 * (paths relative to the mobile/ directory, the argument's parent).
 */
#include "mobile_ui.h"
#include "kbd_gfx.h"
#include "mobile_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { L_FULL, L_FG, L_MONO };

typedef struct { float ox, oy, u; } Space;   /* 108-unit design space -> pixels */

static float X(const Space *s, float v) { return s->ox + v * s->u; }
static float Y(const Space *s, float v) { return s->oy + v * s->u; }

static void background(Fb *fb) {
    g_rrect(fb, 0, 0, fb->w, fb->h, 0, 0xFF7A8FD6u, 0xFF3B4C92u, 1);
    g_disc(fb, fb->w * 0.5f, fb->h * 0.36f, fb->w * 0.42f, 0xFFFFFFFFu, 0.10f);
    g_disc(fb, fb->w * 0.5f, fb->h * 0.36f, fb->w * 0.28f, 0xFFFFFFFFu, 0.07f);
}

/* The folded phone. mono: one flat colour with the screen cut back to show depth. */
static void phone(Fb *fb, const Space *s, int mono) {
    uint32_t body = mono ? 0xFFFFFFFFu : 0xFF26282Eu, edge = mono ? 0xFFFFFFFFu : 0xFF4A4D55u;
    float u = s->u;
    if (!mono) g_shadow(fb, X(s, 20), Y(s, 72), 68 * u, 12 * u, 6 * u, 6 * u, 0xFF000000u, 0.35f);
    /* deck, seen from above at an angle: a trapezoid */
    g_rrect(fb, X(s, 30), Y(s, 60), 48 * u, 21 * u, 0, body, body, 1);   /* overlaps the sides: no seams */
    g_triangle(fb, X(s, 31), Y(s, 60), X(s, 31), Y(s, 81), X(s, 22), Y(s, 81), body, 1);
    g_triangle(fb, X(s, 77), Y(s, 60), X(s, 86), Y(s, 81), X(s, 77), Y(s, 81), body, 1);
    if (!mono) {
        uint32_t key = 0xFFDCDCE0u;
        for (int r = 0; r < 3; r++) {
            float y = 63.5f + r * 4.4f, inset = 2.5f + (2 - r) * 1.4f;
            for (int k = 0; k < 9; k++) {
                float x0 = 31 - (r * 1.5f) + inset, x1 = 77 + (r * 1.5f) - inset;
                float kw = (x1 - x0) / 9;
                g_rrect(fb, X(s, x0 + k * kw + 0.4f), Y(s, y), (kw - 0.8f) * u, 3.0f * u, 0.5f * u,
                        key, 0xFFB9B9C0u, 1);
            }
        }
        g_rrect(fb, X(s, 45), Y(s, 76.3f), 18 * u, 3.6f * u, 1 * u, 0xFFB0B2BAu, 0xFF8E9099u, 1);
    }
    /* hinge */
    g_rrect(fb, X(s, 30), Y(s, 57.5f), 48 * u, 3.4f * u, 1.7f * u, edge, body, 1);
    /* upright screen half */
    g_rrect(fb, X(s, 30), Y(s, 22), 48 * u, 37 * u, 3.2f * u, edge, body, 1);
    if (mono) {
        g_rrect(fb, X(s, 33), Y(s, 25), 42 * u, 30 * u, 1.2f * u, 0xFF000000u, 0xFF000000u, 1);
        g_rrect(fb, X(s, 38), Y(s, 33), 24 * u, 15 * u, 0.6f * u, 0xFFFFFFFFu, 0xFFFFFFFFu, 1);
        return;
    }
    float sx = 32.5f, sy = 24.5f, sw = 43, sh = 31;
    g_rrect(fb, X(s, sx), Y(s, sy), sw * u, sh * u, 1.2f * u, 0xFF6B82C4u, 0xFF4E64A6u, 1);
    g_rrect(fb, X(s, sx), Y(s, sy), sw * u, 3 * u, 0, 0xFFF2F2F2u, 0xFFE2E2E2u, 1);   /* menu bar */
    for (int i = 0; i < 4; i++)
        g_rrect(fb, X(s, sx + 5 + i * 4.3f), Y(s, sy + 1.1f), 3 * u, 0.8f * u, 0.3f * u,
                0xFF333333u, 0xFF333333u, 1);
    g_rrect(fb, X(s, sx + 3.5f), Y(s, sy + 0.8f), 1.4f * u, 1.4f * u, 0.7f * u, 0xFF5AA02Cu, 0xFF3E7A1Cu, 1);
    /* a Platinum window */
    float wx = sx + 5, wy = sy + 7, ww = 25, wh = 18;
    g_shadow(fb, X(s, wx), Y(s, wy + 0.6f), ww * u, wh * u, 0.4f * u, 1.2f * u, 0xFF000000u, 0.35f);
    g_rrect(fb, X(s, wx), Y(s, wy), ww * u, wh * u, 0.3f * u, 0xFFFFFFFFu, 0xFFF0F0F0u, 1);
    g_rrect(fb, X(s, wx), Y(s, wy), ww * u, 3 * u, 0, 0xFFE4E4E4u, 0xFFCFCFCFu, 1);
    for (int i = 0; i < 3; i++)
        g_line(fb, X(s, wx + 3), Y(s, wy + 0.8f + i * 0.7f), X(s, wx + ww - 1.5f), Y(s, wy + 0.8f + i * 0.7f),
               0.25f * u, 0xFF9A9A9Au, 1);
    g_rrect_stroke(fb, X(s, wx), Y(s, wy), ww * u, wh * u, 0.3f * u, 0.35f * u, 0xFF222222u, 1);
    for (int i = 0; i < 3; i++) {   /* folder icons */
        float fx = wx + 3 + i * 7.5f, fy = wy + 7;
        g_rrect(fb, X(s, fx), Y(s, fy), 5 * u, 3.6f * u, 0.4f * u, 0xFFA8A6F2u, 0xFF8583D8u, 1);
        g_rrect(fb, X(s, fx), Y(s, fy - 0.8f), 2.2f * u, 1.2f * u, 0.3f * u, 0xFFC4C2FFu, 0xFFC4C2FFu, 1);
        g_line(fb, X(s, fx), Y(s, fy + 6), X(s, fx + 5), Y(s, fy + 6), 0.5f * u, 0xFF777777u, 1);
    }
    g_disc(fb, X(s, 54), Y(s, 23.2f), 0.7f * u, 0xFF0D0D10u, 1);   /* selfie camera in the bezel */
    /* disk icon top right */
    g_rrect(fb, X(s, sx + 35), Y(s, sy + 5), 5.5f * u, 3.5f * u, 0.4f * u, 0xFFE8E8E8u, 0xFFB8B8B8u, 1);
}

/* Draw `layer` on a solid colour (used twice for matting). */
static void render(uint32_t *px, int size, int layer, float fg_scale, uint32_t base) {
    Fb fb;
    fb = (Fb){.px = px, .w = size, .h = size};
    for (int i = 0; i < size * size; i++) px[i] = base;
    if (layer == L_FULL) background(&fb);
    float u = size / 108.0f * fg_scale;
    Space s = {size / 2.0f - 54 * u, size / 2.0f - 51 * u, u};
    phone(&fb, &s, layer == L_MONO);
}

/* Anti-aliased artwork with real alpha: draw on black and on white, then
 * alpha = 1 - (white - black). */
static uint32_t *render_alpha(int size, int layer, float fg_scale) {
    uint32_t *b = malloc((size_t)size * size * 4), *w = malloc((size_t)size * size * 4);
    if (!b || !w) { free(b); free(w); return NULL; }
    render(b, size, layer, fg_scale, 0xFF000000u);
    if (layer == L_MONO) {   /* white shape on black: brightness is the mask */
        for (int i = 0; i < size * size; i++) b[i] = ((b[i] >> 8) & 255) << 24 | 0xFFFFFFu;
        free(w);
        return b;
    }
    render(w, size, layer, fg_scale, 0xFFFFFFFFu);
    for (int i = 0; i < size * size; i++) {
        int diff = (int)((w[i] >> 8) & 255) - (int)((b[i] >> 8) & 255);
        int a = 255 - diff;
        if (a <= 0) { b[i] = 0; continue; }
        uint32_t out = (uint32_t)a << 24;
        for (int sh = 0; sh < 24; sh += 8) {
            int c = (int)((b[i] >> sh) & 255) * 255 / a;
            out |= (uint32_t)(c > 255 ? 255 : c) << sh;
        }
        b[i] = out;
    }
    free(w);
    return b;
}

static void mkdirs(const char *path) {
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *q = p + 1; *q; q++)
        if (*q == '/') { *q = 0; mkdir(p, 0755); *q = '/'; }
}

static void out_png(const char *root, const char *rel, const uint32_t *px, int w, int h, int alpha) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    mkdirs(path);
    if (mobile_png_write(path, px, w, h, alpha) != 0) fprintf(stderr, "could not write %s\n", path);
    else printf("wrote %s\n", rel);
}

static void opaque(const char *root, const char *rel, int size, float fg_scale, int round) {
    uint32_t *px = malloc((size_t)size * size * 4);
    if (!px) return;
    render(px, size, L_FULL, fg_scale, 0xFF000000u);
    if (round) {   /* legacy round launcher: clear outside the circle */
        float r = size / 2.0f;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                float dx = x + 0.5f - r, dy = y + 0.5f - r, d = dx * dx + dy * dy;
                if (d > r * r) px[y * size + x] = 0;
            }
    }
    out_png(root, rel, px, size, size, round);
    free(px);
}

static void glyph_sheet(const char *root) {
    int cell = 120, cols = MS_TILE_COUNT, rows = 3, w = cell * cols, h = cell * rows;
    uint32_t *px = malloc((size_t)w * h * 4);
    if (!px) return;
    Fb fb;
    fb = (Fb){.px = px, .w = w, .h = h};
    g_rrect(&fb, 0, 0, w, h, 0, 0xFFE9E9EEu, 0xFFE9E9EEu, 1);
    MobileStripState st[3] = {
        {0, 0, 1.0f, 540, 1126, MP_COVER, 0, "Platinum", 1},
        {1, 1, 2.0f, 692, 684, MP_LAPTOP, 1, "Graphite", 0},
        {0, 0, 1.0f, 1043, 498, MP_BOOK, 0, "Teal", 1}};
    char buf[32];
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            float x = c * cell + 12.0f, y = r * cell + 8.0f;
            mobile_platinum_box(&fb, x, y, cell - 24.0f, cell - 16.0f, 8, 0);
            mobile_glyph(&fb, (MobileStripTile)c, x + 20, y + 8, cell - 64.0f, &st[r], MUI_INK);
            const char *label = mobile_strip_label((MobileStripTile)c, &st[r], buf, sizeof buf);
            g_text_center(&fb, x + (cell - 24) / 2.0f, y + cell - 30, g_fit(label, 14, cell - 30.0f),
                          MUI_INK, 1, label);
        }
    out_png(root, "assets/glyphs.png", px, w, h, 0);
    free(px);
}

int main(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : "mobile/assets";
    char root[512];   /* mobile/ */
    snprintf(root, sizeof root, "%s", assets);
    char *slash = strrchr(root, '/');
    if (slash) *slash = 0; else snprintf(root, sizeof root, ".");
    g_font_init("fonts/ChicagoFLF.ttf", NULL);

    opaque(root, "ios/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png", 1024, 1.25f, 0);
    opaque(root, "assets/store/play-icon-512.png", 512, 1.25f, 0);
    for (int k = 2; k <= 3; k++) {
        char rel[256];
        uint32_t *m = render_alpha(120 * k, L_FG, 1.35f);
        snprintf(rel, sizeof rel, "ios/Assets.xcassets/LaunchMark.imageset/LaunchMark@%dx.png", k);
        if (m) out_png(root, rel, m, 120 * k, 120 * k, 1);
        free(m);
    }
    static const struct { const char *name; int scale4; } dens[] = {
        {"mdpi", 4}, {"hdpi", 6}, {"xhdpi", 8}, {"xxhdpi", 12}, {"xxxhdpi", 16}};
    for (unsigned i = 0; i < sizeof dens / sizeof *dens; i++) {
        char rel[256];
        int legacy = 48 * dens[i].scale4 / 4, adaptive = 108 * dens[i].scale4 / 4;
        snprintf(rel, sizeof rel, "android/app/src/main/res/mipmap-%s/ic_launcher.png", dens[i].name);
        opaque(root, rel, legacy, 1.25f, 0);
        snprintf(rel, sizeof rel, "android/app/src/main/res/mipmap-%s/ic_launcher_round.png", dens[i].name);
        opaque(root, rel, legacy, 1.1f, 1);
        uint32_t *fg = render_alpha(adaptive, L_FG, 1.0f);
        snprintf(rel, sizeof rel, "android/app/src/main/res/mipmap-%s/ic_launcher_foreground.png", dens[i].name);
        if (fg) out_png(root, rel, fg, adaptive, adaptive, 1);
        free(fg);
        uint32_t *mono = render_alpha(adaptive, L_MONO, 1.0f);
        snprintf(rel, sizeof rel, "android/app/src/main/res/mipmap-%s/ic_launcher_monochrome.png", dens[i].name);
        if (mono) out_png(root, rel, mono, adaptive, adaptive, 1);
        free(mono);
    }
    glyph_sheet(root);
    return 0;
}
