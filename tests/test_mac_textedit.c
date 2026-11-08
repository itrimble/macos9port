/* TextEdit on a real colour port with a hand-built font: record fields, line breaking, drawing, selection, caret, clicks, keys. */
#include "mac_text.h"
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
/* Characters 32..90. Every glyph is a 3x3 box with a hole in the middle, 4 pixels wide, except the space: blank and 2 wide. ascent 2, descent 1, leading 1. */
static uint8_t strike[400];
static size_t make_strike(void) {
    memset(strike, 0, sizeof strike);
    p16(strike, 0x9000); p16(strike + 2, 32); p16(strike + 4, 90); p16(strike + 6, 4); p16(strike + 12, 4); p16(strike + 14, 3);
    p16(strike + 16, 102); p16(strike + 18, 2); p16(strike + 20, 1); p16(strike + 22, 1); p16(strike + 24, 12);
    for (unsigned g = 1; g < 60; g++) {                        /* glyph g occupies columns 3g..3g+2 */
        for (unsigned row = 0; row < 3; row++) for (unsigned c = 0; c < 3; c++) {
            int ink = (row != 1) || (c != 1); unsigned col = 3 * g + c;
            if (ink) strike[26 + row * 24 + col / 8] |= (uint8_t)(0x80u >> (col % 8));
        }
    }
    for (unsigned i = 0; i < 61; i++) p16(strike + 98 + 2 * i, 3 * i);
    for (unsigned i = 0; i < 61; i++) p16(strike + 220 + 2 * i, i == 0 ? 0x0002 : i == 60 ? 0xffff : 0x0004);
    return 342;
}
enum { SPT = 0x9000, PORT = 0x6000, PMH = 0x6200, PM = 0x6100, PIX = 0x7000, W = 40, H = 24, REG1H = 0x6300, REG1 = 0x6310, REG2H = 0x6320, REG2 = 0x6330,
       DEST = 0x5000, VIEW = 0x5010, UPD = 0x5020, TEXT = 0x5100 };
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
    memset(mac_cpu_ram(rt.cpu) + PIX, 50, W * H);
    wrw(PORT + 2, 4, PMH); wrw(PMH, 4, PM); wrw(PM, 4, PIX); wrw(PM + 4, 2, 0x8000 | W);
    rect(PM + 6, 0, 0, H, W); wrw(PM + 32, 2, 8); wrw(PM + 34, 2, 1); wrw(PM + 36, 2, 8);
    wrw(PORT + 6, 2, 0xc000); rect(PORT + 16, 0, 0, H, W); wrw(PORT + 24, 4, REG1H); wrw(PORT + 28, 4, REG2H);
    for (unsigned k = 0; k < 2; k++) { uint32_t hh = k ? REG2H : REG1H, rr = k ? REG2 : REG1; wrw(hh, 4, rr); wrw(rr, 2, 10); rect(rr + 2, 0, 0, H, W); }
    wrw(PORT + 66, 2, 0); wrw(PORT + 68, 2, 5); wrw(PORT + 70, 1, 0); wrw(PORT + 72, 2, 1); wrw(PORT + 74, 2, 10); wrw(PORT + 80, 4, 200); wrw(PORT + 84, 4, 7);
    rect(DEST, 3, 2, 21, 14); rect(VIEW, 3, 2, 21, 14);        /* dest: top 3, left 2, width 12 */
    mac_cpu_set(rt.cpu, MAC_A7, SPT);
}
static uint32_t te_new(void) { wrw(SPT, 4, VIEW); wrw(SPT + 4, 4, DEST); wrw(SPT + 8, 4, 0); mac_cpu_set(rt.cpu, MAC_A7, SPT); if (mac_textedit_service(&rt, 0xa9d2) != 1) return 0; return rdw(SPT + 8, 4); }
static int call(uint16_t op) { return mac_textedit_service(&rt, op); }
static int set_text(uint32_t h, const char *s, int n) { memcpy(mac_cpu_ram(rt.cpu) + TEXT, s, (size_t)n); wrw(SPT, 4, h); wrw(SPT + 4, 4, (uint32_t)n); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(0xa9cf); }
static int hcall(uint16_t op, uint32_t h) { wrw(SPT, 4, h); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(op); }
static int select_range(uint32_t h, long a, long b) { wrw(SPT, 4, h); wrw(SPT + 4, 4, (uint32_t)b); wrw(SPT + 8, 4, (uint32_t)a); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(0xa9d1); }
static int update(uint32_t h, int t, int l, int b, int r) { rect(UPD, t, l, b, r); wrw(SPT, 4, h); wrw(SPT + 4, 4, UPD); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(0xa9d3); }
static int key(uint32_t h, unsigned k) { wrw(SPT, 4, h); wrw(SPT + 4, 2, k << 8); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(0xa9dc); }
static int click(uint32_t h, int v, int hh, int extend) { wrw(SPT, 4, h); wrw(SPT + 4, 2, extend ? 0x0100 : 0); wrw(SPT + 6, 2, (uint16_t)v); wrw(SPT + 8, 2, (uint16_t)hh); mac_cpu_set(rt.cpu, MAC_A7, SPT); return call(0xa9d4); }
static int get_point(uint32_t h, int off, int *v, int *hh) {
    wrw(SPT, 2, 8); wrw(SPT + 2, 4, h); wrw(SPT + 6, 2, (uint16_t)off); wrw(SPT + 8, 4, 0xdeadbeef); mac_cpu_set(rt.cpu, MAC_A7, SPT);
    int rc = call(0xa83d); *v = (int16_t)rdw(SPT + 8, 2); *hh = (int16_t)rdw(SPT + 10, 2); return rc;
}
static uint32_t rec(uint32_t h) { return rdw(h, 4); }
static uint32_t host_rec(uint32_t h) { for (size_t i = 0; i < rt.handle_count; i++) if (rt.handles[i].handle == h) return rt.handles[i].pointer; return 0; }
static int fld(uint32_t h, unsigned off) { return (int16_t)rdw(rec(h) + off, 2); }
static int starts(uint32_t h, int k) { return (int16_t)rdw(rec(h) + 96 + 2 * (unsigned)k, 2); }
static void expect_lines(uint32_t h, int n, const int *s) {
    CHECK(fld(h, 94) == n);
    for (int k = 0; k <= n; k++) CHECK(starts(h, k) == s[k]);
}

