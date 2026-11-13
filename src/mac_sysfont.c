#include "mac_sysfont.h"
#include "macroman.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OSPI_FREETYPE
int mac_sysfont_strike(int size, MacStrike *out, const char **why) { (void)size; (void)out; *why = "The system font needs FreeType, which this build does not have."; return -1; }
void mac_sysfont_set_path(const char *path) { (void)path; }
void mac_sysfont_reset(void) {}
unsigned mac_sysfont_codepoint(unsigned byte) { return byte == 0x7F || byte == 0xF0 ? 0 : mr_unicode((uint8_t)byte); }
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <unistd.h>

/* See mac_sysfont.h. The strike is a 26-byte header, the image rows, the location table and the offset/width table, in the order and with the
 * fields mac_strike_parse reads (fontType $9000, firstChar 32, lastChar 255; entry 224 is the missing glyph, entry 225 the end of the image).
 * Character n of a guest string is a Mac Roman byte: glyph n is drawn from the font's glyph for mr_unicode(n) (macroman.h), so the curly quotes
 * $D2/$D3, the trade mark sign $AA and the accented letters draw as themselves, and a width measured from this strike is the width drawn. */
enum { FIRST = 32, LAST = 255, ENTRIES = LAST - FIRST + 3, CACHE = 8 };
typedef struct { int size; uint8_t *data; size_t length; MacStrike strike; } Entry;
static Entry cache[CACHE];
static FT_Library library;
static FT_Face face;
static char override_path[1024];

void mac_sysfont_set_path(const char *path) {
    if (face) { FT_Done_Face(face); face = NULL; }
    snprintf(override_path, sizeof override_path, "%s", path ? path : "");
}
void mac_sysfont_reset(void) {
    for (int i = 0; i < CACHE; i++) { free(cache[i].data); cache[i] = (Entry){0}; }
    if (face) { FT_Done_Face(face); face = NULL; }
    if (library) { FT_Done_FreeType(library); library = NULL; }
}

static int open_face(const char *path) { return path && *path && access(path, R_OK) == 0 && FT_New_Face(library, path, 0, &face) == 0; }
static int load_face(const char **why) {
    if (face) return 0;
    if (!library && FT_Init_FreeType(&library)) { *why = "FreeType could not be started for the system font."; return -1; }
    char path[1200];
    if (open_face(override_path) || open_face(getenv("OSPI_SYSTEM_FONT"))) return 0;
    char exe[1024] = {0};
#ifdef __APPLE__
    uint32_t n = sizeof exe - 1; if (_NSGetExecutablePath(exe, &n)) exe[0] = 0;
#else
    ssize_t k = readlink("/proc/self/exe", exe, sizeof exe - 1); if (k > 0) exe[k] = 0;
#endif
    char *slash = strrchr(exe, '/');
    if (slash) {
        *slash = 0;
        static const char *rel[] = { "/../Resources/Fonts/ChicagoFLF.ttf", "/../fonts/ChicagoFLF.ttf", "/fonts/ChicagoFLF.ttf" };
        for (unsigned i = 0; i < sizeof rel / sizeof *rel; i++) { snprintf(path, sizeof path, "%s%s", exe, rel[i]); if (open_face(path)) return 0; }
    }
    if (open_face("fonts/ChicagoFLF.ttf")) return 0;
    *why = "The system font file (ChicagoFLF.ttf) was not found.";
    return -1;
}

/* The Unicode character whose glyph stands for Mac Roman byte `byte`, or 0 for "draw the font's own missing-glyph shape". The font has a glyph for every
 * character of the table except two, which are documented fallbacks (never an Apple face): $DB is the euro sign, which ChicagoFLF lacks, so the generic currency
 * sign U+00A4 (the character Mac OS gave $DB before the euro) is drawn; $F0 is Apple's logo, which no face here may carry, so the missing-glyph box is drawn.
 * $7F (delete) has no glyph either. */
unsigned mac_sysfont_codepoint(unsigned byte) {
    if (byte == 0x7F || byte == 0xF0) return 0;
    uint32_t cp = mr_unicode((uint8_t)byte);
    if (face && !FT_Get_Char_Index(face, cp)) cp = byte == 0xDB && FT_Get_Char_Index(face, 0xA4) ? 0xA4 : 0;
    return cp;
}

