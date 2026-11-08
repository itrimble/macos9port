#include "text_layout.h"
#include "draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OS999_TEXT_HARFBUZZ
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#endif

#ifdef OS999_TEXT_FRIBIDI
#include <fribidi.h>
#endif

typedef struct {
    char *path;
    int face_index;
#ifdef OS999_TEXT_HARFBUZZ
    FT_Face face;
    hb_font_t *font;
#endif
} LayoutFace;

struct TextLayout {
    LayoutFace *faces;
    size_t face_count;
    unsigned pixel_size;
#ifdef OS999_TEXT_HARFBUZZ
    FT_Library library;
#endif
};

typedef struct {
    uint32_t cp, byte, end, script;
    uint16_t font;
    uint8_t level;
} LayoutChar;

typedef struct {
    size_t first, end;
    uint32_t script;
    uint16_t font;
    uint8_t level;
} LayoutRun;

static int fail(char *error, size_t size, const char *message) {
    if (error && size) snprintf(error, size, "%s", message);
    return -1;
}

static char *copy_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

static int utf8_one(const unsigned char *s, size_t n, uint32_t *cp, size_t *used) {
    if (!n) return -1;
    if (s[0] < 0x80) { *cp = s[0]; *used = 1; return 0; }
    unsigned value, need, minimum;
    if (s[0] >= 0xc2 && s[0] <= 0xdf) { value = s[0] & 31u; need = 1; minimum = 0x80; }
    else if (s[0] >= 0xe0 && s[0] <= 0xef) { value = s[0] & 15u; need = 2; minimum = 0x800; }
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) { value = s[0] & 7u; need = 3; minimum = 0x10000; }
    else return -1;
    if (n <= need) return -1;
    for (unsigned i = 1; i <= need; i++) {
        if ((s[i] & 0xc0u) != 0x80u) return -1;
        value = (value << 6) | (s[i] & 63u);
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu)) return -1;
    *cp = value; *used = need + 1; return 0;
}

static int decode(const char *utf8, size_t length, LayoutChar **out, size_t *count) {
    LayoutChar *chars = length ? calloc(length, sizeof *chars) : NULL;
    if (length && !chars) return -1;
    size_t at = 0, n = 0;
    while (at < length) {
        uint32_t cp; size_t used;
        if (utf8_one((const unsigned char *)utf8 + at, length - at, &cp, &used)) {
            free(chars); return -1;
        }
        chars[n++] = (LayoutChar){.cp = cp, .byte = (uint32_t)at,
                                  .end = (uint32_t)(at + used)};
        at += used;
    }
    *out = chars; *count = n; return 0;
}

unsigned text_layout_capabilities(void) {
    unsigned result = 0;
#ifdef OS999_TEXT_HARFBUZZ
    result |= TEXT_LAYOUT_HAS_HARFBUZZ;
#endif
#ifdef OS999_TEXT_FRIBIDI
    result |= TEXT_LAYOUT_HAS_BIDI;
#endif
#ifdef OS999_TEXT_UNIBREAK
    result |= TEXT_LAYOUT_HAS_LINEBREAK;
#endif
    if ((result & 7u) != 7u) result |= TEXT_LAYOUT_DEGRADED;
    return result;
}

TextLayout *text_layout_create(const TextLayoutFontSpec *fonts, size_t count,
                               unsigned pixels, char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (!fonts || !count || count > UINT16_MAX || !pixels || pixels > 512)
        return (fail(error, error_size, "Invalid font list or pixel size."), NULL);
    TextLayout *layout = calloc(1, sizeof *layout);
    if (!layout) return (fail(error, error_size, "Out of memory."), NULL);
    layout->faces = calloc(count, sizeof *layout->faces);
    if (!layout->faces) { free(layout); return (fail(error, error_size, "Out of memory."), NULL); }
    layout->face_count = count; layout->pixel_size = pixels;
#ifdef OS999_TEXT_HARFBUZZ
    if (FT_Init_FreeType(&layout->library)) {
        text_layout_destroy(layout); return (fail(error, error_size, "FreeType initialization failed."), NULL);
    }
#endif
    for (size_t i = 0; i < count; i++) {
        if (!fonts[i].path || !fonts[i].path[0] || fonts[i].face_index < 0) {
            text_layout_destroy(layout); return (fail(error, error_size, "Invalid font specification."), NULL);
        }
        layout->faces[i].path = copy_string(fonts[i].path);
        layout->faces[i].face_index = fonts[i].face_index;
        if (!layout->faces[i].path) {
            text_layout_destroy(layout); return (fail(error, error_size, "Out of memory."), NULL);
        }
#ifdef OS999_TEXT_HARFBUZZ
        if (FT_New_Face(layout->library, fonts[i].path, fonts[i].face_index,
                        &layout->faces[i].face) ||
            FT_Set_Pixel_Sizes(layout->faces[i].face, 0, pixels)) {
            text_layout_destroy(layout); return (fail(error, error_size, "A font face could not be opened."), NULL);
        }
        layout->faces[i].font = hb_ft_font_create_referenced(layout->faces[i].face);
        if (!layout->faces[i].font) {
            text_layout_destroy(layout); return (fail(error, error_size, "HarfBuzz font creation failed."), NULL);
        }
#endif
    }
    return layout;
}

