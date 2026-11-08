#include "mac_window_int.h"
#include <string.h>

/* SetWTitle(theWindow, title: Str255) ($A91A; signature from the MPW interface listing): copies the Pascal string into the window's title and, for a visible
 * window, redraws its frame so the title bar shows it. A window without a title bar (plainDBox, dBoxProc) keeps the text for GetWTitle and draws nothing
 * new. The string is read from guest RAM with its length byte, so a length that runs past the end of RAM halts, nothing is stored. The window's own
 * guest record (titleHandle) is not updated: only GetWTitle and our chrome read the title. */
int mac_window_set_title(MacRuntime *rt, MacWindowState *w, uint32_t src) {
    size_t z = mac_cpu_ram_size(rt->cpu);
    if (src < 0x400 || src >= z) { mac_cpu_halt(rt->cpu, "SetWTitle title pointer is outside guest RAM."); return -1; }
    unsigned n = mac_cpu_ram(rt->cpu)[src];
    if (n > z - src - 1) { mac_cpu_halt(rt->cpu, "SetWTitle title runs past the end of guest RAM."); return -1; }
    uint8_t title[256];
    title[0] = (uint8_t)n; memcpy(title + 1, mac_cpu_ram(rt->cpu) + src + 1, n);
    memcpy(w->title, title, n + 1);
    if (w->visible) mac_window_draw(rt, 0);
    return 1;
}
