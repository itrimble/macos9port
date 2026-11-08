#include "mac_text.h"
#include <string.h>

/* TextEdit, natively, with the TERec kept in a real guest handle (programs read its fields directly). Calls: TEInit $A9CC,
 * TEDispose $A9CD, TEGetText $A9CB, TESetText $A9CF, TESetSelect $A9D1, TENew $A9D2, TEUpdate $A9D3, TEClick $A9D4, TEActivate $A9D8,
 * TEDeactivate $A9D9, TEIdle $A9DA, TEKey $A9DC, TESetJust $A9DF. Anything else in the family is not claimed and halts by name.
 *
 * Record (offsets): destRect 0, viewRect 8, selRect 16, lineHeight 24, fontAscent 26, selPoint 28, selStart 32, selEnd 34, active 36,
 * wordBreak 38, clikLoop 42, clickTime 46, clickLoc 50, caretTime 52, caretState 56, just 58, teLength 60, hText 62, recalBack 66,
 * recalLines 68, clikStuff 70, crOnly 72, txFont 74, txFace 76, txMode 78, txSize 80, inPort 82, highHook 86, caretHook 90,
 * nLines 94, lineStarts[nLines+1] at 96.
 *
 * Limits, all deliberate: the font is a guest bitmap strike (mac_font.c), so a family/size the guest lacks halts; word and line
 * hooks (wordBreak, clikLoop, highHook, caretHook) must be NIL; TEClick sets the selection at the click and does not track a drag
 * (it cannot block waiting for the mouse); selection and caret are drawn by inverting pixels; an edit redraws from the edited line to
 * the bottom of the view; text is limited to 32000 characters and 4000 lines; left, centre and right justification only. */
enum { REC = 98, TE_TYPE = 0x54455243, TE_TEXT = 0x54455854, MAX_TEXT = 32000, MAX_LINES = 4000, CARET_TICKS = 20 };
enum { DEST = 0, VIEW = 8, LINE_H = 24, ASC = 26, SEL_S = 32, SEL_E = 34, ACTIVE = 36, WB = 38, CLIK = 42, CTIME = 52, CSTATE = 56, JUST = 58,
       LEN = 60, HTEXT = 62, CRONLY = 72, FONT = 74, FACE = 76, MODE = 78, SIZE = 80, INPORT = 82, HIGH = 86, CARET = 90, NLINES = 94, LSTART = 96 };

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static int s16(MacRuntime *r, uint32_t p) { return (int16_t)rd(r, p, 2); }

/* Memory Manager calls through the runtime's own services, so handles are accounted for exactly as the guest's are. */
static int mem_call(MacRuntime *rt, uint16_t op, uint32_t d0, uint32_t a0, uint32_t *out) {
    uint32_t sd0 = mac_cpu_get(rt->cpu, MAC_D0), sa0 = mac_cpu_get(rt->cpu, MAC_A0), ssr = mac_cpu_get(rt->cpu, MAC_SR);
    mac_cpu_set(rt->cpu, MAC_D0, d0); mac_cpu_set(rt->cpu, MAC_A0, a0);
    int rc = mac_memory_service(rt, op), err = (int32_t)mac_cpu_get(rt->cpu, MAC_D0);
    if (out) *out = mac_cpu_get(rt->cpu, MAC_A0);
    mac_cpu_set(rt->cpu, MAC_D0, sd0); mac_cpu_set(rt->cpu, MAC_A0, sa0); mac_cpu_set(rt->cpu, MAC_SR, ssr);
    return rc == 1 ? err : -1;
}
static int new_handle(MacRuntime *rt, uint32_t size, uint32_t *h) { return mem_call(rt, 0xa122, size, 0, h); }
static int set_size(MacRuntime *rt, uint32_t h, uint32_t size) { return mem_call(rt, 0xa024, size, h, NULL); }
static int dispose(MacRuntime *rt, uint32_t h) { return mem_call(rt, 0xa023, 0, h, NULL); }
static MacHandle *record_of(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == h && h) return &rt->handles[i];
    return NULL;
}
/* The record lives in guest memory, so the guest can overwrite any field of it, including the selection, the lengths and the text
 * handle. Everything the host later uses as a size, an index or a pointer is therefore checked here, once per call, against the
 * runtime's own handle table (which the guest cannot reach): the TEHandle and its text handle must be ones this module made, the
 * record's real size must hold the line table, and teLength, the line count and the selection must be consistent. Host pointers
 * are taken from the table, never from the guest-writable master pointers. */
