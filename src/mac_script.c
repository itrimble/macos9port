#include "mac_runtime.h"
#include <stdio.h>

/* Script Manager (ScriptUtil, trap $A8B5): the selector is a long pushed last, so it sits at the top of the stack when the trap runs; the routine
 * pops it and its own arguments and leaves a function result above them. The only script system here is Roman (script 0). Only the selectors a
 * program has actually needed are answered; every other selector halts with its number, so nothing is guessed. Selector numbers are from memory of
 * the public interface (FontScript $82000000, IntlScript $82000002, Font2Script $82020006, GetScript $8404000C). */
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
int mac_script_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa8b5) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    size_t z = mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 8) return fail(rt, "Script Manager call has an invalid stack.");
    uint32_t selector = mac_cpu_read(rt->cpu, sp, 4);
    if (selector == 0x82000000u || selector == 0x82000002u) {   /* FontScript, IntlScript: INTEGER, always smRoman */
        mac_cpu_write(rt->cpu, sp + 4, 2, 0);
        mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
        return 1;
    }
    if (selector == 0x82020006u) {   /* Font2Script(fontNumber: INTEGER): INTEGER; every font belongs to the Roman script here */
        if (z - sp < 10) return fail(rt, "Script Manager call has an invalid stack.");
        mac_cpu_write(rt->cpu, sp + 6, 2, 0);
        mac_cpu_set(rt->cpu, MAC_A7, sp + 6);
        return 1;
    }
    if (selector == 0x8404000cu) {   /* GetScript(script: INTEGER; verb: INTEGER): LONGINT. Only the Roman script exists, and only one verb is answered */
        if (z - sp < 12) return fail(rt, "Script Manager call has an invalid stack.");
        int16_t verb = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2), script = (int16_t)mac_cpu_read(rt->cpu, sp + 6, 2);
        if (verb == 28 && script == 0) {   /* smScriptEncoding: the base text encoding of the script, MacRoman = 0 */
            mac_cpu_write(rt->cpu, sp + 8, 4, 0);
            mac_cpu_set(rt->cpu, MAC_A7, sp + 8);
            return 1;
        }
        char why[96];
        snprintf(why, sizeof why, "GetScript(script %d, verb %d) is unsupported.", script, verb);
        return fail(rt, why);
    }
    if (selector == 0x84020008u && z - sp >= 10) {   /* GetEnvirons(verb: INTEGER): LONGINT */
        int16_t verb = (int16_t)mac_cpu_read(rt->cpu, sp + 4, 2);
        if (verb == 40) {   /* smRegionCode: the system's region within its script; this machine is the US one (verUS = 0) */
            mac_cpu_write(rt->cpu, sp + 6, 4, 0);
            mac_cpu_set(rt->cpu, MAC_A7, sp + 6);
            return 1;
        }
        char why[96];
        snprintf(why, sizeof why, "GetEnvirons(verb %d) is unsupported.", verb);
        return fail(rt, why);
    }
    char why[96];
    snprintf(why, sizeof why, "Script Manager selector %08X is unsupported.", (unsigned)selector);
    return fail(rt, why);
}