void text_layout_destroy(TextLayout *layout) {
    if (!layout) return;
    for (size_t i = 0; i < layout->face_count; i++) {
#ifdef OS999_TEXT_HARFBUZZ
        if (layout->faces[i].font) hb_font_destroy(layout->faces[i].font);
        if (layout->faces[i].face) FT_Done_Face(layout->faces[i].face);
#endif
        free(layout->faces[i].path);
    }
#ifdef OS999_TEXT_HARFBUZZ
    if (layout->library) FT_Done_FreeType(layout->library);
#endif
    free(layout->faces); free(layout);
}

#ifndef OS999_TEXT_FRIBIDI
static int rtl_codepoint(uint32_t cp) {
    return (cp >= 0x0590u && cp <= 0x08ffu) || (cp >= 0xfb1du && cp <= 0xfdffu) ||
           (cp >= 0xfe70u && cp <= 0xfeffu) || (cp >= 0x10800u && cp <= 0x10fffu);
}

static int ltr_codepoint(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 0x00c0u && cp <= 0x058fu) || (cp >= 0x0900u && cp <= 0x1fffu) ||
           (cp >= 0x2e80u && cp <= 0xd7afu);
}
#endif

static int bidi_levels(LayoutChar *chars, size_t count, int direction, int *resolved) {
#ifdef OS999_TEXT_FRIBIDI
    FriBidiChar *text = malloc(count * sizeof *text);
    FriBidiCharType *types = malloc(count * sizeof *types);
    FriBidiBracketType *brackets = malloc(count * sizeof *brackets);
    FriBidiLevel *levels = malloc(count * sizeof *levels);
    if (count && (!text || !types || !brackets || !levels)) {
        free(text); free(types); free(brackets); free(levels); return -1;
    }
    for (size_t i = 0; i < count; i++) text[i] = chars[i].cp;
    fribidi_get_bidi_types(text, (FriBidiStrIndex)count, types);
    fribidi_get_bracket_types(text, (FriBidiStrIndex)count, types, brackets);
    FriBidiParType base = direction == TEXT_LAYOUT_LTR ? FRIBIDI_PAR_LTR :
                          direction == TEXT_LAYOUT_RTL ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_ON;
    int ok = !count || fribidi_get_par_embedding_levels_ex(types, brackets,
                 (FriBidiStrIndex)count, &base, levels) != 0;
    if (ok) for (size_t i = 0; i < count; i++) chars[i].level = levels[i];
    *resolved = FRIBIDI_IS_RTL(base) ? TEXT_LAYOUT_RTL : TEXT_LAYOUT_LTR;
    free(text); free(types); free(brackets); free(levels); return ok ? 0 : -1;
#else
    int rtl = direction == TEXT_LAYOUT_RTL;
    if (direction == TEXT_LAYOUT_AUTO) {
        rtl = 0;
        for (size_t i = 0; i < count; i++) {
            if (rtl_codepoint(chars[i].cp)) { rtl = 1; break; }
            if (ltr_codepoint(chars[i].cp)) break;
        }
    }
    for (size_t i = 0; i < count; i++) chars[i].level = (uint8_t)rtl;
    *resolved = rtl ? TEXT_LAYOUT_RTL : TEXT_LAYOUT_LTR; return 0;
#endif
}

#ifdef OS999_TEXT_HARFBUZZ
static int ignored_for_coverage(uint32_t cp) {
    return cp == '\n' || cp == '\r' || cp == 0x200cu || cp == 0x200du ||
           (cp >= 0xfe00u && cp <= 0xfe0fu) || (cp >= 0xe0100u && cp <= 0xe01efu);
}
#endif