typedef struct { int adv, w, h, left, top; uint8_t *bits; int pitch; } G;
static int render(int index_or_char, int is_index, G *g) {
    FT_Int32 flags = FT_LOAD_RENDER | FT_LOAD_TARGET_MONO;
    if (is_index ? FT_Load_Glyph(face, (FT_UInt)index_or_char, flags) : FT_Load_Char(face, (FT_ULong)index_or_char, flags)) return -1;
    FT_GlyphSlot s = face->glyph;
    if (s->bitmap.pixel_mode != FT_PIXEL_MODE_MONO && s->bitmap.width) return -1;
    g->adv = (int)((s->advance.x + 32) >> 6); g->w = (int)s->bitmap.width; g->h = (int)s->bitmap.rows; g->left = s->bitmap_left; g->top = s->bitmap_top;
    g->pitch = s->bitmap.pitch; g->bits = NULL;
    if (g->w && g->h) { g->bits = malloc((size_t)abs(g->pitch) * (size_t)g->h); if (!g->bits) return -1; memcpy(g->bits, s->bitmap.buffer, (size_t)abs(g->pitch) * (size_t)g->h); }
    return 0;
}
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static int build(int size, Entry *e, const char **why) {
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size)) { *why = "FreeType could not set that pixel size for the system font."; return -1; }
    G g[ENTRIES - 1]; int rc = -1, made = 0;
    for (int i = 0; i < ENTRIES - 2; i++, made++) {
        unsigned cp = mac_sysfont_codepoint((unsigned)(FIRST + i));
        if (cp ? render((int)cp, 0, &g[i]) : render(0, 1, &g[i])) goto done;
    }
    if (render(0, 1, &g[ENTRIES - 2])) goto done;                                  /* the font's own missing-glyph shape */
    made++;
    int ascent = (int)(face->size->metrics.ascender >> 6), descent = -(int)(face->size->metrics.descender >> 6), widMax = 0, kern = 0, columns = 0;
    for (int i = 0; i < ENTRIES - 1; i++) {
        if (g[i].w > 255 || g[i].h > 255 || g[i].adv < 0 || g[i].adv > 255) { *why = "A system-font glyph is too large for a strike."; goto done; }
        if (g[i].adv > widMax) widMax = g[i].adv;
        if (g[i].w && g[i].left < kern) kern = g[i].left;
        if (g[i].w && g[i].top > ascent) ascent = g[i].top;
        if (g[i].w && g[i].h - g[i].top > descent) descent = g[i].h - g[i].top;
        columns += g[i].w;
    }
    int rectH = ascent + descent;
    if (ascent < 0 || descent < 0 || rectH <= 0 || rectH > 255 || -kern > 255) { *why = "The system font's metrics do not fit a strike."; goto done; }
    unsigned rowWords = (unsigned)((columns + 15) / 16); if (!rowWords) rowWords = 1;
    size_t rowBytes = rowWords * 2, image = rowBytes * (size_t)rectH, loc_at = 26 + image, ow_at = loc_at + (size_t)ENTRIES * 2, total = ow_at + (size_t)ENTRIES * 2;
    uint8_t *d = calloc(1, total);
    if (!d) { *why = "Out of memory building the system font."; goto done; }
    put16(d, 0x9000); put16(d + 2, FIRST); put16(d + 4, LAST); put16(d + 6, (unsigned)widMax); put16(d + 8, (unsigned)(uint16_t)(int16_t)kern); put16(d + 10, (unsigned)descent);
    put16(d + 12, (unsigned)widMax + 2); put16(d + 14, (unsigned)rectH); put16(d + 16, (unsigned)((ow_at - 16) / 2)); put16(d + 18, (unsigned)ascent); put16(d + 20, (unsigned)descent);
    put16(d + 22, 0); put16(d + 24, rowWords);
    int col = 0;
    for (int i = 0; i < ENTRIES - 1; i++) {
        put16(d + loc_at + (size_t)i * 2, (unsigned)col);
        put16(d + ow_at + (size_t)i * 2, (unsigned)(((g[i].w ? g[i].left - kern : -kern) & 0xff) << 8 | (unsigned)g[i].adv));
        for (int r = 0; r < g[i].h; r++) {
            int y = ascent - g[i].top + r;
            if (y < 0 || y >= rectH) continue;
            const uint8_t *row = g[i].bits + (size_t)(g[i].pitch < 0 ? g[i].h - 1 - r : r) * (size_t)abs(g[i].pitch);
            for (int x = 0; x < g[i].w; x++) if ((row[x / 8] >> (7 - x % 8)) & 1) d[26 + (size_t)y * rowBytes + (size_t)(col + x) / 8] |= (uint8_t)(0x80 >> ((col + x) % 8));
        }
        col += g[i].w;
    }
    put16(d + loc_at + (size_t)(ENTRIES - 1) * 2, (unsigned)col);
    put16(d + ow_at + (size_t)(ENTRIES - 1) * 2, 0xffff);
    e->size = size; e->data = d; e->length = total;
    if (mac_strike_parse(d, total, &e->strike, why)) { free(d); e->data = NULL; goto done; }
    rc = 0;
done:
    for (int i = 0; i < made; i++) free(g[i].bits);
    return rc;
}

int mac_sysfont_strike(int size, MacStrike *out, const char **why) {
    *why = NULL;
    if (size < MAC_SYSFONT_MIN || size > MAC_SYSFONT_MAX) { *why = "The system font is only rendered for sizes 8 to 36."; return -1; }
    for (int i = 0; i < CACHE; i++) if (cache[i].data && cache[i].size == size) { *out = cache[i].strike; return 0; }
    if (load_face(why)) return -1;
    int slot = -1; for (int i = 0; i < CACHE; i++) if (!cache[i].data) { slot = i; break; }
    if (slot < 0) { slot = 0; free(cache[0].data); cache[0] = (Entry){0}; }         /* full: reuse the first (a strike handed out earlier for that size would dangle, so keep the table large) */
    if (build(size, &cache[slot], why)) return -1;
    *out = cache[slot].strike;
    return 0;
}
#endif
