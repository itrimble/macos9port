#include "mac_runtime.h"

/* CmpString ($A03C and its modifier forms): A0 and A1 point to the two strings, D0 holds the first length in its high word and the second length in its
 * low word; D0 returns 0 when the strings are equal and 1 when not (and the zero condition code follows). Trap word bit 9 ($0200) asks for a case-sensitive
 * comparison, bit 10 ($0400) for diacritical marks to be ignored (from my memory of Inside Macintosh: Operating System Utilities, not a document opened
 * here). Case folding covers the ASCII letters only: the Macintosh character set's accented capitals do not fold, and with marks ignored the accented
 * letters of the Mac OS Roman set are not mapped to their bases either; bytes above 127 therefore compare literally. Both ranges must lie in guest RAM
 * or the guest halts. */
/* UprString ($A054, and UpperString by the same trap): A0 points to the text (no length byte), D0's low word is its length; the letters become capitals in place.
 * The ASCII letters fold, and so do the accented lower-case letters of the Mac OS Roman set to their capitals (a-grave to A-grave and so on, in the table below,
 * from the public character set, not checked against the system's own routine). Bit 10 of the trap ($0400, ignore marks) is accepted and does not change the
 * result. The range must lie in guest RAM or the guest halts. */
static const uint8_t UPPER_PAIRS[][2] = {
    {0x88, 0xCB}, {0x87, 0xE7}, {0x89, 0xE5}, {0x8B, 0xCC}, {0x8A, 0x80}, {0x8C, 0x81}, {0x8D, 0x82}, {0x8F, 0xE9}, {0x8E, 0x83}, {0x90, 0xE6},
    {0x91, 0xE8}, {0x93, 0xED}, {0x92, 0xEA}, {0x94, 0xEB}, {0x95, 0xEC}, {0x96, 0x84}, {0x98, 0xF1}, {0x97, 0xEE}, {0x99, 0xEF}, {0x9B, 0xCD},
    {0x9A, 0x85}, {0x9D, 0xF4}, {0x9C, 0xF2}, {0x9E, 0xF3}, {0x9F, 0x86}, {0xBF, 0xAF}, {0xBE, 0xAE}, {0xCF, 0xCE}, {0xD8, 0xD9},
};
static int upper_string(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), n = mac_cpu_get(cpu, MAC_D0) & 0xffff, z = (uint32_t)mac_cpu_ram_size(cpu);
    if (n && (a0 < 0x400 || a0 > z || z - a0 < n)) { mac_cpu_halt(cpu, "UprString text range is outside guest RAM."); return -1; }
    uint8_t *m = mac_cpu_ram(cpu) + a0;
    for (uint32_t i = 0; i < n; i++) {
        if (m[i] >= 'a' && m[i] <= 'z') m[i] -= 32;
        else if (m[i] >= 0x80) for (unsigned k = 0; k < sizeof UPPER_PAIRS / sizeof UPPER_PAIRS[0]; k++) if (m[i] == UPPER_PAIRS[k][0]) { m[i] = UPPER_PAIRS[k][1]; break; }
    }
    mac_cpu_set(cpu, MAC_D0, 0);
    return 1;
}

int mac_cmpstring_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode < 0xa800 && (opcode & 0xff) == 0x54) {
        if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
        return upper_string(rt);
    }
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
