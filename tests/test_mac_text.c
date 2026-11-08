/* The font engine: strike parsing and lookup from a hand-built resource fork, then metrics and drawing on a real colour port. */
#include "mac_runtime.h"
#include "mac_font.h"
#include "mac_sysfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);fails++;}}while(0)
#define FONT_T 0x464f4e54u
#define NFNT_T 0x4e464e54u
#define FOND_T 0x464f4e44u
typedef struct { uint32_t type; int16_t id; const uint8_t *data; size_t size; } Res;
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }
static size_t build_fork(uint8_t *out, const Res *list, unsigned n) {
    size_t at = 256, data_len = 0; uint32_t off[16];
    for (unsigned i = 0; i < n; i++) { off[i] = (uint32_t)data_len; p32(out + at + data_len, (uint32_t)list[i].size); memcpy(out + at + data_len + 4, list[i].data, list[i].size); data_len += 4 + list[i].size; }
    size_t map = at + data_len; uint32_t types[16]; unsigned nt = 0;
    for (unsigned i = 0; i < n; i++) { unsigned k = 0; while (k < nt && types[k] != list[i].type) k++; if (k == nt) types[nt++] = list[i].type; }
    size_t tl = map + 28, refs = tl + 2 + 8 * nt, len = (refs - map) + 12 * n + 2;
    memset(out + map, 0, len);
    p32(out, 256); p32(out + 4, (uint32_t)map); p32(out + 8, (uint32_t)data_len); p32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); p16(out + map + 24, 28); p16(out + map + 26, (unsigned)(len - 2)); p16(out + tl, nt - 1);
    unsigned ref = 0;
    for (unsigned t = 0; t < nt; t++) {
        unsigned count = 0; for (unsigned i = 0; i < n; i++) count += list[i].type == types[t];
        p32(out + tl + 2 + 8 * t, types[t]); p16(out + tl + 6 + 8 * t, count - 1); p16(out + tl + 8 + 8 * t, (unsigned)(refs - tl + 12 * ref));
        for (unsigned i = 0; i < n; i++) if (list[i].type == types[t]) { uint8_t *r = out + refs + 12 * ref++; p16(r, (uint16_t)list[i].id); p16(r + 2, 0xffff); p32(r + 4, off[i]); }
    }
    return map + len;
}
/* A strike: characters 65 ('A') and 66 ('B') plus the missing glyph. ascent 2, descent 1, 3 rows, one word per row.
 * A (cols 0-2): #.# / ### / #.#   B (cols 3-4): .# / ## / #.   missing (cols 5-6): ## / ## / ##   advance widths 4, 3, 3. */
static uint8_t strike[64];
static size_t make_strike(void) {
    memset(strike, 0, sizeof strike);
    p16(strike, 0x9000); p16(strike + 2, 65); p16(strike + 4, 66); p16(strike + 6, 4); p16(strike + 8, 0); p16(strike + 10, 0);
    p16(strike + 12, 4); p16(strike + 14, 3); p16(strike + 16, 12); p16(strike + 18, 2); p16(strike + 20, 1); p16(strike + 22, 1); p16(strike + 24, 1);
    p16(strike + 26, 0xae00); p16(strike + 28, 0xfe00); p16(strike + 30, 0xb600);              /* the image rows */
    p16(strike + 32, 0); p16(strike + 34, 3); p16(strike + 36, 5); p16(strike + 38, 7);        /* location table */
    p16(strike + 40, 0x0004); p16(strike + 42, 0x0003); p16(strike + 44, 0x0003); p16(strike + 46, 0xffff); /* offset/width table */
    return 48;
}
static uint8_t fond[64];
static size_t make_fond(void) {                 /* family 5: size 9, style 0 -> NFNT 2000 */
    memset(fond, 0, sizeof fond); p16(fond + 2, 5); p16(fond + 52, 0); p16(fond + 54, 9); p16(fond + 56, 0); p16(fond + 58, 2000); return 60;
}
static uint8_t image[4096];

