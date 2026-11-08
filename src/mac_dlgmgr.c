#include "mac_dialog_draw.h"
#include "mac_dlgswallow.h"
#include "mac_ditl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A bounded Dialog Manager for modal dialogs built from DLOG and DITL resources: GetNewDialog $A97C, ModalDialog $A991, DrawDialog $A981,
 * CloseDialog $A982, DisposeDialog $A983, GetDItem $A98D, GetIText $A990, SetIText $A98F, SelIText $A97E, ErrorSound $A98C and
 * DialogDispatch $AA68 selector $0203 (GetStdFilterProc). Pascal argument order: the last argument is at SP and a function's result slot
 * is above the arguments. The stack layouts are the public ones as I know them; the interface listing here has only the declarations.
 *
 * Model. A DialogPtr is a block this runtime allocates in guest memory (or the guest's dStorage), zeroed, with windowKind 2 at +108. It is
 * only a name: everything else lives on the host, keyed by that pointer and checked against the dialogs actually open, so a guest that
 * edits or forges the record changes nothing. Items come from the DITL; each keeps a host copy of its text (static, edit, button, check,
 * radio) mirrored into a small guest block that GetDItem returns as the item's handle, and only the host copy is trusted. The dialog is drawn
 * with our own 8x8 chrome (mac_dialog_draw.c) at its DLOG rectangle moved onto the screen, over a saved backdrop restored on disposal.
 * ^0 to ^3 in static text come from ParamText. Buttons, checkboxes and radio buttons act on mouse-up inside the item; a checkbox toggles; Return
 * and Enter hit item 1 when it is an enabled button; edit text takes printable keys and Backspace and a click selects it.
 * A dialog whose DLOG is not visible draws nothing until ShowWindow (HideWindow puts the screen back); FrontWindow ignores it.
 * Not supported, and halting by name: user items and SetDItem, control items, filter procedures (other than the one GetStdFilterProc
 * hands out), NewDialog, a dStorage that is not zeroable RAM, more than four open
 * dialogs. Icons draw the application's ICON resource when it has one; PICT items are left blank. */
enum { DLOG_TYPE = 0x444c4f47, DITL_TYPE = 0x4449544c, ICON_TYPE = 0x49434f4e, MAX_OPEN = 4, TEXT_CAP = 255, RECORD = 170, FILTER_KEY = 0xbf1, FILTER_OP = 0xabf1 };
#define STD_FILTER mac_native_stub(FILTER_KEY)   /* a guest-callable stub: the standard ModalFilterProc */
typedef struct {
    uint32_t ptr; int16_t id; int left, top, width, height; int visible_flag;
    MacDitl ditl; uint8_t length[MAC_DITL_MAX_ITEMS]; char text[MAC_DITL_MAX_ITEMS][TEXT_CAP + 1]; int16_t value[MAC_DITL_MAX_ITEMS];
    uint32_t handle[MAC_DITL_MAX_ITEMS], block[MAC_DITL_MAX_ITEMS];   /* item handles and the text blocks they point at: the host's own record of both */
    uint32_t region[3];                                            /* strucRgn, contRgn, updateRgn handles, owned as ordinary regions */
    uint8_t item_hidden[MAC_DITL_MAX_ITEMS];                       /* HideDItem: not drawn, not hit, and its box reports 16384 pixels to the right */
    int edit_item, sel_start, sel_end, def_item, cancel_item, hidden;   /* hidden: drawn nothing until ShowWindow */
    uint8_t *backdrop; int drawn;
    int modal, pressed; uint32_t sp, pc, hit_ptr;
    uint32_t filter, scratch; MacEvent pend; int pend_real;     /* ModalDialog filterProc: the guest routine, its scratch (event + itemHit) and the event it is looking at */
    int select_busy; uint32_t select_dlg_ptr;                    /* DialogSelect waiting for the mouse-up of a press */
} Dlg;
typedef struct MacDlgMgr { Dlg *open[MAX_OPEN]; uint32_t port; /* the dialog SetPort last named, 0 once any other port is set */ } Mgr;
#define M(rt) ((Mgr *)(rt)->dlgmgr)

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }

