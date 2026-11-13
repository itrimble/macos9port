#include "mac_dlg_int.h"
#include "mac_handles.h"
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
