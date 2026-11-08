/* Standard File (Pack3 SFGetFile/SFPGetFile): the native modal dialog, driven through the real trap path. */
#include "mac_stdfile_int.h"
#include "mac_drive_queue.h"
#include "mac_files_int.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40, TOTAL = MAP + 62,
       PROGRAM = 0x10000, FILTER = 0x10100, STACK = 0x30000, REPLY = 0x20000, TYPES = 0x20100, PROMPT = 0x20200, BACK = 77 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }
static void fixture(uint8_t *b) {
    memset(b, 0, TOTAL);
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 62); put32(b + 12, 62);
    put32(b + DATA, 32);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100); put32(b + ZERO + 8, 8); put32(b + ZERO + 12, 32);
    put16(b + ZERO + 16, 0); put16(b + ZERO + 18, 0x3f3c); put16(b + ZERO + 20, 1); put16(b + ZERO + 22, 0xa9f0);
    put32(b + DATA + 36, 10); put16(b + ONE, 0); put16(b + ONE + 2, 1); put16(b + ONE + 4, 0xa9f4);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 62);
    put16(b + MAP + 28, 0); put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    for (unsigned i = 0; i < 2; i++) { put16(b + MAP + 38 + i * 12, (uint16_t)i); put16(b + MAP + 40 + i * 12, 0xffff); }
    b[MAP + 57] = 36;
}
static void make(const char *dir, const char *name, const char *type, unsigned flags) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "x", 1) == 1);
    if (type) { uint8_t raw[16] = {0}; memcpy(raw, type, 4); raw[8] = (uint8_t)(flags >> 8); raw[9] = (uint8_t)flags; CHECK(mac_files_finder_set(fd, raw) == 0); }
    close(fd);
}
static void emit(MacCpu *c, uint32_t *p, uint16_t w) { CHECK(mac_cpu_write(c, *p, 2, w) == 0); *p += 2; }
/* Reads that still work after the guest has halted (mac_cpu_read refuses once the CPU is stopped). */
static uint32_t raw(const void *rig, uint32_t p, unsigned n) {
    const uint8_t *ram = mac_cpu_ram(((const MacRuntime *)rig)->cpu); uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v = v << 8 | ram[p + i];
    return v;
}
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { CHECK(mac_cpu_write(rt->cpu, p, n, v) == 0); }

