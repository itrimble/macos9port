#include "mac_font.h"
#include <stdio.h>
#include <string.h>

/* Classic bitmap font strikes (FONT and NFNT share one layout), read from the guest's own resources. A strike is a 26-byte header
 * (fontType, firstChar, lastChar, widMax, kernMax, nDescent, fRectWidth, fRectHeight, owTLoc, ascent, descent, leading, rowWords),
 * then fRectHeight rows of rowWords words of 1-bit glyph images laid side by side, then a location table (lastChar-firstChar+3
 * words, each the bit column where a glyph's image starts, the last giving the end), and, owTLoc words after the owTLoc field, an
 * offset/width table (the same count of words: offset in the high byte, advance width in the low byte, $FFFF for a missing glyph).
 * The last entry of each table is the strike's "missing glyph". Only 1-bit strikes are read; deeper (colour) strikes are refused. */
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
static int sbe16(const uint8_t *p) { return (int16_t)be16(p); }

int mac_strike_parse(const uint8_t *d, size_t n, MacStrike *s, const char **why) {
    *why = NULL;
    if (!d || n < 26) { *why = "Font strike is shorter than its header."; return -1; }
    unsigned type = be16(d);
    if (type & 0x000c) { *why = "Colour (multi-bit) font strikes are unsupported."; return -1; }
    int first = (int)be16(d + 2), last = (int)be16(d + 4);
    if (first > last || last > 255) { *why = "Font strike has an invalid character range."; return -1; }
    MacStrike t;
    t.first = first; t.last = last; t.widMax = sbe16(d + 6); t.kernMax = sbe16(d + 8); t.nDescent = sbe16(d + 10);
    t.rectW = sbe16(d + 12); t.rectH = sbe16(d + 14); t.ascent = sbe16(d + 18); t.descent = sbe16(d + 20); t.leading = sbe16(d + 22);
    unsigned owt = be16(d + 16), rowWords = be16(d + 24);
    if (t.rectH < 0 || t.rectH > 255 || rowWords == 0 || rowWords > 4096) { *why = "Font strike has invalid dimensions."; return -1; }
    size_t entries = (size_t)(last - first + 3), image = (size_t)rowWords * 2 * (size_t)t.rectH;
    size_t loc_at = 26 + image, ow_at = 16 + (size_t)owt * 2;
    if (loc_at + entries * 2 > n || ow_at < 26 || ow_at + entries * 2 > n) { *why = "Font strike tables run past the resource."; return -1; }
    t.rowBytes = (int)(rowWords * 2); t.bits = d + 26; t.loc = d + loc_at; t.ow = d + ow_at;
    for (size_t i = 0; i + 1 < entries; i++) {   /* image columns must be ordered and inside the row */
        unsigned a = be16(t.loc + i * 2), b = be16(t.loc + i * 2 + 2);
        if (b < a || b > rowWords * 16) { *why = "Font strike location table is invalid."; return -1; }
    }
    *s = t;
    return 0;
}

void mac_strike_glyph(const MacStrike *s, unsigned ch, MacGlyph *g) {
    int idx = ((int)ch >= s->first && (int)ch <= s->last) ? (int)ch - s->first : s->last - s->first + 1;
    if (be16(s->ow + idx * 2) == 0xffff) idx = s->last - s->first + 1;   /* the missing glyph */
    unsigned ow = be16(s->ow + idx * 2);
    memset(g, 0, sizeof *g);
    if (ow == 0xffff) return;                                              /* not even a missing glyph: nothing, zero width */
    g->present = 1; g->width = (int)(ow & 0xff); g->left = (int)(ow >> 8) + s->kernMax;
    g->imgCol = (int)be16(s->loc + idx * 2); g->imgWidth = (int)be16(s->loc + idx * 2 + 2) - g->imgCol;
}

int mac_strike_bit(const MacStrike *s, int col, int row) {
    if (row < 0 || row >= s->rectH || col < 0 || col >= s->rowBytes * 8) return 0;
    return (s->bits[row * s->rowBytes + col / 8] >> (7 - col % 8)) & 1;
}

#define FONT_T MAC_RESOURCE_TYPE('F', 'O', 'N', 'T')
#define NFNT_T MAC_RESOURCE_TYPE('N', 'F', 'N', 'T')
#define FOND_T MAC_RESOURCE_TYPE('F', 'O', 'N', 'D')
static int load(const MacResourceFork *f, int id, MacStrike *out, const char **why) {
    MacResource r;
    if (id < -32768 || id > 32767) return 1;
    if (mac_resource_find(f, NFNT_T, (int16_t)id, &r) != 0 && mac_resource_find(f, FONT_T, (int16_t)id, &r) != 0) return 1;
    return mac_strike_parse(r.data, r.size, out, why) ? -1 : 0;
}

int mac_font_find(const MacResourceFork *f, int family, int size, unsigned style, MacStrike *out, const char **why) {
    *why = NULL;
    if (!f || !out) { *why = "No resource fork to find a font in."; return -1; }
    if (size <= 0 || size > 255) { *why = "Text size 0 (the default size) or an invalid size is unsupported."; return -1; }
    if (family < 0 || family > 255) { *why = "Font family number is outside the supported range."; return -1; }
    int rc = (style == 0) ? load(f, family * 128 + size, out, why) : 1;       /* a FONT id carries no style */
    if (rc <= 0) return rc;
    MacResource fond;
    if (mac_resource_find(f, FOND_T, (int16_t)family, &fond) == 0 && fond.size >= 54) {
        unsigned count = be16(fond.data + 52) + 1;
        if (count <= 256 && 54 + (size_t)count * 6 <= fond.size) {
            for (unsigned i = 0; i < count; i++) {
                const uint8_t *e = fond.data + 54 + i * 6;
                if ((int)be16(e) == size && (be16(e + 2) & 0xffu) == (style & 0xffu)) {
                    rc = load(f, (int16_t)be16(e + 4), out, why);
                    if (rc <= 0) return rc;
                }
            }
        }
    }
    static char message[160];                                                  /* names what was asked for, so a halt says which face is missing */
    snprintf(message, sizeof message, "Font family %d size %d style %u is not in the guest application's resources (no scaling or system font is provided).", family, size, style);
    *why = message;
    return -1;
}