static uint32_t rec_ptr(MacRuntime *rt, uint32_t h) { MacHandle *m = ok(rt, h, 4) ? record_of(rt, h) : NULL; return m ? m->pointer : 0; }
static int te_of(MacRuntime *rt, uint32_t h, uint32_t *rec) {
    MacHandle *m = ok(rt, h, 4) ? record_of(rt, h) : NULL;
    if (!m || m->type != TE_TYPE) return fail(rt, "TextEdit call needs a TEHandle made by TENew.");
    uint32_t p = m->pointer;
    if (m->size < REC || !ok(rt, p, m->size)) return fail(rt, "TextEdit record is invalid.");
    int lines = s16(rt, p + NLINES), n = s16(rt, p + LEN), a = s16(rt, p + SEL_S), b = s16(rt, p + SEL_E);
    if (lines < 0 || lines > MAX_LINES || (size_t)(LSTART + 2 * (lines + 1)) > m->size) return fail(rt, "TextEdit record is inconsistent (line count).");
    if (n < 0 || n > MAX_TEXT || a < 0 || a > b || b > n) return fail(rt, "TextEdit record is inconsistent (length or selection).");
    uint32_t th = rd(rt, p + HTEXT, 4);
    MacHandle *t = ok(rt, th, 4) ? record_of(rt, th) : NULL;
    if (!t || t->type != TE_TEXT || t->size < (uint32_t)n || (t->size && !ok(rt, t->pointer, t->size))) return fail(rt, "TextEdit text handle is invalid.");
    *rec = p;
    return 0;
}
static int face_of(MacRuntime *rt, uint32_t r, MacTextFace *f) {
    if (rd(rt, r + WB, 4) || rd(rt, r + CLIK, 4) || rd(rt, r + HIGH, 4) || rd(rt, r + CARET, 4)) return fail(rt, "TextEdit hooks (wordBreak, clikLoop, highHook, caretHook) are unsupported.");
    return mac_text_face(rt, s16(rt, r + FONT), s16(rt, r + SIZE), rd(rt, r + FACE, 1), 0, f);
}
static int text_at(MacRuntime *rt, uint32_t r, uint32_t *p) {
    MacHandle *m = record_of(rt, rd(rt, r + HTEXT, 4));
    if (!m || m->type != TE_TEXT) return fail(rt, "TextEdit text handle is invalid.");
    *p = m->pointer;
    if (rd(rt, r + LEN, 2) && !ok(rt, *p, rd(rt, r + LEN, 2))) return fail(rt, "TextEdit text handle is invalid.");
    return 0;
}
/* Greedy line breaking: a line ends after a CR, or before the (non-space) character that would overflow destRect, preferring the
 * position after the last space on the line; spaces never force a break, they hang past the margin. crOnly < 0 breaks at CRs only. Writes nLines and lineStarts (resizing the record's handle). */
