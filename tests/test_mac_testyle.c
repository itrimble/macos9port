/* Multistyled TextEdit: TEStyleNew, the style records, TESetStyle, TEReplaceStyle, TEGetStyle, TEContinuousStyle, TEStyleInsert, TEUseStyleScrap, TEGetStyleScrapHandle, TENumStyles,
 * per-run font, size and colour in the lines and in the line-height table, typing, disposal and the refusals. Selector numbers and record layouts are Apple's public ones. */
#include "mac_te_int.h"
#include "mac_handles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);fails++;}}while(0)
#define FONT_T 0x464f4e54u
typedef struct { uint32_t type; int16_t id; const uint8_t *data; size_t size; } Res;
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }
static size_t build_fork(uint8_t *out, const Res *list, unsigned n) {
    size_t at = 256, data_len = 0; uint32_t off[8];
    for (unsigned i = 0; i < n; i++) { off[i] = (uint32_t)data_len; p32(out + at + data_len, (uint32_t)list[i].size); memcpy(out + at + data_len + 4, list[i].data, list[i].size); data_len += 4 + list[i].size; }
    size_t map = at + data_len, tl = map + 28, refs = tl + 2 + 8, len = (refs - map) + 12 * n + 2;
    memset(out + map, 0, len);
    p32(out, 256); p32(out + 4, (uint32_t)map); p32(out + 8, (uint32_t)data_len); p32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); p16(out + map + 24, 28); p16(out + map + 26, (unsigned)(len - 2)); p16(out + tl, 0);
    p32(out + tl + 2, FONT_T); p16(out + tl + 6, n - 1); p16(out + tl + 8, (unsigned)(refs - tl));
    for (unsigned i = 0; i < n; i++) { uint8_t *r = out + refs + 12 * i; p16(r, (uint16_t)list[i].id); p16(r + 2, 0xffff); p32(r + 4, off[i]); }
    return map + len;
}
/* Characters 32..90, each glyph a 3x3 box with a hole, 4 pixels wide. Strike A: ascent 2, descent 1, leading 1 (height 4). Strike B: ascent 4, descent 2, leading 0 (height 6). */
static uint8_t strike_a[400], strike_b[400];
static size_t make_strike(uint8_t *s, unsigned asc, unsigned desc, unsigned lead) {
    memset(s, 0, 400);
    p16(s, 0x9000); p16(s + 2, 32); p16(s + 4, 90); p16(s + 6, 4); p16(s + 12, 4); p16(s + 14, 3);
    p16(s + 16, 102); p16(s + 18, asc); p16(s + 20, desc); p16(s + 22, lead); p16(s + 24, 12);
    for (unsigned g = 1; g < 60; g++) for (unsigned row = 0; row < 3; row++) for (unsigned c = 0; c < 3; c++) {
        int ink = (row != 1) || (c != 1); unsigned col = 3 * g + c;
        if (ink) s[26 + row * 24 + col / 8] |= (uint8_t)(0x80u >> (col % 8));
    }
    for (unsigned i = 0; i < 61; i++) p16(s + 98 + 2 * i, 3 * i);
    for (unsigned i = 0; i < 61; i++) p16(s + 220 + 2 * i, i == 0 ? 0x0002 : i == 60 ? 0xffff : 0x0004);
    return 342;
}
enum { SPT = 0x9000, PORT = 0x6000, PMH = 0x6200, PM = 0x6100, PIX = 0x7000, W = 40, H = 40, REG1H = 0x6300, REG1 = 0x6310, REG2H = 0x6320, REG2 = 0x6330,
       DEST = 0x5000, VIEW = 0x5010, UPD = 0x5020, TEXT = 0x5100, TS = 0x5200, TS2 = 0x5220, SCRAP = 0x5300 };
