/* List Manager (Pack0, $A9E7): LNew and LDispose, rows, columns and cell data, selection, search, scrolling and the scroll bar, clicks, drawing by the standard definition, a custom LDEF
 * running as guest code, and the refusals. Selector numbers and argument orders are the ones the implementation documents (not verified against a reference machine). */
#include "mac_runtime.h"
#include "mac_controls.h"
#include "mac_window_int.h"
#include "mac_resource.h"
#include "mac_ppc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#include "fork_builder.h"
#define SP 0x8000u
#define PC 0x5002u
#define STORAGE 0x700000u
#define DAT 0x4400u
static uint8_t fork_bytes[FB_BYTES];
static FbFork fork_def;
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { CHECK(!mac_cpu_write(r->cpu, p, n, v)); }
static int trap_hook(MacCpu *cpu, uint16_t op, uint32_t pc, void *u) {
    MacRuntime *rt = u; (void)pc;
    if (op == 0xa0fe && rt->guest.active) return mac_guest_return(rt);
    mac_cpu_halt(cpu, "unexpected trap in the List Manager test"); return -1;
}
static void put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
/* A hand-assembled LDEF: logs the message ($1000), row ($1002), column ($1004), select ($1006), data offset ($100A), data length ($100C) and counts calls ($1008);
 * Pascal: it pops its 20 bytes of arguments. */
static const uint8_t ldef_code[] = { 0x20,0x5f, 0x31,0xef,0x00,0x12,0x10,0x00, 0x31,0xef,0x00,0x08,0x10,0x02, 0x31,0xef,0x00,0x0a,0x10,0x04, 0x11,0xef,0x00,0x10,0x10,0x06,
    0x31,0xef,0x00,0x06,0x10,0x0a, 0x31,0xef,0x00,0x04,0x10,0x0c, 0x52,0x78,0x10,0x08, 0xde,0xfc,0x00,0x14, 0x4e,0xd0 };
