#include "mac_dlg_int.h"
#include "mac_handles.h"
#include "mac_dlgwindow.h"
#include <stdlib.h>
#include <string.h>

/* SetWTitle(theWindow, title: Str255) ($A91A) on a dialog. Our dialogs are drawn from their DITLs inside a plain box, with no title bar, so the text
 * is kept where a program looks for it: a new string handle in the WindowRecord's titleHandle (+134) and its pixel width estimate in titleWidth (+138),
 * as the window code does for an ordinary window. The title is read from guest RAM with its length byte; one that runs off the end of RAM halts
 * and nothing is stored. A previous titleHandle is disposed. Returns 1, or -1 after halting. */
int mac_dlg_set_title(MacRuntime *rt, Dlg *d, uint32_t src) {
    size_t z = mac_cpu_ram_size(rt->cpu);
    if (src < 0x400 || src >= z) { mac_cpu_halt(rt->cpu, "SetWTitle title pointer is outside guest RAM."); return -1; }
    unsigned n = mac_cpu_ram(rt->cpu)[src];
    if (n > z - src - 1) { mac_cpu_halt(rt->cpu, "SetWTitle title runs past the end of guest RAM."); return -1; }
    uint32_t handle = mac_handle_new(rt, n + 1), pointer, size;
    if (!handle || !mac_handle_block(rt, handle, &pointer, &size) || size < n + 1) { mac_cpu_halt(rt->cpu, "SetWTitle could not allocate the title."); return -1; }
    memcpy(mac_cpu_ram(rt->cpu) + pointer, mac_cpu_ram(rt->cpu) + src, n + 1);
    uint32_t old = (uint32_t)mac_cpu_read(rt->cpu, d->ptr + 134, 4);
    mac_cpu_write(rt->cpu, d->ptr + 134, 4, handle);
    mac_cpu_write(rt->cpu, d->ptr + 138, 2, n * 8u);
    if (old) mac_handle_dispose(rt, old);
    return 1;
}

/* MoveControl(ctl, h, v) on the handle GetDItem gave for a button, check box or radio button item: the item's rectangle moves so its top left is (h, v) in the dialog's local
 * coordinates, size unchanged, like SetDItem the next repaint of the dialog shows it (the dialog's state hash includes the box). A hidden item keeps its off-screen offset. */
int mac_dlg_move_item(MacRuntime *rt, Dlg *d, unsigned k, int h, int v) {
    MacDitlItem *it = &d->ditl.items[k];
    int w = it->right - it->left, ht = it->bottom - it->top;
    if (d->item_hidden[k] && h >= 8192) h -= 16384;
    if (h < -16000 || h > 16000 || v < -16000 || v > 16000) { mac_cpu_halt(rt->cpu, "MoveControl moves a dialog item out of range."); return -1; }
    it->left = (int16_t)h; it->top = (int16_t)v; it->right = (int16_t)(h + w); it->bottom = (int16_t)(v + ht);
    return 0;
}

/* SizeWindow(theDialog, w, h, fUpdate) on a dialog: the box takes the new size (kept on the screen), the saved backdrop is replaced and the dialog is repainted at its next pass.
 * A dialog's port window (the owner of its controls) is resized with it. */
int mac_dlg_size(MacRuntime *rt, Dlg *d, int w, int h) {
    if (d->modal || d->select_busy) { mac_cpu_halt(rt->cpu, "A dialog was resized while it was being used."); return -1; }
    if (w < 1 || h < 1 || w > 640 || h > 480) { mac_cpu_halt(rt->cpu, "SizeWindow gives a dialog a size that does not fit the screen."); return -1; }
    uint8_t *nb = malloc((size_t)w * (size_t)h);
    if (!nb) { mac_cpu_halt(rt->cpu, "No memory for the resized dialog's backdrop."); return -1; }
    MacPen pen; mac_pen_init(&pen, rt, d->left, d->top, d->width, d->height);
    if (d->drawn) mac_pen_restore(&pen, d->backdrop);
    free(d->backdrop); d->backdrop = nb; d->drawn = 0;
    d->width = w; d->height = h;
    if (d->top + h > 480) d->top = 480 - h;
    if (d->left + w > 640) d->left = 640 - w;
    mac_dlg_set_region(rt, d->region[0], d->top - 1, d->left - 1, d->top + h + 1, d->left + w + 1);
    mac_dlg_set_region(rt, d->region[1], d->top, d->left, d->top + h, d->left + w);
    return !d->port_window || mac_dlgwin_size(rt, d->port_window, w, h) ? 0 : -1;
}

/* SetCTitle(ctl, title) and GetCTitle(ctl, VAR title) on the handle GetDItem gave for a button, check box or radio button item: the item's text (up to 255 bytes) is replaced or copied out.
 * The stack at the call holds the string (pointer or VAR pointer) at sp and the control handle at sp + 4; the caller pops them. */
int mac_dlg_item_title(MacRuntime *rt, Dlg *d, unsigned k, int set, uint32_t sp) {
    uint32_t p = (uint32_t)mac_cpu_read(rt->cpu, sp, 4); size_t z = mac_cpu_ram_size(rt->cpu);
    if (!has_text(&d->ditl.items[k])) { mac_cpu_halt(rt->cpu, "SetCTitle/GetCTitle on a dialog item that has no title."); return -1; }
    if (p < 0x400 || p > z || z - p < (set ? 1u : 256u)) { mac_cpu_halt(rt->cpu, "A control title pointer is outside guest RAM."); return -1; }
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    if (set) {
        unsigned n = ram[p];
        if (n > z - p - 1) { mac_cpu_halt(rt->cpu, "A control title runs past the end of guest RAM."); return -1; }
        memcpy(d->text[k], ram + p + 1, n); d->text[k][n] = 0; d->length[k] = (uint8_t)n;
    } else { ram[p] = d->length[k]; memcpy(ram + p + 1, d->text[k], d->length[k]); }
    return 0;
}

/* An update event for a visible dialog that has a port window and has just repainted itself (its native drawing covered what the program draws in it): WaitNextEvent and GetNextEvent
 * hand it out once, like the Window Manager does for a newly shown window, so a program that draws its own items in an event loop (rather than inside ModalDialog) repaints them. 1 and *out, else 0. */
int mac_dlgmgr_update_event(MacRuntime *rt, uint16_t mask, MacEvent *out) {
    if (!rt->dlgmgr || !(mask & (1u << 6))) return 0;
    for (unsigned i = 0; i < MAX_OPEN; i++) {
        Dlg *d = M(rt)->open[i];
        if (!d || !d->update_due || !d->port_window || d->hidden || d->modal) continue;
        out->what = 6; out->message = d->ptr; d->update_due = 0;
        return 1;
    }
    return 0;
}
