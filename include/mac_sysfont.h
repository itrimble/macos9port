#ifndef MAC_SYSFONT_H
#define MAC_SYSFONT_H
#include "mac_font.h"
/* The guest's system font (family 0), for programs that ask for it without carrying it: glyphs rendered from the public-domain ChicagoFLF
 * (fonts/ChicagoFLF.ttf, Robin Casady; see fonts/LICENSE.txt) through FreeType with monochrome hinting at the requested pixel size, for the
 * characters 32 to 255 (the guest's Mac Roman bytes, drawn from the font's glyph for each byte's Unicode character: curly quotes, the trade mark sign, accented letters;
 * see mac_sysfont_codepoint for the two documented fallbacks), packed into a classic strike (the layout mac_strike_parse reads). This is NOT Apple's Chicago and not an authored
 * 12-point bitmap: shapes and advances come from the outline at that size, and the vertical metrics are FreeType's (ascent = the rounded
 * ascender, descent = the rounded descender, leading 0). Supported sizes are MAC_SYSFONT_MIN to MAC_SYSFONT_MAX. Without FreeType, without the
 * font file, or for another size, it refuses with a reason. The strikes are built once per size and kept for the life of the process. */
enum { MAC_SYSFONT_MIN = 8, MAC_SYSFONT_MAX = 36 };
int mac_sysfont_strike(int size, MacStrike *out, const char **why);
/* The Unicode character whose glyph is drawn for Mac Roman byte `byte` (32 to 255), or 0 when the font's own missing-glyph shape stands in. Valid once the face is loaded
 * (after a strike was built); before that it reports the table's character without the fallbacks. */
unsigned mac_sysfont_codepoint(unsigned byte);
/* Where the font file is looked for first (a test or a launcher can point it elsewhere); NULL restores the default search. */
void mac_sysfont_set_path(const char *path);
/* Drops every built strike and the FreeType state (tests). Strikes handed out before are invalid afterwards. */
void mac_sysfont_reset(void);
#endif