static MacRuntime rig(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(rt.cpu);
    rt.heap_next = 0x100000; rt.heap_limit = 0x600000; rt.qd_globals = 0x7000; rt.modifiers = 0x80;
    CHECK(mac_color_init(&rt) == 1); mac_cpu_set(rt.cpu, MAC_A7, SP); CHECK(mac_toolbox_service(&rt, 0xa912) == 1);
    mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, 0x12345678); wr(&rt, SP + 4, 2, 0x01aa);
    wr(&rt, SP + 6, 4, 0xffffffff); wr(&rt, SP + 10, 2, 12); wr(&rt, SP + 12, 2, 0x0100);
    wr(&rt, SP + 14, 4, 0x4001); wr(&rt, SP + 18, 4, 0x4100); wr(&rt, SP + 22, 4, STORAGE);
    memcpy(mac_cpu_ram(rt.cpu) + 0x4001, "\004Test", 5);
    wr(&rt, 0x4100, 2, 40); wr(&rt, 0x4102, 2, 50); wr(&rt, 0x4104, 2, 300); wr(&rt, 0x4106, 2, 450);
    CHECK(mac_window_service(&rt, 0xaa45) == 1);
    CHECK(!mac_resource_open(&rt.fork, fork_bytes, fb_build(&fork_def, fork_bytes), NULL, 0));
    wr(&rt, 0x27f0, 2, 0xa0fe); wr(&rt, PC, 2, 0x60fe); mac_cpu_set_trap(rt.cpu, trap_hook, &rt);
    return rt;
}
/* Pascal call: `spec` lists the arguments in declaration order ('w' word, 'l' long, 'b' boolean); the result slot follows them. Returns the service's code; *res is the result. */
static int call(MacRuntime *rt, unsigned sel, const char *spec, unsigned resbytes, uint32_t *res, ...) {
    unsigned total = 0; for (const char *s = spec; *s; s++) total += *s == 'l' ? 4 : 2;
    wr(rt, SP, 2, sel); unsigned off = 2 + total; va_list ap; va_start(ap, res);
    for (const char *s = spec; *s; s++) {
        uint32_t v = va_arg(ap, uint32_t); unsigned n = *s == 'l' ? 4 : 2; off -= n;
        if (*s == 'b') { wr(rt, SP + off, 1, v ? 1 : 0); wr(rt, SP + off + 1, 1, 0); } else wr(rt, SP + off, n, v);
    }
    va_end(ap);
    wr(rt, SP + 2 + total, 4, 0xdeadbeef);
    mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_set(rt->cpu, MAC_PC, PC);
    int rc = mac_list_service(rt, 0xa9e7);
    if (rt->guest.active) mac_cpu_run(rt->cpu, 20000);
    if (rc == 1 && (rt->guest.active || mac_cpu_get(rt->cpu, MAC_A7) != SP + 2 + total)) fprintf(stderr, "selector $%04x: guest=%d A7=%x expected %x\n", sel, rt->guest.active, mac_cpu_get(rt->cpu, MAC_A7), SP + 2 + total);
    if (rc == 1) { CHECK(!rt->guest.active && mac_cpu_get(rt->cpu, MAC_A7) == SP + 2 + total); if (res) *res = resbytes ? rd(rt, SP + 2 + total, resbytes) : 0; }
    return rc;
}
static uint32_t cell(int row, int col) { return ((uint32_t)(uint16_t)row << 16) | (uint16_t)col; }
static void pstr(MacRuntime *rt, uint32_t at, const char *s) { memcpy(mac_cpu_ram(rt->cpu) + at, s, strlen(s)); }
static uint32_t make(MacRuntime *rt, unsigned proc, int cols, int vert) {
    wr(rt, 0x4300, 2, 60); wr(rt, 0x4302, 2, 60); wr(rt, 0x4304, 2, 108); wr(rt, 0x4306, 2, 160);   /* rView: four 12-pixel rows of 100 */
    wr(rt, 0x4310, 2, 0); wr(rt, 0x4312, 2, 0); wr(rt, 0x4314, 2, 0); wr(rt, 0x4316, 2, (uint16_t)cols);   /* dataBounds: no rows yet */
    uint32_t h = 0; CHECK(call(rt, 0x44, "lllwlbbbb", 4, &h, 0x4300u, 0x4310u, (12u << 16) | 100u, proc, STORAGE, 1u, 0u, 0u, (unsigned)vert) == 1 && h);
    return h;
}
static uint32_t rec(MacRuntime *rt, uint32_t h) { return rd(rt, h, 4); }
static int add_rows(MacRuntime *rt, uint32_t h, int count, int after) { uint32_t r = 0xffff; CHECK(call(rt, 0x08, "wwl", 2, &r, (unsigned)count, (unsigned)after, h) == 1); return (int16_t)r; }
static void set(MacRuntime *rt, uint32_t h, int row, int col, const char *s) {
    pstr(rt, DAT + 0x100, s); CHECK(call(rt, 0x58, "lwll", 0, NULL, DAT + 0x100, (unsigned)strlen(s), cell(row, col), h) == 1);
}
static int get(MacRuntime *rt, uint32_t h, int row, int col, char *out) {
    wr(rt, DAT + 0x200, 2, 64); CHECK(call(rt, 0x38, "llll", 0, NULL, DAT + 0x210, DAT + 0x200, cell(row, col), h) == 1);
    unsigned n = rd(rt, DAT + 0x200, 2); memcpy(out, mac_cpu_ram(rt->cpu) + DAT + 0x210, n); out[n] = 0; return (int)n;
}
static int sum(MacRuntime *rt, int l, int t, int r, int b) { unsigned s = 0; l += 50; r += 50; t += 40; b += 40;   /* local to global: the window's content starts at (50, 40) */ const uint8_t *m = mac_cpu_ram(rt->cpu); for (int y = t; y < b; y++) for (int x = l; x < r; x++) s = s * 31 + m[rt->screen_base + (uint32_t)y * rt->screen_row_bytes + (uint32_t)x] + 1; return (int)s; }
static int bnd(MacRuntime *rt, uint32_t h, int i) { return (int16_t)rd(rt, rec(rt, h) + 72 + 2u * (unsigned)i, 2); }
static int vis(MacRuntime *rt, uint32_t h, int i) { return (int16_t)rd(rt, rec(rt, h) + 20 + 2u * (unsigned)i, 2); }