static void parse_checks(void) {
    size_t n = make_strike(); MacStrike s; const char *why;
    CHECK(mac_strike_parse(strike, n, &s, &why) == 0 && s.first == 65 && s.last == 66 && s.ascent == 2 && s.descent == 1 && s.rectH == 3 && s.widMax == 4 && s.leading == 1 && s.rowBytes == 2);
    MacGlyph g;
    mac_strike_glyph(&s, 'A', &g); CHECK(g.present && g.width == 4 && g.left == 0 && g.imgCol == 0 && g.imgWidth == 3);
    mac_strike_glyph(&s, 'B', &g); CHECK(g.width == 3 && g.imgCol == 3 && g.imgWidth == 2);
    mac_strike_glyph(&s, 'Z', &g); CHECK(g.present && g.width == 3 && g.imgCol == 5 && g.imgWidth == 2);     /* outside the range: the missing glyph */
    mac_strike_glyph(&s, 0, &g); CHECK(g.present && g.imgCol == 5);
    CHECK(mac_strike_bit(&s, 0, 0) == 1 && mac_strike_bit(&s, 1, 0) == 0 && mac_strike_bit(&s, 2, 0) == 1 && mac_strike_bit(&s, 1, 1) == 1);
    CHECK(mac_strike_bit(&s, 0, 3) == 0 && mac_strike_bit(&s, -1, 0) == 0 && mac_strike_bit(&s, 99, 0) == 0);  /* outside the image */
    /* A glyph marked missing falls back to the missing glyph; with no missing glyph at all there is nothing. */
    uint8_t t[64]; memcpy(t, strike, 48); p16(t + 42, 0xffff); mac_strike_parse(t, 48, &s, &why); mac_strike_glyph(&s, 'B', &g); CHECK(g.present && g.imgCol == 5);
    p16(t + 44, 0xffff); mac_strike_parse(t, 48, &s, &why); mac_strike_glyph(&s, 'B', &g); CHECK(!g.present && g.width == 0);
    /* Malformed or unsupported strikes are refused with a reason. */
    for (unsigned k = 0; k < 9; k++) {
        memcpy(t, strike, 48); size_t len = 48;
        if (k == 0) len = 20;                       /* shorter than the header */
        if (k == 1) p16(t, 0x900c);                 /* a colour (multi-bit) strike */
        if (k == 2) { p16(t + 2, 70); }             /* first > last */
        if (k == 3) p16(t + 4, 300);                /* last beyond 255 */
        if (k == 4) p16(t + 24, 0);                 /* no row words */
        if (k == 5) len = 40;                       /* tables run past the resource */
        if (k == 6) p16(t + 34, 9);                 /* location table out of order (9 then 5) */
        if (k == 7) p16(t + 38, 400);               /* image column beyond the row */
        if (k == 8) p16(t + 16, 2);                 /* offset table inside the header */
        CHECK(mac_strike_parse(t, len, &s, &why) == -1 && why && *why);
    }
    CHECK(mac_strike_parse(NULL, 48, &s, &why) == -1);
    /* Boundaries that other checks would otherwise hide. */
    memcpy(t, strike, 48); p16(t + 2, 67); p16(t + 4, 66);                    /* first just above last, two table entries */
    CHECK(mac_strike_parse(t, 48, &s, &why) == -1);
    memcpy(t, strike, 48); p16(t + 2, 250); p16(t + 4, 256);                  /* last 256: out of range though the tables fit */
    CHECK(mac_strike_parse(t, 48, &s, &why) == -1);
    memcpy(t, strike, 48); p16(t + 24, 0); p16(t + 14, 0);                    /* rowWords 0 with every table consistent */
    p16(t + 16, 9); p16(t + 26, 0); p16(t + 28, 0); p16(t + 30, 0); p16(t + 32, 0); p16(t + 34, 0); p16(t + 36, 0); p16(t + 38, 0); p16(t + 40, 0); p16(t + 42, 0);
    memset(t + 26, 0, 8); p16(t + 34, 0x0004); p16(t + 36, 0x0003); p16(t + 38, 0x0003); p16(t + 40, 0xffff);
    CHECK(mac_strike_parse(t, 42, &s, &why) == -1);
    memcpy(t, strike, 48); p16(t + 16, 5);                                    /* the offset table placed early; the location table runs off the end */
    CHECK(mac_strike_parse(t, 36, &s, &why) == -1);
    memcpy(t, strike, 48); p16(t + 14, 300);                                  /* a strike taller than 255 rows */
    CHECK(mac_strike_parse(t, 48, &s, &why) == -1);
    memset(t, 0, sizeof t); memcpy(t, strike, 48); memset(t + 32, 0, 32); p16(t + 2, 250); p16(t + 4, 256);   /* tables fit and are ordered: only last > 255 can refuse */
    CHECK(mac_strike_parse(t, 64, &s, &why) == -1);
    { static uint8_t big[1024]; memcpy(big, strike, 48); p16(big + 14, 300); p16(big + 16, 317); memset(big + 32, 0, 600);   /* 300 rows, every table inside the data */
      CHECK(mac_strike_parse(big, 700, &s, &why) == -1); }
    /* kernMax is added to every glyph's offset. */
    memcpy(t, strike, 48); p16(t + 8, 0xffff); p16(t + 40, 0x0204);          /* kernMax -1, A's offset 2 */
    CHECK(mac_strike_parse(t, 48, &s, &why) == 0 && s.kernMax == -1); mac_strike_glyph(&s, 'A', &g); CHECK(g.left == 1 && g.width == 4);
    /* Pixels outside the image are clear even where the buffer holds other data. */
    mac_strike_parse(strike, 48, &s, &why);
    CHECK(mac_strike_bit(&s, 16, 0) == 0 && mac_strike_bit(&s, 14, 4) == 0 && mac_strike_bit(&s, 0, -1) == 0);
}
static void lookup_checks(void) {
    size_t sn = make_strike(), fn = make_fond();
    Res list[] = {{FONT_T, 5 * 128 + 10, strike, sn}, {NFNT_T, 2000, strike, sn}, {FOND_T, 5, fond, fn}, {FONT_T, 6 * 128 + 7, strike, 10}, {FONT_T, 5 * 128, strike, sn}, {FONT_T, -128 + 10, strike, sn}};
    MacResourceFork f; char err[200]; size_t total = build_fork(image, list, 6);
    CHECK(mac_resource_open(&f, image, total, err, sizeof err) == 0);
    MacStrike s; const char *why = NULL;
    CHECK(mac_font_find(&f, 5, 10, 0, &s, &why) == 0 && s.ascent == 2);                 /* a FONT resource with id family*128+size */
    CHECK(mac_font_find(&f, 5, 9, 0, &s, &why) == 0 && s.ascent == 2);                  /* through the FOND association table to an NFNT */
    CHECK(mac_font_find(&f, 5, 11, 0, &s, &why) == -1 && why);
    CHECK(strstr(why, "family 5 size 11 style 0") != NULL);                         /* the refusal names the face asked for */                          /* no such size, and nothing is scaled */
    CHECK(mac_font_find(&f, 4, 10, 0, &s, &why) == -1);                                 /* no such family */
    /* families 0 and 1 (system and application font) come from the stand-in face when the program has no strike of that id; 2 and 3 do not */
    {MacStrike a, b; const char *w2; mac_sysfont_set_path("fonts/ChicagoFLF.ttf");
     if (mac_font_find(&f, 0, 12, 0, &a, &w2) == 0) { CHECK(mac_font_find(&f, 1, 12, 0, &b, &w2) == 0 && a.rectH == b.rectH && a.ascent == b.ascent); CHECK(mac_font_find(&f, 1, 12, 1, &b, &w2) == -1); CHECK(mac_font_find(&f, 2, 12, 0, &b, &w2) == -1); CHECK(mac_font_find(&f, 3, 12, 0, &b, &w2) == -1); }}
    CHECK(mac_font_find(&f, 5, 9, 1, &s, &why) == -1);                                  /* a bold strike is not in the association table */
    CHECK(mac_font_find(&f, 5, 10, 1, &s, &why) == -1);                                 /* a FONT id carries no style */
    CHECK(mac_font_find(&f, 5, 0, 0, &s, &why) == -1 && why);                           /* size 0, although a FONT with that id exists */
    CHECK(mac_font_find(&f, -1, 10, 0, &s, &why) == -1);                                /* a negative family, although a FONT with that id exists */
    CHECK(mac_font_find(&f, 5, 256, 0, &s, &why) == -1 && mac_font_find(&f, 5, -3, 0, &s, &why) == -1);
    CHECK(mac_font_find(&f, -1, 10, 0, &s, &why) == -1 && mac_font_find(&f, 256, 10, 0, &s, &why) == -1);
    CHECK(mac_font_find(&f, 6, 7, 0, &s, &why) == -1 && why);                           /* present but malformed */
    CHECK(mac_font_find(NULL, 5, 10, 0, &s, &why) == -1);
}

