#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>

/* ParamText ABI: four Str255 pointers, last argument at SP. Snapshot Pascal
 * strings byte-for-byte; this is not yet dialog rendering or DAStrings support.
 * Public interface: dev.os9.ca/techpubs/mac/Toolbox/Toolbox-417.html */
static int valid(MacRuntime *rt, uint32_t p, size_t n, int aligned) {
    if ((aligned && (p & 1)) || p > mac_cpu_ram_size(rt->cpu) ||
        n > mac_cpu_ram_size(rt->cpu) - p) {
        mac_cpu_halt(rt->cpu, "Dialog argument exceeds guest RAM or is unaligned.");
        return 0;
    }
    return 1;
}

int mac_dialog_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa98b) return 0; /* Alert and its kin live in mac_alert.c */
    MacCpu *cpu = rt->cpu;
    if (mac_cpu_halted(cpu)) return -1;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (!valid(rt, sp, 16, 1)) return -1;
    {
        unsigned char snapshot[4][256] = {{0}};
        for (unsigned i = 0; i < 4; i++) {
            uint32_t p = mac_cpu_read(cpu, sp + (3 - i) * 4, 4);
            /* The public text says to pass empty strings for unused parameters and does not cover NIL. A shipped application passes NIL for
             * parameters it does not set, so a NIL leaves that parameter as it was (an inference from that use, not from a page). */
            if (!p) { memcpy(snapshot[i], rt->dialog_params[i], sizeof snapshot[i]); continue; }
            if (!valid(rt, p, 1, 0)) return -1;
            unsigned n = mac_cpu_read(cpu, p, 1);
            if (!valid(rt, p, n + 1, 0)) return -1;
            memcpy(snapshot[i], mac_cpu_ram(cpu) + p, n + 1);
        }
        memcpy(rt->dialog_params, snapshot, sizeof snapshot);
        mac_cpu_set(cpu, MAC_A7, sp + 16);
        return 1;
    }
    return 0;
}

/* The next mouse or key event queued for a modal dialog (what 1 down, 2 up, 3 key, 5 auto-key), removed from the queue.
 * Update, activate and other events stay queued for the application. Returns 1 if one was taken. */
int mac_dialog_next_event(MacRuntime *rt, MacEvent *out) {
    for (unsigned i = 0; i < rt->event_count; i++) {
        MacEvent e = rt->events[(rt->event_head + i) % 64];
        if (e.what != 1 && e.what != 2 && e.what != 3 && e.what != 5) continue;
        for (unsigned j = i; j + 1 < rt->event_count; j++) rt->events[(rt->event_head + j) % 64] = rt->events[(rt->event_head + j + 1) % 64];
        rt->event_count--;
        *out = e;
        return 1;
    }
    return 0;
}