#define REGION_OWNED UINT32_C(0x4f52474e)
/* A rectangular region of our own in the handle table, so the guest's region calls on a dialog's window regions work like any other. */
static uint32_t new_rect_region(MacRuntime *rt, int t, int l, int b, int r) {
    size_t slot = 0; while (slot < MAC_RUNTIME_HANDLES && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    uint32_t block = slot < MAC_RUNTIME_HANDLES ? mac_runtime_alloc(rt, 16) : 0;
    if (!block) return 0;
    uint32_t p = block + 4; wr(rt, block, 4, p); wr(rt, p, 2, 10);
    wr(rt, p + 2, 2, (uint16_t)t); wr(rt, p + 4, 2, (uint16_t)l); wr(rt, p + 6, 2, (uint16_t)b); wr(rt, p + 8, 2, (uint16_t)r);
    rt->handles[slot] = (MacHandle){.handle = block, .pointer = p, .size = 10, .type = REGION_OWNED};
    if (rt->handle_count <= slot) rt->handle_count = (unsigned)slot + 1;
    return block;
}
static void set_region(MacRuntime *rt, uint32_t h, int t, int l, int b, int r) {   /* rewrite our own rectangular region, found through the host's table */
    for (size_t i = 0; h && i < rt->handle_count; i++) if (rt->handles[i].handle == h && rt->handles[i].size >= 10) {
        uint32_t p = rt->handles[i].pointer; wr(rt, p, 2, 10); wr(rt, p + 2, 2, (uint16_t)t); wr(rt, p + 4, 2, (uint16_t)l); wr(rt, p + 6, 2, (uint16_t)b); wr(rt, p + 8, 2, (uint16_t)r);
    }
}
static void free_region(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; h && i < rt->handle_count; i++) if (rt->handles[i].handle == h) rt->handles[i] = (MacHandle){0};
}
static void dispose(MacRuntime *rt, Dlg *d, int restore) {
    for (unsigned i = 0; i < 3; i++) free_region(rt, d->region[i]);
    if (restore && d->drawn && d->backdrop) { MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height); mac_pen_restore(&pen, d->backdrop); }
    for (unsigned i = 0; i < MAX_OPEN; i++) if (M(rt) && M(rt)->open[i] == d) M(rt)->open[i] = NULL;
    free(d->backdrop); free(d);
}
void mac_dlgmgr_destroy(MacRuntime *rt) {
    if (!rt->dlgmgr) return;
    for (unsigned i = 0; i < MAX_OPEN; i++) if (M(rt)->open[i]) dispose(rt, M(rt)->open[i], 0);
    free(rt->dlgmgr); rt->dlgmgr = NULL;
}
static Dlg *find(MacRuntime *rt, uint32_t ptr) {
    if (!rt->dlgmgr || !ptr) return NULL;
    for (unsigned i = 0; i < MAX_OPEN; i++) if (M(rt)->open[i] && M(rt)->open[i]->ptr == ptr) return M(rt)->open[i];
    return NULL;
}
static Dlg *find_handle(MacRuntime *rt, uint32_t h, unsigned *item) {
    if (!rt->dlgmgr || !h) return NULL;
    for (unsigned i = 0; i < MAX_OPEN; i++) {
        Dlg *d = M(rt)->open[i];
        for (unsigned k = 0; d && k < d->ditl.count; k++) if (d->handle[k] == h) { *item = k; return d; }
    }
    return NULL;
}
static Dlg *front(MacRuntime *rt) {
    for (int i = MAX_OPEN - 1; rt->dlgmgr && i >= 0; i--) if (M(rt)->open[i]) return M(rt)->open[i];
    return NULL;
}
static int has_text(const MacDitlItem *it) { return it->kind == MAC_ITEM_STATIC || it->kind == MAC_ITEM_EDIT || it->kind == MAC_ITEM_BUTTON || it->kind == MAC_ITEM_CHECK || it->kind == MAC_ITEM_RADIO; }
static int enabled(const Dlg *d, unsigned k) { const MacDitlItem *it = &d->ditl.items[k]; return !it->disabled && (it->kind == MAC_ITEM_BUTTON || it->kind == MAC_ITEM_CHECK || it->kind == MAC_ITEM_RADIO || it->kind == MAC_ITEM_EDIT); }
static void mirror(MacRuntime *rt, const Dlg *d, unsigned k) {
    if (!d->handle[k]) return;
    uint32_t block = d->block[k];                                     /* never the master pointer in guest memory: the guest may have changed it */
    if (block) { wr(rt, d->handle[k], 4, block); memset(mac_cpu_ram(rt->cpu) + block, 0, TEXT_CAP + 1); memcpy(mac_cpu_ram(rt->cpu) + block, d->text[k], d->length[k]); }
}

