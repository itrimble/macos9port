#include "mac_runtime.h"

/* Random ($A861): Pascal FUNCTION Random: INTEGER, with the result slot at the top of the stack and no arguments to pop.
 * QuickDraw's randSeed (a long, 126 bytes below the thePort pointer that InitGraf was given) advances as a Lehmer generator,
 * seed = seed * 16807 mod (2^31 - 1), and the result is the seed's low 16 bits as a signed integer, with -32768 returned as 0
 * so the range is -32767..32767. A seed of 0 stays 0 (a property of the generator; programs seed it from the clock).
 * Needs InitGraf to have run: without it there is no randSeed, so the call halts by name. */
int mac_random_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa861) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    size_t z = mac_cpu_ram_size(rt->cpu);
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < 2) { mac_cpu_halt(rt->cpu, "Random stack is invalid."); return -1; }
    if (rt->qd_globals < 126 || rt->qd_globals - 126 > z - 4) { mac_cpu_halt(rt->cpu, "Random needs InitGraf to have set up randSeed."); return -1; }
    uint32_t at = rt->qd_globals - 126;
    uint64_t next = (uint64_t)mac_cpu_read(rt->cpu, at, 4) * 16807u % 2147483647u;
    mac_cpu_write(rt->cpu, at, 4, (uint32_t)next);
    int16_t result = (int16_t)(next & 0xffff);
    mac_cpu_write(rt->cpu, sp, 2, (uint16_t)(result == -32768 ? 0 : result));
    return 1;
}