static int combining(uint32_t cp) {
#ifdef OS999_TEXT_HARFBUZZ
    if (hb_unicode_combining_class(hb_unicode_funcs_get_default(), cp) != 0) return 1;
#endif
    return (cp >= 0x0300u && cp <= 0x036fu) || (cp >= 0x1ab0u && cp <= 0x1affu) ||
           (cp >= 0x1dc0u && cp <= 0x1dffu) || (cp >= 0x20d0u && cp <= 0x20ffu);
}

static uint32_t script_of(uint32_t cp) {
#ifdef OS999_TEXT_HARFBUZZ
    return (uint32_t)hb_unicode_script(hb_unicode_funcs_get_default(), cp);
#else
    (void)cp; return 0;
#endif
}

static void assign_scripts(LayoutChar *chars, size_t count) {
    for (size_t i = 0; i < count; i++) chars[i].script = script_of(chars[i].cp);
#ifdef OS999_TEXT_HARFBUZZ
    uint32_t previous = HB_SCRIPT_UNKNOWN;
    for (size_t i = 0; i < count; i++) {
        uint32_t script = chars[i].script;
        if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN)
            chars[i].script = previous;
        else previous = script;
    }
    uint32_t next = HB_SCRIPT_UNKNOWN;
    for (size_t i = count; i-- > 0;) {
        if (chars[i].script == HB_SCRIPT_UNKNOWN) chars[i].script = next;
        else next = chars[i].script;
    }
#endif
}

static int face_has(const TextLayout *layout, size_t face, uint32_t cp) {
#ifdef OS999_TEXT_HARFBUZZ
    return ignored_for_coverage(cp) || FT_Get_Char_Index(layout->faces[face].face, cp) != 0;
#else
    (void)layout; (void)face; (void)cp; return 1;
#endif
}

static void assign_fonts(const TextLayout *layout, LayoutChar *chars, size_t count) {
    for (size_t first = 0; first < count;) {
        size_t end = first + 1;
        while (end < count && (combining(chars[end].cp) || chars[end].cp == 0x200du ||
               chars[end - 1].cp == 0x200du || (chars[end].cp >= 0xfe00u && chars[end].cp <= 0xfe0fu))) end++;
        size_t chosen = 0;
        for (size_t face = 0; face < layout->face_count; face++) {
            size_t i = first;
            while (i < end && face_has(layout, face, chars[i].cp)) i++;
            if (i == end) { chosen = face; break; }
        }
        for (size_t i = first; i < end; i++) chars[i].font = (uint16_t)chosen;
        first = end;
    }
}

static LayoutRun *make_runs(LayoutChar *chars, size_t count, size_t *run_count) {
    LayoutRun *runs = count ? calloc(count, sizeof *runs) : NULL;
    if (count && !runs) return NULL;
    size_t n = 0;
    for (size_t first = 0; first < count;) {
        uint32_t script = chars[first].script;
        if (chars[first].cp == '\n' || chars[first].cp == '\r') script = UINT32_MAX;
        size_t end = first + 1;
        while (end < count && chars[end].font == chars[first].font &&
               chars[end].level == chars[first].level && chars[end].script == script &&
               chars[end].cp != '\n' && chars[end].cp != '\r') end++;
        runs[n++] = (LayoutRun){first, end, script, chars[first].font, chars[first].level};
        first = end;
    }
    if (n) {
        unsigned maximum = 0, minimum_odd = 255;
        for (size_t i = 0; i < n; i++) {
            if (runs[i].level > maximum) maximum = runs[i].level;
            if ((runs[i].level & 1u) && runs[i].level < minimum_odd) minimum_odd = runs[i].level;
        }
        if (minimum_odd != 255) for (int level = (int)maximum; level >= (int)minimum_odd; level--) {
            for (size_t i = 0; i < n;) {
                while (i < n && runs[i].level < level) i++;
                size_t j = i; while (j < n && runs[j].level >= level) j++;
                for (size_t a = i, b = j ? j - 1 : 0; a < b; a++, b--) {
                    LayoutRun swap = runs[a]; runs[a] = runs[b]; runs[b] = swap;
                }
                i = j;
            }
        }
    }
    *run_count = n; return runs;
}

