#include "mac_list_int.h"
#include "mac_handles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* List Manager, Pack0 ($A9E7). The selector is a word on top of the stack, the arguments follow it in Pascal order (last argument nearest). Selector numbers, argument orders and result types
 * were checked against Apple's archived "More Macintosh Toolbox" List Manager reference (its routine summary and the pages for LActivate, LSetSelect, LNew, LAddRow and LDelRow): LActivate 0,
 * LAddColumn 4, LAddRow 8, LAddToCell C, LAutoScroll 10, LCellSize 14, LClick 18, LClrCell 1C, LDelColumn 20, LDelRow 24, LDispose 28, LSetDrawingMode/LDoDraw 2C, LDraw 30,
 * LGetCellDataLocation/LFind 34, LGetCell 38, LGetSelect 3C, LLastClick 40, LNew 44, LNextCell 48, LRect 4C, LScroll 50, LSearch 54, LSetCell 58, LSetSelect 5C, LSize 60, LUpdate 64.
 * A Cell is a Point: v is the row, h the column, both in dataBounds coordinates. LAddRow/LAddColumn insert at the given number and return the first new one (a number past the end appends);
 * LDelRow/LDelColumn with a count of 0 delete them all and with an invalid start do nothing. Every other selector halts naming itself. */
int list_fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
uint32_t list_rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
void list_wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
#define RD list_rd
#define WR list_wr
static int s16(MacRuntime *rt, uint32_t a) { return (int16_t)RD(rt, a, 2); }
static int ram_ok(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static MacLists *lists(MacRuntime *rt) {
    if (!rt->lists) rt->lists = calloc(1, sizeof *rt->lists);
    if (!rt->lists) list_fail(rt, "List Manager state could not be allocated.");
    return rt->lists;
}
void mac_list_destroy(MacRuntime *rt) {
    if (!rt->lists) return;
    for (unsigned i = 0; i < LIST_MAX; i++) if (rt->lists->list[i].handle) {
        for (int k = 0; k < rt->lists->list[i].rows * rt->lists->list[i].cols; k++) free(rt->lists->list[i].cells[k].data);
        free(rt->lists->list[i].cells);
    }
    free(rt->lists->job.step); free(rt->lists); rt->lists = NULL;
}
uint32_t list_rec(MacRuntime *rt, const ListState *L) { uint32_t p, n; return mac_handle_block(rt, L->handle, &p, &n) && n >= LIST_REC ? p : 0; }
ListState *list_find(MacRuntime *rt, uint32_t handle) {
    MacLists *m = lists(rt); if (!m) return NULL;
    for (unsigned i = 0; i < LIST_MAX; i++) if (m->list[i].handle == handle && handle) return list_rec(rt, &m->list[i]) ? &m->list[i] : NULL;
    list_fail(rt, "A List Manager call names a ListHandle that is not a live list.");
    return NULL;
}
LCell *list_cell(ListState *L, int row, int col) {
    if (row < L->top || row >= L->top + L->rows || col < L->left || col >= L->left + L->cols) return NULL;
    return &L->cells[(row - L->top) * L->cols + (col - L->left)];
}
unsigned list_cell_offset(const ListState *L, int row, int col) {
    unsigned off = 0; int n = (row - L->top) * L->cols + (col - L->left);
    for (int i = 0; i < n; i++) off += L->cells[i].len;
    return off;
}
int list_rebuild(MacRuntime *rt, ListState *L) {
    uint32_t rec = list_rec(rt, L); if (!rec) return list_fail(rt, "A ListHandle lost its record.");
    unsigned total = 0; for (int i = 0; i < L->rows * L->cols; i++) total += L->cells[i].len;
    WR(rt, rec + R_BOUNDS, 2, (uint16_t)L->top); WR(rt, rec + R_BOUNDS + 2, 2, (uint16_t)L->left);
    WR(rt, rec + R_BOUNDS + 4, 2, (uint16_t)(L->top + L->rows)); WR(rt, rec + R_BOUNDS + 6, 2, (uint16_t)(L->left + L->cols));
    uint32_t h = mac_handle_new(rt, total ? total : 1), p, n;
    if (!h || !mac_handle_block(rt, h, &p, &n) || n < total) return list_fail(rt, "The List Manager could not allocate the cell data.");
    unsigned at = 0;
    for (int i = 0; i < L->rows * L->cols; i++) { if (L->cells[i].len) memcpy(mac_cpu_ram(rt->cpu) + p + at, L->cells[i].data, L->cells[i].len); at += L->cells[i].len; }
    if (L->cells_handle) mac_handle_dispose(rt, L->cells_handle);
    L->cells_handle = h; WR(rt, rec + R_CELLS, 4, h); WR(rt, rec + R_MAXINDEX, 2, total > 0x7fff ? 0x7fff : total);
    return 0;
}
void list_dispose(MacRuntime *rt, ListState *L) {
    list_free_scroll_bars(rt, L);
    for (int i = 0; i < L->rows * L->cols; i++) free(L->cells[i].data);
    free(L->cells);
    if (L->cells_handle) mac_handle_dispose(rt, L->cells_handle);
    mac_handle_dispose(rt, L->handle);
    memset(L, 0, sizeof *L);
}
/* Rebuilds the cell array with `rows` x `cols` cells, copying the cells that survive: old cell (r, c) goes to (r + dr, c + dc), the rest are empty. */
static int reshape(MacRuntime *rt, ListState *L, int rows, int cols, int dr, int dc) {
    if (rows < 0 || cols < 0 || (long)rows * cols > 65536) return list_fail(rt, "A list's size is beyond what the List Manager here supports (65,536 cells).");
    LCell *n = calloc((size_t)(rows * cols ? rows * cols : 1), sizeof *n);
    if (!n) return list_fail(rt, "No memory for the list's cells.");
    for (int r = 0; r < L->rows; r++) for (int c = 0; c < L->cols; c++) {
        LCell *o = &L->cells[r * L->cols + c]; int nr = r + dr, nc = c + dc;
        if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) n[nr * cols + nc] = *o; else free(o->data);
    }
    free(L->cells); L->cells = n; L->rows = rows; L->cols = cols;
    return 0;
}
/* Inserts (add) or removes `count` rows (or columns) at dataBounds position `at`: every surviving cell keeps its data and selection. */
static int remap(MacRuntime *rt, ListState *L, int row, int at, int count, int add) {
    int rows = L->rows, cols = L->cols, lo = row ? L->top : L->left, nr = row ? rows + (add ? count : -count) : rows, nc = row ? cols : cols + (add ? count : -count);
    if (nr < 0 || nc < 0 || (long)nr * nc > 65536) return list_fail(rt, "A list's size is beyond what the List Manager here supports (65,536 cells).");
    LCell *n = calloc((size_t)(nr * nc ? nr * nc : 1), sizeof *n);
    if (!n) return list_fail(rt, "No memory for the list's cells.");
    for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
        int idx = (row ? r : c) + lo, to = idx;
        LCell *o = &L->cells[r * cols + c];
        if (add) { if (idx >= at) to = idx + count; }
        else if (idx >= at && idx < at + count) { free(o->data); continue; }
        else if (idx >= at + count) to = idx - count;
        to -= lo;
        n[(row ? to : r) * nc + (row ? c : to)] = *o;
    }
    free(L->cells); L->cells = n; L->rows = nr; L->cols = nc;
    return 0;
}
static int set_data(MacRuntime *rt, ListState *L, LCell *c, uint32_t src, unsigned len, int append) {
    unsigned keep = append ? c->len : 0;
    if (keep + len > LIST_CELL_MAX) return list_fail(rt, "A list cell would hold more than 32,767 bytes.");
    if (len && !ram_ok(rt, src, len)) return list_fail(rt, "A List Manager cell's data pointer is outside guest RAM.");
    uint8_t *d = realloc(c->data, keep + len ? keep + len : 1);
    if (!d) return list_fail(rt, "No memory for a list cell.");
    if (len) memcpy(d + keep, mac_cpu_ram(rt->cpu) + src, len);
    c->data = d; c->len = (uint16_t)(keep + len);
    (void)L; return 0;
}
static void put_cell(MacRuntime *rt, uint32_t a, int row, int col) { WR(rt, a, 2, (uint16_t)row); WR(rt, a + 2, 2, (uint16_t)col); }
static void put_bool(MacRuntime *rt, uint32_t a, int v) { WR(rt, a, 1, v ? 1 : 0); WR(rt, a + 1, 1, 0); }

