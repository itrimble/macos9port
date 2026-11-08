#include "mac_runtime.h"

/* Independent environment-query implementation. Register ABI and selectors:
 * https://leopard-adc.pepas.com/documentation/mac/pdf/Operating_System_Utilities/Gestalt_Manager.pdf
 * sysv is a compatibility profile, not a claim to implement all System 6 APIs.
 * Unsupported selectors return gestaltUndefSelectorErr, never fabricated support.
 */
static int environment_result(MacRuntime *rt, int16_t error) {
    mac_cpu_set(rt->cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(rt->cpu, MAC_SR) & ~15u;
    mac_cpu_set(rt->cpu, MAC_SR, sr | (error ? 8u : 4u));
    return 1;
}

static int sys_environs(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    int16_t requested = (int16_t)mac_cpu_get(cpu, MAC_D0);
    if (requested <= 0) return environment_result(rt, -5501);
    uint32_t p = mac_cpu_get(cpu, MAC_A0);
    size_t size = mac_cpu_ram_size(cpu);
    if (!p || (p & 1) || p > size || size - p < 16) {
        mac_cpu_halt(cpu, "SysEnvirons requires an aligned 16-byte record.");
        return -1;
    }
    /* Same version-1/2 record shape. The virtual machine has no Apple model,
     * physical keyboard identity, FPU or AppleTalk driver. This API has no sound
     * capability field. Color denotes the selected bounded indexed profile, as
     * in Gestalt; it does not claim complete Color QuickDraw implementation.
     * Successful results identify the registered authored System volume, whose
     * pinned System resource fork has real independently generated backing. */
    mac_cpu_write(cpu,p,2,requested > 2 ? 2 : (uint16_t)requested);
    mac_cpu_write(cpu,p+2,2,0); /* envMachUnknown */
    mac_cpu_write(cpu,p+4,2,rt->system_volume ? 0x0607 : 0); /* Compatibility profile. */
    mac_cpu_write(cpu,p+6,2,3); /* env68020 */
    mac_cpu_write(cpu,p+8,1,0);
    mac_cpu_write(cpu,p+9,1,rt->main_device && rt->screen_depth == 8);
    mac_cpu_write(cpu,p+10,2,0); /* envUnknownKbd */
    mac_cpu_write(cpu,p+12,2,0); /* No AppleTalk. */
    mac_cpu_write(cpu,p+14,2,rt->system_volume ? (uint16_t)-2 : 0);
    return environment_result(rt, requested > 2 ? -5502 : rt->system_volume ? 0 : -5500);
}

int mac_system_service(MacRuntime *rt, uint16_t opcode) {
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    if (opcode == 0xa090) return sys_environs(rt);
    /* CacheFlush: interpreter fetches directly from coherent guest RAM.
     * https://leopard-adc.pepas.com/technotes/hw/hw_06.html */
    if (opcode == 0xa0bd) return 1;
    if (opcode != 0xa1ad && opcode != 0xa0ad) return 0;
    uint32_t selector = mac_cpu_get(rt->cpu, MAC_D0), response = 0;
    int16_t error = 0;
    switch (selector) {
    case 0x73797376: response = 0x0607; break; /* sysv */
    case 0x70726f63: response = 3; break; /* proc: MC68020 */
    case 0x72616d20: response = rt->a5 ? rt->a5 : 0x800000; break; /* ram : application address space */
    case 0x66707520: break; /* fpu : no floating point unit */
    case 0x71642020: /* Selected drawing profile; unsupported calls still halt. */
        response = rt->main_device && rt->screen_depth == 8 ? 0x100 : 0; break;
    default: error = -5551; break;
    }
    mac_cpu_set(rt->cpu, MAC_A0, response);
    mac_cpu_set(rt->cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(rt->cpu, MAC_SR) & ~15u;
    mac_cpu_set(rt->cpu, MAC_SR, sr | (error ? 8u : 4u));
    return 1;
}
