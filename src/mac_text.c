#include "mac_runtime.h"
#include "mac_text.h"
#include <string.h>

/* QuickDraw text: GetFontInfo ($A88B), StringWidth ($A88C), CharWidth ($A88D), TextWidth ($A886), SpaceExtra ($A88E), DrawChar
 * ($A883), DrawString ($A884) and DrawText ($A885). Glyphs come only from bitmap strikes in the guest application's own resources
 * (mac_font.c); there is no system font, no scaling, and a family/size the guest does not supply halts by name.
 *
 * Current port fields: txFont +68, txFace +70 (byte), txMode +72, txSize +74, spExtra +76 (Fixed), pen at +48 (v, h), pen hidden when
 * pnVis (+66) is negative, fgColor +80 and bkColor +84 (device indices). Face: plain, bold (synthesised: each glyph is drawn twice,
 * one pixel apart, and advances one pixel more) and underline (a line one row below the baseline across the run) are supported;
 * italic, outline, shadow, condense and extend halt. SpaceExtra adds its whole-pixel value to the width of each space; a fractional
 * part halts. Drawing is to an indexed 8-bit colour port only, clipped to portRect, the pixmap bounds, visRgn and clipRgn (rectangular
 * regions); transfer modes srcCopy, srcOr, srcXor and srcBic are supported and the "not" modes halt. Metrics work on any port. */
typedef struct { int t, l, b, r; } Box;
typedef MacTextFace Face;

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }

int mac_text_face(MacRuntime *rt, int family, int size, unsigned face, int32_t extra, MacTextFace *f) {
    if (face & ~5u) return fail(rt, "Text style other than plain, bold and underline is unsupported.");
    if (extra & 0xffff) return fail(rt, "SpaceExtra with a fractional part is unsupported.");
    const char *why;
    if (mac_font_find(&rt->fork, family, size, 0, &f->s, &why)) return fail(rt, why);
    f->bold = face & 1; f->underline = (face & 4) != 0; f->extra = extra >> 16;
    return 0;
}
static int face_for(MacRuntime *rt, uint32_t port, Face *f) {
    return mac_text_face(rt, (int16_t)rd(rt, port + 68, 2), (int16_t)rd(rt, port + 74, 2), rd(rt, port + 70, 1), (int32_t)rd(rt, port + 76, 4), f);
}
int mac_text_advance(const MacTextFace *f, unsigned ch) {
    MacGlyph g; mac_strike_glyph(&f->s, ch, &g);
    return g.width + f->bold + (ch == ' ' ? f->extra : 0);
}
static int range_width(MacRuntime *rt, const Face *f, uint32_t at, int count) {
    int w = 0;
    for (int i = 0; i < count; i++) w += mac_text_advance(f, rd(rt, at + (uint32_t)i, 1));
    return w;
}
static int box_of(MacRuntime *rt, uint32_t p, Box *b) {
    if (!ok(rt, p, 8) || (p & 1)) return fail(rt, "Text clip rectangle is outside guest RAM.");
    *b = (Box){(int16_t)rd(rt, p, 2), (int16_t)rd(rt, p + 2, 2), (int16_t)rd(rt, p + 4, 2), (int16_t)rd(rt, p + 6, 2)};
    return 0;
}
static Box meet(Box a, Box b) { if (b.t > a.t) a.t = b.t; if (b.l > a.l) a.l = b.l; if (b.b < a.b) a.b = b.b; if (b.r < a.r) a.r = b.r; return a; }
static int region_box(MacRuntime *rt, uint32_t h, Box *b) {
    if (!ok(rt, h, 4) || (h & 1)) return fail(rt, "Text clip region handle is invalid.");
    uint32_t p = rd(rt, h, 4);
    if (!ok(rt, p, 10) || (p & 1)) return fail(rt, "Text clip region is invalid.");
    if (rd(rt, p, 2) != 10) return fail(rt, "Complex clipping regions are unsupported for text.");
    return box_of(rt, p + 2, b);
}