static int recalc(MacRuntime *rt, uint32_t h, uint32_t r) {
    MacTextFace f; uint32_t t;
    if (face_of(rt, r, &f) || text_at(rt, r, &t)) return -1;
    int n = (int)rd(rt, r + LEN, 2), width = s16(rt, r + DEST + 6) - s16(rt, r + DEST + 2), wrap = s16(rt, r + CRONLY) >= 0;
    static uint16_t starts[MAX_LINES + 2]; int lines = 0, s = 0;
    while (s < n) {
        if (lines >= MAX_LINES) return fail(rt, "TextEdit text has more than 4000 lines.");
        starts[lines++] = (uint16_t)s;
        int i = s, w = 0, space = -1, end = -1;
        while (i < n) {
            unsigned ch = rd(rt, t + (uint32_t)i, 1);
            if (ch == 13) { end = i + 1; break; }
            int a = mac_text_advance(&f, ch);
            if (wrap && ch != ' ' && w + a > width && i > s) { end = space >= 0 ? space + 1 : i; break; }   /* a space hangs past the margin */
            w += a; if (ch == ' ') space = i; i++;
        }
        s = end >= 0 ? end : n;
    }
    if (n > 0 && rd(rt, t + (uint32_t)(n - 1), 1) == 13) { if (lines >= MAX_LINES) return fail(rt, "TextEdit text has more than 4000 lines."); starts[lines++] = (uint16_t)n; }
    if (set_size(rt, h, (uint32_t)(LSTART + 2 * (lines + 1)))) return fail(rt, "TextEdit could not grow its record.");
    r = rec_ptr(rt, h);
    if (!r || !ok(rt, r, (size_t)(LSTART + 2 * (lines + 1)))) return fail(rt, "TextEdit record is invalid.");
    for (int k = 0; k < lines; k++) wr(rt, r + LSTART + 2 * (unsigned)k, 2, starts[k]);
    wr(rt, r + LSTART + 2 * (unsigned)lines, 2, (uint16_t)n); wr(rt, r + NLINES, 2, (uint16_t)lines);
    return 0;
}
static int line_start(MacRuntime *rt, uint32_t r, int k) { return s16(rt, r + LSTART + 2 * (unsigned)k); }
static int line_of(MacRuntime *rt, uint32_t r, int off) {
    int lines = s16(rt, r + NLINES), k = 0;
    while (k + 1 < lines && line_start(rt, r, k + 1) <= off) k++;
    return k;
}
static int text_end(MacRuntime *rt, uint32_t r, uint32_t t, int k) {    /* end of line k, without its CR */
    int e = line_start(rt, r, k + 1);
    return (e > line_start(rt, r, k) && rd(rt, t + (uint32_t)(e - 1), 1) == 13) ? e - 1 : e;
}
static int width_of(MacRuntime *rt, const MacTextFace *f, uint32_t t, int from, int to) {
    int w = 0; for (int i = from; i < to; i++) w += mac_text_advance(f, rd(rt, t + (uint32_t)i, 1)); return w;
}
/* Left edge of line k's text, honouring justification (0 left, 1 centre, -1 right). */
static int line_left(MacRuntime *rt, uint32_t r, const MacTextFace *f, uint32_t t, int k) {
    int left = s16(rt, r + DEST + 2), right = s16(rt, r + DEST + 6), w = width_of(rt, f, t, line_start(rt, r, k), text_end(rt, r, t, k)), just = s16(rt, r + JUST);
    return just == 0 ? left : just == 1 ? left + (right - left - w) / 2 : right - w;
}
static void rect4(MacRuntime *rt, uint32_t p, int out[4]) { for (int i = 0; i < 4; i++) out[i] = s16(rt, p + 2 * (unsigned)i); }

