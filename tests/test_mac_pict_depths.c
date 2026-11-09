#include "mac_pict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PICT version 2 PackBitsRect with indexed PixMaps of 1, 2, 4 and 8 bits per pixel. The destination palette holds 256
 * distinct colors, and the picture's color table uses the same colors, so each decoded pixel must equal the palette
 * index the source color maps to. Row padding bits must never be treated as color data. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

typedef struct { uint8_t b[65536]; size_t n; } W;
static void byte(W *w, unsigned v) { if (w->n < sizeof w->b) w->b[w->n++] = (uint8_t)v; }
static void word(W *w, unsigned v) { byte(w, v >> 8); byte(w, v); }
static void lng(W *w, uint32_t v) { word(w, v >> 16); word(w, v & 0xffff); }
static void rect(W *w, int t, int l, int b, int r) { word(w, (unsigned)t); word(w, (unsigned)l); word(w, (unsigned)b); word(w, (unsigned)r); }

typedef struct {
    unsigned width, height, bits;      /* PixMap bounds and depth */
    unsigned row;                      /* rowBytes (even); 0 = the smallest even size that holds the row */
    unsigned colors;                   /* color table entries */
    unsigned pad_value;                /* value written into the padding bits of every row */
    unsigned pixel_type, comp_size;    /* overrides for rejection cases (0 = valid) */
    int dst_scale;                     /* destination rect = source scaled by this (1 = same size) */
} Spec;

/* The source color for index i is (i*257, 65535-i*257, 7*i) and the palette entry for device index i is the same. */
static unsigned color_r(unsigned i) { return (i * 257u) & 0xffff; }
static unsigned color_g(unsigned i) { return (65535u - i * 257u) & 0xffff; }
static unsigned color_b(unsigned i) { return (7u * i) & 0xffff; }
/* Index of pixel (x, y) in the test image: distinct per position within the depth's range. */
static unsigned pix(const Spec *s, unsigned x, unsigned y) { return (x * 3 + y * 5 + x / 3) % (s->colors ? s->colors : 1u << s->bits); }

