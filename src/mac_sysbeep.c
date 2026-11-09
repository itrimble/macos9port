#include "mac_runtime.h"

/* SysBeep(duration: INTEGER) ($A9C8): the system alert sound. Alert sounds are not played by this runtime (the same as ErrorSound and the alert dialogs'
 * beep counts): the call validates its stack, pops its one word argument and returns. */
int mac_sysbeep_service(MacRuntime *rt, uint16_t op) {
    if (op != 0xa9c8) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 2) { mac_cpu_halt(rt->cpu, "SysBeep has an invalid stack."); return -1; }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}