static unsigned expand(const MacRuntime *rt, const Dlg *d, unsigned k, char *out, unsigned cap) {
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
static void draw(MacRuntime *rt, Dlg *d) {
    if (d->hidden) return;
    MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height);
    if (!d->drawn) { mac_pen_save(&pen, d->backdrop); d->drawn = 1; }
    mac_pen_frame(&pen);
    for (unsigned k = 0; k < d->ditl.count; k++) {
        const MacDitlItem *it = &d->ditl.items[k];
        if (d->item_hidden[k]) continue;
        int l = d->left + it->left, t = d->top + it->top, r = d->left + it->right, b = d->top + it->bottom;
        char text[512]; unsigned n = has_text(it) ? expand(rt, d, k, text, sizeof text) : 0;
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

static int screen_ok(MacRuntime *rt) {
    return rt->screen_depth == 8 && rt->screen_width == 640 && rt->screen_height == 480 && rt->screen_row_bytes == 640 && valid(rt, rt->screen_base, 640 * 480);
}
static int get_new_dialog(MacRuntime *rt, uint32_t sp) {
    if (!valid(rt, sp, 14)) return fail(rt, "GetNewDialog stack is invalid.");
    uint32_t behind = rd(rt, sp, 4), storage = rd(rt, sp + 4, 4); int16_t id = (int16_t)rd(rt, sp + 8, 2);
    if (behind != 0xffffffffu && behind != 0) return fail(rt, "GetNewDialog behind a particular window is unsupported.");
    if (!screen_ok(rt)) return fail(rt, "Dialogs require the initialized 640x480 indexed display.");
    if (!rt->dlgmgr && !(rt->dlgmgr = calloc(1, sizeof(Mgr)))) return fail(rt, "The Dialog Manager could not allocate its state.");
    unsigned slot = 0; while (slot < MAX_OPEN && M(rt)->open[slot]) slot++;
    if (slot == MAX_OPEN) return fail(rt, "More than four open dialogs are unsupported.");
    MacResource dlog, ditl; MacDlog g; MacDitl list; const char *why = NULL;
    if (mac_resource_find(&rt->fork, DLOG_TYPE, id, &dlog)) { wr(rt, sp + 10, 4, 0); mac_cpu_set(rt->cpu, MAC_A7, sp + 10); return 1; }   /* no such template: NIL */
    if (mac_dlog_parse(dlog.data, dlog.size, &g, &why)) return fail(rt, why);
    if (mac_resource_find(&rt->fork, DITL_TYPE, g.items_id, &ditl) || mac_ditl_parse(ditl.data, ditl.size, &list, &why))
        return fail(rt, why ? why : "The dialog's item list (DITL) is missing.");
    int w = g.right - g.left, h = g.bottom - g.top;
    if (w < 16 || h < 16 || w > 640 || h > 480) return fail(rt, "The dialog size is unsupported.");
    for (unsigned k = 0; k < list.count; k++) {
        const MacDitlItem *it = &list.items[k];
        /* a user item (type 0) is accepted but its draw procedure is never called: it stays blank; if enabled it still counts as a hit */
        if (it->kind == MAC_ITEM_CONTROL) return fail(rt, "Dialog control items are unsupported.");
        if (it->kind != MAC_ITEM_HELP && (it->right <= it->left || it->bottom <= it->top)) return fail(rt, "A dialog item has an empty rectangle.");
    }
    uint32_t ptr = storage;
    if (storage) { if ((storage & 1) || !valid(rt, storage, RECORD)) return fail(rt, "GetNewDialog dStorage is invalid."); }
    else { ptr = mac_runtime_alloc(rt, RECORD); if (!ptr) return fail(rt, "GetNewDialog could not allocate the dialog record."); }
    memset(mac_cpu_ram(rt->cpu) + ptr, 0, RECORD); wr(rt, ptr + 108, 2, 2);
    Dlg *d = calloc(1, sizeof *d);
    if (!d) return fail(rt, "GetNewDialog could not allocate the dialog.");
    d->backdrop = malloc((size_t)w * (size_t)h);
    if (!d->backdrop) { free(d); return fail(rt, "GetNewDialog could not allocate the dialog backdrop."); }
    d->hidden = !g.visible; d->ptr = ptr; d->id = id; d->width = w; d->height = h; d->ditl = list; d->visible_flag = g.visible;
    int top = g.top, left = g.left;
    if (top + h > 480) top = 480 - h;
    if (left + w > 640) left = 640 - w;
    d->top = top < 0 ? 0 : top; d->left = left < 0 ? 0 : left;
    for (unsigned k = 0; k < list.count; k++) {
        const MacDitlItem *it = &list.items[k];
        if (!has_text(it)) continue;
        d->length[k] = it->length; memcpy(d->text[k], it->data, it->length);
        if (it->kind == MAC_ITEM_EDIT && !d->edit_item) d->edit_item = (int)k + 1;
        uint32_t master = mac_runtime_alloc(rt, 4), block = mac_runtime_alloc(rt, TEXT_CAP + 1);
        if (master && block) { wr(rt, master, 4, block); d->handle[k] = master; d->block[k] = block; mirror(rt, d, k); }
    }
    d->sel_start = d->sel_end = 0; d->def_item = 1;
    d->region[0] = new_rect_region(rt, d->top - 1, d->left - 1, d->top + h + 1, d->left + w + 1);   /* structure */
    d->region[1] = new_rect_region(rt, d->top, d->left, d->top + h, d->left + w);                   /* content */
    d->region[2] = new_rect_region(rt, 0, 0, 0, 0);                                                 /* update: nothing to draw */
    if (!d->region[0] || !d->region[1] || !d->region[2]) { dispose(rt, d, 0); return fail(rt, "GetNewDialog could not allocate the window regions."); }
    wr(rt, ptr + 110, 1, d->hidden ? 0 : 1); wr(rt, ptr + 111, 1, d->hidden ? 0 : 1);               /* visible, hilited */
    wr(rt, ptr + 114, 4, d->region[0]); wr(rt, ptr + 118, 4, d->region[1]); wr(rt, ptr + 122, 4, d->region[2]);
    wr(rt, ptr + 16, 2, 0); wr(rt, ptr + 18, 2, 0); wr(rt, ptr + 20, 2, (uint16_t)h); wr(rt, ptr + 22, 2, (uint16_t)w);   /* portRect, local */
    M(rt)->open[slot] = d;
    draw(rt, d);
    wr(rt, sp + 10, 4, ptr); mac_cpu_set(rt->cpu, MAC_A7, sp + 10);
    return 1;
}

static void select_edit(Dlg *d, int item) { d->edit_item = item; d->sel_start = d->sel_end = d->length[item - 1]; }
static int hit_item(const Dlg *d, int x, int y) {
    for (unsigned k = 0; k < d->ditl.count; k++) {
        const MacDitlItem *it = &d->ditl.items[k];
        if (it->kind == MAC_ITEM_HELP || d->item_hidden[k] || !enabled(d, k)) continue;
        if (x >= d->left + it->left && x < d->left + it->right && y >= d->top + it->top && y < d->top + it->bottom) return (int)k + 1;
    }
    return 0;
}
static unsigned handle_event(Dlg *d, const MacEvent *e) {
    if (e->what == 3 || e->what == 5) {
        unsigned ch = e->message & 0xff;
        if (ch == 13 || ch == 3) return d->def_item && d->ditl.items[d->def_item - 1].kind == MAC_ITEM_BUTTON && !d->ditl.items[d->def_item - 1].disabled ? (unsigned)d->def_item : 0;
        if (d->cancel_item && (ch == 27 || ((e->modifiers & 0x100) && ch == '.'))) return (unsigned)d->cancel_item;
        if (!d->edit_item || (e->modifiers & 0x100)) return 0;
        unsigned k = (unsigned)d->edit_item - 1;
        if (ch == 9) { for (unsigned i = 1; i <= d->ditl.count; i++) { unsigned n = (k + i) % d->ditl.count; if (d->ditl.items[n].kind == MAC_ITEM_EDIT) { select_edit(d, (int)n + 1); break; } } }
        else if (ch == 8 || ch == 0x7f) { if (d->length[k]) d->length[k]--; }
        else if (ch >= 32 && ch < 127 && d->length[k] < TEXT_CAP) d->text[k][d->length[k]++] = (char)ch;
        d->sel_start = d->sel_end = d->length[k];
        return 0;
    }
    int item = hit_item(d, e->x, e->y);
    if (e->what == 1) {
        if (!item) return 0;
        if (d->ditl.items[item - 1].kind == MAC_ITEM_EDIT) { select_edit(d, item); return (unsigned)item; }
        d->pressed = item; return 0;
    }
    int pressed = d->pressed; d->pressed = 0;
    return pressed && item == pressed ? (unsigned)item : 0;
}
static void finish_modal(MacRuntime *rt, Dlg *d, unsigned chosen, int toggle) {
    if (toggle && chosen && chosen <= d->ditl.count && d->ditl.items[chosen - 1].kind == MAC_ITEM_CHECK) d->value[chosen - 1] = !d->value[chosen - 1];
    for (unsigned k = 0; k < d->ditl.count; k++) mirror(rt, d, k);
    wr(rt, d->hit_ptr, 2, chosen);
    d->modal = 0; d->pressed = 0; d->filter = 0;
    draw(rt, d);
    mac_cpu_set(rt->cpu, MAC_A7, d->sp + 8);
    mac_cpu_set(rt->cpu, MAC_PC, d->pc + 2);
}
/* The guest filter returned (mac_guestcall runs this with A7 and PC restored to the values at the call). TRUE: the item it stored in itemHit is
 * the answer and the event is the filter's. FALSE: the dialog handles the event itself. Either way ModalDialog resumes at its own trap. */
static int filter_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; Dlg *d = ctx;
    if (!find(rt, d->ptr) || !d->modal) return -1;
    unsigned chosen = 0;
    if ((result >> 8) & 0xff) {
        uint32_t hit = rd(rt, d->scratch + 18, 2);
        finish_modal(rt, d, hit, 0); return 0;
    }
    if (d->pend_real) chosen = handle_event(d, &d->pend);
    if (chosen) { finish_modal(rt, d, chosen, 1); return 0; }
    for (unsigned k = 0; k < d->ditl.count; k++) mirror(rt, d, k);
    draw(rt, d);
    mac_cpu_set(rt->cpu, MAC_A7, d->sp); mac_cpu_set(rt->cpu, MAC_PC, d->pc); mac_cpu_yield(rt->cpu);
    return 0;
}
/* Offers one event (or a null event) to the guest filter: writes an EventRecord and a zero itemHit into scratch below the stack and calls
 * filter(theDialog, VAR event, VAR itemHit): Boolean. */
static int call_filter(MacRuntime *rt, Dlg *d, const MacEvent *e, int real) {
    uint32_t at = (d->sp - 64) & ~1u;
    if (!valid(rt, at - 32, 96)) return fail(rt, "ModalDialog has no stack room for its filter procedure.");
    d->scratch = at; d->pend = *e; d->pend_real = real;
    wr(rt, at, 2, e->what); wr(rt, at + 2, 4, e->message); wr(rt, at + 6, 4, e->when); wr(rt, at + 10, 2, (uint16_t)e->y); wr(rt, at + 12, 2, (uint16_t)e->x); wr(rt, at + 14, 2, e->modifiers);
    wr(rt, at + 18, 2, 0);
    uint8_t args[12]; uint32_t v[3] = { d->ptr, at, at + 18 };
    for (int i = 0; i < 3; i++) for (int k = 0; k < 4; k++) args[i * 4 + k] = (uint8_t)(v[i] >> (24 - 8 * k));
    mac_cpu_set(rt->cpu, MAC_PC, d->pc); mac_cpu_set(rt->cpu, MAC_A7, at);
    return mac_guest_call(rt, d->filter, args, 12, 2, filter_done, d);
}
static int modal_dialog(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    Dlg *d = front(rt);
    if (!d) return fail(rt, "ModalDialog with no open dialog is unsupported.");
    if (!d->modal) {
        if (!valid(rt, sp, 8)) return fail(rt, "ModalDialog stack is invalid.");
        uint32_t filter = rd(rt, sp + 4, 4), hit = rd(rt, sp, 4);
        if (filter == STD_FILTER) filter = 0;                                                          /* the standard filter is what the dialog does anyway */
        else if (filter && (filter & 1 || filter < 0x400 || filter >= mac_cpu_ram_size(rt->cpu) - 2)) return fail(rt, "ModalDialog filter procedure is outside guest memory.");
        if (!valid(rt, hit, 2)) return fail(rt, "ModalDialog itemHit pointer is invalid.");
        d->modal = 1; d->sp = sp; d->pc = pc; d->hit_ptr = hit; d->filter = filter; draw(rt, d);
    } else if (pc != d->pc || sp != d->sp) return fail(rt, "ModalDialog called again while a dialog is in progress.");
    unsigned chosen = 0; MacEvent e; int before = d->pressed;
    if (d->filter) {                                                                                    /* one event (or a null event) per pass through the guest filter */
        int real = mac_dialog_next_event(rt, &e);
        if (!real) e = (MacEvent){ 0, rt->modifiers, 0, rt->ticks, rt->mouse_x, rt->mouse_y };
        return call_filter(rt, d, &e, real) < 0 ? -1 : 1;
    }
    while (!chosen && mac_dialog_next_event(rt, &e)) chosen = handle_event(d, &e);
    if (!chosen) {
        for (unsigned k = 0; k < d->ditl.count; k++) mirror(rt, d, k);
        draw(rt, d); (void)before;
        mac_cpu_set(rt->cpu, MAC_PC, d->pc); mac_cpu_yield(rt->cpu);
        return 1;
    }
    finish_modal(rt, d, chosen, 1);
    return 1;
}

/* IsDialogEvent(VAR event): Boolean. True for the events a front dialog takes: mouse, key, update, activate. EventRecord: what +0. */
static int is_dialog_event(MacRuntime *rt, uint32_t sp) {
    if (!valid(rt, sp, 6)) return fail(rt, "IsDialogEvent stack is invalid.");
    uint32_t ev = rd(rt, sp, 4);
    if (!valid(rt, ev, 16)) return fail(rt, "IsDialogEvent event pointer is invalid.");
    unsigned what = rd(rt, ev, 2);
    int yes = front(rt) && !front(rt)->hidden && (what == 1 || what == 3 || what == 5 || what == 6 || what == 8);
    wr(rt, sp + 4, 1, yes ? 1 : 0); wr(rt, sp + 5, 1, 0);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
    return 1;
}
/* DialogSelect(VAR event; VAR theDialog; VAR itemHit): Boolean. A press on a button or check box waits for the mouse-up like TrackControl
 * (the trap rewinds and yields), then reports the item; a press on edit text selects it and reports it; Return and Enter hit the default
 * button; other keys edit the selected edit text; everything else returns FALSE. */
static int dialog_select(MacRuntime *rt, uint32_t sp, uint32_t pc) {
    if (!valid(rt, sp, 14)) return fail(rt, "DialogSelect stack is invalid.");
    uint32_t ev = rd(rt, sp + 8, 4), dp = rd(rt, sp + 4, 4), ip = rd(rt, sp, 4);
    Dlg *d = front(rt);
    if (d && d->hidden) d = NULL;
    if (!d) { wr(rt, sp + 12, 2, 0); mac_cpu_set(rt->cpu, MAC_A7, sp + 12); return 1; }
    if (!valid(rt, ev, 16) || !valid(rt, dp, 4) || !valid(rt, ip, 2)) return fail(rt, "DialogSelect pointer is invalid.");
    unsigned chosen = 0, handled = 0;
    if (d->select_busy) {
        if (pc != d->pc || sp != d->sp) return fail(rt, "DialogSelect called again while a press is being tracked.");
        MacEvent e;
        while (!handled && mac_dialog_next_event(rt, &e)) if (e.what == 2) { chosen = handle_event(d, &e); handled = 1; }
        if (!handled) { draw(rt, d); mac_cpu_set(rt->cpu, MAC_PC, d->pc); mac_cpu_yield(rt->cpu); return 1; }
        d->select_busy = 0;
    } else {
        MacEvent e = { (uint16_t)rd(rt, ev, 2), (uint16_t)rd(rt, ev + 14, 2), rd(rt, ev + 2, 4), rd(rt, ev + 6, 4), (int16_t)rd(rt, ev + 12, 2), (int16_t)rd(rt, ev + 10, 2) };
        if (e.what == 1) {
            int item = hit_item(d, e.x, e.y);
            if (item && d->ditl.items[item - 1].kind == MAC_ITEM_EDIT) { select_edit(d, item); chosen = (unsigned)item; }
            else if (item) { d->pressed = item; d->select_busy = 1; d->sp = sp; d->pc = pc; draw(rt, d); mac_cpu_set(rt->cpu, MAC_PC, pc); mac_cpu_yield(rt->cpu); return 1; }
        } else if (e.what == 3 || e.what == 5) chosen = handle_event(d, &e);
        else if (e.what == 6 || e.what == 8) draw(rt, d);
    }
    d->pressed = 0;
    if (chosen && d->ditl.items[chosen - 1].kind == MAC_ITEM_CHECK) d->value[chosen - 1] = !d->value[chosen - 1];
    for (unsigned k = 0; k < d->ditl.count; k++) mirror(rt, d, k);
    if (chosen) { wr(rt, dp, 4, d->ptr); wr(rt, ip, 2, chosen); }
    draw(rt, d);
    wr(rt, sp + 12, 1, chosen ? 1 : 0); wr(rt, sp + 13, 1, 0);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 12);
    return 1;
}