typedef struct { MacRuntime rt; uint8_t image[TOTAL]; char dir[64], saves[64]; } Rig;
/* A runtime with an 8-bit 640x480 screen painted BACK, a volume with known files, and a program: A9EA; MOVEQ #5,D1; ExitToShell. */
static int rig_open(Rig *g, int filter_routine) {
    char err[240]; MacResourceFork fork;
    memset(g, 0, sizeof *g);
    snprintf(g->dir, sizeof g->dir, "/tmp/ospi-sf-data-XXXXXX"); snprintf(g->saves, sizeof g->saves, "/tmp/ospi-sf-saves-XXXXXX");
    CHECK(mkdtemp(g->dir) && mkdtemp(g->saves));
    make(g->dir, "alpha", "TEXT", 0); make(g->dir, "Beta", "gliH", 0); make(g->dir, "gamma", "gliH", 0);
    make(g->dir, "none", NULL, 0); make(g->dir, "secret", "gliH", 0x4000);
    fixture(g->image);
    if (mac_resource_open(&fork, g->image, TOTAL, err, sizeof err) || mac_runtime_init(&g->rt, &fork, err, sizeof err)) { CHECK(0); return -1; }
    MacRuntime *rt = &g->rt;
    CHECK(mac_files_root(rt, g->dir) == 0);
    rt->screen_depth = 8; rt->screen_width = 640; rt->screen_height = 480; rt->screen_row_bytes = 640; rt->screen_base = 0xe00000;
    rt->palette[0] = 0xffffffffu; rt->palette[1] = 0xff000000u;
    memset(mac_cpu_ram(rt->cpu) + rt->screen_base, BACK, 640 * 480);
    MacCpu *c = rt->cpu; uint32_t p = PROGRAM;
    emit(c, &p, 0x7200); emit(c, &p, 0xa9ea); emit(c, &p, 0x7205); emit(c, &p, 0xa9f4);
    if (filter_routine) { /* Pascal FUNCTION f(pb): BOOLEAN; returns TRUE (hide) when the file name starts with 'a' */
        p = FILTER;
        emit(c, &p, 0x206f); emit(c, &p, 4);            /* MOVEA.L 4(A7),A0   pb */
        emit(c, &p, 0x2068); emit(c, &p, 18);           /* MOVEA.L 18(A0),A0  ioNamePtr */
        emit(c, &p, 0x0c28); emit(c, &p, 0x0061); emit(c, &p, 1); /* CMPI.B #'a',1(A0) */
        emit(c, &p, 0x6606);                            /* BNE.S +6 */
        emit(c, &p, 0x3f7c); emit(c, &p, 0x0100); emit(c, &p, 8); /* MOVE.W #$0100,8(A7) */
        emit(c, &p, 0x205f); emit(c, &p, 0x588f); emit(c, &p, 0x4ed0);
    }
    memcpy(mac_cpu_ram(c) + PROMPT, "\006Prompt", 7);
    memset(mac_cpu_ram(c) + REPLY, 0xee, 74);
    return 0;
}
static void rig_close(Rig *g) { mac_runtime_destroy(&g->rt); }
/* Push SFGetFile (sel 2) or SFPGetFile (sel 4) arguments and start the program. */
static uint32_t start(Rig *g, unsigned sel, int num_types, uint32_t filter, uint32_t hook, uint32_t filter_proc, int dialog_id, int v, int h) {
    MacRuntime *rt = &g->rt; uint32_t sp = STACK;
    unsigned total = sel == 4 ? 34 : 28; sp -= total;
    uint32_t a = sp; wr(rt, a, 2, sel); a += 2;
    if (sel == 4) { wr(rt, a, 4, filter_proc); wr(rt, a + 4, 2, (uint16_t)dialog_id); a += 6; }
    wr(rt, a, 4, REPLY); wr(rt, a + 4, 4, hook); wr(rt, a + 8, 4, TYPES); wr(rt, a + 12, 2, (uint16_t)num_types);
    wr(rt, a + 14, 4, filter); wr(rt, a + 18, 4, PROMPT); wr(rt, a + 22, 2, (uint16_t)v); wr(rt, a + 24, 2, (uint16_t)h);
    mac_cpu_set(rt->cpu, MAC_A7, sp); mac_cpu_set(rt->cpu, MAC_PC, PROGRAM);
    return sp;
}
static void pump(Rig *g, unsigned slices) { for (unsigned i = 0; i < slices && !mac_cpu_halted(g->rt.cpu); i++) mac_runtime_run(&g->rt, 400); }
static MacSfState *dialog(Rig *g) { return g->rt.stdfile; }
static void click(Rig *g, int x, int y) { mac_runtime_post_event(&g->rt, 1, 0, 0, (int16_t)x, (int16_t)y); mac_runtime_post_event(&g->rt, 2, 0, 0, (int16_t)x, (int16_t)y); pump(g, 4); }
static void key(Rig *g, unsigned ch, unsigned mods) { mac_runtime_post_event(&g->rt, 3, ch, (uint16_t)mods, 0, 0); pump(g, 4); }
static unsigned names(Rig *g, const char **out, unsigned max) {
    MacSfState *s = dialog(g); unsigned n = 0;
    for (; s && n < s->list.count && n < max; n++) out[n] = s->list.entries[n].name;
    return n;
}
/* Global point inside dialog item `item` (1-based) of the built-in layout at (v, h): its centre. */
static void item_point(int v, int h, unsigned item, int *x, int *y) {
    static const int r[][4] = { {138,256,156,336}, {0,0,0,0}, {163,256,181,336}, {0,0,0,0}, {68,256,86,336}, {93,256,111,336}, {39,12,185,230}, {39,229,185,246} };
    *y = v + (r[item - 1][0] + r[item - 1][2]) / 2; *x = h + (r[item - 1][1] + r[item - 1][3]) / 2;
}
static int screen_changed(Rig *g, int v, int h) { return mac_cpu_ram(g->rt.cpu)[g->rt.screen_base + (uint32_t)(v + 10) * 640 + (uint32_t)(h + 10)] != BACK; }
static int screen_all_back(Rig *g) {
    const uint8_t *s = mac_cpu_ram(g->rt.cpu) + g->rt.screen_base;
    for (unsigned i = 0; i < 640 * 480; i++) if (s[i] != BACK) return 0;
    return 1;
}