static MacRuntime rt;
static uint32_t rdw(uint32_t p, unsigned n) { return mac_cpu_read(rt.cpu, p, n); }
static void wrw(uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt.cpu, p, n, v); }
static unsigned px(int x, int y) { return mac_cpu_ram(rt.cpu)[PIX + y * W + x]; }
static void rect(uint32_t p, int t, int l, int b, int r) { wrw(p, 2, (uint16_t)t); wrw(p + 2, 2, (uint16_t)l); wrw(p + 4, 2, (uint16_t)b); wrw(p + 6, 2, (uint16_t)r); }
static MacResourceFork fork;
static uint8_t img[4096];
static void rig(void) {
    memset(&rt, 0, sizeof rt); rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); rt.fork = fork; rt.current_port = PORT;
    rt.heap_next = 0x100000; rt.heap_limit = 0x200000;
    for (unsigned i = 0; i < 256; i++) rt.palette[i] = 0xffffff;
    rt.palette[0] = 0x000000; rt.palette[1] = 0xff0000; rt.palette[200] = 0x000000;
    memset(mac_cpu_ram(rt.cpu) + PIX, 50, W * H);
    wrw(PORT + 2, 4, PMH); wrw(PMH, 4, PM); wrw(PM, 4, PIX); wrw(PM + 4, 2, 0x8000 | W);
    rect(PM + 6, 0, 0, H, W); wrw(PM + 32, 2, 8); wrw(PM + 34, 2, 1); wrw(PM + 36, 2, 8);
    wrw(PORT + 6, 2, 0xc000); rect(PORT + 16, 0, 0, H, W); wrw(PORT + 24, 4, REG1H); wrw(PORT + 28, 4, REG2H);
    for (unsigned k = 0; k < 2; k++) { uint32_t hh = k ? REG2H : REG1H, rr = k ? REG2 : REG1; wrw(hh, 4, rr); wrw(rr, 2, 10); rect(rr + 2, 0, 0, H, W); }
    wrw(PORT + 66, 2, 0); wrw(PORT + 68, 2, 5); wrw(PORT + 70, 1, 0); wrw(PORT + 72, 2, 1); wrw(PORT + 74, 2, 10); wrw(PORT + 80, 4, 200); wrw(PORT + 84, 4, 7);
    rect(DEST, 3, 2, 37, 14); rect(VIEW, 3, 2, 37, 14);
    mac_cpu_set(rt.cpu, MAC_A7, SPT);
}
static int svc(uint16_t op) { return mac_textedit_service(&rt, op); }
static uint32_t style_new(void) { wrw(SPT, 4, VIEW); wrw(SPT + 4, 4, DEST); wrw(SPT + 8, 4, 0); mac_cpu_set(rt.cpu, MAC_A7, SPT); if (svc(0xa83e) != 1) return 0; return rdw(SPT + 8, 4); }
static uint32_t rec(uint32_t h) { return rdw(h, 4); }
static int fld(uint32_t h, unsigned off) { return (int16_t)rdw(rec(h) + off, 2); }
static int set_text(uint32_t h, const char *s, int n) { memcpy(mac_cpu_ram(rt.cpu) + TEXT, s, (size_t)n); wrw(SPT, 4, h); wrw(SPT + 4, 4, (uint32_t)n); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT); return svc(0xa9cf); }
static int select_range(uint32_t h, long a, long b) { wrw(SPT, 4, h); wrw(SPT + 4, 4, (uint32_t)b); wrw(SPT + 8, 4, (uint32_t)a); mac_cpu_set(rt.cpu, MAC_A7, SPT); return svc(0xa9d1); }
static int update(uint32_t h) { rect(UPD, 0, 0, H, W); wrw(SPT, 4, h); wrw(SPT + 4, 4, UPD); mac_cpu_set(rt.cpu, MAC_A7, SPT); return svc(0xa9d3); }
static int key(uint32_t h, unsigned k) { wrw(SPT, 4, h); wrw(SPT + 4, 2, k << 8); mac_cpu_set(rt.cpu, MAC_A7, SPT); return svc(0xa9dc); }
static void text_style(uint32_t at, int font, unsigned face, int size, unsigned r, unsigned g, unsigned b) {
    wrw(at, 2, (uint16_t)font); wrw(at + 2, 1, face); wrw(at + 3, 1, 0); wrw(at + 4, 2, (uint16_t)size); wrw(at + 6, 2, r); wrw(at + 8, 2, g); wrw(at + 10, 2, b);
}
static int disp(unsigned sel) { wrw(SPT, 2, sel); mac_cpu_set(rt.cpu, MAC_A7, SPT); return svc(0xa83d); }
static int set_style(uint32_t h, int mode, uint32_t ts, int redraw) { wrw(SPT + 2, 4, h); wrw(SPT + 6, 2, redraw ? 0x0100 : 0); wrw(SPT + 8, 4, ts); wrw(SPT + 12, 2, (uint16_t)mode); return disp(1); }
static int replace_style(uint32_t h, int mode, uint32_t old, uint32_t nw, int redraw) { wrw(SPT + 2, 4, h); wrw(SPT + 6, 2, redraw ? 0x0100 : 0); wrw(SPT + 8, 4, nw); wrw(SPT + 12, 4, old); wrw(SPT + 16, 2, (uint16_t)mode); return disp(2); }
static int cont_style(uint32_t h, int *mode, uint32_t ts) { wrw(TS2 + 0x40, 2, (uint16_t)*mode); wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, ts); wrw(SPT + 10, 4, TS2 + 0x40); wrw(SPT + 14, 2, 0x5555);
    int rc = disp(10); *mode = (int16_t)rdw(TS2 + 0x40, 2); return rc == 1 ? (int)(rdw(SPT + 14, 1)) : -1; }
