#include "mac_runtime.h"

/* Index2Color ($AA34) and Color2Index ($AA33), signatures from the MPW interface listing:
 *   PROCEDURE Index2Color(index: LONGINT; VAR aColor: RGBColor)      aColor SP, index SP+4
 *   FUNCTION  Color2Index(myColor: RGBColor): LONGINT                 a Pascal record argument larger than 4 bytes is passed as its address (from my
 *                                                                      memory of the calling convention; the C interface takes const RGBColor *): the
 *                                                                      stack holds that pointer at SP and the 4-byte result above it
 * The device is the 8-bit indexed main device: Index2Color gives the entry's colour widened to 16 bits per channel; an index above 255 halts (the real
 * result for an index the depth cannot hold is undefined). Color2Index returns the entry nearest in squared RGB distance (first of equals), limited
 * to the entries the Palette Manager has not reserved (the same rule as RGBForeColor here). */
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }

int mac_colorindex_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xaa33 && opcode != 0xaa34) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (rt->screen_depth != 8) return fail(rt, "Index2Color/Color2Index need the 8-bit indexed display.");
    if (opcode == 0xaa34) {
        if ((sp & 1) || !ok(rt, sp, 8)) return fail(rt, "Index2Color stack is invalid.");
        uint32_t out = mac_cpu_read(cpu, sp, 4), index = mac_cpu_read(cpu, sp + 4, 4);
        if (!ok(rt, out, 6) || (out & 1)) return fail(rt, "Index2Color RGBColor pointer is invalid.");
        if (index > 255) return fail(rt, "Index2Color index is outside the 8-bit device table.");
        uint32_t c = rt->palette[index];
        mac_cpu_write(cpu, out, 2, ((c >> 16) & 255) * 257);
        mac_cpu_write(cpu, out + 2, 2, ((c >> 8) & 255) * 257);
        mac_cpu_write(cpu, out + 4, 2, (c & 255) * 257);
        mac_cpu_set(cpu, MAC_A7, sp + 8);
        return 1;
    }
    if ((sp & 1) || !ok(rt, sp, 8)) return fail(rt, "Color2Index stack is invalid.");
    uint32_t in = mac_cpu_read(cpu, sp, 4);
    if (!ok(rt, in, 6) || (in & 1)) return fail(rt, "Color2Index RGBColor pointer is invalid.");
    long want[3] = { (long)mac_cpu_read(cpu, in, 2), (long)mac_cpu_read(cpu, in + 2, 2), (long)mac_cpu_read(cpu, in + 4, 2) };
    uint8_t allowed[256]; for (unsigned i = 0; i < 256; i++) allowed[i] = 1;
    if (rt->main_device && mac_palette_match_mask(rt, allowed) < 0) return -1;
    unsigned best = 0; long bd = -1;
    for (unsigned i = 0; i < 256; i++) {
        if (!allowed[i]) continue;
        uint32_t c = rt->palette[i];
        long dr = want[0] - (long)(((c >> 16) & 255) * 257), dg = want[1] - (long)(((c >> 8) & 255) * 257), db = want[2] - (long)((c & 255) * 257), d = dr * dr + dg * dg + db * db;
        if (bd < 0 || d < bd) { bd = d; best = i; }
    }
    if (bd < 0) return fail(rt, "Color2Index has no unreserved device entry to match.");
    mac_cpu_write(cpu, sp + 4, 4, best);
    mac_cpu_set(cpu, MAC_A7, sp + 4);
    return 1;
}