int main(void) {
    const int V = 60, H = 50;
    Rig g; const char *n[16];

    /* --- Open: listing, selection by click, Open button; reply written, stack popped, backdrop restored. */
    CHECK(rig_open(&g, 0) == 0);
    uint32_t sp = start(&g, 4, -1, 0, 0, 0, -4000, V, H);
    pump(&g, 3);
    CHECK(dialog(&g) && dialog(&g)->stage == SF_STAGE_RUN && !mac_cpu_halted(g.rt.cpu));
    CHECK(dialog(&g)->top == V && dialog(&g)->left == H && dialog(&g)->width == 348 && dialog(&g)->height == 208);
    CHECK(names(&g, n, 16) == 4 && !strcmp(n[0], "alpha") && !strcmp(n[1], "Beta") && !strcmp(n[2], "gamma") && !strcmp(n[3], "none")); /* "secret" is invisible */
    CHECK(screen_changed(&g, V, H) && g.rt.cpu && mac_cpu_get(g.rt.cpu, MAC_D1) != 5);
    int x, y;
    item_point(V, H, 7, &x, &y); y = V + 39 + 1 + SF_ROW + 4;                 /* second row */
    click(&g, x, y);
    CHECK(dialog(&g)->list.selected == 1 && dialog(&g)->stage == SF_STAGE_RUN && raw(&g.rt, REPLY, 1) == 0xee); /* not finished */
    item_point(V, H, 1, &x, &y);
    mac_runtime_post_event(&g.rt, 1, 0, 0, (int16_t)x, (int16_t)y); pump(&g, 3);
    CHECK(dialog(&g) && dialog(&g)->pressed == 1);                            /* pressed, not yet released */
    mac_runtime_post_event(&g.rt, 2, 0, 0, (int16_t)(x + 200), (int16_t)y); pump(&g, 3);
    CHECK(dialog(&g) && dialog(&g)->pressed == 0 && raw(&g.rt, REPLY, 1) == 0xee); /* released elsewhere: no action */
    click(&g, x, y);
    CHECK(!dialog(&g) && raw(&g.rt, REPLY, 1) == 1 && raw(&g.rt, REPLY + 1, 1) == 0 && raw(&g.rt, REPLY + 2, 4) == 0x676c6948);
    CHECK(raw(&g.rt, REPLY + 6, 2) == 0xffff && raw(&g.rt, REPLY + 8, 2) == 0 && raw(&g.rt, REPLY + 10, 1) == 4 && !memcmp(mac_cpu_ram(g.rt.cpu) + REPLY + 11, "Beta", 4));
    pump(&g, 3);
    CHECK(mac_cpu_get(g.rt.cpu, MAC_D1) == 5 && g.rt.exited && mac_cpu_get(g.rt.cpu, MAC_A7) == sp + 34); /* program resumed; stack popped */
    CHECK(screen_all_back(&g));                                               /* backdrop restored */
    rig_close(&g);

    /* --- Double click opens; a slow second click does not. */
    CHECK(rig_open(&g, 0) == 0); start(&g, 2, -1, 0, 0, 0, 0, V, H); pump(&g, 3);
    item_point(V, H, 7, &x, &y); y = V + 39 + 1 + 4;
    g.rt.ticks = 100; mac_runtime_post_event(&g.rt, 1, 0, 0, (int16_t)x, (int16_t)y); mac_runtime_post_event(&g.rt, 2, 0, 0, (int16_t)x, (int16_t)y); pump(&g, 3);
    g.rt.ticks = 200; mac_runtime_post_event(&g.rt, 1, 0, 0, (int16_t)x, (int16_t)y); pump(&g, 3);
    CHECK(dialog(&g) && raw(&g.rt, REPLY, 1) == 0xee);                         /* too slow: still open */
    g.rt.ticks = 210; mac_runtime_post_event(&g.rt, 1, 0, 0, (int16_t)x, (int16_t)y); pump(&g, 3);
    CHECK(!dialog(&g) && raw(&g.rt, REPLY, 1) == 1 && !memcmp(mac_cpu_ram(g.rt.cpu) + REPLY + 11, "alpha", 5));
    rig_close(&g);

    /* --- Cancel button, Escape, Cmd-period: good = 0 and the rest of the reply cleared. */
    for (unsigned how = 0; how < 3; how++) {
        CHECK(rig_open(&g, 0) == 0); sp = start(&g, 4, -1, 0, 0, 0, -4000, V, H); pump(&g, 3);
        if (how == 0) { item_point(V, H, 3, &x, &y); click(&g, x, y); }
        if (how == 1) key(&g, 27, 0);
        if (how == 2) key(&g, '.', 0x100);
        CHECK(!dialog(&g) && raw(&g.rt, REPLY, 1) == 0 && raw(&g.rt, REPLY + 2, 4) == 0 && raw(&g.rt, REPLY + 10, 1) == 0);
        pump(&g, 3); CHECK(mac_cpu_get(g.rt.cpu, MAC_D1) == 5 && mac_cpu_get(g.rt.cpu, MAC_A7) == sp + 34 && screen_all_back(&g));
        rig_close(&g);
    }

    /* --- Keyboard: Return with no selection does nothing; arrows select; Return opens; type-ahead selects. */
    CHECK(rig_open(&g, 0) == 0); start(&g, 2, -1, 0, 0, 0, 0, V, H); pump(&g, 3);
    key(&g, 13, 0); CHECK(dialog(&g) && raw(&g.rt, REPLY, 1) == 0xee);
    key(&g, 'g', 0x100); CHECK(dialog(&g)->list.selected == -1);             /* Cmd-G is a command, not typing */
    key(&g, 'g', 0); CHECK(dialog(&g)->list.selected == 2);                  /* "gamma" */
    key(&g, 0x1f, 0); CHECK(dialog(&g)->list.selected == 3);
    key(&g, 0x1e, 0); key(&g, 0x1e, 0); CHECK(dialog(&g)->list.selected == 1);
    key(&g, 13, 0); CHECK(!dialog(&g) && raw(&g.rt, REPLY, 1) == 1 && !memcmp(mac_cpu_ram(g.rt.cpu) + REPLY + 11, "Beta", 4));
    rig_close(&g);

    /* --- Type filter: only the listed types appear; a type never set matches nothing; numTypes 0 lists none. */
    CHECK(rig_open(&g, 0) == 0); memcpy(mac_cpu_ram(g.rt.cpu) + TYPES, "gliHTEXT", 8);
    start(&g, 2, 1, 0, 0, 0, 0, V, H); pump(&g, 3);
    CHECK(names(&g, n, 16) == 2 && !strcmp(n[0], "Beta") && !strcmp(n[1], "gamma")); rig_close(&g);
    CHECK(rig_open(&g, 0) == 0); memcpy(mac_cpu_ram(g.rt.cpu) + TYPES, "gliHTEXT", 8);
    start(&g, 2, 2, 0, 0, 0, 0, V, H); pump(&g, 3);
    CHECK(names(&g, n, 16) == 3 && !strcmp(n[0], "alpha")); rig_close(&g);
    CHECK(rig_open(&g, 0) == 0); start(&g, 2, 0, 0, 0, 0, 0, V, H); pump(&g, 3);
    CHECK(dialog(&g) && dialog(&g)->list.count == 0); key(&g, 13, 0); CHECK(dialog(&g)); rig_close(&g);

    /* --- A guest file filter (TRUE hides): called once per candidate through the real CPU. */
    CHECK(rig_open(&g, 1) == 0); start(&g, 4, -1, FILTER, 0, 0, -4000, V, H); pump(&g, 12);
    CHECK(dialog(&g) && names(&g, n, 16) == 3 && !strcmp(n[0], "Beta") && !strcmp(n[1], "gamma") && !strcmp(n[2], "none"));
    rig_close(&g);

    /* --- Placement is clamped onto the screen. */
    CHECK(rig_open(&g, 0) == 0); start(&g, 2, -1, 0, 0, 0, 0, 400, 600); pump(&g, 3);
    CHECK(dialog(&g) && dialog(&g)->top == 480 - 208 && dialog(&g)->left == 640 - 348); rig_close(&g);

    /* --- Refusals halt with a named message before anything is drawn. */
    for (unsigned k = 0; k < 9; k++) {
        CHECK(rig_open(&g, 0) == 0);
        uint32_t hook = k == 1 ? 0x1234 : 0, fproc = k == 2 ? 0x1234 : 0; int num = k == 3 ? 5 : k == 4 ? -2 : -1;
        unsigned sel = k == 0 ? 1 : 4;
        if (k == 7) g.rt.screen_depth = 1;
        start(&g, sel, num, k == 8 ? 0x10101 : 0, hook, fproc, -4000, V, H);
        if (k == 5) wr(&g.rt, STACK - 34 + 8, 4, 0xfffff0);                    /* reply pointer beyond RAM */
        if (k == 6) wr(&g.rt, STACK - 34 + 26, 4, 0x1000010);                  /* prompt pointer beyond RAM */
        pump(&g, 4);
        if (!(mac_cpu_halted(g.rt.cpu) && !g.rt.exited && !dialog(&g) && screen_all_back(&g) && mac_cpu_get(g.rt.cpu, MAC_D1) != 5))
            fprintf(stderr, "refusal case %u: halted=%d exited=%d dialog=%d back=%d d1=%u\n", k, mac_cpu_halted(g.rt.cpu), g.rt.exited, dialog(&g) != NULL, screen_all_back(&g), mac_cpu_get(g.rt.cpu, MAC_D1));
        CHECK(mac_cpu_halted(g.rt.cpu) && !g.rt.exited && !dialog(&g) && screen_all_back(&g) && mac_cpu_get(g.rt.cpu, MAC_D1) != 5);
        rig_close(&g);
    }

    /* --- A second Pack3 call while a dialog is open halts. */
    CHECK(rig_open(&g, 0) == 0); start(&g, 2, -1, 0, 0, 0, 0, V, H); pump(&g, 3);
    mac_cpu_set(g.rt.cpu, MAC_A7, mac_cpu_get(g.rt.cpu, MAC_A7) - 2); pump(&g, 2);
    CHECK(mac_cpu_halted(g.rt.cpu)); rig_close(&g);

    if (!fails) puts("Standard File checks passed");
    return fails ? 1 : 0;
}
