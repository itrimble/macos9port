/* Alert / StopAlert / NoteAlert / CautionAlert: the native modal alert, driven through the real trap path. */
#include "mac_dialog_draw.h"
#include "mac_alert_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PROGRAM = 0x10000, STACK = 0x30000, BACK = 77 };
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

/* ---- DITL / ALRT builders ---- */
typedef struct { uint8_t b[512]; size_t n; } W;
static void byte(W *w, unsigned v) { CHECK(w->n < sizeof w->b); w->b[w->n++] = (uint8_t)v; }
static void word(W *w, unsigned v) { byte(w, v >> 8); byte(w, v); }
static void item(W *w, int t, int l, int b, int r, unsigned type, const void *d, unsigned n) {
    word(w, 0); word(w, 0); word(w, (unsigned)t); word(w, (unsigned)l); word(w, (unsigned)b); word(w, (unsigned)r); byte(w, type); byte(w, n);
    for (unsigned i = 0; i < n; i++) byte(w, ((const uint8_t *)d)[i]); if (n & 1) byte(w, 0);
}
static void alrt(Fork *f, int16_t id, int t, int l, int b, int r, int16_t items, unsigned stages) {
    W w = {0}; word(&w, (unsigned)t); word(&w, (unsigned)l); word(&w, (unsigned)b); word(&w, (unsigned)r); word(&w, (unsigned)items); word(&w, stages);
    add(f, 0x414c5254, id, w.b, w.n);
}
static const uint8_t ICONBITS[128] = { [0] = 0x80, [3] = 0x01 };   /* asymmetric: first and last pixel of row 0 only */

static void fixture_fork(Fork *f) {
    uint8_t zero[32] = {0}, code[10] = {0, 0, 0, 1, 0xa9, 0xf4};
    put32(zero, 0x100); put32(zero + 4, 0x100); put32(zero + 8, 8); put32(zero + 12, 32);
    put16(zero + 16, 0); put16(zero + 18, 0x3f3c); put16(zero + 20, 1); put16(zero + 22, 0xa9f0);
    add(f, 0x434f4445, 0, zero, 32); add(f, 0x434f4445, 1, code, 10);
    W w = {0};
    /* DITL 128: OK, Cancel (disabled), text with ParamText, an icon, and a picture item (ignored). */
    word(&w, 4); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 70, 150, 90, 200, 4 | 0x80, "No", 2);
    item(&w, 10, 70, 60, 270, 8, "Hi ^0 and ^1", 12); item(&w, 10, 10, 42, 42, 32, "\x01\x2c", 2); item(&w, 5, 5, 6, 6, 64, "\0\1", 2);
    add(f, 0x4449544c, 128, w.b, w.n);
    /* DITL 129: two enabled buttons, for the default-button bit. */
    memset(&w, 0, sizeof w); word(&w, 2); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 70, 150, 90, 200, 4, "Cancel", 6); item(&w, 10, 10, 60, 270, 8, "Sure?", 5);
    add(f, 0x4449544c, 129, w.b, w.n);
    /* DITL 130: item 1 is static, not a button. DITL 131: truncated item list. */
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 10, 10, 60, 270, 8, "Text", 4); item(&w, 70, 220, 90, 270, 4, "OK", 2);
    add(f, 0x4449544c, 130, w.b, w.n);
    memset(&w, 0, sizeof w); word(&w, 3); item(&w, 70, 220, 90, 270, 4, "OK", 2);
    add(f, 0x4449544c, 131, w.b, w.n);
    add(f, 0x49434f4e, 300, ICONBITS, 128);
    { uint8_t ones[128]; memset(ones, 0xff, sizeof ones); add(f, 0x49434f4e, 301, ones, 64); add(f, 0x49434f4e, 302, ones, 128); } /* 301 is too short; 302 follows it */
    memset(&w, 0, sizeof w); word(&w, 1); item(&w, 70, 220, 90, 270, 4, "OK", 2); item(&w, 10, 10, 42, 42, 32, "\x01\x2d", 2);
    add(f, 0x4449544c, 132, w.b, w.n);
    alrt(f, 128, 40, 50, 140, 330, 128, 0x5555);   /* ordinary */
    alrt(f, 129, 40, 50, 140, 330, 129, 0xdddd);   /* bold item 2 */
    alrt(f, 130, 40, 50, 140, 330, 128, 0x5554);   /* stages disagree */
    alrt(f, 131, 40, 50, 140, 330, 128, 0x1111);   /* box not drawn */
    alrt(f, 132, 40, 50, 140, 330, 130, 0x5555);   /* default item is not a button */
    alrt(f, 133, 40, 50, 140, 330, 999, 0x5555);   /* DITL missing */
    alrt(f, 134, 40, 50, 140, 330, 131, 0x5555);   /* malformed DITL */
    alrt(f, 135, 420, 560, 520, 700, 128, 0x5555); /* hangs off the screen: moved on */
    alrt(f, 136, 40, 50, 50, 60, 128, 0x5555);     /* far too small */
    alrt(f, 137, 40, 50, 140, 330, 128, 0x5555);
    { W p = {0}; word(&p, 40); word(&p, 50); word(&p, 140); word(&p, 330); word(&p, 128); word(&p, 0x5555); word(&p, 0x700a); add(f, 0x414c5254, 138, p.b, p.n); } /* 14 bytes */
    { W p = {0}; word(&p, 40); word(&p, 50); word(&p, 140); word(&p, 330); word(&p, 128); word(&p, 0x5555); byte(&p, 0); add(f, 0x414c5254, 139, p.b, p.n); } /* 13 bytes: only the size is wrong */
    alrt(f, 140, 40, 50, 140, 330, 128, 0x5545);   /* stage 2 differs */
    alrt(f, 141, 40, 50, 140, 330, 128, 0x5455);   /* stage 3 differs */
    alrt(f, 142, 40, 50, 140, 330, 128, 0x4555);   /* stage 4 differs */
    alrt(f, 143, 40, 50, 140, 330, 132, 0x5555);   /* its icon resource is too short */
}

