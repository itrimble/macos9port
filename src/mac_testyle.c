#include "mac_te_int.h"
#include "mac_handles.h"
#include <stdio.h>
#include <string.h>

/* Multistyled TextEdit: the style records and the TEDispatch selectors that work on them. Layouts and selector numbers are from Apple's public TextEdit interface
 * (TextEdit.h, the TextEdit chapter of Inside Macintosh: Text). TEStyleRec: nRuns 0, nStyles 2, styleTab 4, lhTab 8, teRefCon 12, nullStyle 16, runs from 20 (startChar, styleIndex) with one
 * more run after the last that holds the text length. STElement: stCount 0, stHeight 2, stAscent 4, stFont 6, stFace 8, stSize 10, stColor 12 (18 bytes). TextStyle: tsFont 0, tsFace 2,
 * tsSize 4, tsColor 6 (12 bytes). ScrpSTElement: scrpStartChar 0 (long), height 4, ascent 6, font 8, face 10, size 12, color 14 (20 bytes) after the count word of a StScrpRec.
 * Selectors: 1 TESetStyle, 2 TEReplaceStyle, 3 TEGetStyle, 4 TEGetStyleHandle, 6 TEGetStyleScrapHandle, 7 TEStyleInsert, 10 TEContinuousStyle, 11 TEUseStyleScrap, 13 TENumStyles; the others
 * (0 TEStylePaste, 5 TESetStyleHandle, 12 TECustomHook, 14 TEFeatureFlag) are not implemented and halt by name. */
enum { MAX_RUNS = 2000, MAX_ST = 512, SREC = 20, ELT = 18, SCRP = 20,
       DO_FONT = 1, DO_FACE = 2, DO_SIZE = 4, DO_COLOR = 8, ADD_SIZE = 16, DO_TOGGLE = 32 };
typedef struct { uint16_t count; int16_t height, ascent, font; uint8_t face; int16_t size; uint16_t rgb[3]; } Elt;
typedef struct { int16_t start, idx; } Run;
typedef struct {
    Run run[MAX_RUNS + 1]; int nruns; Elt st[MAX_ST]; int nst; int len;
    uint32_t rec, style_h, tab_h, lh_h, null_h;
} Sty;
static Sty S;