int mac_dlgmgr_service(MacRuntime *rt, uint16_t opcode) {
    if (rt && rt->dlgmgr && M(rt)->port && rt->cpu && !mac_cpu_halted(rt->cpu) && find(rt, M(rt)->port) && (mac_dlg_drawing_trap(opcode) || mac_dlg_swallowed_args(opcode) >= 0)) {
        int args = mac_dlg_swallowed_args(opcode);
        uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
        if (args >= 0 && !(sp & 1) && valid(rt, sp, (size_t)args + 2)) { mac_cpu_set(rt->cpu, MAC_A7, sp + (uint32_t)args); return 1; }
        char m[128]; snprintf(m, sizeof m, "QuickDraw trap $%04x with a dialog as the current port is unsupported.", opcode);
        return fail(rt, m);
    }
    switch (opcode) {
    case 0xa97c: case 0xa991: case 0xa981: case 0xa982: case 0xa983: case 0xa98d: case 0xa990: case 0xa98f: case 0xa97e: case 0xa98c: case 0xaa68:
    case 0xa97d: case 0xa98e: case 0xa980: case 0xa97f: case 0xa984: case 0xa978: case 0xa979: case 0xa97a: case 0xa924: case 0xa873: case 0xabf1: case 0xa915: case 0xa916: case 0xa91b: case 0xa91f: case 0xa827: case 0xa828: break;
    default: return 0;
    }
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), pc = (mac_cpu_get(rt->cpu, MAC_PC) - 2) & 0xffffff;
    if ((sp & 1) || !valid(rt, sp, 4)) return fail(rt, "Dialog Manager call has an invalid stack.");
    int passthrough = opcode == 0xa924 || opcode == 0xa873 || opcode == 0xa915 || opcode == 0xa916 || opcode == 0xa91b || opcode == 0xa91f;   /* these act only on our dialogs and otherwise belong to other services */
    if (!passthrough && opcode != FILTER_OP && rt->last_trap_pc >= 0x2000 && rt->last_trap_pc < 0x2000 + 4096 * 8) return fail(rt, "A Dialog Manager call reached through a patched trap vector is unsupported.");
    unsigned pop = 0;
    if (rt->dlgmgr && M(rt)->port && !find(rt, M(rt)->port)) M(rt)->port = 0;                       /* the dialog went away */
    switch (opcode) {
    case 0xa924: {                                    /* FrontWindow: an open, visible dialog is the front window */
        Dlg *d = front(rt);
        if (!d || d->hidden) return 0;
        if (!valid(rt, sp, 4)) return fail(rt, "FrontWindow stack is invalid.");
        wr(rt, sp, 4, d->ptr); return 1; }
    case 0xa873: {                                    /* SetPort on an open dialog: nothing is drawn through the port, so accept it */
        if (!find(rt, rd(rt, sp, 4))) { if (rt->dlgmgr) M(rt)->port = 0; return 0; }
        M(rt)->port = rd(rt, sp, 4);
        pop = 4; break; }
    case 0xabf1: {                                    /* the standard filter, called by the guest: theDialog, VAR event, VAR itemHit -> Boolean */
        if (!valid(rt, sp, 14)) return fail(rt, "The standard filter procedure has an invalid stack.");
        uint32_t ev = rd(rt, sp + 4, 4), ip = rd(rt, sp, 4); Dlg *d = find(rt, rd(rt, sp + 8, 4));
        if (!d || !valid(rt, ev, 16) || !valid(rt, ip, 2)) return fail(rt, "The standard filter procedure was called with a bad dialog or pointer.");
        unsigned what = rd(rt, ev, 2), ch = rd(rt, ev + 2, 4) & 0xff, mods = rd(rt, ev + 14, 2), hit = 0;
        if (what == 3 || what == 5) {
            if ((ch == 13 || ch == 3) && d->def_item && d->ditl.items[d->def_item - 1].kind == MAC_ITEM_BUTTON) hit = (unsigned)d->def_item;
            else if (d->cancel_item && (ch == 27 || ((mods & 0x100) && ch == '.'))) hit = (unsigned)d->cancel_item;
        }
        if (hit) wr(rt, ip, 2, hit);
        wr(rt, sp + 12, 1, hit ? 1 : 0); wr(rt, sp + 13, 1, 0);
        pop = 12; break; }
    case 0xa91b: {                                    /* MoveWindow(window, h, v, front) on a dialog: content to (h, v), kept on the screen */
        if (!valid(rt, sp, 10)) return fail(rt, "MoveWindow stack is invalid.");
        Dlg *d = find(rt, rd(rt, sp + 6, 4));
        if (!d) return 0;
        if (d->modal || d->select_busy) return fail(rt, "A dialog was moved while it was being used.");
        int left = (int16_t)rd(rt, sp + 4, 2), top = (int16_t)rd(rt, sp + 2, 2);
        if (top + d->height > 480) top = 480 - d->height;
        if (left + d->width > 640) left = 640 - d->width;
        if (top < 0) top = 0; if (left < 0) left = 0;
        MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height);
        int was = d->drawn;
        if (was) mac_pen_restore(&pen, d->backdrop);
        d->drawn = 0; d->left = left; d->top = top;
        set_region(rt, d->region[0], top - 1, left - 1, top + d->height + 1, left + d->width + 1);
        set_region(rt, d->region[1], top, left, top + d->height, left + d->width);
        if (was) draw(rt, d);
        pop = 10; break; }
    case 0xa827: case 0xa828: {                      /* HideDItem(dialog, item) / ShowDItem(dialog, item) */
        if (!valid(rt, sp, 6)) return fail(rt, "HideDItem stack is invalid.");
        Dlg *d = find(rt, rd(rt, sp + 2, 4)); int item = (int16_t)rd(rt, sp, 2);
        if (!d || item < 1 || (unsigned)item > d->ditl.count) return fail(rt, "HideDItem names a dialog or item that does not exist.");
        d->item_hidden[item - 1] = opcode == 0xa827;
        if (opcode == 0xa827 && d->edit_item == item) d->edit_item = 0;
        if (opcode == 0xa828 && !d->edit_item && d->ditl.items[item - 1].kind == MAC_ITEM_EDIT) d->edit_item = item;
        if (d->drawn) { MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height); mac_pen_restore(&pen, d->backdrop); d->drawn = 0; draw(rt, d); }
        pop = 6; break; }
    case 0xa91f: {                                    /* SelectWindow on a dialog: it is already the only active window, so this only marks it hilited */
        Dlg *d = find(rt, rd(rt, sp, 4));
        if (!d) return 0;
        if (!d->hidden) wr(rt, d->ptr + 111, 1, 1);
        pop = 4; break; }
    case 0xa915: case 0xa916: {                       /* ShowWindow, HideWindow on a dialog; any other window is not ours */
        Dlg *d = find(rt, rd(rt, sp, 4));
        if (!d) return 0;
        if (opcode == 0xa915 && d->hidden) { d->hidden = 0; wr(rt, d->ptr + 110, 1, 1); wr(rt, d->ptr + 111, 1, 1); draw(rt, d); }
        else if (opcode == 0xa916 && !d->hidden) {
            if (d->modal || d->select_busy) return fail(rt, "A dialog was hidden while it was being used.");
            MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height);
            if (d->drawn) mac_pen_restore(&pen, d->backdrop);
            d->drawn = 0; d->hidden = 1; wr(rt, d->ptr + 110, 1, 0); wr(rt, d->ptr + 111, 1, 0);
        }
        pop = 4; break; }
    case 0xa97f: return is_dialog_event(rt, sp);
    case 0xa980: return dialog_select(rt, sp, pc);
    case 0xa97c: return get_new_dialog(rt, sp);
    case 0xa991: return modal_dialog(rt, sp, pc);
    case 0xa98c: pop = 4; break;                                                                    /* ErrorSound: nothing to play */
    case 0xa981: case 0xa982: case 0xa983: {                                                         /* DrawDialog, CloseDialog, DisposeDialog */
        Dlg *d = find(rt, rd(rt, sp, 4));
        if (!d) return fail(rt, "The dialog call names a dialog that is not open.");
        if (d->modal) return fail(rt, "A dialog was closed while ModalDialog was running.");
        if (opcode == 0xa981) draw(rt, d); else dispose(rt, d, 1);
        pop = 4; break; }
    case 0xa98d: {                                                                                   /* GetDItem(dlg, item, VAR type, VAR handle, VAR box) */
        if (!valid(rt, sp, 18)) return fail(rt, "GetDItem stack is invalid.");
        Dlg *d = find(rt, rd(rt, sp + 14, 4)); int item = (int16_t)rd(rt, sp + 12, 2);
        uint32_t type_p = rd(rt, sp + 8, 4), handle_p = rd(rt, sp + 4, 4), box_p = rd(rt, sp, 4);
        if (!d || item < 1 || (unsigned)item > d->ditl.count) return fail(rt, "GetDItem names a dialog or item that does not exist.");
        if (!valid(rt, type_p, 2) || !valid(rt, handle_p, 4) || !valid(rt, box_p, 8)) return fail(rt, "GetDItem output pointer is invalid.");
        const MacDitlItem *it = &d->ditl.items[item - 1];
        wr(rt, type_p, 2, (unsigned)it->kind | (it->disabled ? 128u : 0));
        wr(rt, handle_p, 4, d->handle[item - 1]);
        int away = d->item_hidden[item - 1] ? 16384 : 0;
        wr(rt, box_p, 2, (uint16_t)it->top); wr(rt, box_p + 2, 2, (uint16_t)(it->left + away)); wr(rt, box_p + 4, 2, (uint16_t)it->bottom); wr(rt, box_p + 6, 2, (uint16_t)(it->right + away));
        pop = 18; break; }
    case 0xa990: case 0xa98f: {                                                                      /* GetIText(h, VAR s) / SetIText(h, s) */
        if (!valid(rt, sp, 8)) return fail(rt, "Dialog text call has an invalid stack.");
        unsigned k = 0; Dlg *d = find_handle(rt, rd(rt, sp + 4, 4), &k); uint32_t s = rd(rt, sp, 4);
        if (!d) return fail(rt, "The item handle does not belong to an open dialog.");
        if (!valid(rt, s, 1)) return fail(rt, "The dialog text pointer is invalid.");
        if (opcode == 0xa990) {
            if (!valid(rt, s, (size_t)d->length[k] + 1)) return fail(rt, "GetIText output string does not fit.");
            mac_cpu_ram(rt->cpu)[s] = d->length[k]; memcpy(mac_cpu_ram(rt->cpu) + s + 1, d->text[k], d->length[k]);
        } else {
            unsigned n = rd(rt, s, 1);
            if (!valid(rt, s, (size_t)n + 1)) return fail(rt, "SetIText string is invalid.");
            if (n > TEXT_CAP) n = TEXT_CAP;
            d->length[k] = (uint8_t)n; memcpy(d->text[k], mac_cpu_ram(rt->cpu) + s + 1, n); mirror(rt, d, k);
            if ((int)k + 1 == d->edit_item) d->sel_start = d->sel_end = (int)n;
            draw(rt, d);
        }
        pop = 8; break; }
    case 0xa97e: {                                                                                   /* SelIText(dlg, item, start, end) */
        if (!valid(rt, sp, 10)) return fail(rt, "SelIText stack is invalid.");
        Dlg *d = find(rt, rd(rt, sp + 6, 4)); int item = (int16_t)rd(rt, sp + 4, 2);
        if (!d || item < 1 || (unsigned)item > d->ditl.count || d->ditl.items[item - 1].kind != MAC_ITEM_EDIT) return fail(rt, "SelIText names a dialog or edit item that does not exist.");
        select_edit(d, item); draw(rt, d);
        pop = 10; break; }
    case 0xaa68: {                                                                                   /* DialogDispatch: D0 selects */
        uint32_t sel = mac_cpu_get(rt->cpu, MAC_D0) & 0xffff;
        if (sel == 0x0304 || sel == 0x0305 || sel == 0x0306) {                                       /* SetDialogDefaultItem, SetDialogCancelItem, SetDialogTracksCursor */
            if (!valid(rt, sp, 8)) return fail(rt, "DialogDispatch stack is invalid.");
            Dlg *d = find(rt, rd(rt, sp + 2, 4)); int item = (int16_t)rd(rt, sp, 2);
            if (!d) return fail(rt, "The dialog call names a dialog that is not open.");
            if (sel == 0x0306) { wr(rt, sp + 6, 2, 0); pop = 6; break; }                             /* the cursor never changes shape here */
            if (item < 0 || (unsigned)item > d->ditl.count || (item && d->ditl.items[item - 1].kind != MAC_ITEM_BUTTON)) { wr(rt, sp + 6, 2, (uint16_t)-1); pop = 6; break; }
            if (sel == 0x0304) d->def_item = item; else d->cancel_item = item;
            if (d->drawn) draw(rt, d);
            wr(rt, sp + 6, 2, 0); pop = 6; break;                                                     /* result: noErr */
        }
        if (sel != 0x0203) { char m[96]; snprintf(m, sizeof m, "DialogDispatch selector $%04x is unsupported.", sel); return fail(rt, m); }
        if (!valid(rt, sp, 6)) return fail(rt, "GetStdFilterProc stack is invalid.");
        uint32_t out = rd(rt, sp, 4);
        if (!valid(rt, out, 4)) return fail(rt, "GetStdFilterProc output pointer is invalid.");
        wr(rt, out, 4, STD_FILTER); wr(rt, sp + 4, 2, 0);                                            /* result: noErr */
        pop = 4; break; }
    default: {
        char m[96]; snprintf(m, sizeof m, "Dialog Manager trap $%04x is unsupported.", opcode); return fail(rt, m); }
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + pop);
    return 1;
}
