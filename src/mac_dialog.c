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
    if (opcode != 0xa98b && opcode != 0xa985) return 0;
    MacCpu *cpu = rt->cpu;
    if (mac_cpu_halted(cpu)) return -1;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (!valid(rt, sp, opcode == 0xa98b ? 16 : 8, 1)) return -1;
    if (opcode == 0xa98b) {
        unsigned char snapshot[4][256] = {{0}};
        for (unsigned i = 0; i < 4; i++) {
            uint32_t p = mac_cpu_read(cpu, sp + (3 - i) * 4, 4);
            /* Public API requires an empty Pascal string, not a NULL pointer. */
            if (!p) { mac_cpu_halt(cpu, "ParamText requires a Pascal string, not NIL."); return -1; }
            if (!valid(rt, p, 1, 0)) return -1;
            unsigned n = mac_cpu_read(cpu, p, 1);
            if (!valid(rt, p, n + 1, 0)) return -1;
            memcpy(snapshot[i], mac_cpu_ram(cpu) + p, n + 1);
        }
        memcpy(rt->dialog_params, snapshot, sizeof snapshot);
        mac_cpu_set(cpu, MAC_A7, sp + 16);
        return 1;
    }
    /* Alert cannot return a fabricated button selection. Stop while preserving
     * the guest frame, so the host failure UI can show the application message. */
    int16_t id = (int16_t)mac_cpu_read(cpu, sp + 4, 2);
    char message[256];
    const unsigned char *param = (const unsigned char *)rt->dialog_params[0];
    unsigned n = param[0];
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = param[i + 1];
        message[i] = c >= 32 && c < 127 ? (char)c : '?';
    }
    message[n] = 0;
    char reason[384];
    snprintf(reason, sizeof reason, "Alert %d unsupported: %s", id,
             n ? message : "guest dialog requires interaction");
    mac_cpu_halt(cpu, reason);
    return -1;
}
