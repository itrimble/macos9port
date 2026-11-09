#include "draw.h"
#include "ospi.h"
#include "text_system.h"

#include <stddef.h>
#include <string.h>

static int utf8_advance(const char *text, size_t available) {
    const unsigned char *s = (const unsigned char *)text;
    if (!available) return 0;
    int need = s[0] < 0x80 ? 1 : s[0] >= 0xc2 && s[0] <= 0xdf ? 2 :
               s[0] >= 0xe0 && s[0] <= 0xef ? 3 :
               s[0] >= 0xf0 && s[0] <= 0xf4 ? 4 : 0;
    if (!need || (size_t)need > available) return 0;
    for (int i = 1; i < need; i++) if ((s[i] & 0xc0u) != 0x80u) return 0;
    return need;
}

static int take_line(const char *s, int max_w, int *draw_n, int *advance) {
    if (*s == '\n') {
        *draw_n = 0;
        *advance = 1;
        return 1;
    }
    int bytes = 0;
    int width = 0;
    int last_break = -1;
    int limit = max_w < 8 ? 8 : max_w;
    size_t span = strcspn(s, "\n");
    if (span >= OSPI_TEXT) span = OSPI_TEXT - 1;
    uint8_t breaks[OSPI_TEXT];
    int modern_breaks = span && text_system_breaks(s, span, breaks) == 0;
    while (s[bytes] && s[bytes] != '\n') {
        int adv = utf8_advance(s + bytes, span - (size_t)bytes);
        int legacy_adv = 0;
        int px = 0;
        int legacy = typeface_next(1, s + bytes, &legacy_adv, &px);
        if (!adv) adv = legacy ? legacy_adv : 0;
        if (adv <= 0) break;
        if (!legacy || legacy_adv != adv) px = 8;
        int start = bytes;
        int modern_width = text_system_width(s, (size_t)(bytes + adv));
        int candidate_width = modern_width >= 0 ? modern_width : width + px;
        if (candidate_width > limit && bytes > 0) break;
        bytes += adv;
        width = candidate_width;
        if ((modern_breaks && (size_t)start < span && breaks[start]) || s[start] == ' ')
            last_break = bytes;
    }
    if (bytes == 0) {
        return 0;
    }
    if (s[bytes] && s[bytes] != '\n' && last_break > 0) {
        *draw_n = last_break;
        *advance = last_break;
        return 1;
    }
    *draw_n = bytes;
    *advance = bytes + (s[bytes] == '\n');
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
    if (p > text && p[-1] == '\n') lines++;
    return lines * typeface_line_height(1);
}

void text_draw(void *fb_ptr, int x, int y, int max_w, uint32_t color, const char *text) {
    Fb *fb = fb_ptr;
    if (!text) {
        return;
    }
    const char *p = text;
    int cy = y, line_height = typeface_line_height(1);
    int top = fb->clip ? fb->cy : 0, bottom = fb->clip ? fb->cy + fb->ch : fb->h;
    while (*p && cy < bottom) {
        int draw_n = 0, adv = 0;
        if (!take_line(p, max_w, &draw_n, &adv) || adv <= 0) {
            break;
        }
        if (draw_n > 0 && cy + line_height > top) {
            if (!text_system_draw(fb, x, cy, color, p, (size_t)draw_n))
                fb_text_n(fb, x, cy, color, p, draw_n);
        }
        p += adv;
        cy += line_height;
    }
}

/* Shared layout for painting and editing: offsets are UTF-8 byte boundaries. */
void text_caret(const char *text, int width, size_t offset, int *x, int *y) {
    const char *p = text; size_t at = 0; *x = *y = 0;
    while (*p) {
        int n, adv;
        if (!take_line(p, width, &n, &adv) || adv <= 0) break;
        if (offset < at + (size_t)adv || !p[adv]) {
            if (offset >= at + (size_t)adv && adv > 0 && p[adv-1] == '\n') {
                *y += typeface_line_height(1); return;
            }
            size_t end = offset > at ? offset-at : 0;
            if (end > (size_t)n) end = (size_t)n;
            for (size_t i = 0; i < end;) { int bytes, px;
                if (!typeface_next(1, p+i, &bytes, &px) || bytes <= 0) break;
                *x += px; i += (size_t)bytes;
            }
            return;
        }
        at += (size_t)adv; p += adv; *y += typeface_line_height(1);
    }
}
size_t text_offset(const char *text, int width, int x, int y) {
    const char *p = text; size_t at = 0; int cy = 0;
    while (*p) {
        int n, adv;
        if (!take_line(p,width,&n,&adv) || adv <= 0) break;
        if (y < cy + typeface_line_height(1)) {
            int px = 0; size_t i = 0;
            while (i < (size_t)n) { int bytes, step;
                if (!typeface_next(1,p+i,&bytes,&step) || bytes <= 0 || x < px+step/2) break;
                px += step; i += (size_t)bytes;
            }
            return at+i;
        }
        p += adv; at += (size_t)adv; cy += typeface_line_height(1);
    }
    return at;
}
