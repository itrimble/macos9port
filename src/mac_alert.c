#include "mac_dialog_draw.h"
#include "mac_alert_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Alert ($A985), StopAlert ($A986), NoteAlert ($A987), CautionAlert ($A988) as a native modal dialog.
 *
 * Pascal FUNCTION Alert(alertID: INTEGER; filterProc: ProcPtr): INTEGER. Stack from SP: filterProc4, alertID2, result2.
 * The ALRT resource is: bounds rect (top, left, bottom, right), the DITL resource ID, a stages word and, in a 14-byte
 * variant, an alert-position word. The DITL items are positioned relative to the alert's top-left corner.
 *
 * Behaviour, with our own limits stated:
 * - The alert is drawn at its authored rectangle, moved onto the 640x480 screen if it would stick out. A position word is
 *   ignored (not interpreted); the alert is not recentred.
 * - The four stage nibbles of `stages` must agree (every alert in the six apps checked does). Bit 3 of a nibble picks the
 *   default button (item 2 instead of 1), bit 2 says the alert is drawn (an undrawn stage halts), bits 1-0 name a beep count:
 *   alert sounds are not played.
 * - Buttons act on mouse-up inside the button; Return and Enter press the default button. Escape and Cmd-period do nothing.
 * - Static and edit-text items show their text word-wrapped in the 8x8 font, with ^0 to ^3 replaced from ParamText. Icon items
 *   draw the application's own ICON resource if it has one; the system's stop, note and caution icons are not drawn, and
 *   StopAlert, NoteAlert and CautionAlert add no icon of their own. Picture, control and user items are not drawn.
 * - The trap rewinds and yields while the alert is open, as Standard File does, so with no input a run stays in the alert.
 * - A filter procedure halts with a message. The result is the number of the button hit. */
enum { ALRT_TYPE = 0x414c5254, DITL_TYPE = 0x4449544c, ICON_TYPE = 0x49434f4e };


static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }

void mac_alert_destroy(MacRuntime *rt) {
    MacAlertState *s = rt->alert;
    if (!s) return;
    free(s->backdrop); free(s);
    rt->alert = NULL;
}

static void item_rect(const MacAlertState *s, const MacDitlItem *it, int *l, int *t, int *r, int *b) {
    *l = s->left + it->left; *t = s->top + it->top; *r = s->left + it->right; *b = s->top + it->bottom;
}
static int inside(const MacAlertState *s, const MacDitlItem *it) {
    return it->left >= 0 && it->top >= 0 && it->right <= s->width && it->bottom <= s->height && it->right > it->left && it->bottom > it->top;
}
static int is_button(const MacDitlItem *it) { return it->kind == MAC_ITEM_BUTTON; }
static int enabled(const MacDitlItem *it) { return is_button(it) && !it->disabled; }

/* Item text with ^0..^3 replaced from ParamText; returns the length written (bounded). */
static unsigned expand(const MacRuntime *rt, const MacDitlItem *it, char *out, unsigned cap) {
    unsigned n = 0;
    for (unsigned i = 0; i < it->length && n + 1 < cap; i++) {
        char c = (char)it->data[i];
        if (c == '^' && i + 1 < it->length && it->data[i + 1] >= '0' && it->data[i + 1] <= '3') {
            const unsigned char *param = (const unsigned char *)rt->dialog_params[it->data[++i] - '0'];
            for (unsigned k = 0; k < param[0] && n + 1 < cap; k++) out[n++] = (char)param[k + 1];
        } else out[n++] = c;
    }
    return n;
}

static void draw(MacRuntime *rt, const MacAlertState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height);
    mac_pen_frame(&pen);
    for (unsigned i = 0; i < s->ditl.count; i++) {
        const MacDitlItem *it = &s->ditl.items[i];
        int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
        if (!inside(s, it)) continue;
        if (is_button(it)) mac_pen_button(&pen, l, t, r, b, it->data, it->length, enabled(it), s->pressed == (int)i + 1, i + 1 == s->default_item);
        else if (it->kind == MAC_ITEM_STATIC || it->kind == MAC_ITEM_EDIT) {
            char text[1024]; unsigned n = expand(rt, it, text, sizeof text);
            mac_pen_wrap(&pen, l, t, r, b, text, n);
        } else if (it->kind == MAC_ITEM_ICON) {
            MacResource icon;
            if (!mac_resource_find(&rt->fork, ICON_TYPE, (int16_t)be16(it->data), &icon) && icon.size == 128) mac_pen_icon(&pen, l, t, icon.data);
        }
    }
}