typedef struct { MacRuntime rt; Fork fork; uint8_t image[16384]; } Rig;
static void emit(MacCpu *c, uint32_t *p, uint16_t w) { CHECK(mac_cpu_write(c, *p, 2, w) == 0); *p += 2; }
static uint32_t raw(const MacRuntime *rt, uint32_t p, unsigned n) { const uint8_t *ram = mac_cpu_ram(rt->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | ram[p + i]; return v; }
static int rig_open(Rig *g, uint16_t opcode) {
    char err[240]; MacResourceFork fork; memset(g, 0, sizeof *g);
    fixture_fork(&g->fork); size_t n = build(&g->fork, g->image);
    if (mac_resource_open(&fork, g->image, n, err, sizeof err) || mac_runtime_init(&g->rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); CHECK(0); return -1; }
    MacRuntime *rt = &g->rt;
    rt->screen_depth = 8; rt->screen_width = 640; rt->screen_height = 480; rt->screen_row_bytes = 640; rt->screen_base = 0xe00000;
    rt->palette[0] = 0xffffffffu; rt->palette[1] = 0xff000000u;
    memset(mac_cpu_ram(rt->cpu) + rt->screen_base, BACK, 640 * 480);
    memcpy(rt->dialog_params[0], "\003Bob", 4); memcpy(rt->dialog_params[1], "\002Al", 3);
    uint32_t p = PROGRAM; emit(rt->cpu, &p, 0x7200); emit(rt->cpu, &p, opcode); emit(rt->cpu, &p, 0x7205); emit(rt->cpu, &p, 0xa9f4);
    return 0;
}
static void rig_close(Rig *g) { mac_runtime_destroy(&g->rt); }
/* Push the result slot, alertID and filterProc, and start the program. Returns the stack pointer at the trap. */
static uint32_t start(Rig *g, int16_t id, uint32_t filter) {
    uint32_t sp = STACK - 8;
    CHECK(mac_cpu_write(g->rt.cpu, sp, 4, filter) == 0 && mac_cpu_write(g->rt.cpu, sp + 4, 2, (uint16_t)id) == 0 && mac_cpu_write(g->rt.cpu, sp + 6, 2, 0xeeee) == 0);
    mac_cpu_set(g->rt.cpu, MAC_A7, sp); mac_cpu_set(g->rt.cpu, MAC_PC, PROGRAM);
    return sp;
}
static void pump(Rig *g, unsigned n) { for (unsigned i = 0; i < n && !mac_cpu_halted(g->rt.cpu); i++) mac_runtime_run(&g->rt, 400); }
static void click(Rig *g, int x, int y) { mac_runtime_post_event(&g->rt, 1, 0, 0, (int16_t)x, (int16_t)y); mac_runtime_post_event(&g->rt, 2, 0, 0, (int16_t)x, (int16_t)y); pump(g, 4); }
static void key(Rig *g, unsigned ch) { mac_runtime_post_event(&g->rt, 3, ch, 0, 0, 0); pump(g, 4); }
static int screen_all_back(Rig *g) { const uint8_t *s = mac_cpu_ram(g->rt.cpu) + g->rt.screen_base; for (unsigned i = 0; i < 640 * 480; i++) if (s[i] != BACK) return 0; return 1; }
static uint8_t px(Rig *g, int x, int y) { return mac_cpu_ram(g->rt.cpu)[g->rt.screen_base + (uint32_t)y * 640 + (uint32_t)x]; }
static int open_now(Rig *g) { return g->rt.alert != NULL; }

int main(void) {
    static const uint16_t kinds[] = { 0xa985, 0xa986, 0xa987, 0xa988 };
    for (unsigned k = 0; k < 4; k++) { /* all four traps behave alike */
        Rig g; CHECK(rig_open(&g, kinds[k]) == 0); uint32_t sp = start(&g, 128, 0); pump(&g, 3);
        CHECK(open_now(&g) && !mac_cpu_halted(g.rt.cpu) && mac_cpu_get(g.rt.cpu, MAC_D1) != 5);
        CHECK(raw(&g.rt, sp + 6, 2) == 0xeeee);                                  /* no button is chosen on the user's behalf */
        CHECK(px(&g, 50, 40) != BACK);                                           /* the frame is drawn at the authored corner */
        CHECK(px(&g, 50 + 10, 40 + 10) == 1 && px(&g, 50 + 11, 40 + 10) == 0 && px(&g, 50 + 10 + 31, 40 + 10) == 1); /* the app's icon: first and last pixel of row 0 */
        click(&g, 50 + 245, 40 + 80);                                            /* OK */
        CHECK(!open_now(&g) && raw(&g.rt, sp + 6, 2) == 1);
        pump(&g, 3);
        CHECK(mac_cpu_get(g.rt.cpu, MAC_D1) == 5 && g.rt.exited && mac_cpu_get(g.rt.cpu, MAC_A7) == sp + 6 && screen_all_back(&g));
        rig_close(&g);
    }
    Rig g; int x, y;
    /* ParamText substitution: the item renders exactly as the expanded text would. */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 128, 0); pump(&g, 3);
    {
        MacPen pen; mac_pen_init(&pen, &g.rt, 50, 40, 280, 100);
        enum { L = 50 + 70, T = 40 + 10, R = 50 + 270, B = 40 + 60 };
        uint8_t a[(R - L) * (B - T)], b[(R - L) * (B - T)];
        for (int yy = T; yy < B; yy++) for (int xx = L; xx < R; xx++) a[(yy - T) * (R - L) + xx - L] = px(&g, xx, yy);
        mac_pen_fill(&pen, L, T, R, B, pen.light); mac_pen_wrap(&pen, L, T, R, B, "Hi Bob and Al", 13);
        for (int yy = T; yy < B; yy++) for (int xx = L; xx < R; xx++) b[(yy - T) * (R - L) + xx - L] = px(&g, xx, yy);
        CHECK(!memcmp(a, b, sizeof a));
        mac_pen_fill(&pen, L, T, R, B, pen.light); mac_pen_wrap(&pen, L, T, R, B, "Hi ^0 and ^1", 12);
        for (int yy = T; yy < B; yy++) for (int xx = L; xx < R; xx++) b[(yy - T) * (R - L) + xx - L] = px(&g, xx, yy);
        CHECK(memcmp(a, b, sizeof a) != 0);                                      /* the unexpanded text really differs */
    }
    /* A disabled button does nothing; press-and-release elsewhere does nothing; the picture item is ignored. */
    click(&g, 50 + 175, 40 + 80); CHECK(open_now(&g));
    x = 50 + 245; y = 40 + 80;
    mac_runtime_post_event(&g.rt, 1, 0, 0, (int16_t)x, (int16_t)y); pump(&g, 3); CHECK(open_now(&g) && g.rt.alert->pressed == 1);
    mac_runtime_post_event(&g.rt, 2, 0, 0, (int16_t)(x - 150), (int16_t)y); pump(&g, 3); CHECK(open_now(&g) && g.rt.alert->pressed == 0);
    click(&g, 50 + 8, 40 + 8); CHECK(open_now(&g));
    key(&g, 'x'); key(&g, 27); CHECK(open_now(&g));                              /* only Return and Enter act */
    /* Update events are left for the application. */
    mac_runtime_post_event(&g.rt, 6, 0x1234, 0, 0, 0); pump(&g, 2); CHECK(g.rt.event_count == 1 && g.rt.events[g.rt.event_head].what == 6);
    key(&g, 13); CHECK(!open_now(&g) && raw(&g.rt, STACK - 8 + 6, 2) == 1 && g.rt.event_count == 1);
    rig_close(&g);

    /* Default button: Return and Enter press item 1, or item 2 when the stage says so. */
    for (unsigned c = 0; c < 4; c++) {
        CHECK(rig_open(&g, 0xa985) == 0); start(&g, c < 2 ? 128 : 129, 0); pump(&g, 3);
        key(&g, c % 2 ? 3 : 13);
        CHECK(!open_now(&g) && raw(&g.rt, STACK - 8 + 6, 2) == (c < 2 ? 1u : 2u));
        rig_close(&g);
    }
    /* The second button of an ordinary alert is a real button. */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 129, 0); pump(&g, 3); click(&g, 50 + 175, 40 + 80);
    CHECK(!open_now(&g) && raw(&g.rt, STACK - 8 + 6, 2) == 2); rig_close(&g);

    /* An ICON resource that is not 128 bytes is not drawn (and nothing beyond it is read). */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 143, 0); pump(&g, 3);
    { int dark = 0; for (int yy = 50; yy < 82; yy++) for (int xx = 60; xx < 92; xx++) dark += px(&g, xx, yy) == 1; CHECK(open_now(&g) && dark == 0); }
    rig_close(&g);
    /* A 14-byte ALRT with a position word is accepted; the position is not interpreted (drawn at the authored corner). */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 138, 0); pump(&g, 3); CHECK(open_now(&g) && px(&g, 50, 40) != BACK); rig_close(&g);
    /* An alert hanging off the screen is moved back onto it. */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 135, 0); pump(&g, 3);
    CHECK(open_now(&g) && g.rt.alert->left == 640 - 140 && g.rt.alert->top == 480 - 100 && px(&g, 640 - 140, 480 - 100) != BACK); rig_close(&g);

    /* Refusals halt with a named message before anything is drawn. */
    static const struct { int16_t id; uint32_t filter; int depth; } bad[] = {
        { 128, 0x1234, 8 }, { 99, 0, 8 }, { 130, 0, 8 }, { 131, 0, 8 }, { 132, 0, 8 }, { 133, 0, 8 }, { 134, 0, 8 }, { 136, 0, 8 }, { 139, 0, 8 }, { 128, 0, 1 },
        { 140, 0, 8 }, { 141, 0, 8 }, { 142, 0, 8 },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        CHECK(rig_open(&g, 0xa985) == 0); g.rt.screen_depth = (uint16_t)bad[i].depth; start(&g, bad[i].id, bad[i].filter); pump(&g, 4);
        if (!(mac_cpu_halted(g.rt.cpu) && !g.rt.exited && !open_now(&g) && screen_all_back(&g))) fprintf(stderr, "refusal %u not clean\n", i);
        CHECK(mac_cpu_halted(g.rt.cpu) && !g.rt.exited && !open_now(&g) && screen_all_back(&g) && mac_cpu_get(g.rt.cpu, MAC_D1) != 5);
        rig_close(&g);
    }
    /* A second alert call while one is open halts. */
    CHECK(rig_open(&g, 0xa985) == 0); start(&g, 128, 0); pump(&g, 3);
    mac_cpu_set(g.rt.cpu, MAC_A7, mac_cpu_get(g.rt.cpu, MAC_A7) - 2); pump(&g, 2); CHECK(mac_cpu_halted(g.rt.cpu)); rig_close(&g);
    if (!fails) puts("Alert checks passed");
    return fails ? 1 : 0;
}