/* Selection highlight or caret by inversion. `caret_on` says whether the caret is currently shown. */
static int paint_sel(MacRuntime *rt, uint32_t r, int caret) {
    MacTextFace f; uint32_t t; int view[4], lh = s16(rt, r + LINE_H), top = s16(rt, r + DEST), port = (int)rd(rt, r + INPORT, 4);
    if (face_of(rt, r, &f) || text_at(rt, r, &t)) return -1;
    rect4(rt, r + VIEW, view);
    int a = s16(rt, r + SEL_S), b = s16(rt, r + SEL_E), lines = s16(rt, r + NLINES);
    if (caret) {
        int k = lines ? line_of(rt, r, a) : 0, x = (lines ? line_left(rt, r, &f, t, k) + width_of(rt, &f, t, line_start(rt, r, k), a) : s16(rt, r + DEST + 2));
        int box[4] = {top + k * lh, x, top + (k + 1) * lh, x + 1};
        return mac_text_xor_rect(rt, (uint32_t)port, box, view);
    }
    for (int k = 0; k < lines; k++) {
        int s = line_start(rt, r, k), e = text_end(rt, r, t, k), lo = a > s ? a : s, hi = b < e ? b : e;
        if (lo >= hi) continue;
        int x = line_left(rt, r, &f, t, k), box[4] = {top + k * lh, x + width_of(rt, &f, t, s, lo), top + (k + 1) * lh, x + width_of(rt, &f, t, s, hi)};
        if (mac_text_xor_rect(rt, (uint32_t)port, box, view)) return -1;
    }
    return 0;
}
/* Remove whatever selection mark is showing (highlight, or the caret if on) before the state changes. */
static int clear_marks(MacRuntime *rt, uint32_t r) {
    if (!s16(rt, r + ACTIVE)) return 0;
    if (s16(rt, r + SEL_S) != s16(rt, r + SEL_E)) return paint_sel(rt, r, 0);
    if (s16(rt, r + CSTATE)) { wr(rt, r + CSTATE, 2, 0); return paint_sel(rt, r, 1); }
    return 0;
}
static int show_marks(MacRuntime *rt, uint32_t r) {
    return (s16(rt, r + ACTIVE) && s16(rt, r + SEL_S) != s16(rt, r + SEL_E)) ? paint_sel(rt, r, 0) : 0;
}
/* Draw every line that meets `upd` (top, left, bottom, right), clipped to the view, then the selection mark. */
static int draw_lines(MacRuntime *rt, uint32_t r, const int upd[4], int erase_first) {
    MacTextFace f; uint32_t t; int view[4], lh = s16(rt, r + LINE_H), top = s16(rt, r + DEST), port = (int)rd(rt, r + INPORT, 4), mode = s16(rt, r + MODE);
    if (face_of(rt, r, &f) || text_at(rt, r, &t)) return -1;
    rect4(rt, r + VIEW, view);
    if (erase_first && mac_text_erase_rect(rt, (uint32_t)port, upd, view)) return -1;
    for (int k = 0; k < s16(rt, r + NLINES); k++) {
        int y = top + k * lh;
        if (y + lh <= upd[0] || y >= upd[2] || y + lh <= view[0] || y >= view[2]) continue;
        int s = line_start(rt, r, k), e = text_end(rt, r, t, k);
        if (e > s && mac_text_draw_with(rt, (uint32_t)port, &f, mode, line_left(rt, r, &f, t, k), y + s16(rt, r + ASC), t + (uint32_t)s, e - s, view)) return -1;
    }
    if (s16(rt, r + ACTIVE)) {
        if (s16(rt, r + SEL_S) != s16(rt, r + SEL_E)) return paint_sel(rt, r, 0);
        if (s16(rt, r + CSTATE)) return paint_sel(rt, r, 1);
    }
    return 0;
}
static int set_text(MacRuntime *rt, uint32_t h, uint32_t r, uint32_t src, uint32_t n) {
    uint32_t th = rd(rt, r + HTEXT, 4);
    if (n > MAX_TEXT || (n && !ok(rt, src, n))) return fail(rt, "TextEdit text is too long or outside guest RAM.");
    if (set_size(rt, th, n)) return fail(rt, "TextEdit could not size its text handle.");
    MacHandle *m = record_of(rt, th);
    if (!m || m->type != TE_TEXT || m->size < n || (n && !ok(rt, m->pointer, n))) return fail(rt, "TextEdit text handle is invalid.");
    if (n) memmove(mac_cpu_ram(rt->cpu) + m->pointer, mac_cpu_ram(rt->cpu) + src, n);
    wr(rt, r + LEN, 2, n);
    return recalc(rt, h, rec_ptr(rt, h));
}
static void clamp_sel(MacRuntime *rt, uint32_t r) {
    int n = s16(rt, r + LEN), a = s16(rt, r + SEL_S), b = s16(rt, r + SEL_E);
    if (a > n) a = n; if (b > n) b = n; if (a < 0) a = 0; if (b < 0) b = 0;
    wr(rt, r + SEL_S, 2, (uint16_t)a); wr(rt, r + SEL_E, 2, (uint16_t)b);
}
/* The character offset nearest the point (v, h): choose the line, then the closest character boundary on it. */
static int offset_at(MacRuntime *rt, uint32_t r, int v, int h) {
    MacTextFace f; uint32_t t; int lines = s16(rt, r + NLINES);
    if (!lines || face_of(rt, r, &f) || text_at(rt, r, &t)) return 0;
    int k = (v - s16(rt, r + DEST)) / s16(rt, r + LINE_H); if (v < s16(rt, r + DEST)) k = 0; if (k >= lines) k = lines - 1;
    int s = line_start(rt, r, k), e = text_end(rt, r, t, k), x = line_left(rt, r, &f, t, k), best = s;
    for (int i = s; i < e; i++) { int a = mac_text_advance(&f, rd(rt, t + (uint32_t)i, 1)); if (h < x + a / 2) return i; x += a; best = i + 1; }
    return best;
}