static int grow(TextLayoutResult *out, size_t *capacity, size_t add) {
    if (add > SIZE_MAX - out->count) return -1;
    size_t need = out->count + add;
    if (need <= *capacity) return 0;
    size_t next = *capacity ? *capacity : 16;
    while (next < need) { if (next > SIZE_MAX / 2) return -1; next *= 2; }
    TextLayoutGlyph *glyphs = realloc(out->glyphs, next * sizeof *glyphs);
    if (!glyphs) return -1;
    out->glyphs = glyphs; *capacity = next; return 0;
}

static int emit_run(TextLayout *layout, const char *utf8, size_t length,
                    const LayoutChar *chars, LayoutRun run, const char *language,
                    TextLayoutResult *out, size_t *capacity) {
    if (run.script == UINT32_MAX) {
        if (grow(out, capacity, 1)) return -1;
        out->glyphs[out->count++] = (TextLayoutGlyph){.cluster = chars[run.first].byte,
            .font_index = run.font, .bidi_level = run.level,
            .break_after = TEXT_LAYOUT_BREAK_MUST};
        return 0;
    }
#ifdef OS999_TEXT_HARFBUZZ
    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer) return -1;
    unsigned start = chars[run.first].byte;
    unsigned bytes = chars[run.end - 1].end - start;
    hb_buffer_add_utf8(buffer, utf8, (int)length, start, bytes);
    hb_buffer_set_direction(buffer, run.level & 1u ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    if (run.script) hb_buffer_set_script(buffer, (hb_script_t)run.script);
    if (language && language[0]) hb_buffer_set_language(buffer, hb_language_from_string(language, -1));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(layout->faces[run.font].font, buffer, NULL, 0);
    unsigned n = 0;
    hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buffer, &n);
    hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buffer, &n);
    if (grow(out, capacity, n)) { hb_buffer_destroy(buffer); return -1; }
    for (unsigned i = 0; i < n; i++) {
        TextLayoutGlyph glyph = {.glyph_id = info[i].codepoint, .cluster = info[i].cluster,
            .x_advance = pos[i].x_advance, .y_advance = pos[i].y_advance,
            .x_offset = pos[i].x_offset, .y_offset = pos[i].y_offset,
            .font_index = run.font, .bidi_level = run.level,
            .unsafe_to_break = !!(hb_glyph_info_get_glyph_flags(&info[i]) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK)};
        out->advance_x += glyph.x_advance; out->glyphs[out->count++] = glyph;
    }
    hb_buffer_destroy(buffer);
#else
    (void)layout; (void)utf8; (void)length; (void)language;
    if (grow(out, capacity, run.end - run.first)) return -1;
    if (run.level & 1u) {
        for (size_t i = run.end; i-- > run.first;) out->glyphs[out->count++] =
            (TextLayoutGlyph){.glyph_id = chars[i].cp, .cluster = chars[i].byte,
             .x_advance = (int32_t)layout->pixel_size * 38, .font_index = run.font,
             .bidi_level = run.level};
    } else for (size_t i = run.first; i < run.end; i++) out->glyphs[out->count++] =
        (TextLayoutGlyph){.glyph_id = chars[i].cp, .cluster = chars[i].byte,
         .x_advance = (int32_t)layout->pixel_size * 38, .font_index = run.font,
         .bidi_level = run.level};
    out->advance_x += (int32_t)(run.end - run.first) * (int32_t)layout->pixel_size * 38;
#endif
    return 0;
}

static void apply_breaks(TextLayoutResult *out, const uint8_t *breaks, size_t length) {
    for (size_t i = 0; i < out->count; i++) {
        uint32_t start = out->glyphs[i].cluster;
        uint32_t end = (uint32_t)length;
        for (size_t j = 0; j < out->count; j++) {
            uint32_t other = out->glyphs[j].cluster;
            if (other > start && other < end) end = other;
        }
        if (out->glyphs[i].unsafe_to_break) continue;
        for (uint32_t b = start; b < end && b < length; b++)
            if (breaks[b] > out->glyphs[i].break_after) out->glyphs[i].break_after = breaks[b];
    }
}

