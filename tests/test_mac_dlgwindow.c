/* Dialog control items: a dialog with CNTL items owns a hidden port window, its controls (standard or the program's own CDEF) are created with the item's rectangle and
 * drawn after every repaint, item handles are control handles, and a button/check box/radio item handle answers Get/SetCtlValue and HiliteControl. */
#include "mac_dlgwindow.h"
#include "mac_cdef.h"
#include "mac_window_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x30000u
#define BACK 77
/* ---- a minimal resource-fork writer: header, data, map (types in order of first appearance) ---- */
typedef struct { uint32_t type; int16_t id; uint8_t data[512]; size_t n; } Res;
typedef struct { Res r[48]; unsigned count; } Fork;
static void put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, x >> 16); put16(p + 2, x & 0xffff); }
static void add(Fork *f, uint32_t type, int16_t id, const void *data, size_t n) {
    CHECK(f->count < 48 && n <= sizeof f->r[0].data);
    Res *r = &f->r[f->count++]; r->type = type; r->id = id; r->n = n; memcpy(r->data, data, n);
}
static size_t build(const Fork *f, uint8_t *out) {
    size_t at = 256; memset(out, 0, 16384);
    uint32_t offsets[48];
    for (unsigned i = 0; i < f->count; i++) { offsets[i] = (uint32_t)(at - 256); put32(out + at, (uint32_t)f->r[i].n); memcpy(out + at + 4, f->r[i].data, f->r[i].n); at += 4 + f->r[i].n; }
    size_t map = at; uint32_t types[48]; unsigned ntypes = 0;
    for (unsigned i = 0; i < f->count; i++) { unsigned k = 0; while (k < ntypes && types[k] != f->r[i].type) k++; if (k == ntypes) types[ntypes++] = f->r[i].type; }
    uint8_t *m = out + map; unsigned typelist = 28, refs = typelist + 2 + 8 * ntypes, ref_at = refs;
    put16(m + 24, typelist); put16(m + 26, 0); put16(m + typelist, ntypes - 1);
    for (unsigned t = 0; t < ntypes; t++) {
        unsigned count = 0; for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) count++;
        put32(m + typelist + 2 + t * 8, types[t]); put16(m + typelist + 6 + t * 8, count - 1); put16(m + typelist + 8 + t * 8, ref_at - typelist);
        for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) {
            put16(m + ref_at, (unsigned)(uint16_t)f->r[i].id); put16(m + ref_at + 2, 0xffff); m[ref_at + 4] = 0;
            m[ref_at + 5] = (uint8_t)(offsets[i] >> 16); m[ref_at + 6] = (uint8_t)(offsets[i] >> 8); m[ref_at + 7] = (uint8_t)offsets[i];
            ref_at += 12;
        }
    }
    size_t mlen = ref_at; put16(m + 26, (unsigned)mlen); /* the (empty) name list starts at the end of the map */
    put32(out, 256); put32(out + 4, (uint32_t)map); put32(out + 8, (uint32_t)(map - 256)); put32(out + 12, (uint32_t)mlen);
    memcpy(m, out, 16); /* the map starts with a copy of the header */
    return map + mlen;
}