static int do_new(MacRuntime *rt, uint32_t sp) {
    uint32_t port = rt->current_port, hr = 0, ht = 0, h;
    if (port < 0x400 || (port & 1) || !ok(rt, port, 108)) return fail(rt, "TENew needs a current port.");
    if (!ok(rt, rd(rt, sp, 4), 8) || !ok(rt, rd(rt, sp + 4, 4), 8)) return fail(rt, "TENew rectangle is outside guest RAM.");
    MacTextFace f;
    if (mac_text_face(rt, s16(rt, port + 68), s16(rt, port + 74), rd(rt, port + 70, 1), 0, &f)) return -1;
    if (new_handle(rt, REC, &hr) || new_handle(rt, 0, &ht) || !hr || !ht) return fail(rt, "TENew could not allocate its record.");
    h = hr; record_of(rt, h)->type = TE_TYPE; record_of(rt, ht)->type = TE_TEXT;
    uint32_t r = rec_ptr(rt, h);
    if (!ok(rt, r, REC)) return fail(rt, "TENew record is invalid.");
    memset(mac_cpu_ram(rt->cpu) + r, 0, REC);
    memcpy(mac_cpu_ram(rt->cpu) + r + DEST, mac_cpu_ram(rt->cpu) + rd(rt, sp + 4, 4), 8);
    memcpy(mac_cpu_ram(rt->cpu) + r + VIEW, mac_cpu_ram(rt->cpu) + rd(rt, sp, 4), 8);
    wr(rt, r + LINE_H, 2, (uint16_t)(f.s.ascent + f.s.descent + f.s.leading)); wr(rt, r + ASC, 2, (uint16_t)f.s.ascent);
    wr(rt, r + CTIME, 4, rt->ticks); wr(rt, r + HTEXT, 4, ht); wr(rt, r + CRONLY, 2, 1);
    wr(rt, r + FONT, 2, rd(rt, port + 68, 2)); wr(rt, r + FACE, 2, rd(rt, port + 70, 1) << 8); wr(rt, r + MODE, 2, rd(rt, port + 72, 2)); wr(rt, r + SIZE, 2, rd(rt, port + 74, 2));
    wr(rt, r + INPORT, 4, port);
    wr(rt, sp + 8, 4, h); mac_cpu_set(rt->cpu, MAC_A7, sp + 8);
    return 1;
}

/* TEDispatch ($A83D), selector word on top of the stack. Only selector 8, TEGetPoint(offset, hTE): Point, is implemented: the
 * point (v, h) for a character offset has h = the left edge of that character and v = the bottom of its line, which is how the
 * interpreter reads it (it takes the last character's v as the height of a block of text). The offset is clamped to the text and
 * a position past the end of a line stays at that line's end. Stack: selector, hTE, offset, then the Point result. */
static int te_dispatch(MacRuntime *rt) {
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || !ok(rt, sp, 12)) return fail(rt, "TEDispatch has an invalid stack.");
    unsigned sel = rd(rt, sp, 2);
    if (sel != 8) return fail(rt, "TEDispatch selectors other than 8 (TEGetPoint) are unsupported.");
    uint32_t r; MacTextFace f, *fp = &f; uint32_t t = 0;
    if (te_of(rt, rd(rt, sp + 2, 4), &r) || face_of(rt, r, fp) || text_at(rt, r, &t)) return -1;
    int off = s16(rt, sp + 6), n = s16(rt, r + LEN), lines = s16(rt, r + NLINES), lh = s16(rt, r + LINE_H);
    if (off < 0) off = 0; if (off > n) off = n;
    int k = lines ? line_of(rt, r, off) : 0, h = s16(rt, r + DEST + 2);
    if (lines) { int e = text_end(rt, r, t, k); h = line_left(rt, r, fp, t, k) + width_of(rt, fp, t, line_start(rt, r, k), off < e ? off : e); }
    wr(rt, sp + 8, 2, (uint16_t)(s16(rt, r + DEST) + (k + 1) * lh)); wr(rt, sp + 10, 2, (uint16_t)h);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 8);
    return 1;
}