static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static int s16(MacRuntime *r, uint32_t p) { return (int16_t)rd(r, p, 2); }
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ram_ok(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
int mac_testyle_is(MacRuntime *rt, uint32_t rec) { return s16(rt, rec + TE_SIZE) == -1; }
static int block(MacRuntime *rt, uint32_t h, uint32_t min, uint32_t *p) { uint32_t n; return mac_handle_block(rt, h, p, &n) && n >= min && ram_ok(rt, *p, n); }
static void rgb_of(MacRuntime *rt, uint32_t p, uint16_t rgb[3]) { for (int i = 0; i < 3; i++) rgb[i] = (uint16_t)rd(rt, p + 2u * (unsigned)i, 2); }
static void put_rgb(MacRuntime *rt, uint32_t p, const uint16_t rgb[3]) { for (int i = 0; i < 3; i++) wr(rt, p + 2u * (unsigned)i, 2, rgb[i]); }
unsigned mac_testyle_fg(MacRuntime *rt, const uint16_t rgb[3]) {
    unsigned best = 0, bd = UINT32_MAX;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = rt->palette[i]; int dr = (int)((c >> 16) & 255) - (rgb[0] >> 8), dg = (int)((c >> 8) & 255) - (rgb[1] >> 8), db = (int)(c & 255) - (rgb[2] >> 8);
        unsigned d = (unsigned)(dr * dr + dg * dg + db * db);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
/* A style element for a TextStyle: height and ascent come from the font that will draw it. */
static int make_elt(MacRuntime *rt, int font, unsigned face, int size, const uint16_t rgb[3], Elt *e) {
    MacTextFace f;
    if (size < 1) size = 1;
    if (mac_text_face(rt, font, size, face & 5u, 0, &f)) return -1;
    e->count = 0; e->font = (int16_t)font; e->face = (uint8_t)face; e->size = (int16_t)size; memcpy(e->rgb, rgb, sizeof e->rgb);
    e->ascent = (int16_t)f.s.ascent; e->height = (int16_t)(f.s.ascent + f.s.descent + f.s.leading);
    return 0;
}
static int find_or_add(MacRuntime *rt, const Elt *e) {
    for (int i = 0; i < S.nst; i++) if (S.st[i].font == e->font && S.st[i].face == e->face && S.st[i].size == e->size && !memcmp(S.st[i].rgb, e->rgb, sizeof e->rgb)) return i;
    if (S.nst >= MAX_ST) return fail(rt, "A multistyled TextEdit record has more than 512 distinct styles (unsupported).");
    S.st[S.nst] = *e; return S.nst++;
}
/* ---- load and store the working copy ---- */
static int load(MacRuntime *rt, uint32_t rec) {
    S.rec = rec; S.style_h = rd(rt, rec + TE_FONT, 4); uint32_t p;
    if (!block(rt, S.style_h, SREC + 4, &p)) return fail(rt, "A multistyled edit record's style handle is not a live handle.");
    int nr = s16(rt, p), ns = s16(rt, p + 2);
    S.tab_h = rd(rt, p + 4, 4); S.lh_h = rd(rt, p + 8, 4); S.null_h = rd(rt, p + 16, 4); S.len = s16(rt, rec + TE_LEN);
    if (nr < 1 || nr > MAX_RUNS || ns < 1 || ns > MAX_ST || !block(rt, S.style_h, (uint32_t)(SREC + 4 * (nr + 1)), &p)) return fail(rt, "A multistyled edit record's style runs are inconsistent.");
    S.nruns = nr; S.nst = ns;
    for (int i = 0; i < nr; i++) { S.run[i].start = (int16_t)s16(rt, p + SREC + 4u * (unsigned)i); S.run[i].idx = (int16_t)s16(rt, p + SREC + 4u * (unsigned)i + 2); if (S.run[i].idx < 0 || S.run[i].idx >= ns) return fail(rt, "A style run names a style that does not exist."); }
    uint32_t t; if (!block(rt, S.tab_h, (uint32_t)(ELT * ns), &t)) return fail(rt, "A multistyled edit record's style table is not a live handle.");
    for (int i = 0; i < ns; i++) {
        uint32_t e = t + ELT * (unsigned)i; Elt *x = &S.st[i];
        x->count = (uint16_t)rd(rt, e, 2); x->height = (int16_t)s16(rt, e + 2); x->ascent = (int16_t)s16(rt, e + 4); x->font = (int16_t)s16(rt, e + 6); x->face = (uint8_t)rd(rt, e + 8, 1);
        x->size = (int16_t)s16(rt, e + 10); rgb_of(rt, e + 12, x->rgb);
    }
    return 0;
}
static int store(MacRuntime *rt) {
    int remap[MAX_ST], used = 0;                                          /* styles no run uses are dropped, the rest keep their order */
    for (int i = 0; i < S.nst; i++) S.st[i].count = 0;
    for (int i = 0; i < S.nruns; i++) S.st[S.run[i].idx].count++;
    for (int i = 0; i < S.nst; i++) remap[i] = S.st[i].count ? used++ : -1;
    for (int i = 0; i < S.nst; i++) if (remap[i] >= 0 && remap[i] != i) S.st[remap[i]] = S.st[i];
    for (int i = 0; i < S.nruns; i++) S.run[i].idx = (int16_t)remap[S.run[i].idx];
    S.nst = used ? used : 1;
    if (mac_te_set_size(rt, S.style_h, (uint32_t)(SREC + 4 * (S.nruns + 1))) || mac_te_set_size(rt, S.tab_h, (uint32_t)(ELT * S.nst))) return fail(rt, "TextEdit could not grow its style records.");
    uint32_t p, t; if (!block(rt, S.style_h, (uint32_t)(SREC + 4 * (S.nruns + 1)), &p) || !block(rt, S.tab_h, (uint32_t)(ELT * S.nst), &t)) return fail(rt, "A multistyled edit record's style records are invalid.");
    wr(rt, p, 2, (uint16_t)S.nruns); wr(rt, p + 2, 2, (uint16_t)S.nst);
    for (int i = 0; i < S.nruns; i++) { wr(rt, p + SREC + 4u * (unsigned)i, 2, (uint16_t)S.run[i].start); wr(rt, p + SREC + 4u * (unsigned)i + 2, 2, (uint16_t)S.run[i].idx); }
    wr(rt, p + SREC + 4u * (unsigned)S.nruns, 2, (uint16_t)S.len); wr(rt, p + SREC + 4u * (unsigned)S.nruns + 2, 2, 0);
    for (int i = 0; i < S.nst; i++) {
        uint32_t e = t + ELT * (unsigned)i; Elt *x = &S.st[i];
        wr(rt, e, 2, x->count); wr(rt, e + 2, 2, (uint16_t)x->height); wr(rt, e + 4, 2, (uint16_t)x->ascent); wr(rt, e + 6, 2, (uint16_t)x->font); wr(rt, e + 8, 1, x->face); wr(rt, e + 9, 1, 0);
        wr(rt, e + 10, 2, (uint16_t)x->size); put_rgb(rt, e + 12, x->rgb);
    }
    return 0;
}
static int run_at(int off) { int i = 0; while (i + 1 < S.nruns && S.run[i + 1].start <= off) i++; return i; }
static void merge(void) {
    int n = 0;
    for (int i = 0; i < S.nruns; i++) { if (n && S.run[n - 1].idx == S.run[i].idx) continue; if (n && S.run[n - 1].start == S.run[i].start) { S.run[n - 1] = S.run[i]; continue; } S.run[n++] = S.run[i]; }
    S.nruns = n ? n : 1; S.run[0].start = 0;
}
static int split(MacRuntime *rt, int pos) {                              /* a run boundary at pos */
    if (pos <= 0 || pos >= S.len) return 0;
    int i = run_at(pos);
    if (S.run[i].start == pos) return 0;
    if (S.nruns >= MAX_RUNS) return fail(rt, "A multistyled edit record has more than 2000 style runs (unsupported).");
    memmove(&S.run[i + 2], &S.run[i + 1], (size_t)(S.nruns - i - 1) * sizeof(Run));
    S.run[i + 1].start = (int16_t)pos; S.run[i + 1].idx = S.run[i].idx; S.nruns++;
    return 0;
}
/* ---- reading styles for TextEdit ---- */
int mac_testyle_seg(MacRuntime *rt, uint32_t rec, int off, int limit, TeSeg *out) {
    uint32_t h = rd(rt, rec + TE_FONT, 4), p, t;
    if (!block(rt, h, SREC + 4, &p)) return fail(rt, "A multistyled edit record's style handle is not a live handle.");
    int nr = s16(rt, p), ns = s16(rt, p + 2);
    if (nr < 1 || nr > MAX_RUNS || ns < 1 || ns > MAX_ST || !block(rt, h, (uint32_t)(SREC + 4 * (nr + 1)), &p)) return fail(rt, "A multistyled edit record's style runs are inconsistent.");
    int i = 0; while (i + 1 < nr && s16(rt, p + SREC + 4u * (unsigned)(i + 1)) <= off) i++;
    int end = i + 1 < nr ? s16(rt, p + SREC + 4u * (unsigned)(i + 1)) : limit, idx = s16(rt, p + SREC + 4u * (unsigned)i + 2);
    if (idx < 0 || idx >= ns || !block(rt, rd(rt, p + 4, 4), (uint32_t)(ELT * ns), &t)) return fail(rt, "A style run names a style that does not exist.");
    uint32_t e = t + ELT * (unsigned)idx;
    if (mac_text_face(rt, s16(rt, e + 6), s16(rt, e + 10) < 1 ? 1 : s16(rt, e + 10), rd(rt, e + 8, 1) & 5u, 0, &out->f)) return -1;
    out->end = end < limit ? end : limit; if (out->end <= off) out->end = off + 1 < limit ? off + 1 : limit;
    out->ascent = s16(rt, e + 4); out->height = s16(rt, e + 2); rgb_of(rt, e + 12, out->rgb);
    return 0;
}
int mac_testyle_line_h(MacRuntime *rt, uint32_t rec, int k, int *height, int *ascent) {
    uint32_t p;
    uint32_t sp; if (!block(rt, rd(rt, rec + TE_FONT, 4), SREC + 4, &sp)) return fail(rt, "A multistyled edit record's style handle is not a live handle.");
    if (!block(rt, rd(rt, sp + 8, 4), (uint32_t)(4 * (k + 1)), &p)) { *height = 0; *ascent = 0; return 0; }
    *height = s16(rt, p + 4u * (unsigned)k); *ascent = s16(rt, p + 4u * (unsigned)k + 2);
    return 0;
}
int mac_testyle_set_lines(MacRuntime *rt, uint32_t rec, const uint16_t *starts, int lines, int len) {
    uint32_t sp; if (!block(rt, rd(rt, rec + TE_FONT, 4), SREC + 4, &sp)) return fail(rt, "A multistyled edit record's style handle is not a live handle.");
    uint32_t lh = rd(rt, sp + 8, 4);
    if (mac_te_set_size(rt, lh, (uint32_t)(4 * (lines ? lines : 1)))) return fail(rt, "TextEdit could not grow its line-height table.");
    for (int k = 0; k < lines; k++) {
        int s = starts[k], e = k + 1 < lines ? starts[k + 1] : len, h = 0, a = 0, at = s;
        do {
            TeSeg sg; if (mac_testyle_seg(rt, rec, at, e > at ? e : at + 1, &sg)) return -1;
            if (sg.height > h) h = sg.height; if (sg.ascent > a) a = sg.ascent;
            at = sg.end;
        } while (at < e);
        uint32_t p; if (!block(rt, lh, (uint32_t)(4 * (k + 1)), &p)) return fail(rt, "TextEdit's line-height table is invalid.");
        wr(rt, p + 4u * (unsigned)k, 2, (uint16_t)h); wr(rt, p + 4u * (unsigned)k + 2, 2, (uint16_t)a);
    }
    return 0;
}
/* ---- creation and disposal ---- */
int mac_testyle_new(MacRuntime *rt, uint32_t hte, uint32_t rec) {
    (void)hte;
    uint32_t sh = mac_handle_new(rt, SREC + 8), th = mac_handle_new(rt, ELT), lh = mac_handle_new(rt, 4), nh = mac_handle_new(rt, 8), ns = mac_handle_new(rt, 2), p, t, n;
    if (!sh || !th || !lh || !nh || !ns || !block(rt, sh, SREC + 8, &p) || !block(rt, th, ELT, &t) || !block(rt, nh, 8, &n)) return fail(rt, "TEStyleNew could not allocate its style records.");
    Elt e; uint32_t port = rd(rt, rec + 82, 4), pal = rt->palette[rd(rt, port + 80, 4) & 255];
    uint16_t black[3] = {(uint16_t)(((pal >> 16) & 255) * 257), (uint16_t)(((pal >> 8) & 255) * 257), (uint16_t)((pal & 255) * 257)};   /* the default colour is the port's foreground */
    if (make_elt(rt, s16(rt, port + 68), rd(rt, port + 70, 1), s16(rt, port + 74), black, &e)) return -1;
    e.count = 1;
    wr(rt, t, 2, 1); wr(rt, t + 2, 2, (uint16_t)e.height); wr(rt, t + 4, 2, (uint16_t)e.ascent); wr(rt, t + 6, 2, (uint16_t)e.font); wr(rt, t + 8, 1, e.face); wr(rt, t + 10, 2, (uint16_t)e.size);
    wr(rt, p, 2, 1); wr(rt, p + 2, 2, 1); wr(rt, p + 4, 4, th); wr(rt, p + 8, 4, lh); wr(rt, p + 16, 4, nh);
    wr(rt, n + 4, 4, ns);
    wr(rt, rec + TE_FONT, 4, sh); wr(rt, rec + TE_SIZE, 2, 0xffff); wr(rt, rec + TE_LINE_H, 2, 0xffff); wr(rt, rec + TE_ASC, 2, 0xffff);
    return 0;
}
void mac_testyle_dispose(MacRuntime *rt, uint32_t rec) {
    uint32_t sh = rd(rt, rec + TE_FONT, 4), p;
    if (!block(rt, sh, SREC + 4, &p)) return;
    uint32_t nh = rd(rt, p + 16, 4), np;
    if (block(rt, nh, 8, &np) && rd(rt, np + 4, 4)) mac_handle_dispose(rt, rd(rt, np + 4, 4));
    if (nh) mac_handle_dispose(rt, nh);
    mac_handle_dispose(rt, rd(rt, p + 4, 4)); mac_handle_dispose(rt, rd(rt, p + 8, 4)); mac_handle_dispose(rt, sh);
}
/* ---- the null scrap: attributes set at an insertion point, for the next typed character ---- */
void mac_testyle_clear_null(MacRuntime *rt, uint32_t rec) {
    uint32_t sh = rd(rt, rec + TE_FONT, 4), p, np, sp;
    if (!block(rt, sh, SREC + 4, &p) || !block(rt, rd(rt, p + 16, 4), 8, &np)) return;
    if (block(rt, rd(rt, np + 4, 4), 2, &sp)) wr(rt, sp, 2, 0);
}
static int null_get(MacRuntime *rt, Elt *e) {                           /* 1 and *e when the null scrap holds a style */
    uint32_t np, sp;
    if (!block(rt, S.null_h, 8, &np) || !block(rt, rd(rt, np + 4, 4), 2 + SCRP, &sp) || s16(rt, sp) != 1) return 0;
    memset(e, 0, sizeof *e); e->font = (int16_t)s16(rt, sp + 2 + 8); e->face = (uint8_t)rd(rt, sp + 2 + 10, 1); e->size = (int16_t)s16(rt, sp + 2 + 12); rgb_of(rt, sp + 2 + 14, e->rgb);
    return make_elt(rt, e->font, e->face, e->size, e->rgb, e) == 0;
}
static int null_set(MacRuntime *rt, const Elt *e) {
    uint32_t np; if (!block(rt, S.null_h, 8, &np)) return fail(rt, "A multistyled edit record's null style is invalid.");
    uint32_t sh = rd(rt, np + 4, 4), sp;
    if (mac_te_set_size(rt, sh, 2 + SCRP) || !block(rt, sh, 2 + SCRP, &sp)) return fail(rt, "TextEdit could not grow its null scrap.");
    wr(rt, sp, 2, 1); wr(rt, sp + 2, 4, 0); wr(rt, sp + 2 + 4, 2, (uint16_t)e->height); wr(rt, sp + 2 + 6, 2, (uint16_t)e->ascent); wr(rt, sp + 2 + 8, 2, (uint16_t)e->font);
    wr(rt, sp + 2 + 10, 1, e->face); wr(rt, sp + 2 + 11, 1, 0); wr(rt, sp + 2 + 12, 2, (uint16_t)e->size); put_rgb(rt, sp + 2 + 14, e->rgb);
    return 0;
}
/* ---- edits ---- */
int mac_testyle_edit(MacRuntime *rt, uint32_t rec, int at, int removed, int added) {
    if (load(rt, rec)) return -1;
    int old_len = S.len - added + removed, inherit;                     /* S.len is the record's new length */
    Elt ne; int use_null = added > 0 && null_get(rt, &ne);
    inherit = S.run[run_at(at > 0 ? at - 1 : 0)].idx;
    if (use_null) { int k = find_or_add(rt, &ne); if (k < 0) return -1; inherit = k; }
    Run out[MAX_RUNS + 4]; int n = 0;
    #define ADD(s, i) do { if (n >= MAX_RUNS) return fail(rt, "A multistyled edit record has more than 2000 style runs (unsupported)."); out[n].start = (int16_t)(s); out[n].idx = (int16_t)(i); n++; } while (0)
    for (int i = 0; i < S.nruns; i++) if (S.run[i].start < at) ADD(S.run[i].start, S.run[i].idx);          /* what lies before the change */
    if (added) ADD(at, inherit);                                                                          /* the new bytes */
    for (int i = 0; i < S.nruns; i++) {                                                                   /* what lies after the removed bytes */
        int st = S.run[i].start, e = i + 1 < S.nruns ? S.run[i + 1].start : old_len, s2 = st > at + removed ? st : at + removed;
        if (e > at + removed) ADD(s2 - removed + added, S.run[i].idx);
    }
    #undef ADD
    if (!n) { out[0].start = 0; out[0].idx = (int16_t)inherit; n = 1; }
    memcpy(S.run, out, (size_t)n * sizeof(Run)); S.nruns = n; S.run[0].start = 0;
    merge();
    if (use_null) mac_testyle_clear_null(rt, rec);
    return store(rt);
}
/* ---- styles in a range ---- */
static void change(Elt *e, int mode, const uint8_t *ts, int toggle_on) {
    if (mode & DO_FONT) e->font = (int16_t)((ts[0] << 8) | ts[1]);
    if (mode & DO_FACE) { unsigned f = ts[2]; if (mode & DO_TOGGLE) e->face = (uint8_t)(toggle_on ? (e->face | f) : (e->face & ~f)); else e->face = (uint8_t)f; }
    if (mode & DO_SIZE) e->size = (int16_t)((ts[4] << 8) | ts[5]);
    if (mode & ADD_SIZE) { int v = e->size + (int16_t)((ts[4] << 8) | ts[5]); e->size = (int16_t)(v < 1 ? 1 : v); }
    if (mode & DO_COLOR) for (int i = 0; i < 3; i++) e->rgb[i] = (uint16_t)((ts[6 + 2 * i] << 8) | ts[7 + 2 * i]);
}
static int apply_range(MacRuntime *rt, int a, int b, int mode, const uint8_t *oldts, const uint8_t *ts) {
    if (a >= b || a < 0 || b > S.len) return 0;
    if (split(rt, a) || split(rt, b)) return -1;
    int toggle_on = 1;                                                                 /* toggle: remove the faces if every selected run already has them all, else add them */
    if ((mode & DO_TOGGLE) && (mode & DO_FACE)) {
        int all = 1;
        for (int i = 0; i < S.nruns; i++) if (S.run[i].start >= a && S.run[i].start < b && (S.st[S.run[i].idx].face & ts[2]) != ts[2]) all = 0;
        toggle_on = !all;
    }
    for (int i = 0; i < S.nruns; i++) {
        if (S.run[i].start < a || S.run[i].start >= b) continue;
        Elt e = S.st[S.run[i].idx];
        if (oldts) {                                                                   /* TEReplaceStyle: only styles that match the old attributes in the requested fields */
            if (((mode & DO_FONT) && e.font != (int16_t)((oldts[0] << 8) | oldts[1])) || ((mode & DO_FACE) && e.face != oldts[2]) || ((mode & DO_SIZE) && e.size != (int16_t)((oldts[4] << 8) | oldts[5]))) continue;
            if (mode & DO_COLOR) { int same = 1; for (int c = 0; c < 3; c++) same &= e.rgb[c] == (uint16_t)((oldts[6 + 2 * c] << 8) | oldts[7 + 2 * c]); if (!same) continue; }
        }
        change(&e, mode, ts, toggle_on);
        Elt ne; if (make_elt(rt, e.font, e.face, e.size, e.rgb, &ne)) return -1;
        int k = find_or_add(rt, &ne); if (k < 0) return -1;
        S.run[i].idx = (int16_t)k;
    }
    merge();
    return 0;
}
static int range_of(MacRuntime *rt, uint32_t rec, int *a, int *b) { *a = s16(rt, rec + TE_SELS); *b = s16(rt, rec + TE_SELE); return 0; }
static int apply_scrap(MacRuntime *rt, int base, int end, uint32_t hst) {                 /* a style scrap's elements, offsets relative to `base` */
    uint32_t sp, n;
    if (!hst) return 0;
    if (!block(rt, hst, 2, &sp)) return fail(rt, "A style scrap handle is not a live handle.");
    n = (uint32_t)s16(rt, sp);
    if (n > 1600 || !block(rt, hst, 2 + SCRP * n, &sp)) return fail(rt, "A style scrap is inconsistent.");
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = sp + 2 + SCRP * k; int s = base + (int)rd(rt, e, 4), t = k + 1 < n ? base + (int)rd(rt, e + SCRP, 4) : end;
        if (s < base || s > end) return fail(rt, "A style scrap element starts outside the text it styles.");
        if (t > end) t = end;
        Elt ne; uint16_t c[3]; rgb_of(rt, e + 14, c);
        if (make_elt(rt, s16(rt, e + 8), rd(rt, e + 10, 1), s16(rt, e + 12), c, &ne)) return -1;
        int idx = find_or_add(rt, &ne); if (idx < 0) return -1;
        if (s < t) { if (split(rt, s) || split(rt, t)) return -1; for (int i = 0; i < S.nruns; i++) if (S.run[i].start >= s && S.run[i].start < t) S.run[i].idx = (int16_t)idx; }
    }
    merge();
    return 0;
}
/* ---- TEDispatch selectors ---- */
int mac_testyle_dispatch(MacRuntime *rt, unsigned sel, uint32_t sp) {
    static const struct { unsigned sel, args, result; } shape[] = { {1, 12, 0}, {2, 16, 0}, {3, 18, 0}, {4, 4, 4}, {6, 4, 4}, {7, 16, 0}, {10, 12, 2}, {11, 18, 0}, {13, 12, 4} };
    unsigned args = 0, result = 0, k;
    for (k = 0; k < sizeof shape / sizeof shape[0]; k++) if (shape[k].sel == sel) { args = shape[k].args; result = shape[k].result; break; }
    if (k == sizeof shape / sizeof shape[0]) {
        if (sel == 0 || sel == 5 || sel == 12 || sel == 14) { char w[96]; snprintf(w, sizeof w, "TEDispatch selector %u (TEStylePaste, TESetStyleHandle, TECustomHook, TEFeatureFlag) is unsupported.", sel); return fail(rt, w); }
        return 0;
    }
    if (!ram_ok(rt, sp, 2 + args + result)) return fail(rt, "TEDispatch has an invalid stack.");
    uint32_t A = sp + 2, hte = rd(rt, A, 4), rec, res = A + args;
    if (mac_te_check(rt, hte, &rec)) return -1;
    int a, b, styled = mac_testyle_is(rt, rec); range_of(rt, rec, &a, &b);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2 + args);
    if (sel == 4) { if (!styled) return fail(rt, "TEGetStyleHandle on a monostyled edit record is unsupported."); wr(rt, res, 4, rd(rt, rec + TE_FONT, 4)); return 1; }
    if (!styled) {
        if (sel == 10) {   /* TEContinuousStyle on a monostyled record: the record's own attributes */
            uint32_t mp = rd(rt, A + 8, 4), sp2 = rd(rt, A + 4, 4);
            if (!ram_ok(rt, mp, 2) || !ram_ok(rt, sp2, 12)) return fail(rt, "TEContinuousStyle's pointers are outside guest RAM.");
            int mode = s16(rt, mp) & 15; wr(rt, sp2, 2, rd(rt, rec + TE_FONT, 2)); wr(rt, sp2 + 2, 1, rd(rt, rec + 76, 1)); wr(rt, sp2 + 3, 1, 0); wr(rt, sp2 + 4, 2, rd(rt, rec + TE_SIZE, 2));
            for (int i = 0; i < 6; i++) wr(rt, sp2 + 6 + 2u * (unsigned)i / 2 * 2, 2, 0);
            wr(rt, mp, 2, (uint16_t)mode); wr(rt, res, 1, 1); wr(rt, res + 1, 1, 0); return 1;
        }
        if (sel == 1 || sel == 2) return 1;                                             /* no effect on a monostyled record (the reference says so) */
        return fail(rt, "A multistyled TextEdit selector was called on a monostyled edit record.");
    }
    if (load(rt, rec)) return -1;
    int redraw = 0;
    switch (sel) {
    case 1: case 2: {                                                                   /* TESetStyle(mode, newStyle, redraw, hTE) / TEReplaceStyle(mode, oldStyle, newStyle, redraw, hTE) */
        int rep = sel == 2; int mode = s16(rt, A + (rep ? 14 : 10)); uint32_t ns = rd(rt, A + 6, 4), os = rep ? rd(rt, A + 10, 4) : 0;
        if (!ram_ok(rt, ns, 12) || (rep && !ram_ok(rt, os, 12))) return fail(rt, "A TextStyle pointer is outside guest RAM.");
        uint8_t nb[12], ob[12]; memcpy(nb, mac_cpu_ram(rt->cpu) + ns, 12); if (rep) memcpy(ob, mac_cpu_ram(rt->cpu) + os, 12);
        redraw = rd(rt, A + 4, 1) != 0;
        if (a == b && !rep) {                                                           /* an insertion point: the attributes wait in the null scrap */
            Elt e = S.st[S.run[run_at(a > 0 ? a - 1 : 0)].idx]; Elt cur; if (null_get(rt, &cur)) e = cur;
            change(&e, mode, nb, 1); Elt ne; if (make_elt(rt, e.font, e.face, e.size, e.rgb, &ne) || null_set(rt, &ne)) return -1;
            return 1;
        }
        if (apply_range(rt, a, b, mode, rep ? ob : NULL, nb) || store(rt)) return -1;
        break; }
    case 3: {                                                                           /* TEGetStyle(offset, VAR style, VAR lineHeight, VAR fontAscent, hTE) */
        int off = s16(rt, A + 16); uint32_t sp2 = rd(rt, A + 12, 4), lp = rd(rt, A + 8, 4), ap = rd(rt, A + 4, 4);
        if (!ram_ok(rt, sp2, 12) || !ram_ok(rt, lp, 2) || !ram_ok(rt, ap, 2)) return fail(rt, "TEGetStyle's pointers are outside guest RAM.");
        if (off < 0) off = 0; if (off > S.len) off = S.len;
        Elt e = S.st[S.run[run_at(off)].idx];
        wr(rt, sp2, 2, (uint16_t)e.font); wr(rt, sp2 + 2, 1, e.face); wr(rt, sp2 + 3, 1, 0); wr(rt, sp2 + 4, 2, (uint16_t)e.size); put_rgb(rt, sp2 + 6, e.rgb);
        int lines = s16(rt, rec + 94), line = 0; while (line + 1 < lines && s16(rt, rec + 96 + 2u * (unsigned)(line + 1)) <= off) line++;
        int h = e.height, asc = e.ascent; if (lines) mac_testyle_line_h(rt, rec, line, &h, &asc);
        wr(rt, lp, 2, (uint16_t)h); wr(rt, ap, 2, (uint16_t)asc);
        return 1; }
    case 7: {                                                                           /* TEStyleInsert(text, length, hST, hTE): before the selection, which is not changed */
        uint32_t text = rd(rt, A + 12, 4); long n = (int32_t)rd(rt, A + 8, 4), hst = rd(rt, A + 4, 4);
        if (n < 0 || n > 32000 || s16(rt, rec + TE_LEN) + n > 32000 || !ram_ok(rt, text, (size_t)n)) return fail(rt, "TEStyleInsert's text is too long or outside guest RAM.");
        if (mac_te_marks(rt, rec, 0) || mac_te_insert(rt, hte, a, text, (int)n)) return -1;
        rec = mac_te_rec(rt, hte);
        if (load(rt, rec) || apply_scrap(rt, a, a + (int)n, (uint32_t)hst) || store(rt)) return -1;
        if (mac_te_after_edit(rt, hte, a, 1)) return -1;
        return 1; }
    case 11: {                                                                          /* TEUseStyleScrap(rangeStart, rangeEnd, newStyles, redraw, hTE) */
        long rs = (int32_t)rd(rt, A + 14, 4), re = (int32_t)rd(rt, A + 10, 4); redraw = rd(rt, A + 4, 1) != 0;
        if (rs < 0) rs = 0; if (re > S.len) re = S.len; if (rs > re) return 1;
        if (apply_scrap(rt, (int)rs, (int)re, rd(rt, A + 6, 4)) || store(rt)) return -1;
        break; }
    case 13: {                                                                          /* TENumStyles(rangeStart, rangeEnd, hTE): LONGINT */
        long rs = (int32_t)rd(rt, A + 8, 4), re = (int32_t)rd(rt, A + 4, 4), cnt = 0;
        if (rs < 0) rs = 0; if (re > S.len) re = S.len;
        for (int i = run_at((int)rs); i < S.nruns && S.run[i].start < re; i++) cnt++;
        wr(rt, res, 4, (uint32_t)(re > rs ? cnt : 0)); return 1; }
    case 6: {                                                                           /* TEGetStyleScrapHandle(hTE): a new style scrap of the selection */
        int first = run_at(a), last = run_at(b > a ? b - 1 : a), cnt = last - first + 1;
        uint32_t h = mac_handle_new(rt, (uint32_t)(2 + SCRP * cnt)), p;
        if (!h || !block(rt, h, (uint32_t)(2 + SCRP * cnt), &p)) return fail(rt, "TEGetStyleScrapHandle could not allocate the scrap.");
        wr(rt, p, 2, (uint16_t)cnt);
        for (int i = 0; i < cnt; i++) {
            uint32_t e = p + 2 + SCRP * (unsigned)i; Elt *x = &S.st[S.run[first + i].idx]; int st = S.run[first + i].start - a;
            wr(rt, e, 4, (uint32_t)(st < 0 ? 0 : st)); wr(rt, e + 4, 2, (uint16_t)x->height); wr(rt, e + 6, 2, (uint16_t)x->ascent); wr(rt, e + 8, 2, (uint16_t)x->font);
            wr(rt, e + 10, 1, x->face); wr(rt, e + 11, 1, 0); wr(rt, e + 12, 2, (uint16_t)x->size); put_rgb(rt, e + 14, x->rgb);
        }
        wr(rt, res, 4, h); return 1; }
    case 10: {                                                                          /* TEContinuousStyle(VAR mode, VAR aStyle, hTE): BOOLEAN */
        uint32_t mp = rd(rt, A + 8, 4), sp2 = rd(rt, A + 4, 4);
        if (!ram_ok(rt, mp, 2) || !ram_ok(rt, sp2, 12)) return fail(rt, "TEContinuousStyle's pointers are outside guest RAM.");
        int mode = s16(rt, mp) & 15, out = 0; Elt base, cur;
        if (a == b) { if (null_get(rt, &base)) {} else base = S.st[S.run[run_at(a > 0 ? a - 1 : 0)].idx]; out = mode; }
        else {
            int first = run_at(a), last = run_at(b - 1); base = S.st[S.run[first].idx]; out = mode; unsigned common = base.face;
            for (int i = first + 1; i <= last; i++) {
                cur = S.st[S.run[i].idx]; common &= cur.face;
                if (cur.font != base.font) out &= ~DO_FONT;
                if (cur.size != base.size) out &= ~DO_SIZE;
                if (memcmp(cur.rgb, base.rgb, sizeof cur.rgb)) out &= ~DO_COLOR;
            }
            base.face = (uint8_t)common;
        }
        wr(rt, sp2, 2, (uint16_t)base.font); wr(rt, sp2 + 2, 1, base.face); wr(rt, sp2 + 3, 1, 0); wr(rt, sp2 + 4, 2, (uint16_t)base.size); put_rgb(rt, sp2 + 6, base.rgb);
        wr(rt, mp, 2, (uint16_t)out); wr(rt, res, 1, out == mode ? 1 : 0); wr(rt, res + 1, 1, 0);
        return 1; }
    default: return 0;
    }
    if (redraw) { if (mac_te_recalc(rt, hte) || mac_te_redraw_all(rt, hte)) return -1; }
    return 1;
}
