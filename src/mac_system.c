#include "mac_runtime.h"

/* Independent environment-query implementation. Register ABI and selectors:
 * https://leopard-adc.pepas.com/documentation/mac/pdf/Operating_System_Utilities/Gestalt_Manager.pdf
 * sysv and the machine type are a compatibility profile (see MAC_PROFILE_* in mac_runtime.h), not a claim to
 * implement every System 7 API.
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
     * The machine type is the Mac II-class profile identity (68020 + Color QuickDraw, no FPU).
     * Successful results identify the registered authored System volume, whose
     * pinned System resource fork has real independently generated backing. */
    mac_cpu_write(cpu,p,2,requested > 2 ? 2 : (uint16_t)requested);
    mac_cpu_write(cpu,p+2,2,rt->system_volume ? MAC_PROFILE_SYSENV_MACHINE : 0); /* envMacII with the registered System */
    mac_cpu_write(cpu,p+4,2,rt->system_volume ? MAC_PROFILE_SYSTEM_VERSION : 0); /* Compatibility profile. */
    mac_cpu_write(cpu,p+6,2,3); /* env68020 */
    mac_cpu_write(cpu,p+8,1,0);
    mac_cpu_write(cpu,p+9,1,rt->main_device && rt->screen_depth == 8);
    mac_cpu_write(cpu,p+10,2,0); /* envUnknownKbd */
    mac_cpu_write(cpu,p+12,2,0); /* No AppleTalk. */
    mac_cpu_write(cpu,p+14,2,rt->system_volume ? (uint16_t)-2 : 0);
    return environment_result(rt, requested > 2 ? -5502 : rt->system_volume ? 0 : -5500);
}

/* Built-in selectors; -5551 (gestaltUndefSelectorErr) when not built in. */
static int16_t builtin_gestalt(MacRuntime *rt, uint32_t selector, uint32_t *response) {
    switch (selector) {
    case 0x73797376: *response = MAC_PROFILE_SYSTEM_VERSION; return 0; /* sysv */
    case 0x6d616368: *response = MAC_PROFILE_GESTALT_MACHINE; return 0; /* mach */
    case 0x70726f63: *response = 3; return 0; /* proc: MC68020 */
    case 0x72616d20: *response = rt->a5 ? rt->a5 : 0x800000; return 0; /* ram : application address space */
    case 0x66707520: return 0; /* fpu : no floating point unit */
    case 0x71642020: /* Selected drawing profile; unsupported calls still halt. */
        *response = rt->main_device && rt->screen_depth == 8 ? 0x100 : 0; return 0;
    case 0x71647277: /* qdrw: only gestaltHasColor (bit 0), under the same condition as 'qd  '. */
        *response = rt->main_device && rt->screen_depth == 8 ? 1 : 0; return 0;
    case 0x66732020: /* fs  : gestaltHasFSSpecCalls (bit 1) only; the FSp calls dispatch through $AA52 (mac_fsspec.c). */
        *response = 1u << 1; return 0;
    case 0x73746466: /* stdf: gestaltStandardFile58 (bit 0) only (mac_stdput.c, mac_stdfile.c). */
        *response = 1; return 0;
    default: return -5551;
    }
}

static int gestalt_find(const MacRuntime *rt, uint32_t selector) {
    for (unsigned i = 0; i < rt->gestalt_count; i++)
        if (rt->gestalt_sel[i] == selector) return (int)i;
    return -1;
}

int mac_gestalt_set(MacRuntime *rt, uint32_t selector, uint32_t value, uint32_t proc) {
    uint32_t ignored;
    if (!rt || builtin_gestalt(rt, selector, &ignored) != -5551 || gestalt_find(rt, selector) >= 0 ||
        rt->gestalt_count >= MAC_GESTALT_MAX) return -1;
    rt->gestalt_sel[rt->gestalt_count] = selector;
    rt->gestalt_val[rt->gestalt_count] = value;
    rt->gestalt_proc[rt->gestalt_count++] = proc;
    return 0;
}

/* NewGestalt ($A3AD) / ReplaceGestalt ($A5AD): D0 selector, A0 procedure.
 * The procedure address is recorded but never invoked, so the registered
 * selector reports "present" with response 0 (see mac_init.h). */
static int gestalt_register(MacRuntime *rt, int replace) {
    uint32_t selector = mac_cpu_get(rt->cpu, MAC_D0), proc = mac_cpu_get(rt->cpu, MAC_A0), ignored;
    int16_t error = 0;
    int at = gestalt_find(rt, selector);
    if (builtin_gestalt(rt, selector, &ignored) != -5551) error = replace ? 0 : -5550;
    else if (replace) { if (at < 0) error = -5551; else rt->gestalt_proc[at] = proc; }
    else if (at >= 0) error = -5550; /* gestaltDupSelectorErr */
    else if (mac_gestalt_set(rt, selector, 0, proc)) error = -108; /* memFullErr */
    mac_cpu_set(rt->cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(rt->cpu, MAC_SR) & ~15u;
    mac_cpu_set(rt->cpu, MAC_SR, sr | (error ? 8u : 4u));
    return 1;
}

int mac_system_service(MacRuntime *rt, uint16_t opcode) {
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    if (opcode == 0xa090) return sys_environs(rt);
    /* CacheFlush: interpreter fetches directly from coherent guest RAM.
     * https://leopard-adc.pepas.com/technotes/hw/hw_06.html */
    if (opcode == 0xa0bd) return 1;
    if (opcode == 0xa3ad || opcode == 0xa5ad) return gestalt_register(rt, opcode == 0xa5ad);
    if (opcode != 0xa1ad && opcode != 0xa0ad) return 0;
    uint32_t selector = mac_cpu_get(rt->cpu, MAC_D0), response = 0;
    int16_t error = builtin_gestalt(rt, selector, &response);
    if (error == -5551) {
        int at = gestalt_find(rt, selector);
        if (at >= 0) { response = rt->gestalt_val[at]; error = 0; }
    }
    mac_cpu_set(rt->cpu, MAC_A0, response);
    mac_cpu_set(rt->cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(rt->cpu, MAC_SR) & ~15u;
    mac_cpu_set(rt->cpu, MAC_SR, sr | (error ? 8u : 4u));
    return 1;
}
