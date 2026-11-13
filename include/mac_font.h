#ifndef MAC_FONT_INCLUDED
#define MAC_FONT_INCLUDED
#include "mac_resource.h"

/* A bitmap font strike taken from the guest application's own FONT or NFNT resource. All pointers borrow the resource bytes. */
typedef struct {
    const uint8_t *bits, *loc, *ow;
    int first, last, widMax, kernMax, nDescent, rectW, rectH, ascent, descent, leading, rowBytes;
} MacStrike;
typedef struct { int width, left, imgCol, imgWidth; int present; } MacGlyph; /* left: pen-relative offset of the image's left edge */

/* Finds the strike for (family, size, face). A FONT resource with id family*128+size is used if present; otherwise the family's
 * FOND association table is searched for that exact size and style and names an NFNT (or FONT) id. Only exact sizes are served;
 * nothing is scaled. mac_font_find then substitutes as the Font Manager does: the nearest size of the family the program carries, else the OS999 stand-in face
 * (mac_sysfont.h), so a missing face is drawn rather than refused. 0: found, strike filled in; -1: malformed, out of range, or no substitute available, with *why. */
int mac_font_find(const MacResourceFork *fork, int family, int size, unsigned style, MacStrike *out, const char **why);
/* Only what the program's own fonts (and, for families 0 and 1, the stand-in system face) hold at exactly this family, size and style; the answers of RealFont. */
int mac_font_find_exact(const MacResourceFork *fork, int family, int size, unsigned style, MacStrike *out, const char **why);
/* Parses and bounds-checks one strike. 0 ok, -1 malformed or unsupported (*why). */
int mac_strike_parse(const uint8_t *data, size_t size, MacStrike *out, const char **why);
/* The glyph for a character; characters outside the strike (or marked missing) use the strike's missing glyph. */
void mac_strike_glyph(const MacStrike *s, unsigned ch, MacGlyph *g);
/* Whether image pixel (col,row) of a strike is set. col is a bit column in the strike's bit image. */
int mac_strike_bit(const MacStrike *s, int col, int row);
#endif
