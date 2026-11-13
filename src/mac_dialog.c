#include "mac_runtime.h"
#include "mac_handles.h"
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

/* The Dialog Manager keeps ParamText's four strings in guest handles whose addresses sit in the low-memory array DAStrings ($0AA0, four longs), and a
 * program may read them from there (the QuickTime installer builds its own alert text that way, then disposes the handles and zeroes the array). Each
 * new string replaces the old handle of its slot. A runtime without a Memory Manager (a bare test rig) simply leaves the slot empty. */
enum { DA_STRINGS = 0x0aa0 };
static void publish_da_string(MacRuntime *rt, unsigned slot, const unsigned char *pascal) {
    uint32_t at = DA_STRINGS + slot * 4, old = mac_cpu_read(rt->cpu, at, 4), p, n;
    if (old && mac_handle_block(rt, old, &p, &n)) mac_handle_dispose(rt, old);   /* only a block we can vouch for; a stale slot is just overwritten */
    uint32_t h = mac_handle_new(rt, (uint32_t)pascal[0] + 1u);
    if (h && mac_handle_block(rt, h, &p, &n) && n >= (uint32_t)pascal[0] + 1u) memcpy(mac_cpu_ram(rt->cpu) + p, pascal, (size_t)pascal[0] + 1u);
    else if (h) { mac_handle_dispose(rt, h); h = 0; }
    mac_cpu_write(rt->cpu, at, 4, h);
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
        uint32_t given[4];
        for (unsigned i = 0; i < 4; i++) given[i] = mac_cpu_read(cpu, sp + (3 - i) * 4, 4);
        memcpy(rt->dialog_params, snapshot, sizeof snapshot);
        for (unsigned i = 0; i < 4; i++) if (given[i]) publish_da_string(rt, i, snapshot[i]);   /* a NIL parameter leaves its slot as it was, like the host copy */
        if (rt->trace) for (unsigned i = 0; i < 4; i++) {
            fprintf(stderr, "param-text ^%u='", i);
            for (unsigned k = 1; k <= snapshot[i][0]; k++) fputc(snapshot[i][k] >= 32 && snapshot[i][k] < 127 ? snapshot[i][k] : '.', stderr);
            fprintf(stderr, "' (len %u)\n", snapshot[i][0]);
        }
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
