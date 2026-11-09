#ifndef OS999_TEXT_LAYOUT_H
#define OS999_TEXT_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

enum {
    TEXT_LAYOUT_AUTO = 0,
    TEXT_LAYOUT_LTR = 1,
    TEXT_LAYOUT_RTL = 2
};

enum {
    TEXT_LAYOUT_BREAK_NONE = 0,
    TEXT_LAYOUT_BREAK_ALLOW = 1,
    TEXT_LAYOUT_BREAK_MUST = 2
};

enum {
    TEXT_LAYOUT_HAS_HARFBUZZ = 1,
    TEXT_LAYOUT_HAS_BIDI = 2,
    TEXT_LAYOUT_HAS_LINEBREAK = 4,
    TEXT_LAYOUT_DEGRADED = 8
};

typedef struct {
    const char *path;
    int face_index;
} TextLayoutFontSpec;

typedef struct {
    uint32_t glyph_id;
    uint32_t cluster;       /* UTF-8 byte offset in the original string. */
    int32_t x_advance;      /* 26.6 pixel units. */
    int32_t y_advance;
    int32_t x_offset;
    int32_t y_offset;
    uint16_t font_index;    /* Index into the ordered font specification list. */
    uint8_t bidi_level;
    uint8_t break_after;    /* TEXT_LAYOUT_BREAK_* after this glyph's cluster. */
    uint8_t unsafe_to_break;
} TextLayoutGlyph;

typedef struct {
    TextLayoutGlyph *glyphs;
    size_t count;
    int32_t advance_x;      /* Sum of x advances in 26.6 pixel units. */
    int resolved_direction;
    unsigned capabilities;
} TextLayoutResult;

typedef struct TextLayout TextLayout;
struct Fb;

/* Opens the ordered fallback faces at one pixel size. Invalid faces fail the
 * whole operation so a successful layout has stable font indices. */
TextLayout *text_layout_create(const TextLayoutFontSpec *fonts, size_t font_count,
                               unsigned pixel_size, char *error, size_t error_size);
void text_layout_destroy(TextLayout *layout);

/* Shapes one UTF-8 paragraph in visual glyph order. Newline characters are
 * retained as mandatory break clusters with zero advance. */
int text_layout_shape(TextLayout *layout, const char *utf8, size_t length,
                      int direction, const char *language,
                      TextLayoutResult *out, char *error, size_t error_size);
void text_layout_result_destroy(TextLayoutResult *result);

/* Returns one TEXT_LAYOUT_BREAK_* value for every UTF-8 byte. Continuation
 * bytes are always TEXT_LAYOUT_BREAK_NONE. */
int text_layout_breaks(const char *utf8, size_t length, const char *language,
                       uint8_t *breaks, size_t break_capacity);

unsigned text_layout_capabilities(void);
int text_layout_draw(TextLayout *layout, const TextLayoutResult *result,
                     struct Fb *fb, int x, int baseline, uint32_t color);
int text_layout_line_height(const TextLayout *layout);

#endif