/* ---- the text calls on a real colour port ---- */
enum { SPT = 0x9000, PORT = 0x6000, PMH = 0x6200, PM = 0x6100, PIX = 0x7000, W = 32, H = 16, REG1H = 0x6300, REG1 = 0x6310, REG2H = 0x6320, REG2 = 0x6330, INFO = 0x8000 };
static MacRuntime rt;
static uint32_t rdw(uint32_t p, unsigned n) { return mac_cpu_read(rt.cpu, p, n); }
static void wrw(uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt.cpu, p, n, v); }
static unsigned px(int x, int y) { return mac_cpu_ram(rt.cpu)[PIX + y * W + x]; }
static void rig(const MacResourceFork *f) {
    memset(&rt, 0, sizeof rt); rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); rt.fork = *f; rt.current_port = PORT;
    uint8_t *m = mac_cpu_ram(rt.cpu);
    memset(m + PIX, 50, W * H);
    wrw(PORT + 2, 4, PMH); wrw(PMH, 4, PM); wrw(PM, 4, PIX); wrw(PM + 4, 2, 0x8000 | W);
    wrw(PM + 6, 2, 0); wrw(PM + 8, 2, 0); wrw(PM + 10, 2, H); wrw(PM + 12, 2, W);
    wrw(PM + 32, 2, 8); wrw(PM + 34, 2, 1); wrw(PM + 36, 2, 8);
    wrw(PORT + 6, 2, 0xc000);
    wrw(PORT + 16, 2, 0); wrw(PORT + 18, 2, 0); wrw(PORT + 20, 2, H); wrw(PORT + 22, 2, W);
    wrw(PORT + 24, 4, REG1H); wrw(PORT + 28, 4, REG2H);
    for (unsigned k = 0; k < 2; k++) { uint32_t hh = k ? REG2H : REG1H, rr = k ? REG2 : REG1; wrw(hh, 4, rr); wrw(rr, 2, 10); wrw(rr + 2, 2, 0); wrw(rr + 4, 2, 0); wrw(rr + 6, 2, H); wrw(rr + 8, 2, W); }
    wrw(PORT + 48, 2, 5); wrw(PORT + 50, 2, 10);                 /* pen: v 5, h 10 */
    wrw(PORT + 66, 2, 0); wrw(PORT + 68, 2, 5); wrw(PORT + 70, 1, 0); wrw(PORT + 72, 2, 1); wrw(PORT + 74, 2, 10); wrw(PORT + 76, 4, 0);
    wrw(PORT + 80, 4, 200); wrw(PORT + 84, 4, 7);
    mac_cpu_set(rt.cpu, MAC_A7, SPT);
}
static int draw_char(unsigned ch) { wrw(SPT, 2, ch << 8); wrw(SPT + 2, 2, 0x7777); mac_cpu_set(rt.cpu, MAC_A7, SPT); return mac_text_service(&rt, 0xa883); }
static void put_string(uint32_t at, const char *s) { mac_cpu_ram(rt.cpu)[at] = (uint8_t)strlen(s); memcpy(mac_cpu_ram(rt.cpu) + at + 1, s, strlen(s)); }
static int draw_string(const char *s) { put_string(0x8100, s); wrw(SPT, 4, 0x8100); mac_cpu_set(rt.cpu, MAC_A7, SPT); return mac_text_service(&rt, 0xa884); }
static int string_width(const char *s) { put_string(0x8100, s); wrw(SPT, 4, 0x8100); wrw(SPT + 4, 2, 0xbeef); mac_cpu_set(rt.cpu, MAC_A7, SPT); int rc = mac_text_service(&rt, 0xa88c); return rc == 1 ? (int)rdw(SPT + 4, 2) : -1; }
static int char_width(unsigned ch) { wrw(SPT, 2, ch << 8); wrw(SPT + 2, 2, 0xbeef); mac_cpu_set(rt.cpu, MAC_A7, SPT); int rc = mac_text_service(&rt, 0xa88d); return rc == 1 ? (int)rdw(SPT + 2, 2) : -1; }
static int text_op(uint16_t op, const char *buf, int first, int count) {
    memcpy(mac_cpu_ram(rt.cpu) + 0x8200, buf, strlen(buf)); wrw(SPT, 2, (uint16_t)count); wrw(SPT + 2, 2, (uint16_t)first); wrw(SPT + 4, 4, 0x8200); wrw(SPT + 8, 2, 0xbeef);
    mac_cpu_set(rt.cpu, MAC_A7, SPT); return mac_text_service(&rt, op);
}
static int pen_h(void) { return (int16_t)rdw(PORT + 50, 2); }
static int untouched_except(const int (*pts)[2], unsigned n) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int listed = 0; for (unsigned i = 0; i < n; i++) if (pts[i][0] == x && pts[i][1] == y) listed = 1; if (!listed && px(x, y) != 50) return 0; }
    return 1;
}
static void service_checks(const MacResourceFork *f) {
    /* metrics */
    rig(f); mac_cpu_set(rt.cpu, MAC_A7, SPT); wrw(SPT, 4, INFO);
    CHECK(mac_text_service(&rt, 0xa88b) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4 && rdw(INFO, 2) == 2 && rdw(INFO + 2, 2) == 1 && rdw(INFO + 4, 2) == 4 && rdw(INFO + 6, 2) == 1);
    CHECK(char_width('A') == 4 && char_width('B') == 3 && char_width('Z') == 3 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 2);
    CHECK(string_width("AB") == 7 && string_width("") == 0 && string_width("AAB") == 11);
    CHECK(text_op(0xa886, "xAB", 1, 2) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8 && rdw(SPT + 8, 2) == 7);
    CHECK(text_op(0xa886, "xAB", 0, 0) == 1 && rdw(SPT + 8, 2) == 0);
    wrw(PORT + 70, 1, 1); CHECK(char_width('A') == 5 && string_width("AB") == 9); wrw(SPT, 4, INFO); mac_cpu_set(rt.cpu, MAC_A7, SPT); mac_text_service(&rt, 0xa88b); CHECK(rdw(INFO + 4, 2) == 5);  /* bold: one pixel wider */
    wrw(PORT + 70, 1, 0);
    wrw(SPT, 4, 2u << 16); mac_cpu_set(rt.cpu, MAC_A7, SPT); CHECK(mac_text_service(&rt, 0xa88e) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4 && rdw(PORT + 76, 4) == (2u << 16));
    CHECK(char_width(' ') == 5 && char_width('A') == 4 && string_width("A B") == 4 + 5 + 3);                    /* the extra applies to spaces only */
    wrw(PORT + 76, 4, 0);
    mac_cpu_destroy(rt.cpu);
    /* DrawChar, srcOr: only the ink; the pen advances by the glyph's width. */
    rig(f); CHECK(draw_char('A') == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 2 && pen_h() == 14);
    { const int ink[][2] = {{10,3},{12,3},{10,4},{11,4},{12,4},{10,5},{12,5}};
      for (unsigned i = 0; i < 7; i++) CHECK(px(ink[i][0], ink[i][1]) == 200);
      CHECK(px(11, 3) == 50 && px(11, 5) == 50 && untouched_except(ink, 7)); }
    mac_cpu_destroy(rt.cpu);
    /* srcCopy paints the glyph's cell in the background colour as well. */
    rig(f); wrw(PORT + 72, 2, 0); draw_char('A');
    CHECK(px(10, 3) == 200 && px(11, 3) == 7 && px(12, 3) == 200 && px(13, 3) == 7 && px(13, 5) == 7 && px(14, 3) == 50 && px(10, 2) == 50 && px(10, 6) == 50);
    mac_cpu_destroy(rt.cpu);
    /* srcXor flips the pixels under the ink; srcBic writes the background colour there. */
    rig(f); wrw(PORT + 72, 2, 2); draw_char('A'); CHECK(px(10, 3) == (50 ^ 255) && px(11, 3) == 50);
    mac_cpu_destroy(rt.cpu);
    rig(f); wrw(PORT + 72, 2, 3); draw_char('A'); CHECK(px(10, 3) == 7 && px(11, 3) == 50 && px(11, 4) == 7);
    mac_cpu_destroy(rt.cpu);
    /* Bold draws the glyph twice, one pixel apart, and advances one more. */
    rig(f); wrw(PORT + 70, 1, 1); draw_char('A');
    CHECK(px(10, 3) == 200 && px(11, 3) == 200 && px(12, 3) == 200 && px(13, 3) == 200 && px(13, 4) == 200 && px(14, 3) == 50 && pen_h() == 15);
    mac_cpu_destroy(rt.cpu);
    /* Underline: a line one row below the baseline across the run. */
    rig(f); wrw(PORT + 70, 1, 4); draw_string("AB");
    for (int x = 10; x < 17; x++) CHECK(px(x, 6) == 200);
    CHECK(px(9, 6) == 50 && px(17, 6) == 50 && pen_h() == 17);
    mac_cpu_destroy(rt.cpu);
    /* DrawString and DrawText: A at 10, B at 14, the pen ends at 17. */
    rig(f); CHECK(draw_string("AB") == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 4 && pen_h() == 17);
    CHECK(px(15, 3) == 200 && px(14, 3) == 50 && px(14, 4) == 200 && px(15, 4) == 200 && px(14, 5) == 200 && px(15, 5) == 50);
    unsigned a[32 * 16]; for (int i = 0; i < W * H; i++) a[i] = mac_cpu_ram(rt.cpu)[PIX + i];
    mac_cpu_destroy(rt.cpu);
    rig(f); CHECK(text_op(0xa885, "xAB", 1, 2) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SPT + 8 && pen_h() == 17);
    { int same = 1; for (int i = 0; i < W * H; i++) if (a[i] != mac_cpu_ram(rt.cpu)[PIX + i]) same = 0; CHECK(same); }
    mac_cpu_destroy(rt.cpu);
    /* A hidden pen draws nothing but still moves; the clip regions stop drawing outside them. */
    rig(f); wrw(PORT + 66, 2, 0xffff); CHECK(draw_string("AB") == 1 && pen_h() == 17 && px(10, 3) == 50 && px(10, 4) == 50);
    mac_cpu_destroy(rt.cpu);
    rig(f); wrw(REG2 + 8, 2, 12); draw_char('A'); CHECK(px(10, 3) == 200 && px(12, 3) == 50 && px(12, 4) == 50 && px(11, 4) == 200);      /* clipRgn stops at column 12 */
    mac_cpu_destroy(rt.cpu);
    rig(f); wrw(REG1 + 2, 2, 4); draw_char('A'); CHECK(px(10, 3) == 50 && px(10, 4) == 200 && px(11, 4) == 200);                                      /* visRgn top at row 4: row 3 is cut, row 4 kept */
    mac_cpu_destroy(rt.cpu);
    rig(f); wrw(PORT + 16, 2, 5); draw_char('A'); CHECK(px(10, 3) == 50 && px(10, 4) == 50 && px(10, 5) == 200);                           /* portRect top at 5 */
    mac_cpu_destroy(rt.cpu);
    rig(f); wrw(PM + 6, 2, 1); wrw(PM + 10, 2, H + 1); draw_char('A');                                                                    /* pixmap bounds (1,0): local row 3 is buffer row 2 */
    CHECK(px(10, 2) == 200 && px(11, 2) == 50 && px(10, 3) == 200);
    mac_cpu_destroy(rt.cpu);
}
static void refusal_checks(const MacResourceFork *f) {
    for (unsigned k = 0; k < 22; k++) {
        rig(f); unsigned op = 0xa884; int draw = 1; uint32_t sp = SPT;
        if (k == 0) wrw(PORT + 70, 1, 2);                              /* italic */
        if (k == 1) wrw(PORT + 70, 1, 8);                              /* outline */
        if (k == 2) wrw(PORT + 76, 4, 0x00018000);                     /* a fractional SpaceExtra */
        if (k == 3) wrw(PORT + 74, 2, 11);                             /* a size the guest does not supply */
        if (k == 4) wrw(PORT + 68, 2, 99);                             /* a family it does not supply */
        if (k == 5) wrw(PORT + 6, 2, 0);                               /* a basic port cannot be drawn on */
        if (k == 6) wrw(PORT + 72, 2, 4);                              /* a "not" transfer mode */
        if (k == 7) wrw(PORT + 72, 2, 8);                              /* a pattern mode */
        if (k == 8) wrw(PORT + 80, 4, 300);                            /* an index beyond the palette */
        if (k == 9) wrw(REG1, 2, 22);                                  /* a complex clip region */
        if (k == 10) wrw(PM + 32, 2, 4);                               /* not an 8-bit pixmap */
        if (k == 11) rt.current_port = 0;                              /* no port */
        if (k == 12) sp = SPT + 1;                                     /* odd stack */
        if (k == 13) { wrw(SPT, 4, 0x1000000 - 2); mac_cpu_ram(rt.cpu)[0x1000000 - 2] = 5; op = 0xa884; }   /* the string's length runs off RAM */
        if (k == 14) { op = 0xa885; draw = 2; }                        /* DrawText with a negative count */
        if (k == 15) { op = 0xa885; draw = 3; }                        /* DrawText buffer beyond RAM */
        if (k == 16) mac_cpu_halt(rt.cpu, "stopped");
        if (k == 17) wrw(PORT + 4 + 2, 2, 0xc000), wrw(PMH, 4, 0);     /* a pixmap handle whose master pointer is nil */
        if (k == 18) wrw(PM + 4, 2, W);                                /* rowBytes without the pixmap flag */
        if (k == 19) wrw(PORT + 24, 4, 0);                             /* a nil visRgn handle */
        if (k == 20) { op = 0xa88b; draw = 0; wrw(SPT, 4, 0x1000000 - 2); }   /* GetFontInfo record off RAM */
        if (k == 21) wrw(PORT + 70, 1, 0x40);                          /* extend */
        (void)draw; uint8_t before[W * H]; memcpy(before, mac_cpu_ram(rt.cpu) + PIX, sizeof before);

        mac_cpu_set(rt.cpu, MAC_A7, sp);
        if (op == 0xa884 && k != 13) { put_string(0x8100, "AB"); wrw(SPT, 4, 0x8100); }
        if (k == 14) { memcpy(mac_cpu_ram(rt.cpu) + 0x8200, "AB", 2); wrw(SPT, 2, 0xffff); wrw(SPT + 2, 2, 0); wrw(SPT + 4, 4, 0x8200); }
        if (k == 15) { wrw(SPT, 2, 8); wrw(SPT + 2, 2, 0); wrw(SPT + 4, 4, 0x1000000 - 4); }
        { int rc = mac_text_service(&rt, (uint16_t)op); if (!(rc == -1 && mac_cpu_halted(rt.cpu))) fprintf(stderr, "refusal case %u did not halt (rc=%d)\n", k, rc); CHECK(rc == -1 && mac_cpu_halted(rt.cpu)); }
        CHECK(!memcmp(before, mac_cpu_ram(rt.cpu) + PIX, sizeof before));
        mac_cpu_destroy(rt.cpu);
    }
    /* Metrics need no pixmap: a basic port still answers; neighbouring opcodes are not ours. */
    rig(f); wrw(PORT + 6, 2, 0); CHECK(string_width("AB") == 7);
    CHECK(mac_text_service(&rt, 0xa887) == 0 && mac_text_service(&rt, 0xa88f) == 0 && mac_text_service(&rt, 0xa882) == 0);
    mac_cpu_destroy(rt.cpu);
}
int main(void) {
    parse_checks(); lookup_checks();
    { size_t sn = make_strike(), fn = make_fond(); Res list[] = {{FONT_T, 5 * 128 + 10, strike, sn}, {NFNT_T, 2000, strike, sn}, {FOND_T, 5, fond, fn}};
      static uint8_t img2[4096]; MacResourceFork f; char err[200]; size_t total = build_fork(img2, list, 3);
      CHECK(mac_resource_open(&f, img2, total, err, sizeof err) == 0); service_checks(&f); refusal_checks(&f); }
    if (!fails) puts("Font engine checks passed");
    return fails ? 1 : 0;
}