static void basics(void) {
    rig(); uint32_t h = te_new();
    CHECK(h && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8 && !mac_cpu_halted(rt.cpu));
    CHECK(fld(h, 0) == 3 && fld(h, 2) == 2 && fld(h, 4) == 21 && fld(h, 6) == 14 && fld(h, 8) == 3 && fld(h, 10) == 2 && fld(h, 12) == 21 && fld(h, 14) == 14);
    CHECK(fld(h, 24) == 4 && fld(h, 26) == 2 && fld(h, 58) == 0 && fld(h, 60) == 0 && fld(h, 94) == 0 && fld(h, 72) == 1 && fld(h, 36) == 0);
    CHECK(fld(h, 74) == 5 && fld(h, 80) == 10 && fld(h, 78) == 1 && rdw(rec(h) + 82, 4) == PORT && fld(h, 32) == 0 && fld(h, 34) == 0 && starts(h, 0) == 0);
    uint32_t th = rdw(rec(h) + 62, 4); CHECK(th && th != h);
    CHECK(hcall(0xa9cb, h) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4 && rdw(SPT + 4, 4) == th);        /* TEGetText: the result above the argument */
    /* TESetJust, TEInit, TEIdle with nothing active. */
    wrw(SPT, 4, h); wrw(SPT + 4, 2, 0xffff); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa9df) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 6 && fld(h, 58) == -1);
    mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa9cc) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT);
    CHECK(hcall(0xa9da, h) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4);
    CHECK(call(0xa9c8) == 0 && call(0xa9e1) == 0 && call(0xa9d0) == 0);          /* TECalText and other calls are not claimed here */
    mac_cpu_destroy(rt.cpu);
}
static void wrap(void) {
    static const struct { const char *t; int n; int lines; int s[6]; int cr; } c[] = {
        {"AAA BBB CCC", 11, 3, {0, 4, 8, 11}, 1}, {"AAAAAAA", 7, 3, {0, 3, 6, 7}, 1}, {"AB\rCD", 5, 2, {0, 3, 5}, 1}, {"AB\r", 3, 2, {0, 3, 3}, 1},
        {"AAAAAAAAAAAAAAA", 15, 1, {0, 15}, -1}, {"AB\rCD\rE", 7, 3, {0, 3, 6, 7}, -1}, {"", 0, 0, {0}, 1}, {"A", 1, 1, {0, 1}, 1},
        {"AAA B", 5, 2, {0, 4, 5}, 1}, {"AA  AA", 6, 2, {0, 4, 6}, 1}, {"A BBBB", 6, 3, {0, 2, 5, 6}, 1}};
    for (unsigned k = 0; k < sizeof c / sizeof *c; k++) {
        rig(); uint32_t h = te_new(); wrw(rec(h) + 72, 2, (uint16_t)c[k].cr);
        CHECK(set_text(h, c[k].t, c[k].n) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 12 && !mac_cpu_halted(rt.cpu));
        CHECK(fld(h, 60) == c[k].n); expect_lines(h, c[k].lines, c[k].s);
        uint32_t th = rdw(rec(h) + 62, 4), tp = rdw(th, 4); CHECK(!memcmp(mac_cpu_ram(rt.cpu) + tp, c[k].t, (size_t)c[k].n));
        MacHandle *m = NULL; for (size_t i = 0; i < rt.handle_count; i++) if (rt.handles[i].handle == h) m = &rt.handles[i];
        CHECK(m && m->size == (uint32_t)(96 + 2 * (c[k].lines + 1)));            /* the record's handle tracks the line table */
        mac_cpu_destroy(rt.cpu);
    }
    /* A shorter text after a longer one shrinks the table; the selection is clamped. */
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11); select_range(h, 5, 9); set_text(h, "AB", 2);
    CHECK(fld(h, 94) == 1 && fld(h, 32) == 2 && fld(h, 34) == 2); mac_cpu_destroy(rt.cpu);
}
static void selection(void) {
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11);
    CHECK(select_range(h, 2, 6) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 12 && fld(h, 32) == 2 && fld(h, 34) == 6);
    select_range(h, 9, 3); CHECK(fld(h, 32) == 3 && fld(h, 34) == 9);                  /* reversed: ordered */
    select_range(h, -5, 99); CHECK(fld(h, 32) == 0 && fld(h, 34) == 11);               /* clamped */
    mac_cpu_destroy(rt.cpu);
}
static void drawing(void) {
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11);
    CHECK(update(h, 0, 0, 24, 40) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8);
    /* line 0 "AAA" at x 2.., rows 3..5; line 1 "BBB" at rows 7..9; line 2 "CCC" at rows 11..13: each glyph a 3x3 ring. */
    CHECK(px(2, 3) == 200 && px(3, 3) == 200 && px(4, 3) == 200 && px(2, 4) == 200 && px(3, 4) == 50 && px(4, 4) == 200 && px(4, 5) == 200);
    CHECK(px(6, 3) == 200 && px(8, 3) == 200 && px(7, 4) == 50 && px(10, 3) == 200 && px(12, 5) == 200 && px(13, 5) == 50);   /* second and third A, and nothing beyond 12 */
    CHECK(px(2, 7) == 200 && px(3, 8) == 50 && px(2, 11) == 200 && px(10, 13) == 200 && px(2, 15) == 50 && px(2, 6) == 50);
    mac_cpu_destroy(rt.cpu);
    /* An update rectangle limits the lines drawn; the view limits the pixels. */
    rig(); h = te_new(); set_text(h, "AAA BBB CCC", 11); update(h, 7, 0, 10, 40);
    CHECK(px(2, 3) == 50 && px(2, 7) == 200 && px(2, 11) == 50);
    mac_cpu_destroy(rt.cpu);
    rig(); h = te_new(); set_text(h, "AAA BBB CCC", 11); rect(rec(h) + 8, 3, 2, 21, 6); update(h, 0, 0, 24, 40);       /* view right edge at 6: columns 2..5 only */
    CHECK(px(2, 3) == 200 && px(4, 3) == 200 && px(5, 3) == 50 && px(6, 3) == 50 && px(7, 3) == 50 && px(10, 3) == 50);
    mac_cpu_destroy(rt.cpu);
    /* Justification: centre and right place a line by its width. "AA" is 8 wide in a 12-wide box. */
    rig(); h = te_new(); set_text(h, "AA", 2); wrw(rec(h) + 58, 2, 1); update(h, 0, 0, 24, 40);
    CHECK(px(4, 3) == 200 && px(3, 3) == 50 && px(2, 3) == 50 && px(8, 3) == 200);
    mac_cpu_destroy(rt.cpu);
    rig(); h = te_new(); set_text(h, "AA", 2); wrw(rec(h) + 58, 2, 0xffff); update(h, 0, 0, 24, 40);
    CHECK(px(6, 3) == 200 && px(5, 3) == 50 && px(10, 3) == 200);
    mac_cpu_destroy(rt.cpu);
    /* The TE's own font and mode are used: srcCopy fills each glyph's cell in the background colour. */
    rig(); h = te_new(); set_text(h, "A", 1); wrw(rec(h) + 78, 2, 0); update(h, 0, 0, 24, 40);
    CHECK(px(3, 4) == 7 && px(5, 4) == 7 && px(2, 3) == 200 && px(6, 3) == 50);
    mac_cpu_destroy(rt.cpu);
}
static void marks(void) {
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11);
    update(h, 0, 0, 24, 40); unsigned before[W * H]; for (int i = 0; i < W * H; i++) before[i] = mac_cpu_ram(rt.cpu)[PIX + i];
    select_range(h, 0, 2);
    CHECK(px(2, 3) == 200 && fld(h, 36) == 0);                                          /* not active: nothing is highlighted */
    CHECK(hcall(0xa9d8, h) == 1 && fld(h, 36) == 1);
    for (int y = 3; y < 7; y++) for (int x = 2; x < 10; x++) CHECK(px(x, y) == (before[y * W + x] ^ 255));
    CHECK(px(10, 3) == before[3 * W + 10] && px(2, 7) == before[7 * W + 2] && px(2, 2) == before[2 * W + 2]);
    CHECK(hcall(0xa9d8, h) == 1);                                                       /* activating twice does not invert twice */
    CHECK(px(2, 3) == (before[3 * W + 2] ^ 255));
    CHECK(hcall(0xa9d9, h) == 1 && fld(h, 36) == 0);
    for (int i = 0; i < W * H; i++) CHECK(mac_cpu_ram(rt.cpu)[PIX + i] == before[i]);
    /* Changing the selection while active moves the highlight. */
    hcall(0xa9d8, h); select_range(h, 4, 6);
    CHECK(px(2, 3) == before[3 * W + 2] && px(2, 7) == (before[7 * W + 2] ^ 255) && px(9, 10) == (before[10 * W + 9] ^ 255) && px(10, 8) == before[8 * W + 10] && px(5, 8) == (before[8 * W + 5] ^ 255));
    hcall(0xa9d9, h);
    for (int i = 0; i < W * H; i++) CHECK(mac_cpu_ram(rt.cpu)[PIX + i] == before[i]);
    /* Caret: an empty selection blinks with TEIdle every 20 ticks, at the left edge of the character at selStart. */
    select_range(h, 2, 2); hcall(0xa9d8, h);
    rt.ticks = 5; hcall(0xa9da, h); CHECK(fld(h, 56) == 0 && px(10, 3) == before[3 * W + 10]);   /* too soon */
    rt.ticks = 30; hcall(0xa9da, h); CHECK(fld(h, 56) == 1);
    for (int y = 3; y < 7; y++) CHECK(px(10, y) == (before[y * W + 10] ^ 255));        /* x = 2 + 2*4, rows 3..6 */
    CHECK(px(9, 3) == before[3 * W + 9] && px(11, 3) == before[3 * W + 11]);
    rt.ticks = 40; hcall(0xa9da, h); CHECK(fld(h, 56) == 1);                            /* only 10 ticks since the last toggle */
    rt.ticks = 51; hcall(0xa9da, h); CHECK(fld(h, 56) == 0 && px(10, 3) == before[3 * W + 10]);
    rt.ticks = 80; hcall(0xa9da, h); CHECK(fld(h, 56) == 1);
    hcall(0xa9d9, h); CHECK(fld(h, 56) == 0 && fld(h, 36) == 0);                        /* deactivating removes a showing caret */
    for (int i = 0; i < W * H; i++) CHECK(mac_cpu_ram(rt.cpu)[PIX + i] == before[i]);
    mac_cpu_destroy(rt.cpu);
}
static void clicks_and_keys(void) {
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11);
    CHECK(click(h, 4, 7, 0) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 10 && fld(h, 32) == 1 && fld(h, 34) == 1);             /* nearest boundary: 6 */
    click(h, 4, 9, 0); CHECK(fld(h, 32) == 2);
    click(h, 4, 100, 0); CHECK(fld(h, 32) == 4);                                        /* past the end of line 0 ("AAA " is 4 long) */
    click(h, 8, 2, 0); CHECK(fld(h, 32) == 4);                                          /* line 1, left of everything */
    click(h, 8, 7, 0); CHECK(fld(h, 32) == 5);
    click(h, 99, 3, 0); CHECK(fld(h, 32) == 8);                                         /* below the text: the last line */
    click(h, -9, 3, 0); CHECK(fld(h, 32) == 0);                                         /* above: the first */
    click(h, 4, 9, 0); click(h, 4, 100, 1); CHECK(fld(h, 32) == 2 && fld(h, 34) == 4);  /* shift-click extends */
    click(h, 4, 2, 1); CHECK(fld(h, 32) == 0 && fld(h, 34) == 4);
    mac_cpu_destroy(rt.cpu);
    /* Typing. */
    rig(); h = te_new();
    CHECK(key(h, 'A') == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 6 && fld(h, 60) == 1 && fld(h, 32) == 1 && fld(h, 34) == 1);
    key(h, 'B'); key(h, 'C'); CHECK(fld(h, 60) == 3 && fld(h, 32) == 3);
    uint32_t tp = rdw(rdw(rec(h) + 62, 4), 4); CHECK(!memcmp(mac_cpu_ram(rt.cpu) + tp, "ABC", 3));
    key(h, 8); CHECK(fld(h, 60) == 2 && fld(h, 32) == 2 && !memcmp(mac_cpu_ram(rt.cpu) + rdw(rdw(rec(h) + 62, 4), 4), "AB", 2));
    key(h, 28); CHECK(fld(h, 32) == 1 && fld(h, 34) == 1);                              /* left */
    key(h, 'X'); CHECK(fld(h, 60) == 3 && fld(h, 32) == 2 && !memcmp(mac_cpu_ram(rt.cpu) + rdw(rdw(rec(h) + 62, 4), 4), "AXB", 3));   /* inserted in the middle */
    key(h, 29); CHECK(fld(h, 32) == 3); key(h, 29); CHECK(fld(h, 32) == 3);             /* right, stopping at the end */
    key(h, 28); key(h, 28); key(h, 28); key(h, 28); CHECK(fld(h, 32) == 0);
    key(h, 8); CHECK(fld(h, 60) == 3 && fld(h, 32) == 0);                               /* backspace at the start does nothing */
    select_range(h, 0, 2); key(h, 'Q'); CHECK(fld(h, 60) == 2 && !memcmp(mac_cpu_ram(rt.cpu) + rdw(rdw(rec(h) + 62, 4), 4), "QB", 2) && fld(h, 32) == 1);   /* a selection is replaced */
    select_range(h, 0, 2); key(h, 8); CHECK(fld(h, 60) == 0 && fld(h, 32) == 0);        /* and deleted by backspace */
    key(h, 'A'); key(h, 13); key(h, 'B'); CHECK(fld(h, 94) == 2 && starts(h, 1) == 2 && starts(h, 2) == 3);                  /* a return starts a new line */
    mac_cpu_destroy(rt.cpu);
    /* Typing redraws from the edited line: the new character appears; text above is left alone. */
    rig(); h = te_new(); hcall(0xa9d8, h); key(h, 'A'); CHECK(px(2, 3) == 200 && px(3, 4) == 7 && px(4, 5) == 200 && px(15, 3) == 50 && px(2, 2) == 50);   /* the edited line is erased to the background (7), then redrawn */
    mac_cpu_destroy(rt.cpu);
}
static void dispatch(void) {
    rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11); int v, hh;
    CHECK(get_point(h, 0, &v, &hh) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8 && v == 7 && hh == 2);
    get_point(h, 2, &v, &hh); CHECK(v == 7 && hh == 10);
    get_point(h, 3, &v, &hh); CHECK(v == 7 && hh == 14);                                /* the hanging space at the end of line 0 */
    get_point(h, 4, &v, &hh); CHECK(v == 11 && hh == 2);
    get_point(h, 5, &v, &hh); CHECK(v == 11 && hh == 6);
    get_point(h, 11, &v, &hh); CHECK(v == 15 && hh == 14);
    get_point(h, 99, &v, &hh); CHECK(v == 15 && hh == 14);
    get_point(h, -4, &v, &hh); CHECK(v == 7 && hh == 2);
    wrw(rec(h) + 58, 2, 1); set_text(h, "AA", 2); get_point(h, 1, &v, &hh); CHECK(v == 7 && hh == 4 + 4);                       /* centred: "AA" starts at 4 */
    set_text(h, "AB\r", 3); get_point(h, 3, &v, &hh); CHECK(v == 11);                  /* the empty last line after a CR */
    wrw(SPT, 2, 7); wrw(SPT + 2, 4, h); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa83d) == -1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
}
static void disposal_and_refusals(void) {
    rig(); uint32_t h = te_new(), th = rdw(rec(h) + 62, 4); set_text(h, "AAA", 3);
    CHECK(hcall(0xa9cd, h) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4);
    for (size_t i = 0; i < rt.handle_count; i++) CHECK(rt.handles[i].handle != h && rt.handles[i].handle != th);
    CHECK(hcall(0xa9d8, h) == -1 && mac_cpu_halted(rt.cpu));                           /* a disposed TEHandle is refused */
    mac_cpu_destroy(rt.cpu);
    for (unsigned k = 0; k < 14; k++) {
        rig(); uint32_t th2 = te_new(); set_text(th2, "AAA BBB", 7); int rc = 0; uint16_t op = 0xa9d8; uint32_t arg = th2;
        if (k == 0) arg = 0x1234;                                      /* not a handle of ours */
        if (k == 1) arg = 0;
        if (k == 2) wrw(rec(th2) + 38, 4, 0x1000);                     /* a wordBreak hook */
        if (k == 3) wrw(rec(th2) + 42, 4, 0x1000);                     /* a clikLoop hook */
        if (k == 4) wrw(rec(th2) + 74, 2, 99);                         /* a font the guest lacks */
        if (k == 5) { mac_cpu_set(rt.cpu, MAC_A7, SPT + 1); rc = call(0xa9d8); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }
        if (k == 6) { op = 0xa9cf; wrw(SPT, 4, th2); wrw(SPT + 4, 4, 40000); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT); rc = call(op); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }
        if (k == 7) { op = 0xa9cf; wrw(SPT, 4, th2); wrw(SPT + 4, 4, 8); wrw(SPT + 8, 4, 0xfffffc); mac_cpu_set(rt.cpu, MAC_A7, SPT); rc = call(op); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }
        if (k == 8) { rt.current_port = 0; wrw(SPT, 4, VIEW); wrw(SPT + 4, 4, DEST); mac_cpu_set(rt.cpu, MAC_A7, SPT); rc = call(0xa9d2); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }
        if (k == 9) { wrw(PORT + 68, 2, 99); wrw(SPT, 4, VIEW); wrw(SPT + 4, 4, DEST); mac_cpu_set(rt.cpu, MAC_A7, SPT); rc = call(0xa9d2); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }
        if (k == 10) { rc = key(th2, 1); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }        /* an unsupported control key */
        if (k == 11) { rc = key(th2, 30); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }       /* up arrow */
        if (k == 12) { wrw(PORT + 6, 2, 0); rect(PORT + 16, 0, 0, 0, 0); hcall(0xa9d8, th2); select_range(th2, 0, 2); CHECK(mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu); continue; }  /* highlighting on a basic port */
        if (k == 13) { mac_cpu_halt(rt.cpu, "stopped"); CHECK(hcall(0xa9d8, th2) == -1); mac_cpu_destroy(rt.cpu); continue; }
        if (op == 0xa9d8) { wrw(SPT, 4, arg); mac_cpu_set(rt.cpu, MAC_A7, SPT); rc = call(op); if (k >= 2 && k <= 4) { rc = update(arg, 0, 0, 24, 40); } }
        CHECK(rc == -1 && mac_cpu_halted(rt.cpu));
        mac_cpu_destroy(rt.cpu);
    }
}

