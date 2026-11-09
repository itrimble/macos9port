#include "text_layout.h"
#include "draw.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const TextLayoutFontSpec fonts[] = {
    {"fonts/system/NotoSans-Regular.ttf", 0},
    {"fonts/system/NotoSansCJK-Regular.ttc", 0},
    {"fonts/system/NotoSansArabic-Regular.ttf", 0},
    {"fonts/system/NotoSansHebrew-Regular.ttf", 0},
    {"fonts/system/NotoSansDevanagari-Regular.ttf", 0},
    {"fonts/system/NotoSansSymbols2-Regular.ttf", 0}
};

static const TextLayoutGlyph *cluster(const TextLayoutResult *result, uint32_t byte) {
    for (size_t i = 0; i < result->count; i++)
        if (result->glyphs[i].cluster == byte) return &result->glyphs[i];
    return NULL;
}

static void test_breaks(void) {
    const char sample[] = "hello world\n\xE6\xBC\xA2\xE5\xAD\x97";
    uint8_t breaks[sizeof sample - 1];
    CHECK(text_layout_breaks(sample, sizeof sample - 1, "en", breaks, sizeof breaks) == 0);
    CHECK(breaks[5] == TEXT_LAYOUT_BREAK_ALLOW);
    CHECK(breaks[11] == TEXT_LAYOUT_BREAK_MUST);
    CHECK(breaks[12] == TEXT_LAYOUT_BREAK_ALLOW);
    CHECK(breaks[13] == TEXT_LAYOUT_BREAK_NONE && breaks[14] == TEXT_LAYOUT_BREAK_NONE);
    CHECK(text_layout_breaks("abc", 3, "en", breaks, 2) == -1);
    CHECK(text_layout_breaks("\xC0\x80", 2, "en", breaks, sizeof breaks) == -1);
}

static void test_layout(void) {
    char error[160];
    TextLayout *layout = text_layout_create(fonts, sizeof fonts / sizeof *fonts,
                                             24, error, sizeof error);
    CHECK(layout != NULL);
    if (!layout) { fprintf(stderr, "create: %s\n", error); return; }

    TextLayoutResult result;
    const char fallback[] = "A\xE6\xBC\xA2";
    CHECK(text_layout_shape(layout, fallback, sizeof fallback - 1, TEXT_LAYOUT_LTR,
                            "en", &result, error, sizeof error) == 0);
    CHECK(result.count >= 2 && result.advance_x > 0);
    const TextLayoutGlyph *latin = cluster(&result, 0);
    const TextLayoutGlyph *han = cluster(&result, 1);
    CHECK(latin && latin->font_index == 0);
#ifdef OS999_TEXT_HARFBUZZ
    CHECK(han && han->font_index == 1 && han->glyph_id != 0);
#else
    CHECK(han && han->font_index == 0);
#endif
    text_layout_result_destroy(&result);

    const char arabic[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    CHECK(text_layout_shape(layout, arabic, sizeof arabic - 1, TEXT_LAYOUT_AUTO,
                            "ar", &result, error, sizeof error) == 0);
    CHECK(result.resolved_direction == TEXT_LAYOUT_RTL);
    CHECK(result.count > 0);
#ifdef OS999_TEXT_HARFBUZZ
    uint32_t pixels[320 * 64];
    for (size_t i = 0; i < sizeof pixels / sizeof *pixels; i++) pixels[i] = C_WHITE;
    Fb fb; fb_init(&fb, pixels, 320, 64);
    CHECK(text_layout_draw(layout, &result, &fb, 8, 40, C_BLACK) == 1);
    int changed = 0;
    for (size_t i = 0; i < sizeof pixels / sizeof *pixels; i++) changed += pixels[i] != C_WHITE;
    CHECK(changed > 20);
    for (size_t i = 0; i < result.count; i++) {
        CHECK(result.glyphs[i].font_index == 2);
        CHECK((result.glyphs[i].bidi_level & 1u) != 0);
        CHECK(result.glyphs[i].glyph_id != 0);
    }
#endif
    text_layout_result_destroy(&result);

    const char mixed[] = "abc \xD7\x90\xD7\x91\xD7\x92";
    CHECK(text_layout_shape(layout, mixed, sizeof mixed - 1, TEXT_LAYOUT_AUTO,
                            "he", &result, error, sizeof error) == 0);
    CHECK(result.resolved_direction == TEXT_LAYOUT_LTR);
#ifdef OS999_TEXT_FRIBIDI
    int saw_rtl = 0;
    for (size_t i = 0; i < result.count; i++) saw_rtl |= result.glyphs[i].bidi_level & 1u;
    CHECK(saw_rtl);
#endif
    text_layout_result_destroy(&result);

    const char devanagari[] = "\xE0\xA4\x95\xE0\xA4\xBF"; /* KA + pre-base I. */
    CHECK(text_layout_shape(layout, devanagari, sizeof devanagari - 1,
                            TEXT_LAYOUT_LTR, "hi", &result, error, sizeof error) == 0);
    CHECK(result.count > 0 && cluster(&result, 0) != NULL);
#ifdef OS999_TEXT_HARFBUZZ
    uint16_t devanagari_font = result.glyphs[0].font_index;
    for (size_t i = 0; i < result.count; i++) {
        CHECK(result.glyphs[i].font_index == devanagari_font);
        CHECK(result.glyphs[i].glyph_id != 0);
    }
#endif
    text_layout_result_destroy(&result);

    const char lines[] = "one two\nthree";
    CHECK(text_layout_shape(layout, lines, sizeof lines - 1, TEXT_LAYOUT_LTR,
                            "en", &result, error, sizeof error) == 0);
    int saw_allow = 0, saw_must = 0;
    for (size_t i = 0; i < result.count; i++) {
        saw_allow |= result.glyphs[i].break_after == TEXT_LAYOUT_BREAK_ALLOW;
        saw_must |= result.glyphs[i].break_after == TEXT_LAYOUT_BREAK_MUST;
    }
    CHECK(saw_allow && saw_must);
    text_layout_result_destroy(&result);

    CHECK(text_layout_shape(layout, "\xED\xA0\x80", 3, TEXT_LAYOUT_LTR, "en",
                            &result, error, sizeof error) == -1);
    CHECK(strstr(error, "UTF-8") != NULL);
    text_layout_destroy(layout);
}

static void test_api_errors(void) {
    char error[128];
    CHECK(text_layout_create(NULL, 0, 12, error, sizeof error) == NULL);
    CHECK(error[0] != 0);
    TextLayoutFontSpec bad = {"fonts/system/not-present.ttf", 0};
    TextLayout *layout = text_layout_create(&bad, 1, 12, error, sizeof error);
#ifdef OS999_TEXT_HARFBUZZ
    CHECK(layout == NULL && error[0] != 0);
#else
    CHECK(layout != NULL);
    text_layout_destroy(layout);
#endif
}

int main(void) {
    unsigned capabilities = text_layout_capabilities();
#ifdef OS999_TEXT_HARFBUZZ
    CHECK(capabilities & TEXT_LAYOUT_HAS_HARFBUZZ);
#endif
#ifdef OS999_TEXT_FRIBIDI
    CHECK(capabilities & TEXT_LAYOUT_HAS_BIDI);
#endif
#ifdef OS999_TEXT_UNIBREAK
    CHECK(capabilities & TEXT_LAYOUT_HAS_LINEBREAK);
#endif
    test_breaks();
    test_api_errors();
    test_layout();
    printf("text layout: %d checks, %d failures, capabilities=0x%x\n",
           checks, failures, capabilities);
    return failures ? 1 : 0;
}
