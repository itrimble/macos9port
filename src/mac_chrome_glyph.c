#include "mac_chrome.h"
#include "mac_sysfont.h"
#include <string.h>

extern const uint8_t font8x8_basic[128][8];

/* The fixed 8x8 cell the runtime's own chrome (menus, window titles, dialog and alert text, list and file-dialog rows) draws with has glyphs for ASCII only.
 * Guest text is Mac Roman, so a byte of $80 to $FF takes the glyph of the character it stands for (mr_unicode) from ChicagoFLF, rendered at 8 pixels
 * and placed in the cell with its baseline under row 6 like the ASCII glyphs (a descender below row 7 is clipped; a glyph that reaches above row 0 is moved down to fit). Where
 * the font cannot be loaded (no FreeType, no font file) this table of plain ASCII look-alikes stands in instead: accents are dropped, curly quotes are
 * straight, symbols that have none are '?'. $F0 (Apple's logo) is '?' in both, and so is any glyph the font hints away entirely at 8 pixels. */
static const char LOOKALIKE[129] =
    "AACENOUa" "aaaaacee" "eeiiiino" "oooouuuu"        /* $80-$9F */
    "+oc?S*Ps" "rcT'\"=AO" "?+<>Yu?E" "?p?ao?ao"       /* $A0-$BF */
    "?!-?f~?<" ">. AAOOo" "--\"\"''/?" "yY/E<>??"      /* $C0-$DF */
    "+.,\"?AEA" "EEIIIIOO" "?OUUUi^~" "-????,??";      /* $E0-$FF */

static int from_font(unsigned byte, uint8_t out[8]) {
    if (byte == 0xF0) return 0;                       /* never a logo */
    MacStrike s; const char *why;
    if (mac_sysfont_strike(8, &s, &why)) return 0;
    MacGlyph g;
    mac_strike_glyph(&s, byte, &g);
    if (byte == 0xCA) return 1;                       /* no-break space: an empty cell */
    if (!g.present || !g.imgWidth) return 0;
    int shift = 0;                                    /* a glyph that reaches above the cell (a capital with a ring, a bare accent) is moved down to fit */
    for (int r = 0; r < s.rectH && !shift; r++)
        for (int c = 0; c < g.imgWidth; c++)
            if (mac_strike_bit(&s, g.imgCol + c, r)) { shift = r - s.ascent + 7 < 0 ? s.ascent - 7 - r : 0; r = s.rectH; break; }
    for (int r = 0; r < s.rectH; r++) {
        int y = r - s.ascent + 7 + shift;
        if (y < 0 || y > 7) continue;
        for (int c = 0; c < g.imgWidth; c++) {
            int x = g.left + c;
            if (x >= 0 && x < 8 && mac_strike_bit(&s, g.imgCol + c, r)) out[y] |= (uint8_t)(1u << x);
        }
    }
    for (int y = 0; y < 8; y++) if (out[y]) return 1;
    return 0;                                         /* hinting at 8 pixels removed every dot (the bare diaeresis): use the look-alike */
}

const uint8_t *mac_chrome_glyph(unsigned ch, uint8_t scratch[8]) {
    ch &= 0xff;
    if (ch < 128) return font8x8_basic[ch];
    memset(scratch, 0, 8);
    if (!from_font(ch, scratch)) return font8x8_basic[(unsigned char)LOOKALIKE[ch - 128]];
    return scratch;
}