int mac_textedit_service(MacRuntime *rt, uint16_t op) {
    if (op == 0xa83d) { if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1; return te_dispatch(rt); }
    if (op < 0xa9c8 || op > 0xa9e0) return 0;
    static const struct { uint16_t op; unsigned args; } calls[] = {
        {0xa9cc, 0}, {0xa9d2, 8}, {0xa9cd, 4}, {0xa9cb, 4}, {0xa9cf, 12}, {0xa9d1, 12}, {0xa9d3, 8}, {0xa9d4, 10}, {0xa9d8, 4}, {0xa9d9, 4}, {0xa9da, 4}, {0xa9dc, 6}, {0xa9df, 6}};
    unsigned args = 0; int known = 0;
    for (unsigned i = 0; i < sizeof calls / sizeof *calls; i++) if (calls[i].op == op) { known = 1; args = calls[i].args; }
    if (!known) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || !ok(rt, sp, args + 4)) return fail(rt, "TextEdit call has an invalid stack.");
    if (op == 0xa9cc) return 1;                                  /* TEInit: nothing to set up */
    if (op == 0xa9d2) return do_new(rt, sp);
    uint32_t hte = rd(rt, sp, 4), r, h = hte;
    if (op == 0xa9dc || op == 0xa9df) hte = rd(rt, sp, 4);       /* TEKey and TESetJust also take the handle last (lowest address) */
    if (te_of(rt, hte, &r)) return -1;
    h = hte;
    int res = 1; mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    switch (op) {
    case 0xa9cd: {                                               /* TEDispose */
        if (clear_marks(rt, r)) return -1;
        uint32_t th = rd(rt, r + HTEXT, 4);
        if (dispose(rt, th) || dispose(rt, h)) return fail(rt, "TEDispose could not release its handles.");
        break; }
    case 0xa9cb: wr(rt, sp + 4, 4, rd(rt, r + HTEXT, 4)); break;    /* TEGetText: the result sits above the argument */
    case 0xa9cf: {                                               /* TESetText(text, length, hTE) */
        uint32_t src = rd(rt, sp + 8, 4), n = rd(rt, sp + 4, 4);
        if (n > MAX_TEXT || !ok(rt, src, n)) return fail(rt, "TESetText text is too long or outside guest RAM.");
        if (clear_marks(rt, r)) return -1;
        if (set_text(rt, h, r, src, n)) return -1;
        r = rec_ptr(rt, h); clamp_sel(rt, r);
        break; }
    case 0xa9d1: {                                               /* TESetSelect(selStart, selEnd, hTE) */
        long a = (int32_t)rd(rt, sp + 8, 4), b = (int32_t)rd(rt, sp + 4, 4), n = s16(rt, r + LEN);
        if (a < 0) a = 0; if (b < 0) b = 0; if (a > n) a = n; if (b > n) b = n; if (a > b) { long x = a; a = b; b = x; }
        if (clear_marks(rt, r)) return -1;
        wr(rt, r + SEL_S, 2, (uint16_t)a); wr(rt, r + SEL_E, 2, (uint16_t)b);
        if (show_marks(rt, r)) return -1;
        break; }
    case 0xa9d3: {                                               /* TEUpdate(rUpdate, hTE) */
        uint32_t up = rd(rt, sp + 4, 4); int box[4];
        if (!ok(rt, up, 8) || (up & 1)) return fail(rt, "TEUpdate rectangle is outside guest RAM.");
        rect4(rt, up, box);
        if (draw_lines(rt, r, box, 0)) return -1;
        break; }
    case 0xa9d8: if (!s16(rt, r + ACTIVE)) { wr(rt, r + ACTIVE, 2, 1); if (show_marks(rt, r)) return -1; } break;       /* TEActivate */
    case 0xa9d9: if (clear_marks(rt, r)) return -1; wr(rt, r + ACTIVE, 2, 0); break;                                    /* TEDeactivate */
    case 0xa9da:                                                 /* TEIdle: blink the caret */
        if (s16(rt, r + ACTIVE) && s16(rt, r + SEL_S) == s16(rt, r + SEL_E) && rt->ticks - rd(rt, r + CTIME, 4) >= CARET_TICKS) {
            wr(rt, r + CSTATE, 2, s16(rt, r + CSTATE) ? 0 : 1); wr(rt, r + CTIME, 4, rt->ticks);
            if (paint_sel(rt, r, 1)) return -1;
        }
        break;
    case 0xa9df: wr(rt, r + JUST, 2, rd(rt, sp + 4, 2)); break;  /* TESetJust(just, hTE) */
    case 0xa9d4: {                                               /* TEClick(pt, extend, hTE): selection at the click; no drag tracking */
        int extend = (int)rd(rt, sp + 4, 1) != 0, v = (int16_t)rd(rt, sp + 6, 2), hh = (int16_t)rd(rt, sp + 8, 2);
        int off = offset_at(rt, r, v, hh), a = s16(rt, r + SEL_S), b = s16(rt, r + SEL_E);
        if (clear_marks(rt, r)) return -1;
        if (extend) { if (off < a) a = off; else b = off; } else a = b = off;
        wr(rt, r + SEL_S, 2, (uint16_t)a); wr(rt, r + SEL_E, 2, (uint16_t)b); wr(rt, r + CTIME, 4, rt->ticks);
        if (show_marks(rt, r)) return -1;
        break; }
    case 0xa9dc: {                                               /* TEKey(key, hTE) */
        unsigned key = rd(rt, sp + 4, 1); int a = s16(rt, r + SEL_S), b = s16(rt, r + SEL_E), n = s16(rt, r + LEN);
        if (clear_marks(rt, r)) return -1;
        if (key >= 28 && key <= 31) {                            /* arrows: left, right; up and down are not implemented */
            if (key > 29) return fail(rt, "TEKey up and down arrows are unsupported.");
            int p = key == 28 ? (a == b ? a - 1 : a) : (a == b ? b + 1 : b); p = p < 0 ? 0 : p > n ? n : p;
            wr(rt, r + SEL_S, 2, (uint16_t)p); wr(rt, r + SEL_E, 2, (uint16_t)p);
            if (show_marks(rt, r)) return -1; break;
        }
        uint32_t t;
        if (text_at(rt, r, &t) || !(n == 0 || ok(rt, t, (size_t)n))) return fail(rt, "TextEdit text handle is invalid.");
        static uint8_t buf[MAX_TEXT + 1]; unsigned m = 0;
        if (key == 8) { if (a == b && a > 0) a--; if (a == b) break; }    /* backspace removes the selection, or the character before the caret */
        else if (key >= 32 || key == 13) { if (n - (b - a) + 1 > MAX_TEXT) break; }
        else return fail(rt, "TEKey control characters other than backspace, return and left/right arrows are unsupported.");
        if (a < 0 || a > b || b > n || n > MAX_TEXT) return fail(rt, "TEKey record is inconsistent.");     /* te_of checked this; kept as a guard for the copies below */
        memcpy(buf, mac_cpu_ram(rt->cpu) + t, (size_t)a); m = (unsigned)a;
        if (key != 8) buf[m++] = (uint8_t)key;
        memcpy(buf + m, mac_cpu_ram(rt->cpu) + t + (unsigned)b, (size_t)(n - b)); m += (unsigned)(n - b);
        { uint32_t tmp = 0; if (new_handle(rt, m ? m : 1, &tmp) || !tmp) return fail(rt, "TEKey could not allocate.");
          MacHandle *tm = record_of(rt, tmp);
          if (!tm || !ok(rt, tm->pointer, m ? m : 1)) return fail(rt, "TEKey scratch handle is invalid.");
          memcpy(mac_cpu_ram(rt->cpu) + tm->pointer, buf, m);
          int rc = set_text(rt, h, r, tm->pointer, m); dispose(rt, tmp); if (rc) return -1; }
        r = rec_ptr(rt, h);
        int p = a + (key == 8 ? 0 : 1);
        wr(rt, r + SEL_S, 2, (uint16_t)p); wr(rt, r + SEL_E, 2, (uint16_t)p); wr(rt, r + CTIME, 4, rt->ticks);
        { int view[4], k = line_of(rt, r, a > 0 ? a - 1 : 0), lh = s16(rt, r + LINE_H); rect4(rt, r + VIEW, view);   /* from the line of the previous character: a reflow can change it */
          int upd[4] = {s16(rt, r + DEST) + k * lh, view[1], view[2], view[3]}; if (draw_lines(rt, r, upd, 1)) return -1; }
        break; }
    default: return fail(rt, "TextEdit call is not implemented.");
    }
    return res;
}