typedef struct { uint8_t *base; unsigned row; Box bounds, clip; unsigned fg, bg; int mode; } Target;
static int target_for(MacRuntime *rt, uint32_t port, int mode, Target *t) {
    if ((rd(rt, port + 6, 2) & 0xc000) != 0xc000) return fail(rt, "Text drawing on a basic (non-colour) port is unsupported.");
    uint32_t h = rd(rt, port + 2, 4);
    if (!ok(rt, h, 4) || (h & 1)) return fail(rt, "Text port pixmap handle is invalid.");
    uint32_t pm = rd(rt, h, 4);
    if (!ok(rt, pm, 50) || (pm & 1)) return fail(rt, "Text port pixmap is invalid.");
    unsigned row = rd(rt, pm + 4, 2);
    if ((row & 0xc000) != 0x8000 || rd(rt, pm + 30, 2) || rd(rt, pm + 32, 2) != 8 || rd(rt, pm + 34, 2) != 1 || rd(rt, pm + 36, 2) != 8 || rd(rt, pm + 16, 2))
        return fail(rt, "Text drawing supports unpacked indexed 8-bit pixmaps only.");
    row &= 0x3fff;
    Box rect, bounds, r;
    if (box_of(rt, pm + 6, &bounds) || box_of(rt, port + 16, &rect)) return -1;
    int w = bounds.r - bounds.l, hgt = bounds.b - bounds.t;
    uint32_t base = rd(rt, pm, 4);
    if (w < 0 || hgt < 0 || (unsigned)w > row || !ok(rt, base, (size_t)row * (unsigned)hgt)) return fail(rt, "Text port pixel memory is invalid.");
    Box clip = meet(rect, bounds);
    for (unsigned off = 24; off <= 28; off += 4) { if (region_box(rt, rd(rt, port + off, 4), &r)) return -1; clip = meet(clip, r); }
    t->base = mac_cpu_ram(rt->cpu) + base; t->row = row; t->bounds = bounds; t->clip = clip;
    t->fg = rd(rt, port + 80, 4); t->bg = rd(rt, port + 84, 4); t->mode = mode < 0 ? (int)rd(rt, port + 72, 2) : mode;
    if (t->fg > 255 || t->bg > 255) return fail(rt, "Text colour indices exceed the 8-bit palette.");
    if (t->mode < 0 || t->mode > 3) return fail(rt, "Text transfer modes other than srcCopy, srcOr, srcXor and srcBic are unsupported.");
    return 0;
}
static void put(const Target *t, int x, int y, int ink, int cell) {
    if (x < t->clip.l || x >= t->clip.r || y < t->clip.t || y >= t->clip.b) return;
    uint8_t *p = t->base + (size_t)(y - t->bounds.t) * t->row + (unsigned)(x - t->bounds.l);
    switch (t->mode) {
    case 0: if (ink || cell) *p = (uint8_t)(ink ? t->fg : t->bg); break;
    case 1: if (ink) *p = (uint8_t)t->fg; break;
    case 2: if (ink) *p ^= 255; break;
    default: if (ink) *p = (uint8_t)t->bg; break;
    }
}
/* One glyph at pen (h, v), advancing by adv; mirrors the synthesised bold by a second pass one pixel to the right. */
static void draw_glyph(const Target *t, const Face *f, unsigned ch, int h, int v) {
    MacGlyph g; mac_strike_glyph(&f->s, ch, &g);
    int adv = g.width + f->bold + (ch == ' ' ? f->extra : 0);
    int top = v - f->s.ascent, rows = f->s.rectH;
    int x0 = h < h + g.left ? h : h + g.left, x1 = h + adv > h + g.left + g.imgWidth + f->bold ? h + adv : h + g.left + g.imgWidth + f->bold;
    for (int r = 0; r < rows; r++)
        for (int x = x0; x < x1; x++) {
            int col = x - (h + g.left), ink = 0;
            if (g.present) {
                if (col >= 0 && col < g.imgWidth) ink = mac_strike_bit(&f->s, g.imgCol + col, r);
                if (f->bold && col - 1 >= 0 && col - 1 < g.imgWidth) ink |= mac_strike_bit(&f->s, g.imgCol + col - 1, r);
            }
            put(t, x, top + r, ink, x >= h && x < h + adv);
        }
}

static void clip_to(Target *t, const int *clip) {
    if (clip) t->clip = meet(t->clip, (Box){clip[0], clip[1], clip[2], clip[3]});
}
int mac_text_draw_with(MacRuntime *rt, uint32_t port, const MacTextFace *f, int mode, int h, int v, uint32_t at, int count, const int *clip) {
    Target t;
    if (target_for(rt, port, mode, &t)) return -1;
    clip_to(&t, clip);
    int start = h;
    for (int i = 0; i < count; i++) {
        unsigned ch = rd(rt, at + (uint32_t)i, 1);
        draw_glyph(&t, f, ch, h, v);
        h += mac_text_advance(f, ch);
    }
    if (f->underline) for (int x = start; x < h; x++) put(&t, x, v + 1, 1, 0);
    return 0;
}
static int paint_rect(MacRuntime *rt, uint32_t port, int mode, const int rect[4], const int *clip, int xor_it) {
    Target t;
    if (target_for(rt, port, mode, &t)) return -1;
    clip_to(&t, clip);
    Box r = meet((Box){rect[0], rect[1], rect[2], rect[3]}, t.clip);
    for (int y = r.t; y < r.b; y++)
        for (int x = r.l; x < r.r; x++) {
            uint8_t *p = t.base + (size_t)(y - t.bounds.t) * t.row + (unsigned)(x - t.bounds.l);
            *p = xor_it ? (uint8_t)(*p ^ 255) : (uint8_t)t.bg;
        }
    return 0;
}
int mac_text_xor_rect(MacRuntime *rt, uint32_t port, const int rect[4], const int *clip) { return paint_rect(rt, port, 2, rect, clip, 1); }
int mac_text_erase_rect(MacRuntime *rt, uint32_t port, const int rect[4], const int *clip) { return paint_rect(rt, port, 0, rect, clip, 0); }

