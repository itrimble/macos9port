#include "mac_dlg_int.h"
#include "mac_dlgwindow.h"
#include <stdio.h>

/* SetDItem ($A98E) and application-drawn (user) items.
 *   PROCEDURE SetDItem(theDialog: DialogPtr; itemNo, itemType: INTEGER; item: Handle; box: Rect)   stack from SP: box pointer, item, itemType, itemNo, dialog
 * The item's rectangle and its disabled bit are replaced. For a user item, `item` is the procedure that draws it,
 *   PROCEDURE MyItem(theWindow: WindowPtr; itemNo: INTEGER),
 * recorded and called, in item order, whenever the dialog's native drawing has repainted the dialog and the program then waits in ModalDialog or calls DrawDialog:
 * the dialog is made the current port (its port window, in dialog coordinates) for the call and the previous port is put back afterwards. For every other kind
 * of item the handle must be the one GetDItem reports (an icon item may be given another handle: a pointer to 128 bytes of 32x32 bitmap, which is then drawn
 * in place of the DITL's ICON resource; nil goes back to the resource) and the kind may not change; anything else halts. The procedures need the colour window machinery (a
 * port window); without it the call halts instead of leaving the item blank. */
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }

int mac_dlg_set_item(MacRuntime *rt, Dlg *d, uint32_t sp) {
    if (!valid(rt, sp, 16)) return fail(rt, "SetDItem stack is invalid.");
    uint32_t box = rd(rt, sp, 4), handle = rd(rt, sp + 4, 4);
    unsigned type = rd(rt, sp + 8, 2); int item = (int16_t)rd(rt, sp + 10, 2);
    if (!d || item < 1 || (unsigned)item > d->ditl.count) return fail(rt, "SetDItem names a dialog or item that does not exist.");
    if (!valid(rt, box, 8)) return fail(rt, "SetDItem rectangle pointer is invalid.");
    MacDitlItem *it = &d->ditl.items[item - 1];
    if ((type & 0x7f) != it->kind) return fail(rt, "SetDItem changing an item's kind is unsupported.");
    int t = (int16_t)rd(rt, box, 2), l = (int16_t)rd(rt, box + 2, 2), b = (int16_t)rd(rt, box + 4, 2), r = (int16_t)rd(rt, box + 6, 2);
    if (it->kind == MAC_ITEM_USER) {
        if (handle && ((handle & 1) || !valid(rt, handle, 2))) return fail(rt, "SetDItem user item procedure is outside guest memory.");
        d->user_proc[item - 1] = handle;
    } else {
        if (handle != d->handle[item - 1] && it->kind == MAC_ITEM_ICON && (!handle || (!(handle & 1) && valid(rt, handle, 4)))) {
            d->handle[item - 1] = handle;   /* an icon item takes the program's own icon (a handle to 128 bytes of bitmap, from GetIcon or its own data): drawn from it */
        } else if (handle != d->handle[item - 1]) {
            char why[160];
            snprintf(why, sizeof why, "SetDItem replacing an item's handle is unsupported (only user item procedures and icon handles can be set): item %d, type $%02X, handle $%08X for $%08X.", item, type, handle, d->handle[item - 1]);
            return fail(rt, why);
        }
        if (b <= t || r <= l) return fail(rt, "SetDItem gives the item an empty rectangle.");
    }
    if (d->item_hidden[item - 1] && l >= 8192) l -= 16384, r -= 16384;   /* a hidden item reports its box 16384 to the right: take it back */
    it->top = (int16_t)t; it->left = (int16_t)l; it->bottom = (int16_t)b; it->right = (int16_t)r;
    it->disabled = (type & 0x80) != 0;
    d->user_due = 1;
    mac_cpu_set(rt->cpu, MAC_A7, sp + 16);
    return 1;
}

static int next_proc(MacRuntime *rt, Dlg *d);
static int proc_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; Dlg *d = ctx; (void)result;
    for (unsigned i = 0; i < MAX_OPEN; i++) if (M(rt)->open[i] == d) return next_proc(rt, d);
    return fail(rt, "A dialog was disposed by its own user item procedure.");
}
static int next_proc(MacRuntime *rt, Dlg *d) {
    while ((unsigned)d->user_next < d->ditl.count) {
        unsigned k = (unsigned)d->user_next++;
        if (d->ditl.items[k].kind != MAC_ITEM_USER || !d->user_proc[k] || d->item_hidden[k]) continue;
        uint8_t args[6] = { (uint8_t)((k + 1) >> 8), (uint8_t)(k + 1), (uint8_t)(d->ptr >> 24), (uint8_t)(d->ptr >> 16), (uint8_t)(d->ptr >> 8), (uint8_t)d->ptr };
        return mac_guest_call(rt, d->user_proc[k], args, 6, 0, proc_done, d);
    }
    M(rt)->port = d->user_prev_dlgport;                                  /* the port as it was before the procedures ran */
    rt->current_port = d->user_prev_current;
    if (rt->qd_globals) wr(rt, rt->qd_globals, 4, d->user_prev_port);
    d->user_running = 0;
    mac_cpu_set(rt->cpu, MAC_A7, d->user_sp); mac_cpu_set(rt->cpu, MAC_PC, d->user_pc);
    return 0;
}

/* Calls the owed user item procedures. `sp` and `pc` are where execution continues when the last one returns (the same trap again for ModalDialog, the
 * instruction after it for DrawDialog). 0: nothing owed, nothing changed. 1: a guest call is under way; the service must return handled. -1: halted. */
int mac_dlg_user_start(MacRuntime *rt, Dlg *d, uint32_t sp, uint32_t pc) {
    if (!d->user_due || d->hidden || d->user_running) return 0;
    d->user_due = 0;
    int any = 0;
    for (unsigned k = 0; k < d->ditl.count; k++) if (d->ditl.items[k].kind == MAC_ITEM_USER && d->user_proc[k] && !d->item_hidden[k]) any = 1;
    if (!any) return 0;
    if (rt->guest.active) return fail(rt, "A user item procedure is owed while another guest call is running.");
    if (!d->port_window && !mac_dlgwin_available(rt)) return fail(rt, "A dialog user item procedure needs the colour window machinery (InitWindows on the indexed display).");
    uint32_t scratch = (sp - 96) & ~1u;
    if (!valid(rt, scratch - 64, 160)) return fail(rt, "A dialog has no stack room for its user item procedures.");
    d->user_prev_dlgport = M(rt)->port; d->user_prev_current = rt->current_port; d->user_prev_port = rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0;
    if (!mac_dlgwin_ensure(rt, d)) return mac_cpu_halted(rt->cpu) ? -1 : fail(rt, "A dialog could not create its port window for a user item.");
    M(rt)->port = d->ptr; rt->current_port = d->port_window;
    if (rt->qd_globals) wr(rt, rt->qd_globals, 4, d->port_window);
    d->user_next = 0; d->user_running = 1; d->user_sp = sp; d->user_pc = pc;
    mac_cpu_set(rt->cpu, MAC_A7, scratch); mac_cpu_set(rt->cpu, MAC_PC, pc);
    int rc = next_proc(rt, d);
    return rc < 0 ? -1 : 1;
}