static void edge_cases(void) {
    /* A box narrower than one glyph: every character gets its own line. */
    rig(); uint32_t h = te_new(); rect(rec(h) + 0, 3, 2, 21, 4); set_text(h, "AAA", 3);
    CHECK(!mac_cpu_halted(rt.cpu) && fld(h, 94) == 3 && starts(h, 0) == 0 && starts(h, 1) == 1 && starts(h, 2) == 2 && starts(h, 3) == 3);
    mac_cpu_destroy(rt.cpu);
    /* The 4000-line cap: 3999 returns make 4000 lines (counting the empty last one); 4000 make 4001 and are refused. */
    for (unsigned k = 0; k < 2; k++) {
        rig(); h = te_new(); memset(mac_cpu_ram(rt.cpu) + TEXT, 13, 4000);
        wrw(SPT, 4, h); wrw(SPT + 4, 4, k ? 4000 : 3999); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT);
        int rc = call(0xa9cf);
        if (k == 0) CHECK(rc == 1 && !mac_cpu_halted(rt.cpu) && fld(h, 94) == 4000); else CHECK(rc == -1 && mac_cpu_halted(rt.cpu));
        mac_cpu_destroy(rt.cpu);
    }
    /* The same cap without a trailing return: 4000 returns then one more character is a 4001st line. */
    rig(); h = te_new(); memset(mac_cpu_ram(rt.cpu) + TEXT, 13, 4000); mac_cpu_ram(rt.cpu)[TEXT + 4000] = 'A';
    wrw(SPT, 4, h); wrw(SPT + 4, 4, 4001); wrw(SPT + 8, 4, TEXT);
    mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa9cf) == -1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    /* Text longer than 32000 characters is refused even when it would lay out fine (spaces never break a line). */
    rig(); h = te_new(); memset(mac_cpu_ram(rt.cpu) + TEXT, ' ', 40000);
    wrw(SPT, 4, h); wrw(SPT + 4, 4, 40000); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa9cf) == -1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    /* Exactly 32000 is accepted, and typing one more is ignored. */
    rig(); h = te_new(); memset(mac_cpu_ram(rt.cpu) + TEXT, ' ', 32000);
    wrw(SPT, 4, h); wrw(SPT + 4, 4, 32000); wrw(SPT + 8, 4, TEXT); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(call(0xa9cf) == 1 && fld(h, 60) == 32000);
    select_range(h, 32000, 32000); CHECK(key(h, 'A') == 1 && fld(h, 60) == 32000 && !mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    /* A selection that spans lines is highlighted line by line, each only over its own part. */
    rig(); h = te_new(); set_text(h, "AAA BBB CCC", 11); rect(rec(h) + 8, 3, 2, 21, 40); update(h, 0, 0, 24, 40); unsigned before[W * H]; for (int i = 0; i < W * H; i++) before[i] = mac_cpu_ram(rt.cpu)[PIX + i];   /* a wide view, so only the line limits show */
    select_range(h, 2, 9); hcall(0xa9d8, h);
    CHECK(px(10, 3) == (before[3 * W + 10] ^ 255) && px(15, 6) == (before[6 * W + 15] ^ 255) && px(9, 3) == before[3 * W + 9] && px(16, 3) == before[3 * W + 16]);   /* line 0: characters 2 and 3 only (x 10 to 15) */
    CHECK(px(2, 7) == (before[7 * W + 2] ^ 255) && px(15, 10) == (before[10 * W + 15] ^ 255) && px(16, 7) == before[7 * W + 16]);                                  /* line 1: all of it (x 2 to 15) */
    CHECK(px(5, 11) == (before[11 * W + 5] ^ 255) && px(6, 11) == before[11 * W + 6] && px(2, 15) == before[15 * W + 2]);                                          /* line 2: one character */
    hcall(0xa9d9, h); for (int i = 0; i < W * H; i++) CHECK(mac_cpu_ram(rt.cpu)[PIX + i] == before[i]);
    /* Arrows with a selection collapse to its ends; clicking just above the first line selects offset 0. */
    select_range(h, 2, 4); key(h, 28); CHECK(fld(h, 32) == 2 && fld(h, 34) == 2);
    select_range(h, 2, 4); key(h, 29); CHECK(fld(h, 32) == 4 && fld(h, 34) == 4);
    click(h, 99, 20, 0); click(h, -1, 3, 0); CHECK(fld(h, 32) == 0);
    mac_cpu_destroy(rt.cpu);
    /* Typing in a later line leaves the lines above untouched (no erase there). */
    rig(); h = te_new(); set_text(h, "AAA BBB", 7); update(h, 0, 0, 24, 40); CHECK(px(3, 4) == 50);
    select_range(h, 7, 7); key(h, 'C'); CHECK(px(3, 4) == 50 && px(3, 8) == 7);                  /* line 0's hole stays 50; line 1 was erased to the background */
    mac_cpu_destroy(rt.cpu);
    /* TENew copies the port's face as well as its font, size and mode. */
    rig(); wrw(PORT + 70, 1, 1); h = te_new(); CHECK(rdw(rec(h) + 76, 1) == 1);
    mac_cpu_destroy(rt.cpu);
    /* A handle that exists but is not a TEHandle, and records whose pointers are bad, are refused. */
    rig(); mac_cpu_set(rt.cpu, MAC_D0, 200); CHECK(mac_memory_service(&rt, 0xa122) == 1); uint32_t other = mac_cpu_get(rt.cpu, MAC_A0);
    CHECK(other && hcall(0xa9d8, other) == -1 && mac_cpu_halted(rt.cpu)); mac_cpu_destroy(rt.cpu);
    rig(); h = te_new(); wrw(h, 4, 0xfffff0); CHECK(hcall(0xa9d8, h) == 1 && !mac_cpu_halted(rt.cpu) && rdw(host_rec(h) + 36, 2) == 1); mac_cpu_destroy(rt.cpu);   /* the host uses its own table, not the guest's master pointer */
    rig(); h = te_new(); set_text(h, "AAA", 3); wrw(rdw(rec(h) + 62, 4), 4, 0x7ffffff0); CHECK(update(h, 0, 0, 24, 40) == 1 && !mac_cpu_halted(rt.cpu) && px(2, 3) == 200); mac_cpu_destroy(rt.cpu);   /* text is read from the table's pointer, so it still draws */
}

