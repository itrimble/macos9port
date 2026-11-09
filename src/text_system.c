#include "text_system.h"
#include "text_layout.h"

#include <stdlib.h>
#include <string.h>

enum { SYSTEM_FALLBACK_LIMIT = 64, SYSTEM_TEXT_PIXELS = 12 };
static TextLayout *g_layout;
static unsigned g_capabilities;

static int duplicate(const TextLayoutFontSpec *fonts, size_t count,
                     const FontBookEntry *entry) {
    for (size_t i = 0; i < count; i++)
        if (fonts[i].face_index == entry->face_index && !strcmp(fonts[i].path, entry->path)) return 1;
    return 0;
}

static int preferred(const FontBookEntry *entry, int pass) {
    int base = strstr(entry->path, "NotoSans-Regular.ttf") != NULL;
    int noto = strstr(entry->family, "Noto") != NULL;
    return pass == 0 ? base : pass == 1 ? noto && !base : !noto;
}

int text_system_init(const FontBookCatalog *catalog) {
    text_system_shutdown();
    if (!catalog) return -1;
    TextLayoutFontSpec fonts[SYSTEM_FALLBACK_LIMIT];
    size_t count = 0;
    for (int pass = 0; pass < 3; pass++) for (int i = 0; i < catalog->count; i++) {
        const FontBookEntry *entry = &catalog->fonts[i];
        if (!entry->active || !entry->valid || !entry->previewable ||
            !preferred(entry, pass) || duplicate(fonts, count, entry)) continue;
        fonts[count++] = (TextLayoutFontSpec){entry->path, entry->face_index};
        if (count == SYSTEM_FALLBACK_LIMIT) break;
    }
    if (!count) return -1;
    char error[160];
    g_layout = text_layout_create(fonts, count, SYSTEM_TEXT_PIXELS, error, sizeof error);
    g_capabilities = text_layout_capabilities();
    return g_layout ? 0 : -1;
}

void text_system_shutdown(void) {
    text_layout_destroy(g_layout);
    g_layout = NULL;
    g_capabilities = text_layout_capabilities();
}

int text_system_available(void) {
    return g_layout && (g_capabilities & TEXT_LAYOUT_HAS_HARFBUZZ);
}

unsigned text_system_capabilities(void) { return g_capabilities; }

static int shape(const char *utf8, size_t length, TextLayoutResult *result) {
    char error[128];
    return g_layout && text_layout_shape(g_layout, utf8, length, TEXT_LAYOUT_AUTO,
                                         NULL, result, error, sizeof error) == 0;
}

int text_system_width(const char *utf8, size_t length) {
    TextLayoutResult result;
    if (!text_system_available() || !shape(utf8, length, &result)) return -1;
    int32_t advance = result.advance_x;
    text_layout_result_destroy(&result);
    if (advance < 0) advance = -advance;
    return (advance + 63) / 64;
}

int text_system_draw(Fb *fb, int x, int y, uint32_t color,
                     const char *utf8, size_t length) {
    TextLayoutResult result;
    if (!text_system_available() || !fb || !shape(utf8, length, &result)) return 0;
    int drawn = text_layout_draw(g_layout, &result, fb, x, y + SYSTEM_TEXT_PIXELS, color);
    text_layout_result_destroy(&result);
    return drawn;
}

int text_system_breaks(const char *utf8, size_t length, uint8_t *breaks) {
    if (!utf8 || !breaks) return -1;
    return text_layout_breaks(utf8, length, NULL, breaks, length);
}
