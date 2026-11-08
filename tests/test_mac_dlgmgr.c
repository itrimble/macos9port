/* Alert / StopAlert / NoteAlert / CautionAlert: the native modal alert, driven through the real trap path. */
#include "mac_dialog_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { BACK = 77 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

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
static void fixture_fork(Fork *f) {
    uint8_t zero[32] = {0}, code[10] = {0, 0, 0, 1, 0xa9, 0xf4};
    put32(zero, 0x100); put32(zero + 4, 0x100); put32(zero + 8, 8); put32(zero + 12, 32);
    put16(zero + 16, 0); put16(zero + 18, 0x3f3c); put16(zero + 20, 1); put16(zero + 22, 0xa9f0);
    add(f, 0x434f4445, 0, zero, 32); add(f, 0x434f4445, 1, code, 10);
    W w = {0};
    /* DITL 128: OK, Cancel, static text with ParamText, a check box, two edit texts, an icon. */
    word(&w, 6); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 70, 150, 90, 200, 4, "Cancel", 6);
    item(&w, 10, 70, 40, 270, 8, "Hi ^0 and ^1", 12); item(&w, 45, 20, 60, 120, 5, "Loud", 4);
    item(&w, 45, 130, 60, 200, 16, "abc", 3); item(&w, 45, 210, 60, 280, 16, "", 0); item(&w, 10, 10, 42, 42, 32, "\x01\x2c", 2);
    add(f, 0x4449544c, 128, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4 | 0x80, "OK", 2); item(&w, 10, 10, 60, 270, 8, "Disabled default", 16);  /* item 1 disabled */
    add(f, 0x4449544c, 129, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 10, 10, 60, 270, 0, "", 0);        /* a user item */
    add(f, 0x4449544c, 130, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 10, 10, 60, 270, 7, "\0\1", 2);   /* a control item */
    add(f, 0x4449544c, 131, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 0); item(&w, 70, 220, 90, 270, 4, "OK", 2);
    add(f, 0x4449544c, 132, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 3); item(&w, 70, 220, 90, 270, 4, "OK", 2);                                              /* truncated */
    add(f, 0x4449544c, 133, w.b, w.n);
    add(f, 0x49434f4e, 300, (const uint8_t[128]){ [0] = 0x80, [3] = 0x01 }, 128);
    dlog(f, 128, 40, 50, 140, 330, 1, 128);   /* ordinary */
    dlog(f, 129, 40, 50, 140, 330, 1, 129);   /* disabled item 1 */
    dlog(f, 130, 40, 50, 140, 330, 1, 130);   /* user item: halts */
    dlog(f, 131, 40, 50, 140, 330, 1, 131);   /* control item: halts */
    dlog(f, 132, 40, 50, 140, 330, 0, 132);   /* hidden: halts */
    dlog(f, 133, 40, 50, 140, 330, 1, 999);   /* DITL missing */
    dlog(f, 134, 40, 50, 140, 330, 1, 133);   /* malformed DITL */
    dlog(f, 135, 420, 560, 520, 700, 1, 128); /* hangs off the screen: moved on */
    dlog(f, 136, 40, 50, 41, 51, 1, 132);     /* far too small */
    dlog(f, 137, 40, 50, 140, 330, 1, 132);   /* one button */
    dlog(f, 138, -30, -20, 70, 260, 1, 132);  /* hangs off the top-left: moved on */
    dlog(f, 139, 40, 50, 140, 330, 0, 128);   /* hidden until ShowWindow */
}
typedef struct { MacRuntime rt; Fork fork; uint8_t image[16384]; } Rig;
static uint32_t raw(const MacRuntime *rt, uint32_t p, unsigned n) { const uint8_t *ram = mac_cpu_ram(rt->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | ram[p + i]; return v; }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static int rig_open(Rig *g) {
    char err[240]; MacResourceFork fork; memset(g, 0, sizeof *g);
    fixture_fork(&g->fork); size_t n = build(&g->fork, g->image);
    if (mac_resource_open(&fork, g->image, n, err, sizeof err) || mac_runtime_init(&g->rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); CHECK(0); return -1; }
    MacRuntime *rt = &g->rt;
    rt->screen_depth = 8; rt->screen_width = 640; rt->screen_height = 480; rt->screen_row_bytes = 640; rt->screen_base = 0xe00000;
    rt->palette[0] = 0xffffffffu; rt->palette[1] = 0xff000000u;
    memset(mac_cpu_ram(rt->cpu) + rt->screen_base, BACK, 640 * 480);
    memcpy(rt->dialog_params[0], "\003Bob", 4); memcpy(rt->dialog_params[1], "\002Al", 3);
    return 0;
}
static int screen_all_back(Rig *g) { const uint8_t *s = mac_cpu_ram(g->rt.cpu) + g->rt.screen_base; for (unsigned i = 0; i < 640 * 480; i++) if (s[i] != BACK) return 0; return 1; }
#define SP 0x30000u
#define TRAP_PC 0x10002u
static int trap(Rig *g, unsigned op, uint32_t sp) { mac_cpu_set(g->rt.cpu, MAC_A7, sp); mac_cpu_set(g->rt.cpu, MAC_PC, TRAP_PC); return mac_dlgmgr_service(&g->rt, (uint16_t)op); }
/* GetNewDialog(id, storage, behind) -> DialogPtr, returned (0 on NIL). */
static uint32_t get_new(Rig *g, int id, uint32_t storage, uint32_t behind, int *rc) {
    wr(&g->rt, SP, 4, behind); wr(&g->rt, SP + 4, 4, storage); wr(&g->rt, SP + 8, 2, (uint16_t)id); wr(&g->rt, SP + 10, 4, 0xdead);
    *rc = trap(g, 0xa97c, SP); return *rc == 1 ? raw(&g->rt, SP + 10, 4) : 0;
}
static int modal(Rig *g, uint32_t filter, uint32_t hit) { wr(&g->rt, SP, 4, hit); wr(&g->rt, SP + 4, 4, filter); return trap(g, 0xa991, SP); }
static void post(Rig *g, unsigned what, unsigned msg, unsigned mods, int x, int y) { mac_runtime_post_event(&g->rt, (uint16_t)what, msg, (uint16_t)mods, (int16_t)x, (int16_t)y); }
static void run_modal(Rig *g, uint32_t hit) { modal(g, 0, hit); }
/* Dialog at (40, 50), 280 x 100: button 1 (OK) at x 270..320, y 110..130; Cancel x 200..250; check x 70..170, y 85..100. */
#define OK_X 295, 120
#define CANCEL_X 225, 120
#define CHECK_X 100, 92

static void creating(void) {
    Rig g; int rc;
    CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    CHECK(rc == 1 && d && !(d & 1) && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 10 && raw(&g.rt, d + 108, 2) == 2 && raw(&g.rt, d + 110, 1) == 1);
    CHECK(!screen_all_back(&g));                                                   /* drawn */
    uint32_t s = raw(&g.rt, d + 114, 4), c = raw(&g.rt, d + 118, 4), u = raw(&g.rt, d + 122, 4);
    CHECK(s && c && u && s != c && c != u);
    {uint32_t p = raw(&g.rt, c, 4); CHECK(raw(&g.rt, p, 2) == 10 && raw(&g.rt, p + 2, 2) == 40 && raw(&g.rt, p + 4, 2) == 50 && raw(&g.rt, p + 6, 2) == 140 && raw(&g.rt, p + 8, 2) == 330);}
    CHECK(raw(&g.rt, d + 20, 2) == 100 && raw(&g.rt, d + 22, 2) == 280);          /* portRect, local */
    /* DisposeDialog restores the screen, frees the regions, and a second dispose names a dialog that is not open */
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa983, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4 && screen_all_back(&g));
    {unsigned live = 0; for (size_t i = 0; i < g.rt.handle_count; i++) live += g.rt.handles[i].handle == s || g.rt.handles[i].handle == c || g.rt.handles[i].handle == u; CHECK(live == 0);}
    CHECK(trap(&g, 0xa983, SP) == -1 && mac_cpu_halted(g.rt.cpu));
    mac_runtime_destroy(&g.rt);
    /* dStorage is used as the record when given; NIL for a missing DLOG; moved onto the screen when it hangs off */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0x40000, 0, &rc); CHECK(rc == 1 && d == 0x40000 && raw(&g.rt, d + 108, 2) == 2); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 999, 0, 0xffffffffu, &rc); CHECK(rc == 1 && d == 0 && screen_all_back(&g)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 135, 0, 0xffffffffu, &rc); CHECK(rc == 1 && d); {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; CHECK(sc[479 * 640 + 600] != BACK && sc[400 * 640 + 639] != BACK);} mac_runtime_destroy(&g.rt);
    /* a dialog whose item list has a user item (type 0) opens; the item's draw procedure is never called, so it stays blank, and the dialog disposes cleanly */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 130, 0, 0xffffffffu, &rc); CHECK(rc == 1 && d != 0 && !screen_all_back(&g));
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa983, SP) == 1 && screen_all_back(&g)); mac_runtime_destroy(&g.rt);
    /* refusals: nothing is allocated and the guest halts */
    static const struct { int id; uint32_t storage, behind; } bad[] = { {131,0,0xffffffffu}, {133,0,0xffffffffu}, {134,0,0xffffffffu}, {136,0,0xffffffffu},
        {128,0,0x50000}, {128,0x40001,0}, {128,0xfffffff0u,0} };
    for (unsigned k = 0; k < sizeof bad / sizeof *bad; k++) {
        CHECK(rig_open(&g) == 0); d = get_new(&g, bad[k].id, bad[k].storage, bad[k].behind, &rc);
        if (!(rc == -1 && mac_cpu_halted(g.rt.cpu) && screen_all_back(&g))) fprintf(stderr, "creation refusal %u: rc=%d\n", k, rc);
        CHECK(rc == -1 && mac_cpu_halted(g.rt.cpu) && screen_all_back(&g)); mac_runtime_destroy(&g.rt);
    }
    CHECK(rig_open(&g) == 0); g.rt.screen_depth = 1; get_new(&g, 128, 0, 0, &rc); CHECK(rc == -1); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); mac_cpu_set(g.rt.cpu, MAC_A7, SP + 1); CHECK(mac_dlgmgr_service(&g.rt, 0xa97c) == -1); mac_runtime_destroy(&g.rt);
    /* five dialogs: the fifth is refused */
    CHECK(rig_open(&g) == 0); for (unsigned i = 0; i < 4; i++) { d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(rc == 1 && d); } get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(rc == -1); mac_runtime_destroy(&g.rt);
}
static void items(void) {
    Rig g; int rc; CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 128, 0, 0xffffffffu, &rc), type_p = 0x31000, handle_p = 0x31010, box_p = 0x31020, str = 0x31100;
    #define GETD(item) (wr(&g.rt, SP, 4, box_p), wr(&g.rt, SP + 4, 4, handle_p), wr(&g.rt, SP + 8, 4, type_p), wr(&g.rt, SP + 12, 2, (item)), wr(&g.rt, SP + 14, 4, d), trap(&g, 0xa98d, SP))
    CHECK(GETD(1) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 18 && raw(&g.rt, type_p, 2) == 4 && raw(&g.rt, box_p, 2) == 70 && raw(&g.rt, box_p + 2, 2) == 220 && raw(&g.rt, box_p + 4, 2) == 90 && raw(&g.rt, box_p + 6, 2) == 270);
    CHECK(GETD(4) == 1 && raw(&g.rt, type_p, 2) == 5);                              /* check box */
    CHECK(GETD(5) == 1 && raw(&g.rt, type_p, 2) == 16);
    CHECK(GETD(5) == 1); uint32_t h = raw(&g.rt, handle_p, 4); CHECK(h && raw(&g.rt, h, 4) && !memcmp(mac_cpu_ram(g.rt.cpu) + raw(&g.rt, h, 4), "abc", 4));  /* the handle's block mirrors the text */
    #define GETT(hh) (wr(&g.rt, SP, 4, str), wr(&g.rt, SP + 4, 4, (hh)), trap(&g, 0xa990, SP))
    #define SETT(hh) (wr(&g.rt, SP, 4, str), wr(&g.rt, SP + 4, 4, (hh)), trap(&g, 0xa98f, SP))
    CHECK(GETT(h) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 8 && raw(&g.rt, str, 1) == 3 && !memcmp(mac_cpu_ram(g.rt.cpu) + str + 1, "abc", 3));
    memcpy(mac_cpu_ram(g.rt.cpu) + str, "\006Zebra!", 7); CHECK(SETT(h) == 1);
    CHECK(GETT(h) == 1 && raw(&g.rt, str, 1) == 6 && !memcmp(mac_cpu_ram(g.rt.cpu) + str + 1, "Zebra!", 6) && !memcmp(mac_cpu_ram(g.rt.cpu) + raw(&g.rt, h, 4), "Zebra!", 7));
    mac_cpu_ram(g.rt.cpu)[str] = 255; memset(mac_cpu_ram(g.rt.cpu) + str + 1, 'x', 255); CHECK(SETT(h) == 1 && GETT(h) == 1 && raw(&g.rt, str, 1) == 255);   /* the longest text */
    mac_runtime_destroy(&g.rt);
    /* the guest repoints an item handle's master pointer: SetIText still writes the host's own block and repairs the master pointer */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    wr(&g.rt, SP, 4, box_p); wr(&g.rt, SP + 4, 4, handle_p); wr(&g.rt, SP + 8, 4, type_p); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
    {uint32_t hh = raw(&g.rt, handle_p, 4), good = raw(&g.rt, hh, 4); memset(mac_cpu_ram(g.rt.cpu) + 0x32000, 0x55, 300); wr(&g.rt, hh, 4, 0x32000);
     memcpy(mac_cpu_ram(g.rt.cpu) + str, "\002ok", 3); SETT(hh);
     CHECK(raw(&g.rt, 0x32000, 4) == 0x55555555 && raw(&g.rt, 0x32000 + 255, 1) == 0x55 && raw(&g.rt, hh, 4) == good && !memcmp(mac_cpu_ram(g.rt.cpu) + good, "ok", 3));}
    mac_runtime_destroy(&g.rt);
    /* SelIText */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    wr(&g.rt, SP, 2, 3); wr(&g.rt, SP + 2, 2, 0); wr(&g.rt, SP + 4, 2, 6); wr(&g.rt, SP + 6, 4, d); CHECK(trap(&g, 0xa97e, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 10);
    wr(&g.rt, SP + 4, 2, 1); wr(&g.rt, SP + 6, 4, d); CHECK(trap(&g, 0xa97e, SP) == -1 && mac_cpu_halted(g.rt.cpu));   /* item 1 is a button */
    mac_runtime_destroy(&g.rt);
}
static void disabled_and_clamped(void) {
    Rig g; int rc; CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 129, 0, 0xffffffffu, &rc), type_p = 0x31000;
    wr(&g.rt, SP, 4, 0x31020); wr(&g.rt, SP + 4, 4, 0x31010); wr(&g.rt, SP + 8, 4, type_p); wr(&g.rt, SP + 12, 2, 1); wr(&g.rt, SP + 14, 4, d);
    CHECK(trap(&g, 0xa98d, SP) == 1 && raw(&g.rt, type_p, 2) == (4 | 128));                    /* a disabled button reports bit 7 */
    mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); get_new(&g, 138, 0, 0xffffffffu, &rc); CHECK(rc == 1);
    {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; CHECK(sc[50 * 640 + 0] == 1 && sc[0 * 640 + 100] == 1);}   /* the border sits on the screen's edge */
    mac_runtime_destroy(&g.rt);
}
static void hidden_moved_and_items(void) {
    Rig g; int rc; CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 139, 0, 0xffffffffu, &rc), ev = 0x31000;
    CHECK(rc == 1 && d && screen_all_back(&g) && raw(&g.rt, d + 110, 1) == 0);                          /* hidden: nothing drawn */
    wr(&g.rt, SP, 4, 0xdead); CHECK(trap(&g, 0xa924, SP) == 0);                                            /* FrontWindow ignores it */
    wr(&g.rt, ev, 2, 1); wr(&g.rt, SP, 4, ev); CHECK(trap(&g, 0xa97f, SP) == 1 && raw(&g.rt, SP + 4, 1) == 0);   /* and so does IsDialogEvent */
    wr(&g.rt, SP, 4, d); wr(&g.rt, 0x31200, 4, 0); CHECK(trap(&g, 0xa981, SP) == 1 && screen_all_back(&g));      /* DrawDialog draws nothing while hidden */
    wr(&g.rt, SP, 4, 0x31500); CHECK(trap(&g, 0xa915, SP) == 0 && trap(&g, 0xa916, SP) == 0 && trap(&g, 0xa91b, SP) == 0 && trap(&g, 0xa91f, SP) == 0);   /* other windows are not ours */
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa915, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4 && !screen_all_back(&g) && raw(&g.rt, d + 110, 1) == 1);   /* ShowWindow */
    wr(&g.rt, SP, 4, 0xdead); CHECK(trap(&g, 0xa924, SP) == 1 && raw(&g.rt, SP, 4) == d);
    /* MoveWindow(window, h, v, front): content to (h, v); the old place is restored, the regions follow, off-screen targets are brought on */
    wr(&g.rt, SP, 2, 0); wr(&g.rt, SP + 2, 2, 200); wr(&g.rt, SP + 4, 2, 300); wr(&g.rt, SP + 6, 4, d);
    CHECK(trap(&g, 0xa91b, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 10);
    {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; CHECK(sc[40 * 640 + 100] == BACK && sc[200 * 640 + 310] != BACK);
     uint32_t cr = raw(&g.rt, raw(&g.rt, d + 118, 4), 4); CHECK(raw(&g.rt, cr + 2, 2) == 200 && raw(&g.rt, cr + 4, 2) == 300 && raw(&g.rt, cr + 6, 2) == 300 && raw(&g.rt, cr + 8, 2) == 580);}
    wr(&g.rt, SP, 2, 0); wr(&g.rt, SP + 2, 2, 1000); wr(&g.rt, SP + 4, 2, 1000); wr(&g.rt, SP + 6, 4, d); CHECK(trap(&g, 0xa91b, SP) == 1);
    {uint32_t cr = raw(&g.rt, raw(&g.rt, d + 118, 4), 4); CHECK(raw(&g.rt, cr + 2, 2) == 380 && raw(&g.rt, cr + 4, 2) == 360);}
    wr(&g.rt, SP, 2, 0); wr(&g.rt, SP + 2, 2, 0xffff); wr(&g.rt, SP + 4, 2, 0xffff); wr(&g.rt, SP + 6, 4, d); CHECK(trap(&g, 0xa91b, SP) == 1);
    {uint32_t cr = raw(&g.rt, raw(&g.rt, d + 118, 4), 4); CHECK(raw(&g.rt, cr + 2, 2) == 0 && raw(&g.rt, cr + 4, 2) == 0);}
    /* SelectWindow, then HideWindow puts the screen back and ShowWindow brings it again */
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa91f, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4);
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa916, SP) == 1 && screen_all_back(&g) && raw(&g.rt, d + 110, 1) == 0);
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa916, SP) == 1 && screen_all_back(&g));                           /* hiding twice is harmless */
    mac_cpu_ram(g.rt.cpu)[g.rt.screen_base + 60 * 640 + 60] = 99;                                              /* the screen changes while it is hidden... */
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa915, SP) == 1 && !screen_all_back(&g));
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa983, SP) == 1 && mac_cpu_ram(g.rt.cpu)[g.rt.screen_base + 60 * 640 + 60] == 99);   /* ...and disposal restores that, not the old pixels */
    mac_cpu_ram(g.rt.cpu)[g.rt.screen_base + 60 * 640 + 60] = BACK; CHECK(screen_all_back(&g));
    mac_runtime_destroy(&g.rt);
    /* HideDItem / ShowDItem(dialog, item): not drawn, not hit, box 16384 to the right; the edit selection moves on */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); uint32_t hit = 0x31000, bx = 0x31040;
    wr(&g.rt, SP, 2, 1); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xa827, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 6);
    {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; unsigned ink = 0; for (int y = 112; y < 128; y++) for (int x = 272; x < 318; x++) ink += sc[y * 640 + x] == 1; CHECK(ink == 0);}
    wr(&g.rt, hit, 2, 0xffff); post(&g, 1, 0, 0, OK_X); post(&g, 2, 0, 0, OK_X); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);
    wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, 0x31010); wr(&g.rt, SP + 8, 4, 0x31020); wr(&g.rt, SP + 12, 2, 1); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
    CHECK(raw(&g.rt, bx + 2, 2) == 220 + 16384 && raw(&g.rt, bx + 6, 2) == 270 + 16384);
    wr(&g.rt, SP, 2, 1); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xa828, SP) == 1);
    wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, 0x31010); wr(&g.rt, SP + 8, 4, 0x31020); wr(&g.rt, SP + 12, 2, 1); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
    CHECK(raw(&g.rt, bx + 2, 2) == 220);
    {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; unsigned ink = 0; for (int y = 112; y < 128; y++) for (int x = 272; x < 318; x++) ink += sc[y * 640 + x] == 1; CHECK(ink > 10);}
    mac_runtime_destroy(&g.rt);
    /* hiding the selected edit item deselects it: typing then goes nowhere, and showing it again restores the selection */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    wr(&g.rt, SP, 2, 5); wr(&g.rt, SP + 2, 4, d); trap(&g, 0xa827, SP);
    post(&g, 3, 'Z', 0, 0, 0); modal(&g, 0, hit);
    wr(&g.rt, SP, 2, 5); wr(&g.rt, SP + 2, 4, d); trap(&g, 0xa828, SP);
    {uint32_t ty = 0x31020, hp = 0x31030, str = 0x31100; wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
     wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, raw(&g.rt, hp, 4)); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 3 && !memcmp(mac_cpu_ram(g.rt.cpu) + str + 1, "abc", 3));
     post(&g, 3, 'Y', 0, 0, 0); modal(&g, 0, hit); wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
     wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, raw(&g.rt, hp, 4)); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 4);}   /* selection back: typing works */
    mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    wr(&g.rt, SP, 2, 99); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xa827, SP) == -1 && mac_cpu_halted(g.rt.cpu));
    mac_runtime_destroy(&g.rt);
    /* a moved/shown/hidden/selected dialog that is busy in ModalDialog halts */
    for (unsigned k = 0; k < 2; k++) {
        CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); modal(&g, 0, hit);
        wr(&g.rt, SP + 40, 2, 0); wr(&g.rt, SP + 42, 2, 5); wr(&g.rt, SP + 44, 2, 5); wr(&g.rt, SP + 46, 4, d);
        if (k == 0) CHECK(trap(&g, 0xa91b, SP + 40) == -1 && mac_cpu_halted(g.rt.cpu)); else { wr(&g.rt, SP + 40, 4, d); CHECK(trap(&g, 0xa916, SP + 40) == -1 && mac_cpu_halted(g.rt.cpu)); }
        mac_runtime_destroy(&g.rt);
    }
    /* drawing with a dialog as the port: decoration is swallowed (arguments popped, nothing drawn), anything else halts, another SetPort ends it */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa873, SP) == 1);
    {static const struct { unsigned op, args; } sw[] = { {0xa8b0,8},{0xa8b1,8},{0xa8b2,8},{0xa8b3,8},{0xa8a1,4},{0xa8a2,4},{0xa8a3,4},{0xa8a4,4},{0xa8a5,8},{0xa8b7,4},{0xa8b8,4},{0xa8b9,4},{0xa8ba,4},
        {0xa884,4},{0xa883,2},{0xa8f6,8},{0xa89b,4},{0xa89c,2},{0xa89d,4},{0xa89e,0},{0xa893,4},{0xa894,4},{0xa891,4},{0xa892,4},{0xa862,4},{0xa863,4},{0xa87c,4},{0xa864,2},{0xa896,0},{0xa897,0},{0xa887,2},{0xa888,2},{0xa889,2},{0xa88a,2} };
     for (unsigned i = 0; i < sizeof sw / sizeof *sw; i++) { mac_cpu_set(g.rt.cpu, MAC_A7, SP); CHECK(mac_dlgmgr_service(&g.rt, (uint16_t)sw[i].op) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + sw[i].args); }}
    CHECK(trap(&g, 0xa8a9, SP) == 0);                                                                                 /* InsetRect is not drawing: not ours */
    wr(&g.rt, SP, 4, 0x31500); CHECK(trap(&g, 0xa873, SP) == 0);                                                      /* another SetPort ends it */
    mac_cpu_set(g.rt.cpu, MAC_A7, SP); CHECK(mac_dlgmgr_service(&g.rt, 0xa8b0) == 0);
    mac_runtime_destroy(&g.rt);
    for (unsigned k = 0; k < 8; k++) {
        static const unsigned unsup[] = { 0xa8ec, 0xa8ef, 0xa885, 0xa8c6, 0xa8d2, 0xa8be, 0xa8bb, 0xa8b4 };
        CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); wr(&g.rt, SP, 4, d); trap(&g, 0xa873, SP);
        mac_cpu_set(g.rt.cpu, MAC_A7, SP); CHECK(mac_dlgmgr_service(&g.rt, (uint16_t)unsup[k]) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    }
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); wr(&g.rt, SP, 4, d); trap(&g, 0xa873, SP);
    wr(&g.rt, SP, 4, d); trap(&g, 0xa983, SP); mac_cpu_set(g.rt.cpu, MAC_A7, SP); CHECK(mac_dlgmgr_service(&g.rt, 0xa8f6) == 0 && !mac_cpu_halted(g.rt.cpu));   /* disposing the dialog ends it too */
    mac_runtime_destroy(&g.rt);
}
static void refusing_items(void) {
    for (unsigned k = 0; k < 9; k++) {
        Rig g; int rc; CHECK(rig_open(&g) == 0); uint32_t d = get_new(&g, 128, 0, 0xffffffffu, &rc), str = 0x31100, forged = 0x31200; int r;
        memcpy(mac_cpu_ram(g.rt.cpu) + str, "\001x", 2); wr(&g.rt, forged, 4, 0x31300); wr(&g.rt, 0x31300, 4, 0x31400);
        if (k == 0) { wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, forged); r = trap(&g, 0xa98f, SP); }                  /* forged item handle */
        else if (k == 1) { wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, 0); r = trap(&g, 0xa990, SP); }
        else if (k == 2) { wr(&g.rt, SP, 4, 0); wr(&g.rt, SP + 4, 4, forged); r = trap(&g, 0xa98f, SP); }
        else if (k == 3) { wr(&g.rt, SP, 4, 0x1000000); wr(&g.rt, SP + 4, 4, forged); r = trap(&g, 0xa98f, SP); }
        else if (k == 4) { wr(&g.rt, SP, 4, str + 0); wr(&g.rt, SP + 4, 4, forged); mac_cpu_set(g.rt.cpu, MAC_A7, 0xffffff0); r = mac_dlgmgr_service(&g.rt, 0xa98f); }
        else if (k == 5) { wr(&g.rt, SP, 4, 0x31000); wr(&g.rt, SP + 4, 4, 0x31000); wr(&g.rt, SP + 8, 4, 0x31000); wr(&g.rt, SP + 12, 2, 99); wr(&g.rt, SP + 14, 4, d); r = trap(&g, 0xa98d, SP); }   /* item 99 */
        else if (k == 6) { wr(&g.rt, SP, 4, 0x31000); wr(&g.rt, SP + 4, 4, 0x31000); wr(&g.rt, SP + 8, 4, 0x31000); wr(&g.rt, SP + 12, 2, 1); wr(&g.rt, SP + 14, 4, d + 2); r = trap(&g, 0xa98d, SP); }   /* a forged DialogPtr */
        else if (k == 7) { wr(&g.rt, SP, 4, 0x1000000); wr(&g.rt, SP + 4, 4, 0x31000); wr(&g.rt, SP + 8, 4, 0x31000); wr(&g.rt, SP + 12, 2, 1); wr(&g.rt, SP + 14, 4, d); r = trap(&g, 0xa98d, SP); }  /* box pointer off RAM */
        else { mac_cpu_ram(g.rt.cpu)[str] = 255; wr(&g.rt, SP, 4, 0xfffffff0u - 1 + 1); wr(&g.rt, SP + 4, 4, 0x31000); r = trap(&g, 0xa98f, SP); }
        if (!(r == -1 && mac_cpu_halted(g.rt.cpu))) fprintf(stderr, "item refusal %u rc=%d\n", k, r);
        CHECK(r == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    }
}
static void modal_loop(void) {
    Rig g; int rc; CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 128, 0, 0xffffffffu, &rc), hit = 0x31000; (void)d;
    wr(&g.rt, hit, 2, 0xffff);
    run_modal(&g, hit); CHECK(mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP && raw(&g.rt, hit, 2) == 0xffff);     /* waits */
    post(&g, 1, 0, 0, OK_X); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 0xffff);                                                                   /* pressed, not yet released */
    post(&g, 2, 0, 0, 10, 10); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);                      /* released elsewhere: nothing */
    post(&g, 1, 0, 0, CANCEL_X); post(&g, 2, 0, 0, CANCEL_X); modal(&g, 0, hit);
    CHECK(raw(&g.rt, hit, 2) == 2 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 8 && g.rt.dlgmgr);                                                           /* Cancel hit; the dialog stays open */
    CHECK(!screen_all_back(&g));
    /* check box toggles on each hit; static text and the icon are not hit */
    {const uint8_t *sc = mac_cpu_ram(g.rt.cpu) + g.rt.screen_base; CHECK(sc[90 * 640 + 74] == 0);
     post(&g, 1, 0, 0, CHECK_X); post(&g, 2, 0, 0, CHECK_X); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 4 && sc[90 * 640 + 74] == 1);       /* marked after one hit */
     post(&g, 1, 0, 0, CHECK_X); post(&g, 2, 0, 0, CHECK_X); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 4 && sc[90 * 640 + 74] == 0);       /* cleared by the next */
     post(&g, 1, 0, 0, CHECK_X); post(&g, 2, 0, 0, CHECK_X); modal(&g, 0, hit);}
    post(&g, 1, 0, 0, 150, 30); post(&g, 2, 0, 0, 150, 30); modal(&g, 0, hit); CHECK(mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);
    /* Return and Enter hit the default button; keys edit the selected edit text; Tab moves; Backspace deletes */
    post(&g, 3, 13, 0, 0, 0); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 1);
    wr(&g.rt, hit, 2, 0xffff); post(&g, 3, 3, 0, 0, 0); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 1);
    wr(&g.rt, hit, 2, 0xffff);
    post(&g, 3, 'X', 0, 0, 0); post(&g, 3, 'Y', 0, 0, 0); post(&g, 3, 8, 0, 0, 0); post(&g, 3, 'Z', 0x100, 0, 0); post(&g, 3, 9, 0, 0, 0); post(&g, 3, 'Q', 0, 0, 0); modal(&g, 0, hit);
    CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);
    {   /* read the texts back through GetIText: item 5 = "abcX", item 6 = "Q" */
        uint32_t h5, h6, ty = 0x31020, hp = 0x31030, bx = 0x31040, str = 0x31100;
        wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP); h5 = raw(&g.rt, hp, 4);
        wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 6); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP); h6 = raw(&g.rt, hp, 4);
        wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, h5); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 4 && !memcmp(mac_cpu_ram(g.rt.cpu) + str + 1, "abcX", 4));
        wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, h6); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 1 && mac_cpu_ram(g.rt.cpu)[str + 1] == 'Q');
        wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, h5); trap(&g, 0xa990, SP);
    }
    post(&g, 3, 13, 0, 0, 0); modal(&g, 0, hit);                                         /* finish the ModalDialog that is still waiting */
    {   /* edit text limits: Backspace on empty text, typing past 255 characters, auto-key like key down, Esc with a cancel item */
        uint32_t ty = 0x31020, hp = 0x31030, bx = 0x31040, str = 0x31100;
        post(&g, 3, 9, 0, 0, 0); post(&g, 3, 8, 0, 0, 0); post(&g, 3, 8, 0, 0, 0);               /* Tab to item 5, then Backspace over 'abcX' and past it */
        for (unsigned i = 0; i < 6; i++) post(&g, 3, 8, 0, 0, 0);
        modal(&g, 0, hit);
        wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
        wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, raw(&g.rt, hp, 4)); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 0);
        modal(&g, 0, hit);
        for (unsigned chunk = 0; chunk < 6; chunk++) { for (unsigned i = 0; i < 50; i++) post(&g, 5, 'a' + i % 20, 0, 0, 0); modal(&g, 0, hit); }   /* 300 auto-keys, 50 at a time */
        wr(&g.rt, SP, 4, bx); wr(&g.rt, SP + 4, 4, hp); wr(&g.rt, SP + 8, 4, ty); wr(&g.rt, SP + 12, 2, 5); wr(&g.rt, SP + 14, 4, d); trap(&g, 0xa98d, SP);
        wr(&g.rt, SP, 4, str); wr(&g.rt, SP + 4, 4, raw(&g.rt, hp, 4)); trap(&g, 0xa990, SP); CHECK(raw(&g.rt, str, 1) == 255);
        wr(&g.rt, hit, 2, 0xffff); mac_cpu_set(g.rt.cpu, MAC_D0, 0x0305); wr(&g.rt, SP, 2, 2); wr(&g.rt, SP + 2, 4, d); trap(&g, 0xaa68, SP);
        wr(&g.rt, SP + 100, 4, hit); wr(&g.rt, SP + 104, 4, 0); post(&g, 3, 27, 0, 0, 0); mac_cpu_set(g.rt.cpu, MAC_A7, SP); mac_cpu_set(g.rt.cpu, MAC_PC, TRAP_PC);
        wr(&g.rt, SP, 4, hit); wr(&g.rt, SP + 4, 4, 0); mac_dlgmgr_service(&g.rt, 0xa991);
        CHECK(raw(&g.rt, hit, 2) == 2);
        post(&g, 3, 13, 0, 0, 0); modal(&g, 0, hit);
    }
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa983, SP) == 1 && screen_all_back(&g));
    mac_runtime_destroy(&g.rt);
    /* a disabled item 1: Return does nothing, and clicking it does nothing; Escape does nothing until a cancel item is set */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 129, 0, 0xffffffffu, &rc); wr(&g.rt, hit, 2, 0xffff);
    post(&g, 3, 13, 0, 0, 0); post(&g, 3, 27, 0, 0, 0); post(&g, 1, 0, 0, OK_X); post(&g, 2, 0, 0, OK_X); modal(&g, 0, hit); CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);
    mac_runtime_destroy(&g.rt);
    /* a guest filter procedure: called with (dialog, event, itemHit) below the stack; TRUE hands its itemHit back, FALSE lets the dialog handle the event */
    {enum { FILT = 0x20000 };
     for (unsigned mode = 0; mode < 4; mode++) {
        CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); wr(&g.rt, hit, 2, 0xffff);
        if (mode == 1) { post(&g, 1, 0, 0, OK_X); post(&g, 2, 0, 0, OK_X); } else if (mode != 3) post(&g, 3, 0x41, 0, 0, 0);
        for (unsigned pass = 0; pass < (mode == 1 ? 2u : 1u); pass++) {
            if (pass) { mac_cpu_set(g.rt.cpu, MAC_A7, SP); mac_cpu_set(g.rt.cpu, MAC_PC, TRAP_PC); CHECK(mac_dlgmgr_service(&g.rt, 0xa991) == 1); }
            else CHECK(modal(&g, FILT, hit) == 1);
            CHECK(g.rt.guest.active && mac_cpu_get(g.rt.cpu, MAC_PC) == FILT);                                        /* a guest call is under way: the CPU runs the filter */
            uint32_t a7 = mac_cpu_get(g.rt.cpu, MAC_A7), ev = raw(&g.rt, a7 + 8, 4), ip = raw(&g.rt, a7 + 12, 4);
            CHECK(raw(&g.rt, a7 + 4, 4) == d && ev == ip - 18 && ev < SP - 40 && a7 < ev && (ev & 1) == 0);         /* Pascal argument order; scratch above the callee's frame */
            unsigned what = raw(&g.rt, ev, 2);
            CHECK(what == (mode == 3 ? 0u : mode == 1 ? (pass ? 2u : 1u) : 3u) && raw(&g.rt, ip, 2) == 0);           /* the event offered (a null event when idle) */
            if (mode == 0) wr(&g.rt, ip, 2, 4);                                                                      /* the filter stores itemHit 4 (the check box) ... */
            mac_cpu_set(g.rt.cpu, MAC_A7, g.rt.guest.sp - 2);                                                        /* ... and returns into the stub with its result */
            wr(&g.rt, g.rt.guest.sp - 2, 2, mode == 0 ? 0x0100 : 0x0000);                                            /* TRUE in mode 0, FALSE otherwise */
            CHECK(mac_guest_return(&g.rt) == 0 && !mac_cpu_halted(g.rt.cpu));
            if (mode == 1 && pass == 0) CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP);   /* a press: nothing chosen yet, trap re-runs */
        }
        if (mode == 0) CHECK(raw(&g.rt, hit, 2) == 4 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 8 && mac_cpu_get(g.rt.cpu, MAC_PC) == TRAP_PC && mac_cpu_ram(g.rt.cpu)[g.rt.screen_base + 90 * 640 + 74] == 0);   /* TRUE: the filter's answer, ModalDialog done, and the check box not toggled behind its back */
        if (mode == 1) CHECK(raw(&g.rt, hit, 2) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 8 && mac_cpu_get(g.rt.cpu, MAC_PC) == TRAP_PC); /* FALSE: the dialog took the click on OK itself */
        if (mode >= 2) CHECK(raw(&g.rt, hit, 2) == 0xffff && mac_cpu_get(g.rt.cpu, MAC_A7) == SP && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000);   /* FALSE, nothing chosen: the trap runs again */
        mac_runtime_destroy(&g.rt);
     }}
    /* disposing the dialog while its filter runs is refused (so the completion's own "still open" check is only defence in depth) */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); post(&g, 3, 0x41, 0, 0, 0); CHECK(modal(&g, 0x20000, hit) == 1 && g.rt.guest.active);
    wr(&g.rt, SP + 200, 4, d); CHECK(trap(&g, 0xa983, SP + 200) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    /* a filter whose dialog disappears under it, a filter outside RAM, an odd filter and the null event (no input) are handled */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(modal(&g, 0x20001, hit) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(modal(&g, 0xfffffe, hit) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(modal(&g, 0x20000, hit) == 1 && raw(&g.rt, raw(&g.rt, mac_cpu_get(g.rt.cpu, MAC_A7) + 8, 4), 2) == 0); mac_runtime_destroy(&g.rt);   /* no events: a null event (what 0) */
    /* filters: the standard one is accepted, any other halts; ModalDialog with no dialog, a bad itemHit pointer and a changed stack halt */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc);
    CHECK(modal(&g, mac_native_stub(0xbf1), hit) == 1 && !mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(modal(&g, 0x12345, hit) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); CHECK(modal(&g, 0, hit) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); CHECK(modal(&g, 0, 0x1000000) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); modal(&g, 0, hit); wr(&g.rt, SP - 2, 4, hit); CHECK(trap(&g, 0xa991, SP - 4) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
    /* disposing a dialog while ModalDialog is running halts */
    CHECK(rig_open(&g) == 0); d = get_new(&g, 128, 0, 0xffffffffu, &rc); modal(&g, 0, hit); wr(&g.rt, SP + 40, 4, d); CHECK(trap(&g, 0xa983, SP + 40) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt);
}
static void event_loop_pieces(void) {
    Rig g; int rc; CHECK(rig_open(&g) == 0);
    uint32_t d = get_new(&g, 128, 0, 0xffffffffu, &rc), ev = 0x31000, dp = 0x31100, ip = 0x31110;
    /* FrontWindow answers the dialog; SetPort on it is accepted; DialogDispatch selectors */
    wr(&g.rt, SP, 4, 0xdead); CHECK(trap(&g, 0xa924, SP) == 1 && raw(&g.rt, SP, 4) == d);
    wr(&g.rt, SP, 4, d); CHECK(trap(&g, 0xa873, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4);
    wr(&g.rt, SP, 4, 0x31500); CHECK(trap(&g, 0xa873, SP) == 0);                                  /* another port: not ours */
    mac_cpu_set(g.rt.cpu, MAC_D0, 0x0203); wr(&g.rt, SP, 4, 0x31200); wr(&g.rt, SP + 4, 2, 0x7777);
    CHECK(trap(&g, 0xaa68, SP) == 1 && raw(&g.rt, 0x31200, 4) == mac_native_stub(0xbf1) && raw(&g.rt, SP + 4, 2) == 0 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4);
    mac_cpu_set(g.rt.cpu, MAC_D0, 0x0304); wr(&g.rt, SP, 2, 2); wr(&g.rt, SP + 2, 4, d); wr(&g.rt, SP + 6, 2, 0x7777);
    CHECK(trap(&g, 0xaa68, SP) == 1 && raw(&g.rt, SP + 6, 2) == 0 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 6);                   /* default item 2 */
    mac_cpu_set(g.rt.cpu, MAC_D0, 0x0305); wr(&g.rt, SP, 2, 2); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xaa68, SP) == 1 && raw(&g.rt, SP + 6, 2) == 0);   /* cancel item 2 */
    mac_cpu_set(g.rt.cpu, MAC_D0, 0x0304); wr(&g.rt, SP, 2, 3); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xaa68, SP) == 1 && raw(&g.rt, SP + 6, 2) == 0xffff);   /* item 3 is not a button */
    mac_cpu_set(g.rt.cpu, MAC_D0, 0x0306); wr(&g.rt, SP, 2, 1); wr(&g.rt, SP + 2, 4, d); CHECK(trap(&g, 0xaa68, SP) == 1 && raw(&g.rt, SP + 6, 2) == 0);
    /* the standard filter procedure, called as the guest calls it: Return hits the default item, Escape the cancel item, else FALSE */
    #define FILTER(what, ch, mods) (wr(&g.rt, ev, 2, (what)), wr(&g.rt, ev + 2, 4, (ch)), wr(&g.rt, ev + 14, 2, (mods)), wr(&g.rt, SP, 4, ip), wr(&g.rt, SP + 4, 4, ev), wr(&g.rt, SP + 8, 4, d), wr(&g.rt, SP + 12, 2, 0x7777), wr(&g.rt, ip, 2, 0), trap(&g, 0xabf1, SP))
    CHECK(FILTER(3, 13, 0) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 2 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 12);
    g.rt.last_trap_pc = mac_native_stub(0xbf1);                                                   /* called through its stub, as a guest does: allowed */
    CHECK(FILTER(3, 13, 0) == 1 && raw(&g.rt, ip, 2) == 2); g.rt.last_trap_pc = 0;
    CHECK(FILTER(3, 27, 0) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 2);
    CHECK(FILTER(3, '.', 0x100) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 2);
    CHECK(FILTER(3, 'a', 0) == 1 && raw(&g.rt, SP + 12, 1) == 0 && raw(&g.rt, ip, 2) == 0);
    CHECK(FILTER(1, 13, 0) == 1 && raw(&g.rt, SP + 12, 1) == 0);
    /* IsDialogEvent */
    #define ISDLG(what) (wr(&g.rt, ev, 2, (what)), wr(&g.rt, SP, 4, ev), wr(&g.rt, SP + 4, 2, 0x7777), trap(&g, 0xa97f, SP))
    for (unsigned w = 0; w < 16; w++) { if (!(ISDLG(w) == 1 && raw(&g.rt, SP + 4, 1) == ((w == 1 || w == 3 || w == 5 || w == 6 || w == 8) ? 1u : 0u))) fprintf(stderr, "ISDLG %u halted=%d res=%u why=%s\n", w, mac_cpu_halted(g.rt.cpu), raw(&g.rt, SP + 4, 1), mac_cpu_error(g.rt.cpu)); CHECK(ISDLG(w) == 1 && raw(&g.rt, SP + 4, 1) == ((w == 1 || w == 3 || w == 5 || w == 6 || w == 8) ? 1u : 0u) && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4); }
    /* DialogSelect: a button press waits for the release, then reports the item; an edit item reports at once; keys; nothing else */
    #define DSEL(what, msg, mods, x, y) (wr(&g.rt, ev, 2, (what)), wr(&g.rt, ev + 2, 4, (msg)), wr(&g.rt, ev + 10, 2, (y)), wr(&g.rt, ev + 12, 2, (x)), wr(&g.rt, ev + 14, 2, (mods)), \
        wr(&g.rt, SP, 4, ip), wr(&g.rt, SP + 4, 4, dp), wr(&g.rt, SP + 8, 4, ev), wr(&g.rt, SP + 12, 2, 0x7777), wr(&g.rt, dp, 4, 0), wr(&g.rt, ip, 2, 0), mac_cpu_set(g.rt.cpu, MAC_A7, SP), mac_cpu_set(g.rt.cpu, MAC_PC, TRAP_PC), mac_dlgmgr_service(&g.rt, 0xa980))
    CHECK(DSEL(1, 0, 0, 295, 120) == 1 && mac_cpu_get(g.rt.cpu, MAC_PC) == 0x10000 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP);                    /* tracking the press */
    post(&g, 2, 0, 0, 295, 120);
    mac_cpu_set(g.rt.cpu, MAC_A7, SP); mac_cpu_set(g.rt.cpu, MAC_PC, TRAP_PC); CHECK(mac_dlgmgr_service(&g.rt, 0xa980) == 1);
    CHECK(raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 1 && raw(&g.rt, dp, 4) == d && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 12);
    CHECK(DSEL(1, 0, 0, 200, 92) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 5);                                              /* edit item: at once */
    CHECK(DSEL(1, 0, 0, 5, 5) == 1 && raw(&g.rt, SP + 12, 1) == 0 && raw(&g.rt, ip, 2) == 0);                                                  /* empty space */
    CHECK(DSEL(3, 13, 0, 0, 0) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 2);                                                 /* Return: default item (2 now) */
    CHECK(DSEL(3, 'k', 0, 0, 0) == 1 && raw(&g.rt, SP + 12, 1) == 0);
    CHECK(DSEL(5, 13, 0, 0, 0) == 1 && raw(&g.rt, SP + 12, 1) == 1 && raw(&g.rt, ip, 2) == 2);                                                /* auto-key Return too */
    CHECK(DSEL(6, 0, 0, 0, 0) == 1 && raw(&g.rt, SP + 12, 1) == 0);
    /* a press released outside the button reports nothing */
    CHECK(DSEL(1, 0, 0, 295, 120) == 1); post(&g, 2, 0, 0, 5, 5); mac_cpu_set(g.rt.cpu, MAC_A7, SP); mac_cpu_set(g.rt.cpu, MAC_PC, TRAP_PC); CHECK(mac_dlgmgr_service(&g.rt, 0xa980) == 1 && raw(&g.rt, SP + 12, 1) == 0);
    mac_runtime_destroy(&g.rt);
    /* no dialog open: FrontWindow and SetPort pass through; DialogSelect and IsDialogEvent say FALSE */
    CHECK(rig_open(&g) == 0); wr(&g.rt, SP, 4, 0xdead); CHECK(trap(&g, 0xa924, SP) == 0); wr(&g.rt, SP, 4, 0x31000); CHECK(trap(&g, 0xa873, SP) == 0);
    wr(&g.rt, ev, 2, 1); wr(&g.rt, SP, 4, ev); CHECK(trap(&g, 0xa97f, SP) == 1 && raw(&g.rt, SP + 4, 1) == 0);
    wr(&g.rt, SP, 4, ip); wr(&g.rt, SP + 4, 4, dp); wr(&g.rt, SP + 8, 4, ev); CHECK(trap(&g, 0xa980, SP) == 1 && raw(&g.rt, SP + 12, 1) == 0 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 12);
    mac_runtime_destroy(&g.rt);
    /* refusals */
    mac_cpu_set(g.rt.cpu, MAC_A7, SP);
    static const unsigned sels[] = { 0x0101, 0x0307, 0x0000 };
    for (unsigned k = 0; k < 3; k++) { CHECK(rig_open(&g) == 0); get_new(&g, 128, 0, 0xffffffffu, &rc); mac_cpu_set(g.rt.cpu, MAC_D0, sels[k]); wr(&g.rt, SP, 4, 0x31200); CHECK(trap(&g, 0xaa68, SP) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt); }
    static const unsigned unsup[] = { 0xa97d, 0xa98e, 0xa984, 0xa978, 0xa979, 0xa97a };
    for (unsigned k = 0; k < 6; k++) { CHECK(rig_open(&g) == 0); wr(&g.rt, SP, 4, 0); CHECK(trap(&g, unsup[k], SP) == -1 && mac_cpu_halted(g.rt.cpu)); mac_runtime_destroy(&g.rt); }
    CHECK(rig_open(&g) == 0); wr(&g.rt, SP, 4, 0); CHECK(trap(&g, 0xa98c, SP) == 1 && mac_cpu_get(g.rt.cpu, MAC_A7) == SP + 4); mac_runtime_destroy(&g.rt);     /* ErrorSound */
    CHECK(rig_open(&g) == 0); CHECK(mac_dlgmgr_service(&g.rt, 0xa900) == 0); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); mac_cpu_halt(g.rt.cpu, "x"); CHECK(mac_dlgmgr_service(&g.rt, 0xa97c) == -1); mac_runtime_destroy(&g.rt);
    CHECK(rig_open(&g) == 0); get_new(&g, 128, 0, 0xffffffffu, &rc); g.rt.last_trap_pc = 0x2100; CHECK(trap(&g, 0xa991, SP) == -1); mac_runtime_destroy(&g.rt);
}
int main(void) {
    creating(); items(); disabled_and_clamped(); hidden_moved_and_items(); refusing_items(); modal_loop(); event_loop_pieces();
    if (!fails) puts("Dialog Manager checks passed");
    return fails ? 1 : 0;
}
