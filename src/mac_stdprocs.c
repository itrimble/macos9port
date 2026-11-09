#include "mac_runtime.h"

/* SetStdCProcs(VAR procs: CQDProcs) ($AA4E): fills the 80-byte record of bottleneck-procedure pointers with the standard routines so a
 * program can replace some of them (Castle of Dr. Brain replaces rectProc with a no-op around TEUpdate to suppress its erase). Entries
 * 0 to 12 (text, line, rect, round rect, oval, arc, poly, region, bits, comment, text measure, get pic, put pic) point at the
 * guest-callable stub of the matching Std... trap, so a program that calls a standard entry runs that trap, and one that is not
 * implemented halts by name. Entry 13 (opcodeProc) and the six reserved entries after it are set to NIL: this runtime has no standard
 * routine to point them at. Nothing here makes QuickDraw call a port's grafProcs: drawing paths do not consult them. */
int mac_stdprocs_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xaa4e) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    static const uint16_t std[13] = {0xa882, 0xa890, 0xa8a0, 0xa8af, 0xa8b6, 0xa8bd, 0xa8c5, 0xa8d1, 0xa8eb, 0xa8f1, 0xa8ed, 0xa8ee, 0xa8f0};
    size_t z = mac_cpu_ram_size(rt->cpu);
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 4) { mac_cpu_halt(rt->cpu, "SetStdCProcs has an invalid stack."); return -1; }
    uint32_t p = mac_cpu_read(rt->cpu, sp, 4);
    if (p < 0x400 || (p & 1) || p > z || z - p < 80) { mac_cpu_halt(rt->cpu, "SetStdCProcs record is outside guest RAM."); return -1; }
    for (unsigned i = 0; i < 20; i++)
        mac_cpu_write(rt->cpu, p + 4 * i, 4, i < 13 ? mac_native_stub(std[i] & 0xfff) : 0);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
    return 1;
}