static int get_style(uint32_t h, int off, uint32_t ts, int *lh, int *asc) { wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, TS2 + 0x50); wrw(SPT + 10, 4, TS2 + 0x52); wrw(SPT + 14, 4, ts); wrw(SPT + 18, 2, (uint16_t)off);
    int rc = disp(3); *asc = (int16_t)rdw(TS2 + 0x50, 2); *lh = (int16_t)rdw(TS2 + 0x52, 2); return rc; }
static uint32_t style_rec(uint32_t h) { return rdw(rec(h) + 74, 4); }
static uint32_t derefh(uint32_t h) { return rdw(h, 4); }
static int nruns(uint32_t h) { return (int16_t)rdw(derefh(style_rec(h)), 2); }
static int nstyles(uint32_t h) { return (int16_t)rdw(derefh(style_rec(h)) + 2, 2); }
static int run_start(uint32_t h, int i) { return (int16_t)rdw(derefh(style_rec(h)) + 20 + 4u * (unsigned)i, 2); }
static uint32_t st_elt(uint32_t h, int run) { uint32_t p = derefh(style_rec(h)); int idx = (int16_t)rdw(p + 20 + 4u * (unsigned)run + 2, 2); return derefh(rdw(p + 4, 4)) + 18u * (unsigned)idx; }
static int lh_of(uint32_t h, int k) { return (int16_t)rdw(derefh(rdw(derefh(style_rec(h)) + 8, 4)) + 4u * (unsigned)k, 2); }
static int sty_calls(void) { return 0; }