static int begin(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    uint32_t filter = mac_cpu_read(rt->cpu, sp, 4);
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2);
    if (rt->trace) fprintf(stderr, "alert ALRT %d\n", id);
    if (!valid(rt, sp, 8)) return fail(rt, "Alert stack is invalid.");
    if (filter) return fail(rt, "Alert filter procedures are unsupported.");
    if (rt->screen_depth != 8 || rt->screen_width != 640 || rt->screen_height != 480 || rt->screen_row_bytes != 640 ||
        !valid(rt, rt->screen_base, 640 * 480))
        return fail(rt, "Alert requires the initialized 640x480 indexed display.");
    MacResource alrt, ditl; const char *why = NULL;
    if (mac_resource_find(&rt->fork, ALRT_TYPE, id, &alrt)) return fail(rt, "Alert resource (ALRT) not found.");
    if (alrt.size != 12 && alrt.size != 14) return fail(rt, "ALRT resource has an unsupported size.");
    int top = (int16_t)be16(alrt.data), left = (int16_t)be16(alrt.data + 2), bottom = (int16_t)be16(alrt.data + 4), right = (int16_t)be16(alrt.data + 6);
    int16_t items_id = (int16_t)be16(alrt.data + 8);
    unsigned stages = be16(alrt.data + 10), nibble = stages & 15;
    if (((stages >> 4) & 15) != nibble || ((stages >> 8) & 15) != nibble || ((stages >> 12) & 15) != nibble)
        return fail(rt, "Alert stages that differ from one another are unsupported.");
    if (!(nibble & 4)) return fail(rt, "Alerts whose box is not drawn are unsupported.");
    int width = right - left, height = bottom - top;
    if (width < 64 || height < 32 || width > 640 || height > 480) return fail(rt, "Alert rectangle is unsupported.");
    if (mac_resource_find(&rt->fork, DITL_TYPE, items_id, &ditl)) return fail(rt, "Alert item list (DITL) not found.");
    MacAlertState *s = calloc(1, sizeof *s);
    if (!s) return fail(rt, "Alert could not allocate its state.");
    rt->alert = s;
    if (mac_ditl_parse(ditl.data, ditl.size, &s->ditl, &why)) { mac_alert_destroy(rt); return fail(rt, why); }
    s->default_item = (nibble & 8) ? 2 : 1;
    if (s->default_item > s->ditl.count || !enabled(&s->ditl.items[s->default_item - 1])) {
        mac_alert_destroy(rt); return fail(rt, "Alert default item is not an enabled button.");
    }
    s->id = id; s->sp = sp; s->trap_pc = pc; s->width = width; s->height = height;
    if (top + height > 480) top = 480 - height;
    if (left + width > 640) left = 640 - width;
    s->top = top < 0 ? 0 : top; s->left = left < 0 ? 0 : left;
    s->backdrop = malloc((size_t)width * (size_t)height);
    if (!s->backdrop) { mac_alert_destroy(rt); return fail(rt, "Alert could not allocate its backdrop."); }
    return 0;
}

static int hit_button(const MacAlertState *s, int x, int y) {
    for (unsigned i = 0; i < s->ditl.count; i++) {
        const MacDitlItem *it = &s->ditl.items[i];
        int l, t, r, b; item_rect(s, it, &l, &t, &r, &b);
        if (enabled(it) && inside(s, it) && x >= l && x < r && y >= t && y < b) return (int)i + 1;
    }
    return 0;
}

/* One re-execution of the trap while the alert is open. Returns the chosen item, or 0 to keep waiting. */
static unsigned handle(MacAlertState *s, const MacEvent *e) {
    if (e->what == 3 || e->what == 5) {
        unsigned ch = e->message & 0xff;
        return ch == 13 || ch == 3 ? s->default_item : 0;
    }
    int item = hit_button(s, e->x, e->y);
    if (e->what == 1) { if (item) { s->pressed = item; s->dirty = 1; } return 0; }
    int pressed = s->pressed; s->pressed = 0;        /* mouse-up */
    if (pressed) s->dirty = 1;
    return pressed && item == pressed ? (unsigned)item : 0;
}

static int step(MacRuntime *rt, MacAlertState *s) {
    MacPen pen; mac_pen_init(&pen, rt, s->left, s->top, s->width, s->height);
    if (!s->drawn) { mac_pen_save(&pen, s->backdrop); s->drawn = 1; s->dirty = 1; }
    unsigned chosen = 0; MacEvent e;
    while (!chosen && mac_dialog_next_event(rt, &e)) chosen = handle(s, &e);
    if (!chosen) {
        if (s->dirty) { draw(rt, s); s->dirty = 0; }
        mac_cpu_set(rt->cpu, MAC_PC, s->trap_pc);
        mac_cpu_yield(rt->cpu);
        return 1;
    }
    mac_pen_restore(&pen, s->backdrop);
    uint32_t sp = s->sp;
    mac_alert_destroy(rt);
    mac_cpu_write(rt->cpu, sp + 6, 2, chosen);       /* the function result */
    mac_cpu_set(rt->cpu, MAC_A7, sp + 6);            /* pop filterProc and alertID */
    return 1;
}

int mac_alert_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode < 0xa985 || opcode > 0xa988) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), pc = (mac_cpu_get(rt->cpu, MAC_PC) - 2) & mac_cpu_addr_mask(rt->cpu);
    if ((sp & 1) || !valid(rt, sp, 8)) return fail(rt, "Alert stack is invalid.");
    if (rt->last_trap_pc >= 0x2000 && rt->last_trap_pc < 0x2000 + 4096 * 8)
        return fail(rt, "Alert reached through a patched trap vector is unsupported.");
    MacAlertState *s = rt->alert;
    if (s) {
        if (pc != s->trap_pc || sp != s->sp) return fail(rt, "Alert called again while an alert is open.");
        return step(rt, s);
    }
    if (rt->stdfile) return fail(rt, "Alert called while a Standard File dialog is open.");
    if (begin(rt, sp, pc)) return -1;
    return step(rt, rt->alert);
}
