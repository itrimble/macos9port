#include "mac_runtime.h"

/* CmpString ($A03C and its modifier forms): A0 and A1 point to the two strings, D0 holds the first length in its high word and the second length in its
 * low word; D0 returns 0 when the strings are equal and 1 when not (and the zero condition code follows). Trap word bit 9 ($0200) asks for a case-sensitive
 * comparison, bit 10 ($0400) for diacritical marks to be ignored (from my memory of Inside Macintosh: Operating System Utilities, not a document opened
 * here). Case folding covers the ASCII letters only: the Macintosh character set's accented capitals do not fold, and with marks ignored the accented
 * letters of the Mac OS Roman set are not mapped to their bases either; bytes above 127 therefore compare literally. Both ranges must lie in guest RAM
 * or the guest halts. */
int mac_cmpstring_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode >= 0xa800 || (opcode & 0xff) != 0x3c) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), a1 = mac_cpu_get(cpu, MAC_A1), d0 = mac_cpu_get(cpu, MAC_D0);
    uint32_t n0 = d0 >> 16, n1 = d0 & 0xffff, z = (uint32_t)mac_cpu_ram_size(cpu);
    if ((n0 && (a0 < 0x400 || a0 > z || z - a0 < n0)) || (n1 && (a1 < 0x400 || a1 > z || z - a1 < n1))) {
        mac_cpu_halt(cpu, "CmpString string range is outside guest RAM."); return -1;
    }
    int sensitive = (opcode & 0x200) != 0;
    int differ = n0 != n1;
    const uint8_t *m = mac_cpu_ram(cpu);
    for (uint32_t i = 0; !differ && i < n0; i++) {
        unsigned x = m[a0 + i], y = m[a1 + i];
        if (!sensitive) { if (x >= 'a' && x <= 'z') x -= 32; if (y >= 'a' && y <= 'z') y -= 32; }
        if (x != y) differ = 1;
    }
    mac_cpu_set(cpu, MAC_D0, differ ? 1 : 0);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (differ ? 0u : 4u));
    return 1;
}
