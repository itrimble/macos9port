#include "mac_stdfile_int.h"
#include <string.h>

extern const uint8_t font8x8_basic[128][8]; /* existing public-domain font */
/* Independently authored dialog chrome: not Apple's art or definition procedure. Everything is clipped to the dialog
 * rectangle and the physical screen. */
typedef struct { MacRuntime *rt; const MacSfState *s; uint8_t dark, light; } Pen;

static void fill(const Pen *p, int l, int t, int r, int b, uint8_t ink) {
    const MacSfState *s = p->s;
    if (l < s->left) l = s->left;
    if (t < s->top) t = s->top;
    if (r > s->left + s->width) r = s->left + s->width;
    if (b > s->top + s->height) b = s->top + s->height;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > p->rt->screen_width) r = p->rt->screen_width;
    if (b > p->rt->screen_height) b = p->rt->screen_height;
    uint8_t *ram = mac_cpu_ram(p->rt->cpu);
    for (int y = t; y < b; y++)
        for (int x = l; x < r; x++) ram[p->rt->screen_base + (uint32_t)y * p->rt->screen_row_bytes + (uint32_t)x] = ink;
}
static void box(const Pen *p, int l, int t, int r, int b, uint8_t ink) {
    fill(p, l, t, r, t + 1, ink); fill(p, l, b - 1, r, b, ink);
    fill(p, l, t, l + 1, b, ink); fill(p, r - 1, t, r, b, ink);
}
static void text(const Pen *p, int x, int y, const char *s, unsigned n, uint8_t ink, int dim) {
    for (unsigned i = 0; i < n; i++) {
        unsigned c = (unsigned char)s[i]; if (c >= 128) c = '?';
        for (int row = 0; row < 8; row++)
            for (int bit = 0; bit < 8; bit++)
                if ((font8x8_basic[c][row] & (1u << bit)) && !(dim && ((bit + row) & 1)))
                    fill(p, x + (int)i * 8 + bit, y + row, x + (int)i * 8 + bit + 1, y + row + 1, ink);
    }
}
static void item_rect(const MacSfState *s, const MacDitlItem *it, int *l, int *t, int *r, int *b) {
    *l = s->left + it->left; *t = s->top + it->top; *r = s->left + it->right; *b = s->top + it->bottom;
}
static int inside(const MacSfState *s, const MacDitlItem *it) {
    return it->left >= 0 && it->top >= 0 && it->right <= s->width && it->bottom <= s->height && it->right > it->left && it->bottom > it->top;
}
int mac_stdfile_item_enabled(const MacSfState *s, unsigned item) {
    if (item == SF_OPEN) return s->list.selected >= 0;
    if (item == SF_CANCEL) return 1;
    return 0; /* one fixed, non-ejectable volume: Eject and Drive are dimmed, as on a one-disk Mac */
}
unsigned mac_stdfile_hit(const MacSfState *s, int x, int y, unsigned *row) {
    for (unsigned i = 0; i < s->ditl.count; i++) {
        const MacDitlItem *it = &s->ditl.items[i];
        int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
        if (!inside(s, it) || x < l || x >= r || y < t || y >= b) continue;
        unsigned number = i + 1;
        if (number == SF_LIST) { *row = (unsigned)(y - t - 1) / SF_ROW; return number; }
        if (number == SF_SCROLL || ((number == SF_OPEN || number == SF_CANCEL || number == SF_EJECT || number == SF_DRIVE) &&
                                    it->kind == MAC_ITEM_BUTTON)) return number;
    }
    return 0;
}
int mac_stdfile_scroll_zone(const MacSfState *s, int x, int y) {
    if (s->ditl.count < SF_SCROLL) return 0;
    const MacDitlItem *it = &s->ditl.items[SF_SCROLL - 1];
    int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    if (!inside(s, it) || x < l || x >= r || y < t || y >= b) return 0;
    if (y < t + SF_ROW) return 1;
    if (y >= b - SF_ROW) return 2;
    unsigned track = (unsigned)(b - t - 2 * SF_ROW), max = mac_sflist_max_top(&s->list);
    if (!max || s->list.count <= s->list.rows) return 0;
    unsigned thumb = track * s->list.rows / s->list.count; if (thumb < SF_ROW) thumb = SF_ROW;
    unsigned at = (track - thumb) * s->list.top / max, from = (unsigned)(t + SF_ROW) + at;
    if ((unsigned)y < from) return 3;
    if ((unsigned)y >= from + thumb) return 4;
    return 0;
}
static void button(const Pen *p, const MacDitlItem *it, unsigned number, int enabled, int pressed) {
    const MacSfState *s = p->s; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    uint8_t face = pressed ? p->dark : p->light, ink = pressed ? p->light : p->dark;
    fill(p, l, t, r, b, face);
    box(p, l, t, r, b, p->dark);
    fill(p, l, t, l + 1, t + 1, face); fill(p, r - 1, t, r, t + 1, face);       /* rounded corners */
    fill(p, l, b - 1, l + 1, b, face); fill(p, r - 1, b - 1, r, b, face);
    if (number == SF_OPEN && enabled) box(p, l - 3, t - 3, r + 3, b + 3, p->dark); /* the default button */
    unsigned n = it->length; int w = (int)n * 8;
    text(p, l + (r - l - w) / 2, t + (b - t - 8) / 2, (const char *)it->data, n, ink, !enabled);
}
static void list_area(const Pen *p, const MacDitlItem *it) {
    const MacSfState *s = p->s; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    fill(p, l, t, r, b, p->light); box(p, l, t, r, b, p->dark);
    unsigned chars = (unsigned)(r - l - 8) / 8;
    for (unsigned i = 0; i < s->list.rows; i++) {
        unsigned index = s->list.top + i; if (index >= s->list.count) break;
        const char *name = s->list.entries[index].name; unsigned n = (unsigned)strlen(name); if (n > chars) n = chars;
        int y = t + 1 + (int)i * SF_ROW, selected = (int)index == s->list.selected;
        if (y + SF_ROW > b) break;
        if (selected) fill(p, l + 1, y, r - 1, y + SF_ROW, p->dark);
        text(p, l + 4, y + (SF_ROW - 8) / 2, name, n, selected ? p->light : p->dark, 0);
    }
}
static void arrow(const Pen *p, int l, int t, int r, int b, int up) {
    box(p, l, t, r, b, p->dark);
    int cx = (l + r) / 2, cy = (t + b) / 2;
    for (int k = 0; k < 4; k++) {
        int y = up ? cy - 2 + k : cy + 1 - k;
        fill(p, cx - k, y, cx + k + 1, y + 1, p->dark);
    }
}
static void scroll_bar(const Pen *p, const MacDitlItem *it) {
    const MacSfState *s = p->s; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    fill(p, l, t, r, b, p->light); box(p, l, t, r, b, p->dark);
    arrow(p, l, t, r, t + SF_ROW, 1); arrow(p, l, b - SF_ROW, r, b, 0);
    unsigned max = mac_sflist_max_top(&s->list);
    if (!max) return; /* everything fits: no thumb */
    unsigned track = (unsigned)(b - t - 2 * SF_ROW), thumb = track * s->list.rows / s->list.count; if (thumb < SF_ROW) thumb = SF_ROW;
    int from = t + SF_ROW + (int)((track - thumb) * s->list.top / max);
    fill(p, l + 1, from, r - 1, from + (int)thumb, p->dark);
    fill(p, l + 3, from + 2, r - 3, from + (int)thumb - 2, p->light);
}
void mac_stdfile_draw(MacRuntime *rt, const MacSfState *s) {
    Pen pen = { rt, s, 0, 0 };
    unsigned min = UINT32_MAX, max = 0;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = rt->palette[i]; unsigned v = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255);
        if (v < min) { min = v; pen.dark = (uint8_t)i; }
        if (v >= max) { max = v; pen.light = (uint8_t)i; }
    }
    const Pen *p = &pen;
    fill(p, s->left, s->top, s->left + s->width, s->top + s->height, p->light);
    box(p, s->left, s->top, s->left + s->width, s->top + s->height, p->dark);
    box(p, s->left + 3, s->top + 3, s->left + s->width - 3, s->top + s->height - 3, p->dark);
    for (unsigned i = 0; i < s->ditl.count; i++) {
        const MacDitlItem *it = &s->ditl.items[i];
        unsigned number = i + 1; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
        if (!inside(s, it)) continue; /* off-dialog coordinates are how an application hides an item */
        if (number == SF_LIST) list_area(p, it);
        else if (number == SF_SCROLL) scroll_bar(p, it);
        else if (it->kind == MAC_ITEM_BUTTON) button(p, it, number, mac_stdfile_item_enabled(s, number), s->pressed == (int)number);
        else if (number == SF_PROMPT && it->kind == MAC_ITEM_STATIC) {
            /* The item's own text wins; the prompt argument is only the fallback for a template item with none. */
            const char *msg = it->length ? (const char *)it->data : s->prompt;
            unsigned n = it->length ? it->length : (unsigned)strlen(s->prompt), fit = (unsigned)(r - l) / 8;
            text(p, l, t, msg, n < fit ? n : fit, p->dark, 0);
        } else if (number == SF_DISK) {
            /* The disk name starts clear of the scroll bar, which the template's item rectangle overlaps. */
            int from = l;
            if (s->ditl.count >= SF_SCROLL) { const MacDitlItem *sb = &s->ditl.items[SF_SCROLL - 1]; if (s->left + sb->right + 2 > from) from = s->left + sb->right + 2; }
            text(p, from, t + 2, s->volume, (unsigned)strlen(s->volume), p->dark, 0);
        }
        else if (it->kind == MAC_ITEM_USER && b - t == 1) fill(p, l, t, r, b, p->dark); /* divider line */
        else if (it->kind == MAC_ITEM_STATIC && it->length) text(p, l, t, (const char *)it->data, it->length, p->dark, 0);
    }
}
void mac_stdfile_save(MacRuntime *rt, MacSfState *s) {
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (int y = 0; y < s->height; y++)
        memcpy(s->backdrop + (size_t)y * (size_t)s->width, ram + rt->screen_base + (uint32_t)(s->top + y) * rt->screen_row_bytes + (uint32_t)s->left, (size_t)s->width);
}
void mac_stdfile_restore(MacRuntime *rt, const MacSfState *s) {
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (int y = 0; y < s->height; y++)
        memcpy(ram + rt->screen_base + (uint32_t)(s->top + y) * rt->screen_row_bytes + (uint32_t)s->left, s->backdrop + (size_t)y * (size_t)s->width, (size_t)s->width);
}