/* ---- DITL / DLOG builders ---- */
typedef struct { uint8_t b[600]; size_t n; } W;
static void byte(W *w, unsigned v) { CHECK(w->n < sizeof w->b); w->b[w->n++] = (uint8_t)v; }
static void word(W *w, unsigned v) { byte(w, v >> 8); byte(w, v); }
static void item(W *w, int t, int l, int b, int r, unsigned type, const void *d, unsigned n) {
    word(w, 0); word(w, 0); word(w, (unsigned)t); word(w, (unsigned)l); word(w, (unsigned)b); word(w, (unsigned)r); byte(w, type); byte(w, n);
    for (unsigned i = 0; i < n; i++) byte(w, ((const uint8_t *)d)[i]); if (n & 1) byte(w, 0);
}
static void dlog(Fork *f, int16_t id, int t, int l, int b, int r, unsigned visible, int16_t items) {
    W w = {0}; word(&w, (unsigned)t); word(&w, (unsigned)l); word(&w, (unsigned)b); word(&w, (unsigned)r); word(&w, 1);   /* procID dBoxProc */
    byte(&w, visible); byte(&w, 0); byte(&w, 1); byte(&w, 0); word(&w, 0); word(&w, 0); word(&w, (unsigned)items); byte(&w, 0);
    add(f, 0x444c4f47, id, w.b, w.n);
}
static uint8_t image[16384];
static void cntl(Fork *f, int16_t id, int vis, unsigned proc, const char *title) {
    uint8_t d[40] = {0}; put16(d + 2, 0); put16(d + 4, 20); put16(d + 6, 100); put16(d + 10, vis ? 0x100 : 0); put16(d + 12, 1); put16(d + 14, 0); put16(d + 16, proc); put32(d + 18, 7);
    d[22] = (uint8_t)strlen(title); memcpy(d + 23, title, strlen(title)); add(f, 0x434e544c, id, d, 23 + strlen(title));
}
static void fixture(Fork *f) {
    uint8_t zero[32] = {0}, code[10] = {0, 0, 0, 1, 0xa9, 0xf4};
    put32(zero, 0x100); put32(zero + 4, 0x100); put32(zero + 8, 8); put32(zero + 12, 32);
    add(f, 0x434f4445, 0, zero, 32); add(f, 0x434f4445, 1, code, 10);
    W w;
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 30, 10, 50, 110, 7, "\0\310", 2); add(f, 0x4449544c, 300, w.b, w.n);         /* CNTL 200 (a push button) */
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 30, 10, 50, 110, 7, "\0\311", 2); add(f, 0x4449544c, 301, w.b, w.n);         /* CNTL 201 (custom) */
    memset(&w, 0, sizeof w); word(&w, 0); item(&w, 30, 10, 50, 110, 7, "\3\347", 2); add(f, 0x4449544c, 302, w.b, w.n);                                           /* CNTL 999: missing */
    memset(&w, 0, sizeof w); word(&w, 0); item(&w, 30, 10, 50, 110, 7, "", 0); add(f, 0x4449544c, 303, w.b, w.n);                                                  /* no CNTL id */
    memset(&w, 0, sizeof w); word(&w, 3); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 70, 100, 90, 160, 4, "Two", 3); item(&w, 40, 10, 55, 100, 5, "Check", 5);
    item(&w, 40, 120, 55, 200, 6, "Radio", 5); add(f, 0x4449544c, 304, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 0); item(&w, 30, 10, 50, 110, 7, "\0\312", 2); add(f, 0x4449544c, 305, w.b, w.n);                                           /* CNTL 202: CDEF 5 is missing */
    cntl(f, 200, 1, 0, "Go"); cntl(f, 201, 1, 2048, "Pop"); cntl(f, 202, 1, 80, "Bad");
    { uint8_t cdef[32]; for (unsigned i = 0; i < 32; i++) cdef[i] = (uint8_t)(0x4e + (i & 1)); add(f, 0x43444546, 128, cdef, 32); }
    dlog(f, 300, 40, 50, 140, 330, 1, 300); dlog(f, 301, 40, 50, 140, 330, 1, 301); dlog(f, 302, 40, 50, 140, 330, 1, 302); dlog(f, 303, 40, 50, 140, 330, 1, 303);
    dlog(f, 304, 40, 50, 140, 330, 1, 304); dlog(f, 305, 40, 50, 140, 330, 1, 305); dlog(f, 306, 40, 50, 140, 330, 0, 301); dlog(f, 307, 40, 50, 140, 330, 0, 300);
}
static uint32_t raw(MacRuntime *rt, uint32_t p, unsigned n) { const uint8_t *r = mac_cpu_ram(rt->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | r[p + i]; return v; }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static MacRuntime fresh(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(rt.cpu);
    rt.heap_next = 0x100000; rt.heap_limit = 0x600000; rt.qd_globals = 0x7000; rt.modifiers = 0x80;
    CHECK(mac_color_init(&rt) == 1); mac_cpu_set(rt.cpu, MAC_A7, SP); CHECK(mac_toolbox_service(&rt, 0xa912) == 1);
    Fork f; memset(&f, 0, sizeof f); fixture(&f); size_t n = build(&f, image); char err[200];
    CHECK(!mac_resource_open(&rt.fork, image, n, err, sizeof err));
    memset(mac_cpu_ram(rt.cpu) + rt.screen_base, BACK, 640 * 480);
    return rt;
}
static int trap(MacRuntime *rt, unsigned op, uint32_t sp) { mac_cpu_set(rt->cpu, MAC_A7, sp); mac_cpu_set(rt->cpu, MAC_PC, 0x10002); return mac_dlgmgr_service(rt, (uint16_t)op); }
static int get_new(MacRuntime *rt, int id) { wr(rt, SP, 4, 0xffffffffu); wr(rt, SP + 4, 4, 0); wr(rt, SP + 8, 2, (uint16_t)id); wr(rt, SP + 10, 4, 0xdead); return trap(rt, 0xa97c, SP); }
static int get_ditem(MacRuntime *rt, uint32_t d, int item, uint32_t *type, uint32_t *handle) {
    wr(rt, SP + 14, 4, d); wr(rt, SP + 12, 2, (uint16_t)item); wr(rt, SP + 8, 4, 0x4000); wr(rt, SP + 4, 4, 0x4004); wr(rt, SP, 4, 0x4008);
    int rc = trap(rt, 0xa98d, SP); *type = raw(rt, 0x4000, 2); *handle = raw(rt, 0x4004, 4); return rc;
}
static uint32_t a7(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_A7); }
static int msg(MacRuntime *rt) { return (int16_t)raw(rt, a7(rt) + 8, 2); }
static uint32_t ctl_arg(MacRuntime *rt) { return raw(rt, a7(rt) + 10, 4); }
static int var_arg(MacRuntime *rt) { return (int16_t)raw(rt, a7(rt) + 14, 2); }
static int ret(MacRuntime *rt, uint32_t result) { uint32_t a = rt->guest.sp - 4; mac_cpu_set(rt->cpu, MAC_A7, a); wr(rt, a, 4, result); return mac_guest_return(rt); }
static int running(MacRuntime *rt) { return rt->guest.active && !mac_cpu_halted(rt->cpu); }
static int all_back(MacRuntime *rt) { const uint8_t *s = mac_cpu_ram(rt->cpu) + rt->screen_base; for (unsigned i = 0; i < 640 * 480; i++) if (s[i] != BACK) return 0; return 1; }
static int ctl_call(MacRuntime *rt, uint16_t op, unsigned args) { mac_cpu_set(rt->cpu, MAC_A7, SP); return mac_controls_service(rt, op); (void)args; }