static void picture(W *w, const Spec *s) {
    memset(w, 0, sizeof *w);
    word(w, 0); rect(w, 0, 0, (int)s->height * s->dst_scale, (int)s->width * s->dst_scale);
    word(w, 0x11); word(w, 0x2ff); word(w, 0xc00); lng(w, UINT32_MAX);
    lng(w, 0); lng(w, 0); lng(w, (uint32_t)(s->width * s->dst_scale) << 16); lng(w, (uint32_t)(s->height * s->dst_scale) << 16); lng(w, 0);
    unsigned minrow = (s->width * s->bits + 7) / 8, row = s->row ? s->row : (minrow + 1) & ~1u;
    unsigned colors = s->colors ? s->colors : 1u << s->bits;
    word(w, 0x98); word(w, 0x8000 | row); rect(w, 0, 0, (int)s->height, (int)s->width);
    word(w, 0); word(w, 0); lng(w, 0); lng(w, 72u << 16); lng(w, 72u << 16);
    word(w, s->pixel_type); word(w, s->bits); word(w, 1); word(w, s->comp_size ? s->comp_size : s->bits); lng(w, 0); lng(w, 0); lng(w, 0);
    lng(w, 123); word(w, 0x8000); word(w, colors - 1);                                    /* color table: seed, flags, size-1 */
    for (unsigned i = 0; i < colors; i++) { word(w, i); word(w, color_r(i)); word(w, color_g(i)); word(w, color_b(i)); }
    rect(w, 0, 0, (int)s->height, (int)s->width); rect(w, 0, 0, (int)(s->height * s->dst_scale), (int)(s->width * s->dst_scale)); word(w, 0);
    for (unsigned y = 0; y < s->height; y++) {
        uint8_t line[1024]; memset(line, 0, row);
        for (unsigned x = 0; x < s->width; x++) { unsigned off = x * s->bits; line[off / 8] |= (uint8_t)(pix(s, x, y) << (8 - s->bits - off % 8)); }
        for (unsigned bitpos = s->width * s->bits; bitpos < row * 8; bitpos += s->bits) {   /* padding bits */
            unsigned off = bitpos; if (off + s->bits > row * 8) break;
            line[off / 8] |= (uint8_t)((s->pad_value & ((1u << s->bits) - 1)) << (8 - s->bits - off % 8));
        }
        if (row < 8) for (unsigned i = 0; i < row; i++) byte(w, line[i]);                   /* tiny rows are stored raw */
        else {                                                                                /* PackBits: literal runs of up to 128 bytes */
            unsigned bytes = 0, at = 0, mark = (unsigned)w->n; if (row > 250) word(w, 0); else byte(w, 0);
            while (at < row) { unsigned n = row - at > 128 ? 128 : row - at; byte(w, n - 1); for (unsigned i = 0; i < n; i++) byte(w, line[at + i]); at += n; bytes += n + 1; }
            if (row > 250) { w->b[mark] = (uint8_t)(bytes >> 8); w->b[mark + 1] = (uint8_t)bytes; } else w->b[mark] = (uint8_t)bytes;
        }
    }
    if (w->n & 1) byte(w, 0);
    word(w, 0xff);
    w->b[0] = (uint8_t)(w->n >> 8); w->b[1] = (uint8_t)w->n;
}
static MacPictPalette palette(void) {
    MacPictPalette p; memset(&p, 0, sizeof p);
    for (unsigned i = 0; i < 256; i++) { p.rgb[i][0] = color_r(i); p.rgb[i][1] = color_g(i); p.rgb[i][2] = color_b(i); p.allowed[i] = 1; }
    return p;
}
static int decode(const Spec *s, unsigned ow, unsigned oh, MacPictImage *img, char *err) {
    static W w; picture(&w, s);
    MacPictPalette p = palette();
    return mac_pict_decode(w.b, w.n, ow, oh, &p, img, err, 160);
}
static int image_matches(const Spec *s, const MacPictImage *img) {
    for (unsigned y = 0; y < s->height; y++) for (unsigned x = 0; x < s->width; x++)
        if (!img->covered[y * s->width + x] || img->pixels[y * s->width + x] != pix(s, x, y)) return 0;
    return 1;
}

static void every_depth(void) {
    const unsigned widths[] = {1, 2, 3, 5, 7, 8, 9, 13, 16, 31, 64, 100};
    for (unsigned bits = 1; bits <= 8; bits *= 2) for (unsigned wi = 0; wi < sizeof widths / sizeof *widths; wi++) {
        Spec s = {.width = widths[wi], .height = 7, .bits = bits, .dst_scale = 1};
        MacPictImage img; char err[160];
        int rc = decode(&s, s.width, s.height, &img, err);
        CHECK(rc == 0 && image_matches(&s, &img));
        mac_pict_destroy(&img);
    }
}

static void padding_is_not_color(void) {
    /* A 3-color table at 4 bits: indices 3..15 are undefined. Padding nibbles hold such an index; they must be ignored. */
    Spec s = {.width = 13, .height = 5, .bits = 4, .colors = 3, .pad_value = 15, .dst_scale = 1};
    MacPictImage img; char err[160];
    CHECK(decode(&s, 13, 5, &img, err) == 0 && image_matches(&s, &img)); mac_pict_destroy(&img);
    Spec t = {.width = 5, .height = 4, .bits = 2, .colors = 3, .pad_value = 3, .dst_scale = 1};   /* 2 bits: only 3 of 4 indices defined */
    CHECK(decode(&t, 5, 4, &img, err) == 0 && image_matches(&t, &img)); mac_pict_destroy(&img);
    Spec u = {.width = 9, .height = 2, .bits = 1, .colors = 2, .pad_value = 1, .dst_scale = 1};
    CHECK(decode(&u, 9, 2, &img, err) == 0 && image_matches(&u, &img)); mac_pict_destroy(&img);
}

