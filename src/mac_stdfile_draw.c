#include "mac_stdfile_int.h"
#include "mac_dialog_draw.h"
#include <string.h>

/* Independently authored dialog chrome: not Apple's art or definition procedure (shared primitives: mac_dialog_draw.c). */
typedef struct { MacPen pen; const MacSfState *s; } Pen;
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
    int l, t, r, b; item_rect(p->s, it, &l, &t, &r, &b);
    mac_pen_button(&p->pen, l, t, r, b, it->data, it->length, enabled, pressed, number == SF_OPEN && enabled);
}
static void list_area(const Pen *p, const MacDitlItem *it) {
    const MacSfState *s = p->s; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    mac_pen_fill(&p->pen, l, t, r, b, p->pen.light); mac_pen_box(&p->pen, l, t, r, b, p->pen.dark);
    unsigned chars = (unsigned)(r - l - 8) / 8;
    for (unsigned i = 0; i < s->list.rows; i++) {
        unsigned index = s->list.top + i; if (index >= s->list.count) break;
        const char *name = s->list.entries[index].name; unsigned n = (unsigned)strlen(name); if (n > chars) n = chars;
        int y = t + 1 + (int)i * SF_ROW, selected = (int)index == s->list.selected;
        if (y + SF_ROW > b) break;
        if (selected) mac_pen_fill(&p->pen, l + 1, y, r - 1, y + SF_ROW, p->pen.dark);
        mac_pen_text(&p->pen, l + 4, y + (SF_ROW - 8) / 2, name, n, selected ? p->pen.light : p->pen.dark, 0);
    }
}
static void arrow(const Pen *p, int l, int t, int r, int b, int up) {
    mac_pen_box(&p->pen, l, t, r, b, p->pen.dark);
    int cx = (l + r) / 2, cy = (t + b) / 2;
    for (int k = 0; k < 4; k++) {
        int y = up ? cy - 2 + k : cy + 1 - k;
        mac_pen_fill(&p->pen, cx - k, y, cx + k + 1, y + 1, p->pen.dark);
    }
}
static void scroll_bar(const Pen *p, const MacDitlItem *it) {
    const MacSfState *s = p->s; int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
    mac_pen_fill(&p->pen, l, t, r, b, p->pen.light); mac_pen_box(&p->pen, l, t, r, b, p->pen.dark);
    arrow(p, l, t, r, t + SF_ROW, 1); arrow(p, l, b - SF_ROW, r, b, 0);
    unsigned max = mac_sflist_max_top(&s->list);
    if (!max) return; /* everything fits: no thumb */
    unsigned track = (unsigned)(b - t - 2 * SF_ROW), thumb = track * s->list.rows / s->list.count; if (thumb < SF_ROW) thumb = SF_ROW;
    int from = t + SF_ROW + (int)((track - thumb) * s->list.top / max);
    mac_pen_fill(&p->pen, l + 1, from, r - 1, from + (int)thumb, p->pen.dark);
    mac_pen_fill(&p->pen, l + 3, from + 2, r - 3, from + (int)thumb - 2, p->pen.light);
}
void mac_stdfile_draw(MacRuntime *rt, const MacSfState *s) {
    Pen pen = { {0}, s };
    mac_pen_init(&pen.pen, rt, s->left, s->top, s->width, s->height);
    const Pen *p = &pen;
    mac_pen_frame(&p->pen);
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
            mac_pen_text(&p->pen, l, t, msg, n < fit ? n : fit, p->pen.dark, 0);
        } else if (number == SF_DISK) {
            /* The disk name starts clear of the scroll bar, which the template's item rectangle overlaps. */
            int from = l;
            if (s->ditl.count >= SF_SCROLL) { const MacDitlItem *sb = &s->ditl.items[SF_SCROLL - 1]; if (s->left + sb->right + 2 > from) from = s->left + sb->right + 2; }
            mac_pen_text(&p->pen, from, t + 2, s->volume, (unsigned)strlen(s->volume), p->pen.dark, 0);
        }
        else if (it->kind == MAC_ITEM_USER && b - t == 1) mac_pen_fill(&p->pen, l, t, r, b, p->pen.dark); /* divider line */
        else if (it->kind == MAC_ITEM_STATIC && it->length) mac_pen_text(&p->pen, l, t, (const char *)it->data, it->length, p->pen.dark, 0);
    }
}
void mac_stdfile_save(MacRuntime *rt, MacSfState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height); mac_pen_save(&pen, s->backdrop);
}
void mac_stdfile_restore(MacRuntime *rt, const MacSfState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height); mac_pen_restore(&pen, s->backdrop);
}