static void standard(void) {
    MacRuntime rt = rig(); uint32_t h = make(&rt, 0, 1, 1), r = rec(&rt, h), res; char buf[80];
    CHECK((int16_t)rd(&rt, r, 2) == 60 && (int16_t)rd(&rt, r + 6, 2) == 160 && rd(&rt, r + 8, 4) == STORAGE && rd(&rt, r + 37, 1) == 1 && rd(&rt, r + 32, 4) == 0 && rd(&rt, r + 28, 4));
    uint32_t vs = rd(&rt, r + 28, 4), vp = rd(&rt, vs, 4);
    CHECK((int16_t)rd(&rt, vp + CTL_RECT, 2) == 59 && (int16_t)rd(&rt, vp + CTL_RECT + 2, 2) == 160 && (int16_t)rd(&rt, vp + CTL_RECT + 4, 2) == 109 && (int16_t)rd(&rt, vp + CTL_RECT + 6, 2) == 176);   /* outside the view, on its right */
    CHECK(rd(&rt, vp + CTL_HILITE, 1) == 255);                                   /* nothing to scroll yet */
    CHECK(add_rows(&rt, h, 10, 32767) == 0 && bnd(&rt, h, 2) == 10 && bnd(&rt, h, 3) == 1);   /* appended: the first new row is row 0 */
    CHECK((int16_t)rd(&rt, vp + CTL_MAX, 2) == 6 && rd(&rt, vp + CTL_HILITE, 1) == 0 && vis(&rt, h, 2) == 4);   /* four rows fit, ten exist */
    for (int i = 0; i < 10; i++) { char t[8]; snprintf(t, sizeof t, "Row%d", i); set(&rt, h, i, 0, t); }
    CHECK(get(&rt, h, 3, 0, buf) == 4 && !strcmp(buf, "Row3"));
    pstr(&rt, DAT + 0x100, "++"); CHECK(call(&rt, 0x0c, "lwll", 0, NULL, DAT + 0x100, 2u, cell(3, 0), h) == 1);                   /* LAddToCell */
    CHECK(get(&rt, h, 3, 0, buf) == 6 && !strcmp(buf, "Row3++"));
    CHECK(call(&rt, 0x1c, "ll", 0, NULL, cell(3, 0), h) == 1 && get(&rt, h, 3, 0, buf) == 0);                                    /* LClrCell */
    set(&rt, h, 3, 0, "Three");
    CHECK(add_rows(&rt, h, 2, 3) == 3 && bnd(&rt, h, 2) == 12);                                                                    /* two rows inserted before row 3 */
    CHECK(get(&rt, h, 2, 0, buf) == 4 && !strcmp(buf, "Row2") && get(&rt, h, 3, 0, buf) == 0 && get(&rt, h, 5, 0, buf) == 5 && !strcmp(buf, "Three"));
    CHECK(call(&rt, 0x24, "wwl", 0, NULL, 3u, 3u, h) == 1 && bnd(&rt, h, 2) == 9 && get(&rt, h, 3, 0, buf) == 4 && !strcmp(buf, "Row4"));   /* LDelRow: the inserted pair and "Three" go */
    CHECK(call(&rt, 0x04, "wwl", 2, &res, 2u, 32767u, h) == 1 && (int16_t)res == 1 && bnd(&rt, h, 3) == 3);                         /* LAddColumn at the end */
    set(&rt, h, 0, 2, "col2"); CHECK(get(&rt, h, 0, 2, buf) == 4);
    CHECK(call(&rt, 0x20, "wwl", 0, NULL, 1u, 1u, h) == 1 && bnd(&rt, h, 3) == 2 && get(&rt, h, 0, 1, buf) == 4 && !strcmp(buf, "col2"));   /* LDelColumn */
    mac_cpu_destroy(rt.cpu);
}
static void selection(void) {
    MacRuntime rt = rig(); uint32_t h = make(&rt, 0, 1, 1), res;
    add_rows(&rt, h, 10, 32767); for (int i = 0; i < 10; i++) { char t[8]; snprintf(t, sizeof t, "Row%d", i); set(&rt, h, i, 0, t); }
    int before = sum(&rt, 60, 72, 160, 84);
    CHECK(call(&rt, 0x5c, "bll", 0, NULL, 1u, cell(1, 0), h) == 1 && sum(&rt, 60, 72, 160, 84) != before);                          /* the hilited row is drawn differently */
    CHECK(call(&rt, 0x5c, "bll", 0, NULL, 1u, cell(6, 0), h) == 1);
    wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 1u, DAT + 0x300, h) == 1 && (res >> 8) == 1 && rd(&rt, DAT + 0x300, 4) == cell(1, 0));   /* LGetSelect(next) finds row 1 */
    wr(&rt, DAT + 0x300, 4, cell(2, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 1u, DAT + 0x300, h) == 1 && rd(&rt, DAT + 0x300, 4) == cell(6, 0));
    wr(&rt, DAT + 0x300, 4, cell(7, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 1u, DAT + 0x300, h) == 1 && (res >> 8) == 0);
    wr(&rt, DAT + 0x300, 4, cell(1, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 1);        /* is row 1 selected: yes */
    wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 0);
    wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x48, "bbll", 2, &res, 0u, 1u, DAT + 0x300, h) == 1 && (res >> 8) == 1 && rd(&rt, DAT + 0x300, 4) == cell(1, 0));   /* LNextCell down */
    wr(&rt, DAT + 0x300, 4, cell(9, 0)); CHECK(call(&rt, 0x48, "bbll", 2, &res, 0u, 1u, DAT + 0x300, h) == 1 && (res >> 8) == 0);   /* ... none after the last row */
    pstr(&rt, DAT + 0x100, "Row7"); wr(&rt, DAT + 0x300, 4, cell(0, 0));
    CHECK(call(&rt, 0x54, "lwlll", 2, &res, DAT + 0x100, 4u, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 1 && rd(&rt, DAT + 0x300, 4) == cell(7, 0));       /* LSearch */
    pstr(&rt, DAT + 0x100, "Nope"); wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x54, "lwlll", 2, &res, DAT + 0x100, 4u, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 0);
    CHECK(call(&rt, 0x54, "lwlll", 2, &res, DAT + 0x100, 4u, 0x5000u, DAT + 0x300, h) == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu);   /* a search procedure of the program's own halts */
    rt = rig(); h = make(&rt, 0, 1, 1); add_rows(&rt, h, 10, 32767);
    CHECK(call(&rt, 0x4c, "lll", 0, NULL, DAT + 0x300, cell(2, 0), h) == 1 && (int16_t)rd(&rt, DAT + 0x300, 2) == 84 && (int16_t)rd(&rt, DAT + 0x302, 2) == 60 && (int16_t)rd(&rt, DAT + 0x304, 2) == 96 && (int16_t)rd(&rt, DAT + 0x306, 2) == 160);   /* LRect */
    CHECK(call(&rt, 0x4c, "lll", 0, NULL, DAT + 0x300, cell(8, 0), h) == 1 && rd(&rt, DAT + 0x300, 4) == 0 && rd(&rt, DAT + 0x304, 4) == 0);               /* not visible: empty */
    CHECK(call(&rt, 0x50, "wwl", 0, NULL, 0u, 3u, h) == 1 && vis(&rt, h, 0) == 3 && vis(&rt, h, 2) == 7);                                              /* LScroll(dCols 0, dRows 3) */
    uint32_t vp = rd(&rt, rec(&rt, h) + 28, 4); CHECK((int16_t)rd(&rt, rd(&rt, vp, 4) + CTL_VALUE, 2) == 3);                        /* the scroll bar follows */
    CHECK(call(&rt, 0x50, "wwl", 0, NULL, 0u, 99u, h) == 1 && vis(&rt, h, 0) == 6);                                                                    /* clamped to the end */
    CHECK(call(&rt, 0x4c, "lll", 0, NULL, DAT + 0x300, cell(8, 0), h) == 1 && (int16_t)rd(&rt, DAT + 0x300, 2) == 84);
    CHECK(call(&rt, 0x5c, "bll", 0, NULL, 1u, cell(0, 0), h) == 1 && call(&rt, 0x10, "l", 0, NULL, h) == 1 && vis(&rt, h, 0) == 0);                       /* LAutoScroll brings row 0 back */
    CHECK(call(&rt, 0x60, "wwl", 0, NULL, 100u, 24u, h) == 1 && vis(&rt, h, 2) == 2 && (int16_t)rd(&rt, rec(&rt, h) + 4, 2) == 84);                      /* LSize: two rows now */
    CHECK(call(&rt, 0x14, "ll", 0, NULL, (24u << 16) | 100u, h) == 1 && vis(&rt, h, 2) == 1 && (int16_t)rd(&rt, rec(&rt, h) + 16, 2) == 24);              /* LCellSize */
    mac_cpu_destroy(rt.cpu);
}
static void clicks(void) {
    MacRuntime rt = rig(); uint32_t h = make(&rt, 0, 1, 1), res, last;
    add_rows(&rt, h, 10, 32767); wr(&rt, 0x2f0, 4, 30);
    wr(&rt, 0x16a, 4, 100);
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (74u << 16) | 90u, 0u, h) == 1 && (res >> 8) == 0 && call(&rt, 0x40, "l", 4, &last, h) == 1 && last == cell(1, 0));
    wr(&rt, 0x16a, 4, 110); CHECK(call(&rt, 0x18, "lwl", 2, &res, (74u << 16) | 90u, 0u, h) == 1 && (res >> 8) == 1);                                    /* the second click in time: a double click */
    wr(&rt, 0x16a, 4, 200); CHECK(call(&rt, 0x18, "lwl", 2, &res, (74u << 16) | 90u, 0u, h) == 1 && (res >> 8) == 0);                                    /* too late */
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (98u << 16) | 90u, 0u, h) == 1 && call(&rt, 0x40, "l", 4, &last, h) == 1 && last == cell(3, 0));
    wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 1u, DAT + 0x300, h) == 1 && rd(&rt, DAT + 0x300, 4) == cell(3, 0));   /* the click replaced the selection */
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (62u << 16) | 90u, 0x0200u, h) == 1);                                                                           /* shift: rows 0..3 */
    wr(&rt, DAT + 0x300, 4, cell(0, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 1u, DAT + 0x300, h) == 1 && rd(&rt, DAT + 0x300, 4) == cell(0, 0));
    wr(&rt, DAT + 0x300, 4, cell(2, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 1);
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (74u << 16) | 90u, 0x0100u, h) == 1);                                                                           /* command: toggles row 1 */
    wr(&rt, DAT + 0x300, 4, cell(1, 0)); CHECK(call(&rt, 0x3c, "bll", 2, &res, 0u, DAT + 0x300, h) == 1 && (res >> 8) == 0);
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (64u << 16) | 168u, 0u, h) == 1 && vis(&rt, h, 0) == 0);                                                       /* scroll bar: up arrow region at the top is 20 */
    CHECK(call(&rt, 0x18, "lwl", 2, &res, (105u << 16) | 168u, 0u, h) == 1 && vis(&rt, h, 0) == 1);                                                       /* the down arrow scrolls one row */
    mac_cpu_destroy(rt.cpu);
}
static void custom(void) {
    MacRuntime rt = rig(); wr(&rt, 0x1008, 2, 0);
    uint32_t h = make(&rt, 1, 1, 0);
    CHECK(rd(&rt, 0x1008, 2) == 1 && rd(&rt, 0x1000, 2) == 0 && rd(&rt, rec(&rt, h) + 64, 4));                                                            /* lInitMsg ran the guest code */
    add_rows(&rt, h, 3, 32767); wr(&rt, 0x1008, 2, 0);
    set(&rt, h, 0, 0, "ab"); set(&rt, h, 1, 0, "cde");
    CHECK(rd(&rt, 0x1008, 2) == 2 && rd(&rt, 0x1000, 2) == 1 && rd(&rt, 0x1002, 2) == 1 && rd(&rt, 0x1004, 2) == 0 && rd(&rt, 0x100a, 2) == 2 && rd(&rt, 0x100c, 2) == 3);   /* drawMsg for (1,0): data at offset 2, 3 bytes */
    uint32_t cells = rd(&rt, rec(&rt, h) + 80, 4);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + rd(&rt, cells, 4), "abcde", 5));                                                                                  /* the guest copy of the data an LDEF reads */
    wr(&rt, 0x1008, 2, 0); CHECK(call(&rt, 0x5c, "bll", 0, NULL, 1u, cell(1, 0), h) == 1 && rd(&rt, 0x1008, 2) == 1 && rd(&rt, 0x1000, 2) == 2 && rd(&rt, 0x1006, 1) == 1);   /* lHiliteMsg, selected */
    wr(&rt, 0x1008, 2, 0); CHECK(call(&rt, 0x2c, "bl", 0, NULL, 0u, h) == 1 && call(&rt, 0x30, "ll", 0, NULL, cell(1, 0), h) == 1 && rd(&rt, 0x1008, 2) == 0);   /* drawing off: no message */
    CHECK(call(&rt, 0x2c, "bl", 0, NULL, 1u, h) == 1 && call(&rt, 0x30, "ll", 0, NULL, cell(1, 0), h) == 1 && rd(&rt, 0x1008, 2) == 1 && rd(&rt, 0x1000, 2) == 1);
    wr(&rt, 0x1008, 2, 0); CHECK(call(&rt, 0x28, "l", 0, NULL, h) == 1 && rd(&rt, 0x1008, 2) == 1 && rd(&rt, 0x1000, 2) == 3);                              /* lCloseMsg */
    CHECK(call(&rt, 0x40, "l", 4, NULL, h) == -1 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "live list"));                                  /* a disposed list is refused */
    mac_cpu_destroy(rt.cpu);
}
/* The PowerPC bridge rows (InterfaceLib exports): every List Manager name is a Pack0 row whose argument bytes match what the 68K service takes. */
static void ppc_rows(void) {
    static const struct { const char *name; unsigned selector, args; } want[] = { {"LNew", 0x44, 26}, {"LDispose", 0x28, 4}, {"LAddRow", 0x08, 8}, {"LSetCell", 0x58, 14}, {"LGetCell", 0x38, 16},
        {"LSetSelect", 0x5c, 10}, {"LClick", 0x18, 10}, {"LSearch", 0x54, 18}, {"LLastClick", 0x40, 4}, {"LRect", 0x4c, 12}, {"LNextCell", 0x48, 12}, {"LUpdate", 0x64, 8} };
    size_t n; const MacPpcSig *sigs = mac_ppc_sigs_managers(&n);
    for (unsigned k = 0; k < sizeof want / sizeof want[0]; k++) {
        const MacPpcSig *row = NULL; for (size_t i = 0; i < n; i++) if (!strcmp(sigs[i].name, want[k].name)) row = &sigs[i];
        CHECK(row && row->trap == 0xa9e7);
        const char *colon = strchr(row->sig, ':'), *slash = strchr(row->sig, '/'); unsigned bytes = 0;
        for (const char *c = colon + 1; c < slash; c++) bytes += *c == 'l' ? 4 : 2;
        CHECK(bytes == want[k].args && strtoul(slash + 2, NULL, 16) == want[k].selector && slash[1] == 'w');
    }
}
static void refusals(void) {
    MacRuntime rt = rig(); uint32_t h = make(&rt, 0, 1, 0);
    CHECK(call(&rt, 0x68, "l", 0, NULL, h) == -1 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "$0068")); mac_cpu_destroy(rt.cpu);
    rt = rig(); wr(&rt, 0x4300, 2, 60); wr(&rt, 0x4302, 2, 60); wr(&rt, 0x4304, 2, 108); wr(&rt, 0x4306, 2, 160); memset(mac_cpu_ram(rt.cpu) + 0x4310, 0, 8);
    CHECK(call(&rt, 0x44, "lllwlbbbb", 4, NULL, 0x4300u, 0x4310u, 0x000c0064u, 7u, STORAGE, 1u, 0u, 0u, 0u) == -1 && strstr(mac_cpu_error(rt.cpu), "no LDEF resource 7")); mac_cpu_destroy(rt.cpu);
    rt = rig(); CHECK(call(&rt, 0x44, "lllwlbbbb", 4, NULL, 0x4300u, 0x4310u, 0x000c0064u, 0u, 0x5000u, 1u, 0u, 0u, 0u) == -1 && strstr(mac_cpu_error(rt.cpu), "neither a registered window")); mac_cpu_destroy(rt.cpu);
    rt = rig(); CHECK(call(&rt, 0x18, "lwl", 2, NULL, 0u, 0u, 0x6000u) == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu);   /* not a list handle */
    CHECK(mac_list_service(&rt, 0xa9e8) == 0);
}
int main(void) {
    fb_add(&fork_def, 0x4c444546, 1, 0, ldef_code, sizeof ldef_code); (void)put16;
    standard(); selection(); clicks(); custom(); ppc_rows(); refusals();
    puts("List Manager checks passed");
    return 0;
}