static void real_pixels_must_be_defined(void) {
    Spec s = {.width = 8, .height = 4, .bits = 4, .colors = 3, .dst_scale = 1};
    s.colors = 16;                                                                           /* build with all colors, then shrink the table */
    static W w; picture(&w, &s);
    /* Rebuild with a 3-entry table but pixel indices up to 15: pix() uses colors for its modulus, so patch the data instead. */
    Spec t = {.width = 8, .height = 4, .bits = 4, .colors = 3, .dst_scale = 1};
    picture(&w, &t);
    size_t at = 0;                                                                           /* find the first scanline: after the destination rect and mode */
    for (size_t i = 40; i + 6 < w.n; i++) if (w.b[i] == 0 && w.b[i + 1] == 0 && w.b[i + 2] == 0 && w.b[i + 3] == 0 && w.b[i + 4] == 0 && w.b[i + 5] == 4) { at = i; break; }
    (void)at;
    MacPictPalette p = palette(); MacPictImage img; char err[160];
    unsigned last = (unsigned)w.n - 4;                                                       /* last data byte before the optional pad and EndPic */
    for (size_t i = w.n - 10; i < w.n - 2; i++) if (w.b[i] != 0xff) { last = (unsigned)i; }
    w.b[last] = 0xff;                                                                        /* a real pixel pair now references index 15 */
    CHECK(mac_pict_decode(w.b, w.n, 8, 4, &p, &img, err, sizeof err) == -1 && strstr(err, "undefined"));
    CHECK(!img.pixels && !img.covered);
}

static void scaling_and_clipping(void) {
    Spec s = {.width = 8, .height = 4, .bits = 4, .dst_scale = 2};                          /* drawn at twice the size */
    MacPictImage img; char err[160];
    CHECK(decode(&s, 16, 8, &img, err) == 0);
    for (unsigned y = 0; y < 8; y++) for (unsigned x = 0; x < 16; x++)
        CHECK(img.covered[y * 16 + x] && img.pixels[y * 16 + x] == pix(&s, x / 2, y / 2));
    mac_pict_destroy(&img);
}

static void rejects_what_it_cannot_read(void) {
    struct { Spec s; const char *why; } cases[] = {
        {{.width = 8, .height = 2, .bits = 3, .colors = 4, .dst_scale = 1}, "3 bits"},
        {{.width = 8, .height = 2, .bits = 16, .colors = 4, .dst_scale = 1}, "16 bits"},
        {{.width = 8, .height = 2, .bits = 32, .colors = 4, .dst_scale = 1}, "32 bits"},
        {{.width = 8, .height = 2, .bits = 4, .comp_size = 8, .dst_scale = 1}, "cmpSize differs from pixelSize"},
        {{.width = 8, .height = 2, .bits = 4, .pixel_type = 16, .dst_scale = 1}, "direct pixels"},
        {{.width = 17, .height = 2, .bits = 4, .row = 8, .dst_scale = 1}, "row too small for the width"},
        {{.width = 8, .height = 2, .bits = 4, .row = 7, .dst_scale = 1}, "odd rowBytes"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        MacPictImage img; char err[160] = "";
        int rc = decode(&cases[i].s, cases[i].s.width, cases[i].s.height, &img, err);
        CHECK(rc == -1 && !img.pixels && !img.covered);
        if (rc != -1) fprintf(stderr, "  not rejected: %s\n", cases[i].why);
    }
}

static void truncation_never_leaks_or_overreads(void) {
    Spec s = {.width = 20, .height = 6, .bits = 4, .dst_scale = 1};
    static W w; picture(&w, &s);
    MacPictPalette p = palette();
    for (size_t n = 0; n < w.n; n++) {
        MacPictImage img; char err[160];
        CHECK(mac_pict_decode(w.b, n, 20, 6, &p, &img, err, sizeof err) == -1 && !img.pixels && !img.covered);
    }
}

int main(void) {
    every_depth();
    padding_is_not_color();
    real_pixels_must_be_defined();
    scaling_and_clipping();
    rejects_what_it_cannot_read();
    truncation_never_leaks_or_overreads();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("PICT depth checks ok");
    return 0;
}
