#include "font_book_window.h"
#include "text_layout.h"

#include <string.h>

int font_book_preview_draw(Fb *fb, const char *path, int face_index, Rect bounds, uint32_t color,
                           const char *sample) {
    if (!fb || !path || !sample) return 0;
    TextLayoutFontSpec font = {path, face_index};
    char error[128];
    TextLayout *layout = text_layout_create(&font, 1, 26, error, sizeof error);
    if (!layout) return 0;
    TextLayoutResult result;
    if (text_layout_shape(layout, sample, strlen(sample), TEXT_LAYOUT_AUTO, NULL,
                          &result, error, sizeof error) != 0) {
        text_layout_destroy(layout);
        return 0;
    }
    int prior_clip = fb->clip, px = fb->cx, py = fb->cy, pw = fb->cw, ph = fb->ch;
    fb_clip(fb, bounds.x, bounds.y, bounds.w, bounds.h);
    int drawn = text_layout_draw(layout, &result, fb, bounds.x + 8, bounds.y + 42, color);
    if (prior_clip) fb_clip(fb, px, py, pw, ph); else fb_unclip(fb);
    text_layout_result_destroy(&result);
    text_layout_destroy(layout);
    return drawn;
}
