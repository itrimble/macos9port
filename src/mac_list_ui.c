#include "mac_list_int.h"
#include "mac_dialog_draw.h"
#include <stdlib.h>
#include <string.h>

/* List Manager geometry, scroll bars, drawing, LDEF calls and clicks (see mac_list_int.h). A call that changes what is on screen queues messages (a job); a custom LDEF's messages are guest
 * calls run one after another, the standard definition (id 0) draws natively in the list's port window. Scrolling redraws the visible cells rather than copying pixels, and LClick
 * handles one press without following the mouse (no drag selection, no scroll-bar tracking loop). All coordinates are the list port's local ones. */
#define RD list_rd
#define WR list_wr
#define LDEF_TYPE UINT32_C(0x4c444546)
static int s16(MacRuntime *rt, uint32_t a) { return (int16_t)RD(rt, a, 2); }
static void put16(MacRuntime *rt, uint32_t a, int v) { WR(rt, a, 2, (uint16_t)v); }

uint32_t list_ldef_load(MacRuntime *rt, unsigned id) {
    uint32_t h = mac_runtime_resource(rt, LDEF_TYPE, (int16_t)id);
    if (!h) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *m = &rt->handles[i];
        if (m->handle != h) continue;
        if (!m->resource || m->type != LDEF_TYPE || m->size < 4 || m->size > 0x100000) return 0;
        m->loaded_code = 1;
        return h;
    }
    return 0;
}
static uint32_t ldef_code(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; i < rt->handle_count; i++) {
        const MacHandle *m = &rt->handles[i];
        if (m->handle != h) continue;
        uint32_t code = RD(rt, h, 4);
        return m->resource && m->type == LDEF_TYPE && m->loaded_code && code == m->pointer && !(code & 1) ? code : 0;
    }
    return 0;
}
/* ---- geometry ---- */
static void view(MacRuntime *rt, uint32_t rec, int v[4]) { for (int i = 0; i < 4; i++) v[i] = s16(rt, rec + R_VIEW + 2u * (unsigned)i); }
static int cell_h(MacRuntime *rt, uint32_t rec) { int h = s16(rt, rec + R_CSIZE); return h > 0 ? h : 12; }
static int cell_w(MacRuntime *rt, uint32_t rec) { int w = s16(rt, rec + R_CSIZE + 2); return w > 0 ? w : 1; }
void list_default_sizes(MacRuntime *rt, ListState *L) {   /* 0 means "choose": the height is 12 pixels (this runtime's text line), the width the view's width over the columns */
    uint32_t rec = list_rec(rt, L); int v[4]; view(rt, rec, v);
    if (s16(rt, rec + R_CSIZE) <= 0) put16(rt, rec + R_CSIZE, 12);
    if (s16(rt, rec + R_CSIZE + 2) <= 0) { int w = (v[3] - v[1]) / (L->cols > 0 ? L->cols : 1); put16(rt, rec + R_CSIZE + 2, w > 0 ? w : 1); }
}
static void scroll_range(MacRuntime *rt, ListState *L, uint32_t rec, int *max_top, int *max_left, int *fit_rows, int *fit_cols) {
    int v[4]; view(rt, rec, v);
    *fit_rows = (v[2] - v[0]) / cell_h(rt, rec); *fit_cols = (v[3] - v[1]) / cell_w(rt, rec);
    *max_top = L->top + L->rows - *fit_rows; if (*max_top < L->top) *max_top = L->top;
    *max_left = L->left + L->cols - *fit_cols; if (*max_left < L->left) *max_left = L->left;
}
int list_geometry(MacRuntime *rt, ListState *L) {
    uint32_t rec = list_rec(rt, L); if (!rec) return list_fail(rt, "A ListHandle lost its record.");
    int v[4], mt, ml, fr, fc; view(rt, rec, v); scroll_range(rt, L, rec, &mt, &ml, &fr, &fc);
    int top = s16(rt, rec + R_VISIBLE), left = s16(rt, rec + R_VISIBLE + 2);
    if (top < L->top) top = L->top; if (top > mt) top = mt; if (left < L->left) left = L->left; if (left > ml) left = ml;
    int ch = cell_h(rt, rec), cw = cell_w(rt, rec), bottom = top + (v[2] - v[0] + ch - 1) / ch, right = left + (v[3] - v[1] + cw - 1) / cw;
    if (bottom > L->top + L->rows) bottom = L->top + L->rows; if (right > L->left + L->cols) right = L->left + L->cols;
    put16(rt, rec + R_VISIBLE, top); put16(rt, rec + R_VISIBLE + 2, left); put16(rt, rec + R_VISIBLE + 4, bottom); put16(rt, rec + R_VISIBLE + 6, right);
    int active = RD(rt, rec + R_ACTIVE, 1) != 0;
    uint32_t vs = RD(rt, rec + R_VSCROLL, 4), hs = RD(rt, rec + R_HSCROLL, 4);
    int16_t vr[4] = {(int16_t)(v[0] - 1), (int16_t)v[3], (int16_t)(v[2] + 1), (int16_t)(v[3] + 16)}, hr[4] = {(int16_t)v[2], (int16_t)(v[1] - 1), (int16_t)(v[2] + 16), (int16_t)(v[3] + 1)};
    if (vs && !mac_control_update(rt, vs, vr, (int16_t)top, (int16_t)L->top, (int16_t)mt, active && mt > L->top ? 0 : 255)) return list_fail(rt, "A list's vertical scroll bar is gone.");
    if (hs && !mac_control_update(rt, hs, hr, (int16_t)left, (int16_t)L->left, (int16_t)ml, active && ml > L->left ? 0 : 255)) return list_fail(rt, "A list's horizontal scroll bar is gone.");
    return mac_cpu_halted(rt->cpu) ? -1 : 0;
}
int list_make_scroll_bars(MacRuntime *rt, ListState *L, int vert, int horiz) {
    uint32_t rec = list_rec(rt, L), win = RD(rt, rec + R_PORT, 4); int v[4]; view(rt, rec, v);
    int16_t vr[4] = {(int16_t)(v[0] - 1), (int16_t)v[3], (int16_t)(v[2] + 1), (int16_t)(v[3] + 16)}, hr[4] = {(int16_t)v[2], (int16_t)(v[1] - 1), (int16_t)(v[2] + 16), (int16_t)(v[3] + 1)};
    if (vert) { int64_t h = mac_control_create(rt, win, vr, 16, 0, 0, 0); if (h <= 0) return list_fail(rt, "The List Manager could not make its vertical scroll bar."); WR(rt, rec + R_VSCROLL, 4, (uint32_t)h); }
    if (horiz) { int64_t h = mac_control_create(rt, win, hr, 16, 0, 0, 0); if (h <= 0) return list_fail(rt, "The List Manager could not make its horizontal scroll bar."); WR(rt, rec + R_HSCROLL, 4, (uint32_t)h); }
    return 0;
}
void list_free_scroll_bars(MacRuntime *rt, ListState *L) {
    uint32_t rec = list_rec(rt, L); if (!rec) return;
    if (RD(rt, rec + R_VSCROLL, 4)) mac_control_destroy(rt, RD(rt, rec + R_VSCROLL, 4));
    if (RD(rt, rec + R_HSCROLL, 4)) mac_control_destroy(rt, RD(rt, rec + R_HSCROLL, 4));
    WR(rt, rec + R_VSCROLL, 4, 0); WR(rt, rec + R_HSCROLL, 4, 0);
}
int list_cell_rect(MacRuntime *rt, const ListState *L, int row, int col, int16_t out[4]) {
    uint32_t rec = list_rec(rt, L); if (!rec) return 0;
    int vt = s16(rt, rec + R_VISIBLE), vl = s16(rt, rec + R_VISIBLE + 2), vb = s16(rt, rec + R_VISIBLE + 4), vr = s16(rt, rec + R_VISIBLE + 6);
    if (row < vt || row >= vb || col < vl || col >= vr) return 0;
    int v[4]; view(rt, rec, v); int ch = cell_h(rt, rec), cw = cell_w(rt, rec);
    out[0] = (int16_t)(v[0] + (row - vt) * ch); out[1] = (int16_t)(v[1] + (col - vl) * cw); out[2] = (int16_t)(out[0] + ch); out[3] = (int16_t)(out[1] + cw);
    return 1;
}
int list_scroll_to(MacRuntime *rt, ListState *L, int top, int left) {
    uint32_t rec = list_rec(rt, L); int mt, ml, fr, fc; scroll_range(rt, L, rec, &mt, &ml, &fr, &fc);
    if (top < L->top) top = L->top; if (top > mt) top = mt; if (left < L->left) left = L->left; if (left > ml) left = ml;
    if (top == s16(rt, rec + R_VISIBLE) && left == s16(rt, rec + R_VISIBLE + 2)) return 0;
    put16(rt, rec + R_VISIBLE, top); put16(rt, rec + R_VISIBLE + 2, left);
    if (list_geometry(rt, L) < 0) return -1;
    return list_redraw_all(rt, L);
}
int list_autoscroll(MacRuntime *rt, ListState *L) {
    uint32_t rec = list_rec(rt, L); int mt, ml, fr, fc; scroll_range(rt, L, rec, &mt, &ml, &fr, &fc);
    int top = s16(rt, rec + R_VISIBLE), left = s16(rt, rec + R_VISIBLE + 2);
    for (int r = L->top; r < L->top + L->rows; r++) for (int c = L->left; c < L->left + L->cols; c++) if (list_cell(L, r, c)->sel) {
        if (r < top) top = r; else if (fr > 0 && r >= top + fr) top = r - fr + 1;
        if (c < left) left = c; else if (fc > 0 && c >= left + fc) left = c - fc + 1;
        return list_scroll_to(rt, L, top, left);
    }
    return 0;
}
void list_set_active(MacRuntime *rt, ListState *L, int on) { WR(rt, list_rec(rt, L) + R_ACTIVE, 1, on ? 1 : 0); list_geometry(rt, L); }

