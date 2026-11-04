#include "draw.h"

#include <stddef.h>

static int take_line(const char *s, int max_w, int *draw_n, int *advance) {
    if (*s == '\n') {
        *draw_n = 0;
        *advance = 1;
        return 1;
    }
    int bytes = 0;
    int width = 0;
    int last_space = -1;
    int limit = max_w < 8 ? 8 : max_w;
    while (s[bytes] && s[bytes] != '\n') {
        int adv = 0;
        const uint8_t *rows = 0;
        if (!font_next(s + bytes, &adv, &rows) || adv <= 0) {
            break;
        }
        if (width + 8 > limit && bytes > 0) {
            break;
        }
        if (s[bytes] == ' ') {
            last_space = bytes;
        }
        bytes += adv;
        width += 8;
    }
    if (bytes == 0) {
        return 0;
    }
    if (s[bytes] && s[bytes] != '\n' && last_space > 0) {
        *draw_n = last_space;
        *advance = last_space + 1;
        return 1;
    }
    *draw_n = bytes;
    *advance = bytes;
    return 1;
}

int text_height(const char *text, int max_w) {
    if (!text || !text[0]) {
        return 0;
    }
    const char *p = text;
    int lines = 0;
    while (*p) {
        int draw_n = 0, adv = 0;
        if (!take_line(p, max_w, &draw_n, &adv) || adv <= 0) {
            break;
        }
        p += adv;
        lines++;
    }
    return lines * 10;
}

void text_draw(void *fb_ptr, int x, int y, int max_w, uint32_t color, const char *text) {
    Fb *fb = fb_ptr;
    if (!text) {
        return;
    }
    const char *p = text;
    int cy = y;
    while (*p) {
        int draw_n = 0, adv = 0;
        if (!take_line(p, max_w, &draw_n, &adv) || adv <= 0) {
            break;
        }
        if (draw_n > 0) {
            fb_text_n(fb, x, cy, color, p, draw_n);
        }
        p += adv;
        cy += 10;
    }
}
