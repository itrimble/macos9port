#include "mac_runtime.h"

/* SetResLoad ($A99B): PROCEDURE SetResLoad(load: BOOLEAN) (signature from the MPW interface listing; the Boolean is the high byte of its stack word).
 * Accepted and ignored: this Resource Manager always loads a resource when it hands out its handle, so a program that turns loading off to
 * learn a resource's size or attributes still gets the right answers, but one that then tests the handle for NIL/purged data to see "unloaded"
 * would see a loaded one. That difference is deliberate and not modelled. */
int mac_resload_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa99b) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), z = (uint32_t)mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 2) { mac_cpu_halt(rt->cpu, "SetResLoad stack is invalid."); return -1; }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}
