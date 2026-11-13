#include "mac_dialog_draw.h"
#include "mac_chrome.h"
#include <string.h>


void mac_pen_init(MacPen *pen, MacRuntime *rt, int left, int top, int width, int height) {
    pen->rt = rt; pen->left = left; pen->top = top; pen->width = width; pen->height = height;
    unsigned min = UINT32_MAX, max = 0;
    pen->dark = pen->light = 0;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = rt->palette[i]; unsigned v = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
        if (v < min) { min = v; pen->dark = (uint8_t)i; }
        if (v >= max) { max = v; pen->light = (uint8_t)i; }
    }
}
void mac_pen_fill(const MacPen *p, int l, int t, int r, int b, uint8_t ink) {
    if (l < p->left) l = p->left;
    if (t < p->top) t = p->top;
    if (r > p->left + p->width) r = p->left + p->width;
    if (b > p->top + p->height) b = p->top + p->height;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > p->rt->screen_width) r = p->rt->screen_width;
    if (b > p->rt->screen_height) b = p->rt->screen_height;
    uint8_t *ram = mac_cpu_ram(p->rt->cpu);
    for (int y = t; y < b; y++)
        for (int x = l; x < r; x++) ram[p->rt->screen_base + (uint32_t)y * p->rt->screen_row_bytes + (uint32_t)x] = ink;
}
void mac_pen_box(const MacPen *p, int l, int t, int r, int b, uint8_t ink) {
    mac_pen_fill(p, l, t, r, t + 1, ink); mac_pen_fill(p, l, b - 1, r, b, ink);
    mac_pen_fill(p, l, t, l + 1, b, ink); mac_pen_fill(p, r - 1, t, r, b, ink);
}
void mac_pen_text(const MacPen *p, int x, int y, const char *s, unsigned n, uint8_t ink, int dim) {
    for (unsigned i = 0; i < n; i++) {
        uint8_t scratch[8];
        const uint8_t *glyph = mac_chrome_glyph((unsigned char)s[i], scratch);   /* Mac Roman bytes $80-$FF have glyphs too (mac_chrome.h) */
        for (int row = 0; row < 8; row++)
            for (int bit = 0; bit < 8; bit++)
                if ((glyph[row] & (1u << bit)) && !(dim && ((bit + row) & 1)))
                    mac_pen_fill(p, x + (int)i * 8 + bit, y + row, x + (int)i * 8 + bit + 1, y + row + 1, ink);
    }
}
void mac_pen_wrap(const MacPen *p, int l, int t, int r, int b, const char *s, unsigned n) {
    unsigned chars = r > l + 8 ? (unsigned)(r - l) / 8 : 1, at = 0;
    for (int y = t; y + 8 <= b && at < n; y += 10) {
        while (at < n && s[at] == ' ') at++;                      /* no leading spaces on a continuation line */
        unsigned end = at, last_space = 0;
        while (end < n && end - at < chars && s[end] != 13) { if (s[end] == ' ') last_space = end; end++; }
        if (end < n && s[end] != 13 && last_space > at) end = last_space; /* break at the last space that fits */
        mac_pen_text(p, l, y, s + at, end - at, p->dark, 0);
        at = end;
        if (at < n && s[at] == 13) at++;
    }
}
void mac_pen_button(const MacPen *p, int l, int t, int r, int b, const uint8_t *text, unsigned n, int enabled, int pressed, int is_default) {
    uint8_t face = pressed ? p->dark : p->light, ink = pressed ? p->light : p->dark;
    mac_pen_fill(p, l, t, r, b, face);
    mac_pen_box(p, l, t, r, b, p->dark);
    mac_pen_fill(p, l, t, l + 1, t + 1, face); mac_pen_fill(p, r - 1, t, r, t + 1, face);       /* rounded corners */
    mac_pen_fill(p, l, b - 1, l + 1, b, face); mac_pen_fill(p, r - 1, b - 1, r, b, face);
    if (is_default) mac_pen_box(p, l - 3, t - 3, r + 3, b + 3, p->dark);
    int w = (int)n * 8;
    mac_pen_text(p, l + (r - l - w) / 2, t + (b - t - 8) / 2, (const char *)text, n, ink, !enabled);
}
void mac_pen_frame(const MacPen *p) {
    mac_pen_fill(p, p->left, p->top, p->left + p->width, p->top + p->height, p->light);
    mac_pen_box(p, p->left, p->top, p->left + p->width, p->top + p->height, p->dark);
    mac_pen_box(p, p->left + 3, p->top + 3, p->left + p->width - 3, p->top + p->height - 3, p->dark);
}
void mac_pen_icon(const MacPen *p, int x, int y, const uint8_t bits[128]) {
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++)
            if (bits[row * 4 + col / 8] & (0x80u >> (col % 8))) mac_pen_fill(p, x + col, y + row, x + col + 1, y + row + 1, p->dark);
}
void mac_pen_save(const MacPen *p, uint8_t *buffer) {
    const uint8_t *ram = mac_cpu_ram(p->rt->cpu);
    for (int y = 0; y < p->height; y++)
        memcpy(buffer + (size_t)y * (size_t)p->width, ram + p->rt->screen_base + (uint32_t)(p->top + y) * p->rt->screen_row_bytes + (uint32_t)p->left, (size_t)p->width);
}
void mac_pen_restore(const MacPen *p, const uint8_t *buffer) {
    uint8_t *ram = mac_cpu_ram(p->rt->cpu);
    for (int y = 0; y < p->height; y++)
        memcpy(ram + p->rt->screen_base + (uint32_t)(p->top + y) * p->rt->screen_row_bytes + (uint32_t)p->left, buffer + (size_t)y * (size_t)p->width, (size_t)p->width);
}
