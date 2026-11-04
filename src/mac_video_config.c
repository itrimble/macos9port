#include "mac_runtime.h"

/* GetVideoDefault returns the two-byte Start Manager DefVideoRec at A0.
 * Public interface: https://www.ioi-xd.net/os9docs/Start_8h.html
 * No slot default is configured for our built-in virtual display: slot zero
 * selects the first available monitor. This is not a NuBus ROM or PRAM image. */
int mac_video_config_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa080) return 0;
    MacCpu *cpu = rt->cpu;
    if (mac_cpu_halted(cpu)) return -1;
    uint32_t p = mac_cpu_get(cpu, MAC_A0);
    size_t size = mac_cpu_ram_size(cpu);
    if (!p || p > size || size - p < 2) {
        mac_cpu_halt(cpu, "GetVideoDefault requires a writable two-byte record.");
        return -1;
    }
    /* Byte fields impose no alignment requirement. Validate both first. */
    mac_cpu_write(cpu, p, 1, 0);
    mac_cpu_write(cpu, p + 1, 1, 0);
    return 1;
}