/* LNew(rView, dataBounds, cSize, theProc, theWindow, drawIt, hasGrow, scrollHoriz, scrollVert): ListHandle */
static int l_new(MacRuntime *rt, uint32_t sp) {
    uint32_t A = sp + 2, vptr = RD(rt, A + 22, 4), bptr = RD(rt, A + 18, 4), win = RD(rt, A + 8, 4);
    unsigned proc = RD(rt, A + 12, 2);
    if (!ram_ok(rt, vptr, 8) || !ram_ok(rt, bptr, 8)) return list_fail(rt, "LNew's rectangle pointers are outside guest RAM.");
    if (!mac_controls_window(rt, win) && !mac_cpu_halted(rt->cpu)) return list_fail(rt, "LNew's window is neither a registered window nor an open dialog.");
    MacLists *m = lists(rt); if (!m) return -1;
    ListState *L = NULL; for (unsigned i = 0; i < LIST_MAX && !L; i++) if (!m->list[i].handle) L = &m->list[i];
    if (!L) return list_fail(rt, "Too many lists are open at once.");
    int bt = s16(rt, bptr), bl = s16(rt, bptr + 2), bb = s16(rt, bptr + 4), br = s16(rt, bptr + 6);
    uint32_t ldef = 0;
    if (proc) { ldef = list_ldef_load(rt, proc); if (!ldef) { char w[96]; snprintf(w, sizeof w, "LNew: the program has no LDEF resource %u.", proc); return list_fail(rt, w); } }
    uint32_t h = mac_handle_new(rt, LIST_REC), rec = 0, n;
    if (!h || !mac_handle_block(rt, h, &rec, &n)) return list_fail(rt, "The List Manager could not allocate a list record.");
    memset(L, 0, sizeof *L); L->handle = h; L->top = bt; L->left = bl; L->ldef = ldef;
    if (reshape(rt, L, bb - bt, br - bl, 0, 0) < 0) { L->handle = 0; return -1; }
    for (int i = 0; i < 8; i++) WR(rt, rec + R_VIEW + (unsigned)i, 1, RD(rt, vptr + (unsigned)i, 1));
    WR(rt, rec + R_PORT, 4, win); WR(rt, rec + R_CSIZE, 4, RD(rt, A + 14, 4));
    WR(rt, rec + R_ACTIVE, 1, 1); WR(rt, rec + R_LASTCLICK, 4, 0xffffffffu); WR(rt, rec + R_DEFPROC, 4, ldef);
    if (!RD(rt, A + 6, 1)) WR(rt, rec + R_LISTFLAGS, 1, LF_DRAWOFF);
    list_default_sizes(rt, L);
    if (list_make_scroll_bars(rt, L, RD(rt, A + 0, 1) != 0, RD(rt, A + 2, 1) != 0) < 0 || list_geometry(rt, L) < 0 || list_rebuild(rt, L) < 0) return -1;
    if (list_job_begin(rt, sp, 28, sp + 28, 4, h) < 0) return -1;
    if (ldef && list_step(rt, L, LDEF_INIT, 0, bt, bl) < 0) return -1;
    return list_job_run(rt);
}
static int clamp_after(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int mac_list_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9e7) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7); size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 40) return list_fail(rt, "List Manager call has an invalid stack.");
    if (rt->lists && rt->lists->job.active) return list_fail(rt, "A List Manager call was made while another one was still running (unsupported).");
    unsigned sel = RD(rt, sp, 2); uint32_t A = sp + 2;
    if (sel == 0x44) return l_new(rt, sp);
    /* every other selector takes the ListHandle as its last argument, which is nearest the selector */
    static const struct { unsigned sel, args, result; } shape[] = {
        {0x00, 6, 0}, {0x04, 8, 2}, {0x08, 8, 2}, {0x0c, 14, 0}, {0x10, 4, 0}, {0x14, 8, 0}, {0x18, 10, 2}, {0x1c, 8, 0}, {0x20, 8, 0}, {0x24, 8, 0}, {0x28, 4, 0},
        {0x2c, 6, 0}, {0x30, 8, 0}, {0x34, 16, 0}, {0x38, 16, 0}, {0x3c, 10, 2}, {0x40, 4, 4}, {0x48, 12, 2}, {0x4c, 12, 0}, {0x50, 8, 0}, {0x54, 18, 2}, {0x58, 14, 0},
        {0x5c, 10, 0}, {0x60, 8, 0}, {0x64, 8, 0}};
    unsigned args = 0, result = 0, k;
    for (k = 0; k < sizeof shape / sizeof shape[0]; k++) if (shape[k].sel == sel) { args = shape[k].args; result = shape[k].result; break; }
    if (k == sizeof shape / sizeof shape[0]) { char w[80]; snprintf(w, sizeof w, "List Manager selector $%04X is unsupported.", sel); return list_fail(rt, w); }
    if (z - sp < 2u + args + result) return list_fail(rt, "List Manager call has an invalid stack.");
    ListState *L = list_find(rt, RD(rt, A, 4)); if (!L) return -1;
    uint32_t rec = list_rec(rt, L), res = A + args, pop = 2 + args;
    int top = L->top, left = L->left, bottom = L->top + L->rows, right = L->left + L->cols;
    if (list_job_begin(rt, sp, pop, 0, 0, 0) < 0) return -1;
    MacLists *m = rt->lists;
    switch (sel) {
    case 0x28: if (list_step(rt, L, LDEF_CLOSE, 0, top, left) < 0) return -1; m->job.dispose = L->handle; break;                  /* LDispose(lHandle): lCloseMsg, then the scroll bars, cells and handles go */
    case 0x00: list_set_active(rt, L, RD(rt, A + 4, 1) != 0); break;                                        /* LActivate(act, lHandle) */
    case 0x04: case 0x08: {                                                                                 /* LAddColumn / LAddRow(count, after, lHandle): INTEGER */
        int row = sel == 0x08, count = s16(rt, A + 6), after = s16(rt, A + 4), first = row ? clamp_after(after, top, bottom) : clamp_after(after, left, right);
        if (count < 0 || (long)count + (row ? L->rows : L->cols) > 32000) return list_fail(rt, "LAddRow/LAddColumn: a count that is negative or makes a list too large.");
        if (count && remap(rt, L, row, first, count, 1) < 0) return -1;
        WR(rt, res, 2, (uint16_t)first);
        if (list_rebuild(rt, L) < 0 || list_geometry(rt, L) < 0 || list_redraw_all(rt, L) < 0) return -1;
        break; }
    case 0x20: case 0x24: {                                                                                 /* LDelColumn / LDelRow(count, start, lHandle) */
        int row = sel == 0x24, count = s16(rt, A + 6), start = s16(rt, A + 4), lo = row ? top : left, hi = row ? bottom : right;
        if (count < 0) return list_fail(rt, "LDelRow/LDelColumn with a negative count.");
        if (count == 0) { start = lo; count = hi - lo; }          /* a count of 0 deletes them all (Apple's List Manager reference) */
        if (start < lo || start >= hi) break;                      /* an invalid row or column number: nothing is done (same source) */
        if (start + count > hi) count = hi - start;
        if (count && remap(rt, L, row, start, count, 0) < 0) return -1;
        if (list_rebuild(rt, L) < 0 || list_geometry(rt, L) < 0 || list_redraw_all(rt, L) < 0) return -1;
        break; }
    case 0x0c: case 0x58: {                                                                                 /* LAddToCell / LSetCell(dataPtr, dataLen, theCell, lHandle) */
        LCell *c = list_cell(L, s16(rt, A + 4), s16(rt, A + 6)); int row = s16(rt, A + 4), col = s16(rt, A + 6);
        if (!c) break;
        if (set_data(rt, L, c, RD(rt, A + 10, 4), RD(rt, A + 8, 2), sel == 0x0c) < 0 || list_rebuild(rt, L) < 0 || list_step(rt, L, LDEF_DRAW, c->sel, row, col) < 0) return -1;
        break; }
    case 0x1c: {                                                                                            /* LClrCell(theCell, lHandle) */
        int row = s16(rt, A + 4), col = s16(rt, A + 6); LCell *c = list_cell(L, row, col); if (!c) break;
        free(c->data); c->data = NULL; c->len = 0;
        if (list_rebuild(rt, L) < 0 || list_step(rt, L, LDEF_DRAW, c->sel, row, col) < 0) return -1;
        break; }
    case 0x38: {                                                                                            /* LGetCell(dataPtr, VAR dataLen, theCell, lHandle) */
        LCell *c = list_cell(L, s16(rt, A + 4), s16(rt, A + 6)); uint32_t lp = RD(rt, A + 8, 4), dp = RD(rt, A + 12, 4);
        if (!ram_ok(rt, lp, 2)) return list_fail(rt, "LGetCell's length pointer is outside guest RAM.");
        unsigned want = RD(rt, lp, 2), have = c ? c->len : 0, n = have < want ? have : want;
        if (n && !ram_ok(rt, dp, n)) return list_fail(rt, "LGetCell's data pointer is outside guest RAM.");
        if (n) memcpy(mac_cpu_ram(rt->cpu) + dp, c->data, n);
        WR(rt, lp, 2, n); break; }
    case 0x34: {                                                                                            /* LFind(VAR offset, VAR len, theCell, lHandle) */
        int row = s16(rt, A + 4), col = s16(rt, A + 6); LCell *c = list_cell(L, row, col); uint32_t lp = RD(rt, A + 8, 4), op = RD(rt, A + 12, 4);
        if (!ram_ok(rt, lp, 2) || !ram_ok(rt, op, 2)) return list_fail(rt, "LFind's result pointers are outside guest RAM.");
        WR(rt, op, 2, c ? list_cell_offset(L, row, col) : 0); WR(rt, lp, 2, c ? c->len : 0); break; }
    case 0x5c: {                                                                                            /* LSetSelect(setIt, theCell, lHandle) */
        if (list_set_select(rt, L, s16(rt, A + 4), s16(rt, A + 6), RD(rt, A + 8, 1) != 0) < 0) return -1;
        break; }
    case 0x3c: {                                                                                            /* LGetSelect(next, VAR theCell, lHandle): BOOLEAN */
        uint32_t cp = RD(rt, A + 4, 4); int next = RD(rt, A + 8, 1) != 0, found = 0;
        if (!ram_ok(rt, cp, 4)) return list_fail(rt, "LGetSelect's cell pointer is outside guest RAM.");
        int row = s16(rt, cp), col = s16(rt, cp + 2);
        if (!next) { LCell *c = list_cell(L, row, col); found = c && c->sel; }
        else for (int r = row < top ? top : row; r < bottom && !found; r++) for (int c = (r == row && col >= left) ? col : left; c < right; c++)
            if (list_cell(L, r, c)->sel) { put_cell(rt, cp, r, c); found = 1; break; }
        put_bool(rt, res, found); break; }
    case 0x40: WR(rt, res, 4, RD(rt, rec + R_LASTCLICK, 4)); break;                                         /* LLastClick(lHandle): Cell */
    case 0x48: {                                                                                            /* LNextCell(hNext, vNext, VAR theCell, lHandle): BOOLEAN */
        uint32_t cp = RD(rt, A + 4, 4); int vnext = RD(rt, A + 8, 1) != 0, hnext = RD(rt, A + 10, 1) != 0;
        if (!ram_ok(rt, cp, 4)) return list_fail(rt, "LNextCell's cell pointer is outside guest RAM.");
        int row = s16(rt, cp), col = s16(rt, cp + 2), ok = 1;
        if (hnext) { col++; if (col >= right) { if (vnext) { col = left; row++; } else ok = 0; } } else if (vnext) row++;
        if (row >= bottom) ok = 0;
        if (ok) put_cell(rt, cp, row, col);
        put_bool(rt, res, ok); break; }
    case 0x4c: {                                                                                            /* LRect(VAR cellRect, theCell, lHandle) */
        uint32_t rp = RD(rt, A + 8, 4); int16_t r[4] = {0, 0, 0, 0};
        if (!ram_ok(rt, rp, 8)) return list_fail(rt, "LRect's rectangle pointer is outside guest RAM.");
        if (!list_cell_rect(rt, L, s16(rt, A + 4), s16(rt, A + 6), r)) memset(r, 0, sizeof r);
        for (int i = 0; i < 4; i++) WR(rt, rp + 2u * (unsigned)i, 2, (uint16_t)r[i]);
        break; }
    case 0x54: {                                                                                            /* LSearch(dataPtr, dataLen, searchProc, VAR theCell, lHandle): BOOLEAN */
        uint32_t cp = RD(rt, A + 4, 4), proc = RD(rt, A + 8, 4), dp = RD(rt, A + 14, 4); unsigned len = RD(rt, A + 12, 2);
        if (proc) return list_fail(rt, "LSearch with a program's own search procedure is unsupported.");
        if (!ram_ok(rt, cp, 4) || (len && !ram_ok(rt, dp, len))) return list_fail(rt, "LSearch's pointers are outside guest RAM.");
        int row = s16(rt, cp), col = s16(rt, cp + 2), found = 0;                                              /* the default match here is exact bytes, from theCell on, row by row */
        for (int r = row < top ? top : row; r < bottom && !found; r++) for (int c = (r == row && col >= left) ? col : left; c < right; c++) {
            LCell *x = list_cell(L, r, c);
            if (x->len == len && (!len || !memcmp(x->data, mac_cpu_ram(rt->cpu) + dp, len))) { put_cell(rt, cp, r, c); found = 1; break; }
        }
        put_bool(rt, res, found); break; }
    case 0x2c: WR(rt, rec + R_LISTFLAGS, 1, (RD(rt, rec + R_LISTFLAGS, 1) & ~(unsigned)LF_DRAWOFF) | (RD(rt, A + 4, 1) ? 0u : LF_DRAWOFF)); break;   /* LDoDraw(drawIt, lHandle) */
    case 0x30: { int row = s16(rt, A + 4), col = s16(rt, A + 6); LCell *c = list_cell(L, row, col); if (c && list_step(rt, L, LDEF_DRAW, c->sel, row, col) < 0) return -1; break; }   /* LDraw(theCell, lHandle) */
    case 0x64: {                                                                                            /* LUpdate(theRgn, lHandle): every visible cell */
        uint32_t rgn = RD(rt, A + 4, 4), p, n; if (!mac_handle_block(rt, rgn, &p, &n) || n < 10) return list_fail(rt, "LUpdate's region is not a live region handle.");
        if (list_redraw_all(rt, L) < 0) return -1;
        break; }
    case 0x50: if (list_scroll_to(rt, L, s16(rt, (uint32_t)(rec + R_VISIBLE)) + s16(rt, A + 4), s16(rt, rec + R_VISIBLE + 2) + s16(rt, A + 6)) < 0) return -1; break;   /* LScroll(dCols, dRows, lHandle) */
    case 0x10: if (list_autoscroll(rt, L) < 0) return -1; break;                                            /* LAutoScroll(lHandle) */
    case 0x60: {                                                                                            /* LSize(listWidth, listHeight, lHandle) */
        int w = s16(rt, A + 6), h = s16(rt, A + 4);
        if (w < 0 || h < 0) return list_fail(rt, "LSize with a negative width or height.");
        WR(rt, rec + R_VIEW + 4, 2, (uint16_t)(s16(rt, rec + R_VIEW) + h)); WR(rt, rec + R_VIEW + 6, 2, (uint16_t)(s16(rt, rec + R_VIEW + 2) + w));
        if (list_geometry(rt, L) < 0 || list_redraw_all(rt, L) < 0) return -1;
        break; }
    case 0x14: {                                                                                            /* LCellSize(cSize, lHandle) */
        WR(rt, rec + R_CSIZE, 4, RD(rt, A + 4, 4)); list_default_sizes(rt, L);
        if (list_geometry(rt, L) < 0 || list_redraw_all(rt, L) < 0) return -1;
        break; }
    case 0x18: {                                                                                            /* LClick(pt, modifiers, lHandle): BOOLEAN */
        int dbl = 0; uint32_t pt = RD(rt, A + 6, 4);
        if (list_click(rt, L, (int16_t)(pt >> 16), (int16_t)(pt & 0xffff), RD(rt, A + 4, 2), &dbl) < 0) return -1;
        put_bool(rt, res, dbl); break; }
    default: return list_fail(rt, "List Manager selector has no implementation.");
    }
    return list_job_run(rt);
}