int text_layout_shape(TextLayout *layout, const char *utf8, size_t length,
                      int direction, const char *language, TextLayoutResult *out,
                      char *error, size_t error_size) {
    if (error && error_size) error[0] = 0;
    if (!layout || !out || (!utf8 && length) || length > UINT32_MAX ||
        direction < TEXT_LAYOUT_AUTO || direction > TEXT_LAYOUT_RTL)
        return fail(error, error_size, "Invalid layout arguments.");
    memset(out, 0, sizeof *out); out->capabilities = text_layout_capabilities();
    LayoutChar *chars = NULL; size_t count = 0;
    if (decode(utf8, length, &chars, &count)) return fail(error, error_size, "Invalid UTF-8.");
    if (bidi_levels(chars, count, direction, &out->resolved_direction)) {
        free(chars); return fail(error, error_size, "Bidirectional analysis failed.");
    }
    assign_fonts(layout, chars, count);
    assign_scripts(chars, count);
    size_t run_count = 0, capacity = 0;
    LayoutRun *runs = make_runs(chars, count, &run_count);
    if (count && !runs) { free(chars); return fail(error, error_size, "Out of memory."); }
    for (size_t i = 0; i < run_count; i++) if (emit_run(layout, utf8, length, chars,
            runs[i], language, out, &capacity)) {
        free(runs); free(chars); text_layout_result_destroy(out);
        return fail(error, error_size, "Text shaping failed.");
    }
    uint8_t *breaks = length ? malloc(length) : NULL;
    if (length && (!breaks || text_layout_breaks(utf8, length, language, breaks, length))) {
        free(breaks); free(runs); free(chars); text_layout_result_destroy(out);
        return fail(error, error_size, "Line-break analysis failed.");
    }
    apply_breaks(out, breaks, length);
    free(breaks); free(runs); free(chars); return 0;
}

void text_layout_result_destroy(TextLayoutResult *result) {
    if (!result) return;
    free(result->glyphs); memset(result, 0, sizeof *result);
}

int text_layout_line_height(const TextLayout *layout) {
    return layout ? (int)layout->pixel_size + (int)layout->pixel_size / 3 : 0;
}

#ifdef OS999_TEXT_HARFBUZZ
static void blend(Fb *fb, int x, int y, uint32_t color, unsigned cover) {
    if (!cover || !fb || !fb->px || x < 0 || y < 0 || x >= fb->w || y >= fb->h ||
        (fb->clip && (x < fb->cx || y < fb->cy || x >= fb->cx + fb->cw || y >= fb->cy + fb->ch))) return;
    uint32_t dst = fb->px[y * fb->w + x];
    unsigned inv = 255 - cover;
    unsigned red = ((((color >> 16) & 255u) * cover) + (((dst >> 16) & 255u) * inv)) / 255;
    unsigned green = ((((color >> 8) & 255u) * cover) + (((dst >> 8) & 255u) * inv)) / 255;
    unsigned blue = (((color & 255u) * cover) + ((dst & 255u) * inv)) / 255;
    fb->px[y * fb->w + x] = 0xff000000u | (red << 16) | (green << 8) | blue;
}
#endif

int text_layout_draw(TextLayout *layout, const TextLayoutResult *result,
                     struct Fb *fb, int x, int baseline, uint32_t color) {
#ifdef OS999_TEXT_HARFBUZZ
    if (!layout || !result || !fb) return 0;
    int32_t pen_x = x * 64, pen_y = baseline * 64;
    int drawn = 0;
    for (size_t i = 0; i < result->count; i++) {
        const TextLayoutGlyph *glyph = &result->glyphs[i];
        if (glyph->font_index >= layout->face_count) return 0;
        FT_Face face = layout->faces[glyph->font_index].face;
        if (FT_Load_Glyph(face, glyph->glyph_id, FT_LOAD_DEFAULT) == 0 &&
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0) {
            FT_GlyphSlot slot = face->glyph;
            int gx = (pen_x + glyph->x_offset) / 64 + slot->bitmap_left;
            int gy = (pen_y - glyph->y_offset) / 64 - slot->bitmap_top;
            int pitch = slot->bitmap.pitch;
            for (unsigned row = 0; row < slot->bitmap.rows; row++) {
                const unsigned char *source = pitch >= 0 ? slot->bitmap.buffer + row * (unsigned)pitch :
                    slot->bitmap.buffer + (slot->bitmap.rows - row - 1) * (unsigned)(-pitch);
                for (unsigned col = 0; col < slot->bitmap.width; col++) {
                    blend(fb, gx + (int)col, gy + (int)row, color, source[col]);
                    drawn |= source[col] != 0;
                }
            }
        }
        pen_x += glyph->x_advance;
        pen_y += glyph->y_advance;
    }
    return drawn;
#else
    (void)layout; (void)result; (void)fb; (void)x; (void)baseline; (void)color;
    return 0;
#endif
}