static void basics(void) {
    rig(); uint32_t h = style_new(); (void)sty_calls;
    CHECK(h && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8 && !mac_cpu_halted(rt.cpu));
    CHECK(fld(h, 80) == -1 && fld(h, 24) == -1 && fld(h, 26) == -1 && style_rec(h) && nruns(h) == 1 && nstyles(h) == 1);
    uint32_t e = st_elt(h, 0); CHECK((int16_t)rdw(e, 2) == 1 && (int16_t)rdw(e + 2, 2) == 4 && (int16_t)rdw(e + 4, 2) == 2 && (int16_t)rdw(e + 6, 2) == 5 && (int16_t)rdw(e + 10, 2) == 10);
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0xdead); CHECK(disp(4) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 6 && rdw(SPT + 6, 4) == style_rec(h));   /* TEGetStyleHandle */
    mac_cpu_destroy(rt.cpu);
}
static void styles(void) {
    rig(); uint32_t h = style_new(); int mode, lh, asc;
    CHECK(set_text(h, "AA\rBB\rCC", 8) == 1 && fld(h, 94) == 3 && lh_of(h, 0) == 4 && lh_of(h, 1) == 4);
    select_range(h, 3, 5); text_style(TS, 5, 0, 14, 0, 0, 0);
    CHECK(set_style(h, 4, TS, 1) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 14 && nruns(h) == 3 && nstyles(h) == 2);                       /* TESetStyle(doSize) splits the run */
    CHECK(run_start(h, 0) == 0 && run_start(h, 1) == 3 && run_start(h, 2) == 5 && (int16_t)rdw(st_elt(h, 1) + 10, 2) == 14 && (int16_t)rdw(st_elt(h, 1) + 2, 2) == 6);
    CHECK(lh_of(h, 0) == 4 && lh_of(h, 1) == 6 && lh_of(h, 2) == 4);                                                                         /* the line-height table follows the runs */
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0); wrw(SPT + 10, 4, 3); wrw(SPT + 14, 4, 0xdead0000u); CHECK(disp(9) == 1 && rdw(SPT + 14, 4) == 14);   /* TEGetHeight(3 lines): 4 + 6 + 4 */
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 2, 3); wrw(SPT + 8, 4, 0xdeadbeef); CHECK(disp(8) == 1 && (int16_t)rdw(SPT + 8, 2) == 3 + 4 + 6);        /* TEGetPoint: the bottom of line 1 */
    CHECK(get_style(h, 3, TS2, &lh, &asc) == 1 && (int16_t)rdw(TS2, 2) == 5 && (int16_t)rdw(TS2 + 4, 2) == 14 && lh == 6 && asc == 4);        /* TEGetStyle */
    select_range(h, 0, 8); mode = 15; CHECK(cont_style(h, &mode, TS2) == 0 && !(mode & 4) && (mode & 1));                                                            /* the whole text: size is not continuous, font is */
    select_range(h, 3, 5); mode = 4 | 1; CHECK(cont_style(h, &mode, TS2) == 1 && mode == 5 && (int16_t)rdw(TS2 + 4, 2) == 14 && (int16_t)rdw(TS2, 2) == 5);
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 8); wrw(SPT + 10, 4, 0); wrw(SPT + 14, 4, 0xdead); CHECK(disp(13) == 1 && rdw(SPT + 14, 4) == 3);          /* TENumStyles */
    select_range(h, 0, 8); text_style(TS, 5, 0, 10, 0, 0, 0); text_style(TS2, 5, 0, 14, 0, 0, 0);                                           /* TEReplaceStyle: 14 -> 10 everywhere */
    CHECK(replace_style(h, 4, TS2, TS, 1) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 18 && nruns(h) == 1 && nstyles(h) == 1 && lh_of(h, 1) == 4);
    select_range(h, 0, 8); text_style(TS, 5, 2, 10, 0, 0, 0);                                                                                  /* doFace with doToggle: adds bold, then removes it */
    CHECK(set_style(h, 2 | 32, TS, 0) == 1 && (rdw(st_elt(h, 0) + 8, 1) & 2) == 2);
    CHECK(set_style(h, 2 | 32, TS, 0) == 1 && (rdw(st_elt(h, 0) + 8, 1) & 2) == 0 && nruns(h) == 1);
    text_style(TS, 5, 0, 4, 0, 0, 0); CHECK(set_style(h, 16, TS, 0) == 1 && (int16_t)rdw(st_elt(h, 0) + 10, 2) == 14);                       /* addSize: 10 + 4 */
    mac_cpu_destroy(rt.cpu);
}
static void drawing(void) {
    rig(); uint32_t h = style_new(); set_text(h, "AA\rBB", 5);
    select_range(h, 3, 5); text_style(TS, 5, 0, 14, 0xffff, 0, 0);
    CHECK(set_style(h, 4 | 8, TS, 1) == 1 && lh_of(h, 1) == 6);                                                                                /* size 14 and red for the second line; redraw TRUE recalculates the lines */
    CHECK(update(h) == 1 && !mac_cpu_halted(rt.cpu));
    int dark0 = 0, red = 0;
    for (int y = 3; y < 7; y++) for (int x = 2; x < 10; x++) if (px(x, y) == 0) dark0++;                                                    /* line 0: 4 pixels high, in the port's own colour */
    for (int y = 7; y < 13; y++) for (int x = 2; x < 10; x++) if (px(x, y) == 1) red++;                                                       /* line 1: 6 pixels high, red (nearest palette index 1) */
    CHECK(dark0 > 8 && red > 8);
    for (int y = 7; y < 13; y++) for (int x = 2; x < 10; x++) CHECK(px(x, y) != 0);
    /* Typing takes the style before the caret; attributes set at an insertion point wait in the null scrap for the next character. */
    select_range(h, 5, 5); CHECK(key(h, 'C') == 1 && fld(h, 60) == 6 && nruns(h) == 2);
    text_style(TS, 5, 0, 10, 0, 0, 0); CHECK(set_style(h, 4, TS, 0) == 1 && nruns(h) == 2);                                                   /* insertion point: only the null scrap changes */
    CHECK(key(h, 'D') == 1 && fld(h, 60) == 7 && nruns(h) == 3 && run_start(h, 2) == 6);
    CHECK(select_range(h, 1, 1) == 1 && key(h, 'E') == 1 && nruns(h) == 3);                                                                   /* a moved selection drops the null style */
    mac_cpu_destroy(rt.cpu);
}
/* A style scrap in a real handle: count, then per element start (long), height, ascent, font, face, size, colour (20 bytes each). */
static uint32_t make_scrap(unsigned n, const int *start, const int *size) {
    uint32_t h = mac_handle_new(&rt, 2 + 20 * n), p = rdw(h, 4);
    wrw(p, 2, n);
    for (unsigned i = 0; i < n; i++) { uint32_t e = p + 2 + 20 * i; wrw(e, 4, (uint32_t)start[i]); wrw(e + 4, 2, 4); wrw(e + 6, 2, 2); wrw(e + 8, 2, 5); wrw(e + 10, 1, 0); wrw(e + 11, 1, 0); wrw(e + 12, 2, (uint16_t)size[i]); }
    return h;
}
static void insert_and_scrap(void) {
    rig(); uint32_t h = style_new(); set_text(h, "ZZ", 2);
    const int starts[2] = {0, 3}, sizes[2] = {10, 14};                                                                                          /* "abc" in the base style then "def" in size 14 */
    uint32_t scrap = make_scrap(2, starts, sizes);
    memcpy(mac_cpu_ram(rt.cpu) + TEXT, "abcdef", 6); select_range(h, 0, 0);
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, scrap); wrw(SPT + 10, 4, 6); wrw(SPT + 14, 4, TEXT);
    CHECK(disp(7) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 18 && fld(h, 60) == 8 && nruns(h) == 3 && run_start(h, 1) == 3 && run_start(h, 2) == 6 && (int16_t)rdw(st_elt(h, 1) + 10, 2) == 14);
    CHECK(fld(h, 32) == 0 && fld(h, 34) == 0);                                                                                                 /* the selection is untouched */
    uint32_t th = rdw(rec(h) + 62, 4); CHECK(!memcmp(mac_cpu_ram(rt.cpu) + rdw(th, 4), "abcdefZZ", 8));
    select_range(h, 0, 6);                                                                                                                      /* TEGetStyleScrapHandle: two runs */
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0xdead); CHECK(disp(6) == 1); uint32_t sh = rdw(SPT + 6, 4), sp = rdw(sh, 4);
    CHECK((int16_t)rdw(sp, 2) == 2 && rdw(sp + 2, 4) == 0 && rdw(sp + 22, 4) == 3 && (int16_t)rdw(sp + 2 + 20 + 12, 2) == 14);
    /* TEUseStyleScrap: the scrap's style (size 14) over 6..8 */
    const int s1[1] = {0}, z1[1] = {14}; uint32_t one = make_scrap(1, s1, z1);
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 2, 0); wrw(SPT + 8, 4, one); wrw(SPT + 12, 4, 8); wrw(SPT + 16, 4, 6);
    CHECK(disp(11) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 20 && nruns(h) == 2 && (int16_t)rdw(st_elt(h, 1) + 10, 2) == 14);
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0); wrw(SPT + 10, 4, 6); wrw(SPT + 14, 4, TEXT); CHECK(disp(7) == 1);                                    /* a nil style scrap: the text takes the style before it */
    wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0x1234); wrw(SPT + 10, 4, 6); wrw(SPT + 14, 4, TEXT); CHECK(disp(7) == -1 && mac_cpu_halted(rt.cpu));       /* a scrap that is not a handle */
    mac_cpu_destroy(rt.cpu);
}
static void lifecycle(void) {
    rig(); uint32_t h = style_new(), sr = style_rec(h), th = rdw(rec(h) + 62, 4);
    wrw(SPT, 4, h); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(svc(0xa9cd) == 1);
    for (size_t i = 0; i < rt.handle_count; i++) CHECK(rt.handles[i].handle != h && rt.handles[i].handle != th && rt.handles[i].handle != sr);   /* the style records go with it */
    mac_cpu_destroy(rt.cpu);
    rig(); h = style_new(); CHECK(disp(0) == -1 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "selector 0")); mac_cpu_destroy(rt.cpu);   /* TEStylePaste is not implemented */
    rig(); h = style_new(); wrw(rec(h) + 74, 4, 0x1234); wrw(SPT + 2, 4, h); wrw(SPT + 6, 4, 0); CHECK(disp(4) == 1);                                    /* the handle is only read back */
    set_text(h, "A", 1); CHECK(update(h) == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu);                                                    /* a forged style handle is refused, not followed */
    rig(); uint32_t m = 0; wrw(SPT, 4, VIEW); wrw(SPT + 4, 4, DEST); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(svc(0xa9d2) == 1); m = rdw(SPT + 8, 4);   /* a monostyled record: style selectors with no effect or refused */
    set_text(m, "A", 1); text_style(TS, 5, 0, 14, 0, 0, 0); CHECK(set_style(m, 4, TS, 1) == 1 && fld(m, 80) == 10);
    wrw(SPT + 2, 4, m); wrw(SPT + 6, 4, 0); CHECK(disp(4) == -1); mac_cpu_destroy(rt.cpu);
    (void)h;
}
int main(void) {
    make_strike(strike_a, 2, 1, 1); make_strike(strike_b, 4, 2, 0);
    Res list[] = {{FONT_T, 5 * 128 + 10, strike_a, 342}, {FONT_T, 5 * 128 + 14, strike_b, 342}};
    char err[200]; size_t total = build_fork(img, list, 2);
    CHECK(mac_resource_open(&fork, img, total, err, sizeof err) == 0);
    basics(); styles(); drawing(); insert_and_scrap(); lifecycle();
    if (!fails) puts("Multistyled TextEdit checks passed");
    return fails ? 1 : 0;
}
