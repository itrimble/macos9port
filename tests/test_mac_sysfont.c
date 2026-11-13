/* The system font (family 0) rendered from ChicagoFLF through FreeType: sizes, metrics, glyph ink, refusals. */
#include "mac_sysfont.h"
#include "macroman.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#ifdef OSPI_FREETYPE
/* The glyph's pixels as a string, so two characters can be compared. */
static void bits_of(const MacStrike *s, unsigned ch, char *out, int cap, int *ink, int *width) {
    MacGlyph g; mac_strike_glyph(s, ch, &g);
    int n = 0; *ink = 0; *width = g.width;
    for (int r = 0; r < s->rectH; r++) for (int c = 0; c < g.imgWidth && n < cap - 1; c++) { int b = mac_strike_bit(s, g.imgCol + c, r); *ink += b; out[n++] = (char)('0' + b); }
    out[n] = 0;
}
/* Guest text is Mac Roman: bytes 128 to 255 draw as their own characters (curly quotes, trade mark, accented letters) and not as the missing-glyph box. */
static void mac_roman_checks(const MacStrike *s) {
    char miss[1200], a[1200], b[1200]; int ink_miss, w_miss, ink_a, w_a, ink_b, w_b;
    bits_of(s, 0x01, miss, sizeof miss, &ink_miss, &w_miss);   /* a control character is outside the strike's drawn range: the missing glyph */
    bits_of(s, '?', a, sizeof a, &ink_a, &w_a);
    for (unsigned ch = 0x80; ch <= 0xFF; ch++) {
        bits_of(s, ch, a, sizeof a, &ink_a, &w_a);
        if (ch == 0xF0) { CHECK(!strcmp(a, miss) && mac_sysfont_codepoint(ch) == 0); continue; }   /* Apple's logo: the font's missing-glyph box, by decision */
        CHECK(w_a > 0);
        if (ch != 0xCA) CHECK(strcmp(a, miss) != 0 && ink_a > 0);                                  /* $CA is a no-break space: width, no ink */
    }
    bits_of(s, 0xD2, a, sizeof a, &ink_a, &w_a); bits_of(s, 0xD3, b, sizeof b, &ink_b, &w_b);
    char q[1200]; int ink_q, w_q; bits_of(s, '?', q, sizeof q, &ink_q, &w_q);
    CHECK(strcmp(a, q) != 0 && strcmp(b, q) != 0 && strcmp(a, b) != 0);                           /* the curly quotes are not "?" and not each other */
    bits_of(s, 0xAA, a, sizeof a, &ink_a, &w_a); CHECK(strcmp(a, q) != 0 && ink_a > 0 && w_a > 0);   /* the trade mark sign */
    bits_of(s, 'E', a, sizeof a, &ink_a, &w_a); bits_of(s, 0x83, b, sizeof b, &ink_b, &w_b);
    CHECK(ink_b > ink_a && w_a == w_b);                                                             /* E-acute is E plus an accent, same advance */
    bits_of(s, 'e', a, sizeof a, &ink_a, &w_a); bits_of(s, 0x8E, b, sizeof b, &ink_b, &w_b);
    CHECK(ink_b > ink_a && w_a == w_b);
    CHECK(mac_sysfont_codepoint('A') == 'A' && mac_sysfont_codepoint(0xD2) == 0x201C && mac_sysfont_codepoint(0xD3) == 0x201D && mac_sysfont_codepoint(0xAA) == 0x2122);
    CHECK(mac_sysfont_codepoint(0x83) == 0xC9 && mac_sysfont_codepoint(0x8E) == 0xE9);
    CHECK(mac_sysfont_codepoint(0xDB) == 0xA4 || mac_sysfont_codepoint(0xDB) == 0x20AC);           /* the euro sign, else the currency sign it replaced */
    CHECK(mac_sysfont_codepoint(0x7F) == 0);
    for (unsigned ch = 0x80; ch <= 0xFF; ch++) if (ch != 0xDB && ch != 0xF0) CHECK(mac_sysfont_codepoint(ch) == mr_unicode((uint8_t)ch));
}
#endif
int main(void) {
    MacStrike s; const char *why = NULL; MacGlyph g;
#ifndef OSPI_FREETYPE
    CHECK(mac_sysfont_strike(12, &s, &why) == -1 && why && strstr(why, "FreeType"));
#else
    mac_sysfont_set_path("fonts/ChicagoFLF.ttf");
    CHECK(mac_sysfont_strike(12, &s, &why) == 0);
    CHECK(s.first == 32 && s.last == 255 && s.ascent == 12 && s.descent >= 3 && s.rectH == s.ascent + s.descent);
    mac_strike_glyph(&s, 'I', &g);                                   /* FreeType at 12 px: 'I' advances 6, ink 2 wide, 9 tall */
    CHECK(g.present && g.width == 6 && g.imgWidth == 2);
    int ink = 0; for (int r = 0; r < s.rectH; r++) for (int c = 0; c < g.imgWidth; c++) ink += mac_strike_bit(&s, g.imgCol + c, r);
    CHECK(ink == 18);                                                /* 2 x 9, solid */
    for (int r = 0; r < s.rectH; r++) {                              /* the stem sits on rows ascent-9 .. ascent-1, both columns set (glyph images sit side by side, so neighbours are not checked) */
        int want = (r >= s.ascent - 9 && r < s.ascent) ? 1 : 0;
        CHECK(mac_strike_bit(&s, g.imgCol, r) == want && mac_strike_bit(&s, g.imgCol + 1, r) == want);
    }
    mac_strike_glyph(&s, ' ', &g); CHECK(g.present && g.width > 0);
    ink = 0; for (int r = 0; r < s.rectH; r++) for (int c = 0; c < g.imgWidth; c++) ink += mac_strike_bit(&s, g.imgCol + c, r);
    CHECK(ink == 0);                                                 /* a space has no ink */
    mac_strike_glyph(&s, 'W', &g); CHECK(g.width == 12);
    mac_strike_glyph(&s, 200, &g); CHECK(g.present);                 /* $C8 is a letter-shaped glyph now (the guillemet), not the missing glyph */
    mac_roman_checks(&s);
    int last = 0; for (int sz = 8; sz <= 20; sz++) {                 /* the advance of 'W' never shrinks as the size grows */
        MacStrike t; CHECK(mac_sysfont_strike(sz, &t, &why) == 0); mac_strike_glyph(&t, 'W', &g); CHECK(g.width >= last); last = g.width;
    }
    CHECK(mac_sysfont_strike(7, &s, &why) == -1 && why && strstr(why, "sizes"));
    CHECK(mac_sysfont_strike(37, &s, &why) == -1);
    CHECK(mac_sysfont_strike(0, &s, &why) == -1 && mac_sysfont_strike(-5, &s, &why) == -1);
    mac_sysfont_reset(); mac_sysfont_set_path("/nonexistent/none.ttf");
    CHECK(mac_sysfont_strike(12, &s, &why) == 0);                    /* a bad override falls through to the next place (fonts/ beside the working directory) */
    mac_sysfont_reset();
#endif
    if (!fails) puts("System font checks passed");
    return fails ? 1 : 0;
}