int main(void) {
    /* A standard push button control item: the item handle is the control handle, with the item's rectangle; the port window is hidden, port_only 2 (dialog shown). */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 300) == 1 && !rt.guest.active && a7(&rt) == SP + 10);
        uint32_t d = raw(&rt, SP + 10, 4), type, h; CHECK(d && rt.window_count == 1);
        CHECK(get_ditem(&rt, d, 2, &type, &h) == 1 && type == 7 && h && !(h & 1));
        uint32_t p = raw(&rt, h, 4), win = raw(&rt, p + CTL_OWNER, 4); MacWindowState *w = mac_window_find(&rt, win);
        {   uint32_t vr = raw(&rt, raw(&rt, win + 24, 4), 4); CHECK((int16_t)raw(&rt, vr + 2, 2) == 0 && (int16_t)raw(&rt, vr + 4, 2) == 0 && (int16_t)raw(&rt, vr + 6, 2) == 100 && (int16_t)raw(&rt, vr + 8, 2) == 280); }   /* visRgn: the whole content */
        CHECK(p && w && !w->visible && w->port_only == 2 && w->bounds[0] == 40 && w->bounds[1] == 50 && w->bounds[2] == 140 && w->bounds[3] == 330);
        CHECK((int16_t)raw(&rt, p + CTL_RECT, 2) == 30 && (int16_t)raw(&rt, p + CTL_RECT + 2, 2) == 10 && (int16_t)raw(&rt, p + CTL_RECT + 4, 2) == 50 && (int16_t)raw(&rt, p + CTL_RECT + 6, 2) == 110);
        CHECK(raw(&rt, p + CTL_DEFPROC, 4) == 0 && raw(&rt, p + CTL_DATA, 4) == 0 && raw(&rt, p + CTL_TITLE, 1) == 2);
        {   const uint8_t *s = mac_cpu_ram(rt.cpu) + rt.screen_base; unsigned drawn = 0;                               /* the button was drawn inside its item rectangle, in screen coordinates */
            for (int y = 70; y < 90; y++) for (int x = 60; x < 160; x++) if (s[y * 640 + x] != BACK) drawn++; CHECK(drawn > 40); }
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 1); wr(&rt, SP + 2, 4, h); CHECK(mac_controls_service(&rt, 0xa963) == 1 && (int16_t)raw(&rt, p + CTL_VALUE, 2) == 1);
        wr(&rt, SP + 4, 2, 0x5555); wr(&rt, SP, 4, h); mac_cpu_set(rt.cpu, MAC_A7, SP); CHECK(mac_controls_service(&rt, 0xa960) == 1 && raw(&rt, SP + 4, 2) == 1);
        /* DisposeDialog: the control and the port window go, the screen is restored. */
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa983, SP) == 1 && rt.window_count == 0 && raw(&rt, h, 4) == 0 && all_back(&rt) && mac_window_find(&rt, win) == NULL);
        mac_cpu_destroy(rt.cpu); }
    /* A custom CDEF item: initCntl then drawCntl as a guest job that ends GetNewDialog; the result and the stack pop come at the end. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 301) == 1 && running(&rt));
        CHECK(msg(&rt) == CDEF_INIT && var_arg(&rt) == 0 && raw(&rt, SP + 10, 4) == 0xdead);
        uint32_t h = ctl_arg(&rt), p = raw(&rt, h, 4); MacWindowState *w = mac_window_find(&rt, raw(&rt, p + CTL_OWNER, 4));
        CHECK(w && w->port_only == 2 && raw(&rt, p + CTL_DEFPROC, 4) != 0 && rt.current_port == w->address);                 /* the CDEF runs in the port window */
        CHECK(ret(&rt, 0) == 0 && running(&rt) && msg(&rt) == CDEF_DRAW && ctl_arg(&rt) == h);
        CHECK(ret(&rt, 0) == 0 && !rt.guest.active && a7(&rt) == SP + 10 && raw(&rt, SP + 10, 4) != 0xdead);
        uint32_t d = raw(&rt, SP + 10, 4), type, hh; CHECK(get_ditem(&rt, d, 2, &type, &hh) == 1 && type == 7 && hh == h);
        /* DrawDialog repaints the dialog over its controls: they are sent drawCntl again, and the trap's own results are untouched. */
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa981, SP) == 1 && running(&rt) && msg(&rt) == CDEF_DRAW && ctl_arg(&rt) == h);
        CHECK(ret(&rt, 0) == 0 && !rt.guest.active && a7(&rt) == SP + 4);
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa983, SP) == 1 && rt.window_count == 0 && raw(&rt, h, 4) == 0 && all_back(&rt));         /* no dispCntl is sent on DisposeDialog */
        mac_cpu_destroy(rt.cpu); }
    /* A hidden dialog is not drawn, controls included, until ShowWindow; HideWindow stops them again. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 306) == 1 && running(&rt) && msg(&rt) == CDEF_INIT);
        uint32_t h = ctl_arg(&rt), p = raw(&rt, h, 4); MacWindowState *w = mac_window_find(&rt, raw(&rt, p + CTL_OWNER, 4));
        CHECK(w && w->port_only == 1);
        CHECK(ret(&rt, 0) == 0 && !rt.guest.active && a7(&rt) == SP + 10 && all_back(&rt));                                   /* no drawCntl while hidden */
        uint32_t d = raw(&rt, SP + 10, 4);
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 1); wr(&rt, SP + 2, 4, h); CHECK(mac_controls_service(&rt, 0xa963) == 1 && !rt.guest.active && a7(&rt) == SP + 6);   /* a value change draws nothing while hidden */
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa915, SP) == 1 && w->port_only == 2 && running(&rt) && msg(&rt) == CDEF_DRAW);   /* ShowWindow */
        CHECK(ret(&rt, 0) == 0 && !rt.guest.active && a7(&rt) == SP + 4);
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa916, SP) == 1 && w->port_only == 1 && !rt.guest.active && all_back(&rt));       /* HideWindow */
        mac_cpu_destroy(rt.cpu); }
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 307) == 1 && !rt.guest.active && a7(&rt) == SP + 10);                      /* a standard control in a hidden dialog */
        CHECK(all_back(&rt)); mac_cpu_destroy(rt.cpu); }
    /* SetPort(dialog) gives the dialog its port window: QuickDraw is no longer swallowed, thePort is the port window, DisposeDialog puts the port back. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 304) == 1); uint32_t d = raw(&rt, SP + 10, 4);
        uint32_t before_port = rt.current_port; CHECK(rt.window_count == 0);
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa873, SP) == 1 && a7(&rt) == SP + 4 && rt.window_count == 1);
        MacWindowState *w = mac_window_find(&rt, rt.current_port);
        CHECK(w && w->port_only == 2 && !w->visible && rt.current_port != d && raw(&rt, rt.qd_globals, 4) == rt.current_port && w->bounds[0] == 40 && w->bounds[1] == 50);
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, 0x4100); CHECK(mac_dlgmgr_service(&rt, 0xa8a1) == 0);                   /* FrameRect now goes to the port window's QuickDraw */
        uint32_t win = rt.current_port;
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa873, SP) == 1 && rt.window_count == 1 && rt.current_port == win);                 /* a second SetPort(dialog) reuses it */
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa983, SP) == 1 && rt.window_count == 0 && rt.current_port != win && mac_window_find(&rt, win) == NULL && raw(&rt, rt.qd_globals, 4) == rt.current_port);
        (void)before_port; mac_cpu_destroy(rt.cpu); }
    /* Without the colour window machinery SetPort(dialog) keeps the old behaviour: no port window, QuickDraw on the dialog is swallowed. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 304) == 1); uint32_t d = raw(&rt, SP + 10, 4); uint32_t port = rt.current_port; rt.manager_flags &= ~2u;
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa873, SP) == 1 && a7(&rt) == SP + 4 && rt.window_count == 0 && rt.current_port == port);
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, 0x4100); CHECK(mac_dlgmgr_service(&rt, 0xa8a1) == 1 && a7(&rt) == SP + 4);
        mac_cpu_destroy(rt.cpu); }
    /* ModalDialog repaints only when the dialog's state changed, so what the program drew into the dialog itself survives the waiting loop. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 304) == 1); uint32_t d = raw(&rt, SP + 10, 4);
        uint8_t *s = mac_cpu_ram(rt.cpu) + rt.screen_base; const unsigned at = 100 * 640 + 100;                                  /* inside the dialog (50..330 x 40..140), clear of its items */
        CHECK(s[at] != 0x13); s[at] = 0x13;
        wr(&rt, SP, 4, 0x4000); wr(&rt, SP + 4, 4, 0); CHECK(trap(&rt, 0xa991, SP) == 1); CHECK(s[at] == 0x13);                    /* begin: unchanged state, no repaint */
        for (unsigned k = 0; k < 3; k++) { CHECK(trap(&rt, 0xa991, SP) == 1); CHECK(s[at] == 0x13); }                          /* waiting passes */
        memcpy(rt.dialog_params[0], "\003Bob", 4); (void)d;                                                                       /* ParamText changes what the dialog shows: the next pass repaints */
        wr(&rt, SP, 4, 0x4000); wr(&rt, SP + 4, 4, 0); CHECK(trap(&rt, 0xa991, SP) == 1); CHECK(s[at] != 0x13);
        mac_cpu_destroy(rt.cpu); }
    /* While a CDEF runs for a dialog's control, QuickDraw calls are the CDEF's (drawing in the port window). */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 301) == 1 && running(&rt)); CHECK(ret(&rt, 0) == 0 && ret(&rt, 0) == 0 && !rt.guest.active);
        uint32_t d = raw(&rt, SP + 10, 4); wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa873, SP) == 1);                                       /* SetPort(dialog) */
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, 0x4100); CHECK(mac_dlgmgr_service(&rt, 0xa8a1) == 0);   /* FrameRect on the dialog port: the QuickDraw service's, not swallowed (the dialog has its port window) */
        wr(&rt, SP, 4, d); CHECK(trap(&rt, 0xa981, SP) == 1 && running(&rt));                                                          /* DrawDialog: the CDEF runs */
        mac_cpu_set(rt.cpu, MAC_A7, SP - 0x100); CHECK(mac_dlgmgr_service(&rt, 0xa8a1) == 0);                                          /* ... and its FrameRect is not swallowed */
        mac_cpu_destroy(rt.cpu); }
    /* GetCtlValue / SetCtlValue / HiliteControl on button, check box and radio button item handles. */
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 304) == 1); uint32_t d = raw(&rt, SP + 10, 4), type, ok = 0, chk = 0, rad = 0;
        CHECK(get_ditem(&rt, d, 1, &type, &ok) == 1 && type == 4 && ok && get_ditem(&rt, d, 3, &type, &chk) == 1 && type == 5 && get_ditem(&rt, d, 4, &type, &rad) == 1 && type == 6);
        wr(&rt, SP + 4, 2, 0x5555); wr(&rt, SP, 4, chk); CHECK(ctl_call(&rt, 0xa960, 4) == 1 && raw(&rt, SP + 4, 2) == 0 && a7(&rt) == SP + 4);
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 5); wr(&rt, SP + 2, 4, chk); CHECK(mac_controls_service(&rt, 0xa963) == 1 && a7(&rt) == SP + 6);   /* any non-zero value is checked */
        wr(&rt, SP + 4, 2, 0x5555); wr(&rt, SP, 4, chk); CHECK(ctl_call(&rt, 0xa960, 4) == 1 && raw(&rt, SP + 4, 2) == 1);
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 0); wr(&rt, SP + 2, 4, chk); CHECK(mac_controls_service(&rt, 0xa963) == 1);
        wr(&rt, SP + 4, 2, 0x5555); wr(&rt, SP, 4, chk); CHECK(ctl_call(&rt, 0xa960, 4) == 1 && raw(&rt, SP + 4, 2) == 0);
        unsigned before = 0; { const uint8_t *s = mac_cpu_ram(rt.cpu) + rt.screen_base; for (int y = 110; y < 130; y++) for (int x = 270; x < 320; x++) before += s[y * 640 + x]; }
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 255); wr(&rt, SP + 2, 4, ok); CHECK(mac_controls_service(&rt, 0xa95d) == 1 && a7(&rt) == SP + 6);                 /* dim OK */
        { const uint8_t *s = mac_cpu_ram(rt.cpu) + rt.screen_base; unsigned dim = 0; for (int y = 110; y < 130; y++) for (int x = 270; x < 320; x++) dim += s[y * 640 + x]; CHECK(dim != before); }   /* the OK button is drawn dimmed */
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 0); wr(&rt, SP + 2, 4, rad); CHECK(mac_controls_service(&rt, 0xa95d) == 1);                                        /* active again */
        mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, ok); CHECK(mac_controls_service(&rt, 0xa957) == -1 && mac_cpu_halted(rt.cpu));                                 /* ShowControl: not supported */
        mac_cpu_destroy(rt.cpu); }
    {   MacRuntime rt = fresh(); CHECK(get_new(&rt, 304) == 1); uint32_t d = raw(&rt, SP + 10, 4), type, ok;                    /* a value change on a static or a button is refused */
        CHECK(get_ditem(&rt, d, 1, &type, &ok) == 1); mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 2, 1); wr(&rt, SP + 2, 4, ok);
        CHECK(mac_controls_service(&rt, 0xa963) == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); }
    /* Refusals: a missing CNTL, an item that names none, a CDEF the program lacks. Each halts and registers no window. */
    for (unsigned k = 0; k < 3; k++) {
        MacRuntime rt = fresh(); int rc = get_new(&rt, k == 0 ? 302 : k == 1 ? 303 : 305);
        CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu);
    }
    if (!fails) puts("Dialog control item checks passed");
    return fails ? 1 : 0;
}
