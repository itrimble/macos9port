/* The system font (family 0) rendered from ChicagoFLF through FreeType: sizes, metrics, glyph ink, refusals. */
#include "mac_sysfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void) {
    MacStrike s; const char *why = NULL; MacGlyph g;
#ifndef OSPI_FREETYPE
    CHECK(mac_sysfont_strike(12, &s, &why) == -1 && why && strstr(why, "FreeType"));
#else
    mac_sysfont_set_path("fonts/ChicagoFLF.ttf");
    CHECK(mac_sysfont_strike(12, &s, &why) == 0);
    CHECK(s.first == 32 && s.last == 126 && s.ascent == 12 && s.descent >= 3 && s.rectH == s.ascent + s.descent);
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
    mac_strike_glyph(&s, 200, &g); CHECK(g.present);                 /* outside the range: the missing glyph */
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