/* ---- native drawing of the standard definition ---- */
static void native_cell(MacRuntime *rt, ListState *L, const ListStep *s) {
    uint32_t rec = list_rec(rt, L); MacWindowState *w = mac_controls_window(rt, RD(rt, rec + R_PORT, 4));
    if (!w) { list_fail(rt, "A list's port is no longer a window."); return; }
    uint32_t pm = RD(rt, w->handles[0], 4);
    int oy = pm >= 0x400 ? s16(rt, pm + 6) : 0, ox = pm >= 0x400 ? s16(rt, pm + 8) : 0, v[4]; view(rt, rec, v);
    MacPen pen; mac_pen_init(&pen, rt, w->bounds[1], w->bounds[0], w->bounds[3] - w->bounds[1], w->bounds[2] - w->bounds[0]);
    int t = s->rect[0], l = s->rect[1], b = s->rect[2], r = s->rect[3];
    if (t < v[0]) t = v[0]; if (l < v[1]) l = v[1]; if (b > v[2]) b = v[2]; if (r > v[3]) r = v[3];
    if (b <= t || r <= l) return;
    if (s->msg == -1) { mac_pen_fill(&pen, l - ox, t - oy, r - ox, b - oy, pen.light); return; }
    LCell *c = list_cell(L, s->row, s->col);
    uint8_t face = s->sel ? pen.dark : pen.light, ink = s->sel ? pen.light : pen.dark;
    mac_pen_fill(&pen, l - ox, t - oy, r - ox, b - oy, face);
    if (!c || !c->len) return;
    int room = (r - l - 8) / 8, n = c->len < (unsigned)room ? (int)c->len : room;
    if (n > 0) mac_pen_text(&pen, s->rect[1] + 4 - ox, s->rect[0] + (s->rect[2] - s->rect[0] - 8) / 2 - oy, (const char *)c->data, (unsigned)n, ink, 0);
}
/* ---- the job: queued messages, run in order ---- */
int list_job_begin(MacRuntime *rt, uint32_t sp, uint32_t pop, uint32_t result_at, unsigned result_bytes, uint32_t result) {
    MacLists *m = rt->lists;
    if (!m->job.step) { m->job.step = calloc(LIST_MAX_STEPS, sizeof(ListStep)); if (!m->job.step) return list_fail(rt, "No memory for a List Manager job."); }
    m->job.active = 1; m->job.count = m->job.next = 0; m->job.sp = sp; m->job.pop = pop; m->job.result_at = result_at; m->job.result_bytes = result_bytes; m->job.result = result; m->job.dispose = 0;
    return 0;
}
int list_step(MacRuntime *rt, ListState *L, int msg, int sel, int row, int col) {
    MacLists *m = rt->lists; uint32_t rec = list_rec(rt, L); ListStep s; memset(&s, 0, sizeof s);
    if (msg == LDEF_DRAW || msg == LDEF_HILITE) {
        if (RD(rt, rec + R_LISTFLAGS, 1) & LF_DRAWOFF) return 0;
        if (!list_cell_rect(rt, L, row, col, s.rect)) return 0;
    } else { if (!L->ldef) return 0; int v[4]; view(rt, rec, v); for (int i = 0; i < 4; i++) s.rect[i] = (int16_t)v[i]; }
    if (m->job.count == LIST_MAX_STEPS) return list_fail(rt, "A List Manager call needs more cell messages than this runtime queues.");
    s.msg = msg; s.sel = sel; s.row = (int16_t)row; s.col = (int16_t)col; s.list = L->handle;
    m->job.step[m->job.count++] = s;
    return 0;
}
int list_redraw_all(MacRuntime *rt, ListState *L) {
    uint32_t rec = list_rec(rt, L); int v[4]; view(rt, rec, v);
    if (RD(rt, rec + R_LISTFLAGS, 1) & LF_DRAWOFF) return 0;
    MacLists *m = rt->lists; ListStep e; memset(&e, 0, sizeof e); e.msg = -1; e.list = L->handle;
    for (int i = 0; i < 4; i++) e.rect[i] = (int16_t)v[i];
    if (m->job.count == LIST_MAX_STEPS) return list_fail(rt, "A List Manager call needs more cell messages than this runtime queues.");
    m->job.step[m->job.count++] = e;
    for (int r = s16(rt, rec + R_VISIBLE); r < s16(rt, rec + R_VISIBLE + 4); r++) for (int c = s16(rt, rec + R_VISIBLE + 2); c < s16(rt, rec + R_VISIBLE + 6); c++)
        if (list_step(rt, L, LDEF_DRAW, list_cell(L, r, c)->sel, r, c) < 0) return -1;
    return 0;
}
static void port_set(MacRuntime *rt, uint32_t port) { rt->current_port = port; if (rt->qd_globals) WR(rt, rt->qd_globals, 4, port); }
static int finish(MacRuntime *rt) {
    MacLists *m = rt->lists; uint32_t dispose = m->job.dispose;
    if (dispose) { ListState *L = list_find(rt, dispose); if (!L) return -1; list_dispose(rt, L); }
    if (m->job.result_at) WR(rt, m->job.result_at, m->job.result_bytes, m->job.result);
    mac_cpu_set(rt->cpu, MAC_A7, m->job.sp + m->job.pop);
    m->job.active = 0;
    return 1;
}
static int step_done(void *vrt, void *ctx, uint32_t result);
int list_job_run(MacRuntime *rt) {
    MacLists *m = rt->lists;
    while (m->job.next < m->job.count) {
        ListStep *s = &m->job.step[m->job.next++];
        ListState *L = NULL; for (unsigned i = 0; i < LIST_MAX; i++) if (m->list[i].handle == s->list) L = &m->list[i];
        if (!L) continue;
        if (!L->ldef) { native_cell(rt, L, s); if (mac_cpu_halted(rt->cpu)) return -1; continue; }
        uint32_t rec = list_rec(rt, L), code = ldef_code(rt, L->ldef); MacWindowState *w = mac_controls_window(rt, RD(rt, rec + R_PORT, 4));
        if (!code || !w) return list_fail(rt, "A list's LDEF is not a live LDEF resource, or its port is gone.");
        if (!m->scratch && !(m->scratch = mac_runtime_alloc(rt, 16))) return list_fail(rt, "No memory for the rectangle an LDEF is given.");
        for (int i = 0; i < 4; i++) put16(rt, m->scratch + 2u * (unsigned)i, s->rect[i]);
        unsigned off = (s->msg == LDEF_DRAW || s->msg == LDEF_HILITE) ? list_cell_offset(L, s->row, s->col) : 0;
        LCell *c = list_cell(L, s->row, s->col);
        if (off > 0x7fff) return list_fail(rt, "A custom list holds more than 32 KB of cell data before this cell (unsupported).");
        uint8_t a[20];   /* lowest address first: lHandle, lDataLen, lDataOffset, lCell, lRect, lSelect, lMessage */
        uint32_t lrect = m->scratch, len = c ? c->len : 0;
        a[0] = (uint8_t)(L->handle >> 24); a[1] = (uint8_t)(L->handle >> 16); a[2] = (uint8_t)(L->handle >> 8); a[3] = (uint8_t)L->handle;
        a[4] = (uint8_t)(len >> 8); a[5] = (uint8_t)len; a[6] = (uint8_t)(off >> 8); a[7] = (uint8_t)off;
        a[8] = (uint8_t)((unsigned)s->row >> 8); a[9] = (uint8_t)s->row; a[10] = (uint8_t)((unsigned)s->col >> 8); a[11] = (uint8_t)s->col;
        a[12] = (uint8_t)(lrect >> 24); a[13] = (uint8_t)(lrect >> 16); a[14] = (uint8_t)(lrect >> 8); a[15] = (uint8_t)lrect;
        a[16] = s->sel ? 1 : 0; a[17] = 0; a[18] = (uint8_t)((unsigned)s->msg >> 8); a[19] = (uint8_t)s->msg;
        m->job.saved_port = rt->current_port; m->job.port_changed = 1; port_set(rt, w->address);
        int rc = mac_guest_call(rt, code, a, 20, 0, step_done, NULL);
        if (rc < 0) { port_set(rt, m->job.saved_port); m->job.port_changed = 0; return -1; }
        return 1;
    }
    return finish(rt);
}
static int step_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; MacLists *m = rt->lists; (void)ctx; (void)result;
    if (m->job.port_changed) { port_set(rt, m->job.saved_port); m->job.port_changed = 0; }
    return list_job_run(rt) < 0 ? -1 : 0;
}
/* ---- selection and clicks ---- */
int list_set_select(MacRuntime *rt, ListState *L, int row, int col, int on) {
    LCell *c = list_cell(L, row, col); if (!c || (c->sel != 0) == (on != 0)) return 0;
    c->sel = on ? 1 : 0;
    return list_step(rt, L, LDEF_HILITE, c->sel, row, col);
}
static int scroll_by(MacRuntime *rt, ListState *L, int dr, int dc) {
    uint32_t rec = list_rec(rt, L);
    return list_scroll_to(rt, L, s16(rt, rec + R_VISIBLE) + dr, s16(rt, rec + R_VISIBLE + 2) + dc);
}
int list_click(MacRuntime *rt, ListState *L, int v, int h, unsigned mods, int *result) {
    uint32_t rec = list_rec(rt, L); *result = 0;
    for (int bar = 0; bar < 2; bar++) {
        uint32_t ctl = RD(rt, rec + (bar ? R_HSCROLL : R_VSCROLL), 4), p = ctl ? mac_control_record(rt, ctl) : 0;
        if (!p) { if (ctl) return -1; continue; }
        int part = mac_control_part(rt, p, v, h); if (!part) continue;
        int mt, ml, fr, fc; scroll_range(rt, L, rec, &mt, &ml, &fr, &fc); (void)mt; (void)ml;
        if (part == 129) return list_fail(rt, "LClick on a scroll bar's thumb needs the mouse tracked, which is unsupported.");
        int step = part == 20 ? -1 : part == 21 ? 1 : part == 22 ? -(bar ? fc : fr) : part == 23 ? (bar ? fc : fr) : 0;
        return bar ? scroll_by(rt, L, 0, step) : scroll_by(rt, L, step, 0);
    }
    int vw[4]; view(rt, rec, vw);
    if (v < vw[0] || v >= vw[2] || h < vw[1] || h >= vw[3]) return 0;
    int row = s16(rt, rec + R_VISIBLE) + (v - vw[0]) / cell_h(rt, rec), col = s16(rt, rec + R_VISIBLE + 2) + (h - vw[1]) / cell_w(rt, rec);
    LCell *c = list_cell(L, row, col); if (!c) return 0;
    unsigned flags = RD(rt, rec + R_SELFLAGS, 1); uint32_t last = RD(rt, rec + R_LASTCLICK, 4);
    int lr = (int16_t)(last >> 16), lc = (int16_t)(last & 0xffff), have_last = list_cell(L, lr, lc) != NULL;
    if (!(flags & SF_ONLYONE) && (mods & 0x100) && !(flags & SF_NODISJOINT)) { if (list_set_select(rt, L, row, col, !c->sel) < 0) return -1; }   /* command: toggle */
    else if (!(flags & SF_ONLYONE) && (mods & 0x200) && !(flags & SF_NOEXTEND) && have_last) {                                                        /* shift: extend from the last click */
        int r0 = row < lr ? row : lr, r1 = row < lr ? lr : row, c0 = col < lc ? col : lc, c1 = col < lc ? lc : col;
        if (flags & SF_NORECT) { r0 = row; r1 = row; c0 = col; c1 = col; }
        for (int r = r0; r <= r1; r++) for (int k = c0; k <= c1; k++) if (list_set_select(rt, L, r, k, 1) < 0) return -1;
    } else {
        for (int r = L->top; r < L->top + L->rows; r++) for (int k = L->left; k < L->left + L->cols; k++) if ((r != row || k != col) && list_set_select(rt, L, r, k, 0) < 0) return -1;
        if (list_set_select(rt, L, row, col, 1) < 0) return -1;
    }
    uint32_t now = RD(rt, 0x16a, 4), dbl_time = RD(rt, 0x2f0, 4); if (!dbl_time) dbl_time = 30;
    *result = have_last && lr == row && lc == col && now - RD(rt, rec + R_CLIKTIME, 4) <= dbl_time;
    WR(rt, rec + R_CLIKTIME, 4, now); WR(rt, rec + R_CLIKLOC, 4, ((uint32_t)(uint16_t)v << 16) | (uint16_t)h); WR(rt, rec + R_CLICKLOC, 4, ((uint32_t)(uint16_t)v << 16) | (uint16_t)h);
    WR(rt, rec + R_LASTCLICK, 4, ((uint32_t)(uint16_t)row << 16) | (uint16_t)col);
    return 0;
}