/* The TERec is guest memory, so the guest can corrupt it. Every corruption must end in a named halt, never in a host overflow
 * (these run under ASan). op 0: TEKey, 1: TESetText, 2: TEUpdate, 3: TEClick, 4: TEActivate. */
static void corruption(void) {
    for (unsigned k = 0; k < 12; k++) for (unsigned op = 0; op < 5; op++) {
        rig(); uint32_t h = te_new(); set_text(h, "AAA BBB", 7); uint32_t r = rec(h), th = rdw(r + 62, 4);
        if (k == 0) wrw(r + 32, 2, 0xffff);                                 /* selStart -1 */
        if (k == 1) wrw(r + 34, 2, 30000);                                  /* selEnd far beyond the text */
        if (k == 2) { wrw(r + 32, 2, 6); wrw(r + 34, 2, 2); }               /* selStart > selEnd */
        if (k == 3) wrw(r + 60, 2, 32100);                                  /* teLength above the cap */
        if (k == 4) wrw(r + 60, 2, 0xfff0);                                 /* teLength negative */
        if (k == 5) wrw(r + 94, 2, 30000);                                  /* nLines far beyond the record */
        if (k == 6) wrw(r + 94, 2, 0xffff);                                 /* nLines negative */
        if (k == 7) wrw(r + 62, 4, 0);                                      /* no text handle */
        if (k == 8) wrw(r + 62, 4, h);                                      /* the record's own handle as its text */
        if (k == 9) { mac_cpu_set(rt.cpu, MAC_D0, 100); mac_memory_service(&rt, 0xa122); wrw(r + 62, 4, mac_cpu_get(rt.cpu, MAC_A0)); }   /* a plain handle that is not ours */
        if (k == 10) wrw(r + 60, 2, 400);                                   /* teLength larger than the text handle */
        if (k == 11) { wrw(r + 32, 2, 3); wrw(r + 34, 2, 4); wrw(th, 4, 0x7ffffff0); }   /* master pointer of the text moved out of RAM */
        int rc = 0; (void)th;
        if (op == 0) rc = key(h, 'X');
        else if (op == 1) rc = set_text(h, "ZZ", 2);
        else if (op == 2) rc = update(h, 0, 0, 24, 40);
        else if (op == 3) rc = click(h, 4, 7, 0);
        else { rc = hcall(0xa9d8, h); }
        if (k == 11) CHECK(rc == 1 || (rc == -1 && mac_cpu_halted(rt.cpu)));   /* the host pointer is used: a moved master pointer is the guest's own problem */
        else CHECK(rc == -1 && mac_cpu_halted(rt.cpu));
        mac_cpu_destroy(rt.cpu);
    }
}
/* A guest can call SetHandleSize on a TEHandle. A record shrunk below its header, or below what its line table needs, is refused. */
static void shrunk_record(void) {
    for (unsigned k = 0; k < 2; k++) for (unsigned op = 0; op < 3; op++) {
        rig(); uint32_t h = te_new(); set_text(h, "AAA BBB CCC", 11);                 /* three lines: the table needs 96 + 8 bytes */
        mac_cpu_set(rt.cpu, MAC_D0, k ? 98 : 10); mac_cpu_set(rt.cpu, MAC_A0, h); CHECK(mac_memory_service(&rt, 0xa024) == 1);
        int rc = op == 0 ? update(h, 0, 0, 24, 40) : op == 1 ? key(h, 'X') : hcall(0xa9d8, h);
        CHECK(rc == -1 && mac_cpu_halted(rt.cpu));
        mac_cpu_destroy(rt.cpu);
    }
}
int main(void) {
    size_t sn = make_strike(); Res list[] = {{FONT_T, 5 * 128 + 10, strike, sn}};
    char err[200]; size_t total = build_fork(img, list, 1);
    if (mac_resource_open(&fork, img, total, err, sizeof err)) { fprintf(stderr, "fork: %s\n", err); return 1; }
    basics(); wrap(); edge_cases(); corruption(); shrunk_record(); selection(); drawing(); marks(); clicks_and_keys(); dispatch(); disposal_and_refusals();
    if (!fails) puts("TextEdit checks passed");
    return fails ? 1 : 0;
}
