#include "mac_dlg_int.h"
#include <string.h>

/* Dialog drawing and the rectangular window regions of a dialog (moved out of mac_dlgmgr.c; see there for the model). */
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
#define REGION_OWNED UINT32_C(0x4f52474e)
/* A rectangular region of our own in the handle table, so the guest's region calls on a dialog's window regions work like any other. */
uint32_t mac_dlg_new_rect_region(MacRuntime *rt, int t, int l, int b, int r) {
    size_t slot = 0; while (slot < MAC_RUNTIME_HANDLES && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    uint32_t block = slot < MAC_RUNTIME_HANDLES ? mac_runtime_alloc(rt, 16) : 0;
    if (!block) return 0;
    uint32_t p = block + 4; wr(rt, block, 4, p); wr(rt, p, 2, 10);
    wr(rt, p + 2, 2, (uint16_t)t); wr(rt, p + 4, 2, (uint16_t)l); wr(rt, p + 6, 2, (uint16_t)b); wr(rt, p + 8, 2, (uint16_t)r);
    rt->handles[slot] = (MacHandle){.handle = block, .pointer = p, .size = 10, .type = REGION_OWNED};
    if (rt->handle_count <= slot) rt->handle_count = (unsigned)slot + 1;
    return block;
}
void mac_dlg_set_region(MacRuntime *rt, uint32_t h, int t, int l, int b, int r) {   /* rewrite our own rectangular region, found through the host's table */
    for (size_t i = 0; h && i < rt->handle_count; i++) if (rt->handles[i].handle == h && rt->handles[i].size >= 10) {
        uint32_t p = rt->handles[i].pointer; wr(rt, p, 2, 10); wr(rt, p + 2, 2, (uint16_t)t); wr(rt, p + 4, 2, (uint16_t)l); wr(rt, p + 6, 2, (uint16_t)b); wr(rt, p + 8, 2, (uint16_t)r);
    }
}
void mac_dlg_free_region(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; h && i < rt->handle_count; i++) if (rt->handles[i].handle == h) rt->handles[i] = (MacHandle){0};
}

unsigned mac_dlg_expand(const MacRuntime *rt, const Dlg *d, unsigned k, char *out, unsigned cap) {
    unsigned n = 0;
    for (unsigned i = 0; i < d->length[k] && n + 1 < cap; i++) {
        char c = d->text[k][i];
        if (c == '^' && i + 1 < d->length[k] && d->text[k][i + 1] >= '0' && d->text[k][i + 1] <= '3') {
            const unsigned char *param = (const unsigned char *)rt->dialog_params[d->text[k][++i] - '0'];
            for (unsigned q = 0; q < param[0] && n + 1 < cap; q++) out[n++] = (char)param[q + 1];
        } else out[n++] = c;
    }
    return n;
}
void mac_dlg_draw(MacRuntime *rt, Dlg *d) {
    if (d->hidden) return;
    MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height);
    if (!d->drawn) { mac_pen_save(&pen, d->backdrop); d->drawn = 1; }
    mac_pen_frame(&pen);
    for (unsigned k = 0; k < d->ditl.count; k++) {
        const MacDitlItem *it = &d->ditl.items[k];
        if (d->item_hidden[k]) continue;
        int l = d->left + it->left, t = d->top + it->top, r = d->left + it->right, b = d->top + it->bottom;
        char text[512]; unsigned n = has_text(it) ? mac_dlg_expand(rt, d, k, text, sizeof text) : 0;
        if (it->kind == MAC_ITEM_BUTTON) mac_pen_button(&pen, l, t, r, b, (const uint8_t *)text, n, !it->disabled, d->modal && d->pressed == (int)k + 1, (int)k + 1 == d->def_item);
        else if (it->kind == MAC_ITEM_STATIC) mac_pen_wrap(&pen, l, t, r, b, text, n);
        else if (it->kind == MAC_ITEM_EDIT) {
            mac_pen_box(&pen, l - 2, t - 2, r + 2, b + 2, pen.dark);
            mac_pen_text(&pen, l, t, text, n, pen.dark, 0);
            if ((int)k + 1 == d->edit_item) mac_pen_fill(&pen, l + (int)n * 8, t, l + (int)n * 8 + 2, t + 9, pen.dark);
        } else if (it->kind == MAC_ITEM_CHECK || it->kind == MAC_ITEM_RADIO) {
            mac_pen_box(&pen, l, t + 1, l + 10, t + 11, pen.dark);
            if (d->value[k]) mac_pen_fill(&pen, l + 3, t + 4, l + 7, t + 8, pen.dark);
            mac_pen_text(&pen, l + 16, t + 2, text, n, pen.dark, it->disabled);
        } else if (it->kind == MAC_ITEM_ICON) {
            MacResource icon;
            if (!mac_resource_find(&rt->fork, ICON_TYPE, (int16_t)be16(it->data), &icon) && icon.size == 128) mac_pen_icon(&pen, l, t, icon.data);
        }
    }
}

