#include "mac_runtime.h"

/* Toolbox Utilities, integer and bit operations: BitAnd $A858, BitXor $A859, BitNot $A85A, BitOr $A85B, BitShift $A85C, BitTst $A85D,
 * BitSet $A85E, BitClr $A85F, HiWord $A86A, LoWord $A86B. Not here (and so halting by name when reached): the fixed-point routines
 * (FixMul, FixRatio, FixRound, ...). Pascal order: the last argument is on top of the stack and a function's result slot is above the
 * arguments. BitTst, BitSet and BitClr number bits from the HIGH bit of the byte at bytePtr (bit 0 is $80) and run on into the following
 * bytes; a negative bit number halts. BitShift is a logical shift of a long: a positive count shifts left, a negative count right, zero
 * fill, and a count of 32 or more in either direction gives 0 (this is how I remember the documentation; it is not in the interface
 * listing available here). */
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
int mac_toolutil_service(MacRuntime *rt, uint16_t op) {
    if (!(op >= 0xa858 && op <= 0xa85f) && op != 0xa86a && op != 0xa86b) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *c = rt->cpu;
    size_t z = mac_cpu_ram_size(c);
    uint32_t sp = mac_cpu_get(c, MAC_A7);
    unsigned args = (op == 0xa85a || op == 0xa86a || op == 0xa86b) ? 4 : op == 0xa85c ? 6 : 8, result = (op == 0xa85e || op == 0xa85f) ? 0 : (op == 0xa85d ? 2 : op == 0xa86a || op == 0xa86b ? 2 : 4);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < args + result) return fail(rt, "Toolbox utility call has an invalid stack.");
    uint32_t a = mac_cpu_read(c, sp, 4), b = mac_cpu_read(c, sp + 4, 4);
    switch (op) {
    case 0xa858: mac_cpu_write(c, sp + 8, 4, b & a); break;                  /* BitAnd(value1, value2): value2 is on top */
    case 0xa859: mac_cpu_write(c, sp + 8, 4, b ^ a); break;
    case 0xa85b: mac_cpu_write(c, sp + 8, 4, b | a); break;
    case 0xa85a: mac_cpu_write(c, sp + 4, 4, ~a); break;
    case 0xa85c: {                                                            /* BitShift(value: LONGINT; count: INTEGER) */
        int count = (int16_t)mac_cpu_read(c, sp, 2); uint32_t v = mac_cpu_read(c, sp + 2, 4);
        mac_cpu_write(c, sp + 6, 4, count >= 32 || count <= -32 ? 0 : count >= 0 ? v << count : v >> -count);
        break; }
    case 0xa85d: case 0xa85e: case 0xa85f: {                                  /* BitTst/BitSet/BitClr(bytePtr: Ptr; bitNum: LONGINT) */
        int32_t bit = (int32_t)a; uint32_t ptr = b;
        if (bit < 0) return fail(rt, "BitTst, BitSet and BitClr with a negative bit number are unsupported.");
        uint32_t at = ptr + (uint32_t)bit / 8;
        if (ptr < 0x400 || at < ptr || at >= z) return fail(rt, "BitTst, BitSet or BitClr address is outside guest RAM.");
        unsigned mask = 0x80u >> ((uint32_t)bit % 8), byte = mac_cpu_read(c, at, 1);
        if (op == 0xa85d) mac_cpu_write(c, sp + 8, 1, (byte & mask) ? 1 : 0);
        else mac_cpu_write(c, at, 1, op == 0xa85e ? (byte | mask) : (byte & ~mask));
        break; }
    case 0xa86a: mac_cpu_write(c, sp + 4, 2, a >> 16); break;                 /* HiWord(x) */
    default:     mac_cpu_write(c, sp + 4, 2, a & 0xffff); break;              /* LoWord(x) */
    }
    mac_cpu_set(c, MAC_A7, sp + args);
    return 1;
}