static int draw_run(MacRuntime *rt, uint32_t port, uint32_t at, int count) {
    Face f;
    if (face_for(rt, port, &f)) return -1;
    int hidden = (int16_t)rd(rt, port + 66, 2) < 0;
    int v = (int16_t)rd(rt, port + 48, 2), h = (int16_t)rd(rt, port + 50, 2);
    if (!hidden && mac_text_draw_with(rt, port, &f, -1, h, v, at, count, NULL)) return -1;
    for (int i = 0; i < count; i++) h += mac_text_advance(&f, rd(rt, at + (uint32_t)i, 1));
    wr(rt, port + 50, 2, (uint16_t)h);
    return 0;
}

int mac_text_service(MacRuntime *rt, uint16_t op) {
    if (!((op >= 0xa883 && op <= 0xa886) || (op >= 0xa88b && op <= 0xa88e))) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), port = rt->current_port;
    unsigned need = (op == 0xa883 || op == 0xa88d) ? 4 : (op == 0xa885 || op == 0xa886) ? 10 : 6;
    if ((sp & 1) || !ok(rt, sp, need)) return fail(rt, "Text call has an invalid stack.");
    if (port < 0x400 || (port & 1) || !ok(rt, port, 108)) return fail(rt, "Text call needs a current port.");
    if (op == 0xa88e) { wr(rt, port + 76, 4, rd(rt, sp, 4)); mac_cpu_set(rt->cpu, MAC_A7, sp + 4); return 1; }
    if (op == 0xa883) { /* DrawChar: a byte in the high half of the argument word */
        uint32_t tmp = sp; /* the byte lives in the stack itself */
        if (draw_run(rt, port, tmp, 1)) return -1;
        mac_cpu_set(rt->cpu, MAC_A7, sp + 2); return 1;
    }
    Face f;
    if (op == 0xa88b) { /* GetFontInfo(VAR info): ascent, descent, widMax, leading */
        uint32_t info = rd(rt, sp, 4);
        if (!ok(rt, info, 8) || (info & 1)) return fail(rt, "GetFontInfo record is outside guest RAM.");
        if (face_for(rt, port, &f)) return -1;
        wr(rt, info, 2, (uint16_t)f.s.ascent); wr(rt, info + 2, 2, (uint16_t)f.s.descent);
        wr(rt, info + 4, 2, (uint16_t)(f.s.widMax + f.bold)); wr(rt, info + 6, 2, (uint16_t)f.s.leading);
        mac_cpu_set(rt->cpu, MAC_A7, sp + 4); return 1;
    }
    if (op == 0xa88d) { /* CharWidth(ch): result above the argument */
        if (face_for(rt, port, &f)) return -1;
        wr(rt, sp + 2, 2, (uint16_t)mac_text_advance(&f, rd(rt, sp, 1)));
        mac_cpu_set(rt->cpu, MAC_A7, sp + 2); return 1;
    }
    if (op == 0xa884 || op == 0xa88c) { /* DrawString / StringWidth: a Pascal string */
        uint32_t s = rd(rt, sp, 4);
        if (!ok(rt, s, 1) || !ok(rt, s, 1u + rd(rt, s, 1))) return fail(rt, "Text string is outside guest RAM.");
        int n = (int)rd(rt, s, 1);
        if (op == 0xa884) { if (draw_run(rt, port, s + 1, n)) return -1; mac_cpu_set(rt->cpu, MAC_A7, sp + 4); return 1; }
        if (face_for(rt, port, &f)) return -1;
        wr(rt, sp + 4, 2, (uint16_t)range_width(rt, &f, s + 1, n));
        mac_cpu_set(rt->cpu, MAC_A7, sp + 4); return 1;
    }
    /* DrawText / TextWidth (textBuf, firstByte, byteCount) */
    int count = (int16_t)rd(rt, sp, 2), first = (int16_t)rd(rt, sp + 2, 2);
    uint32_t buf = rd(rt, sp + 4, 4);
    if (count < 0 || first < 0 || !ok(rt, buf, (size_t)first + (size_t)count)) return fail(rt, "Text buffer is outside guest RAM.");
    if (op == 0xa885) { if (draw_run(rt, port, buf + (uint32_t)first, count)) return -1; mac_cpu_set(rt->cpu, MAC_A7, sp + 8); return 1; }
    if (face_for(rt, port, &f)) return -1;
    wr(rt, sp + 8, 2, (uint16_t)range_width(rt, &f, buf + (uint32_t)first, count));
    mac_cpu_set(rt->cpu, MAC_A7, sp + 8); return 1;
}
