#include "mac_runtime.h"
#include <stdio.h>

/* Pack7 ($A9EE, Binary-Decimal Conversion Package), selectors 0 and 1 only.
 * Register interface, as the library glue of real applications uses it
 * (observed in Glider PRO: the glue loads the arguments into registers,
 * pushes the selector word, executes the trap, then stores D0 itself):
 *   0  NumToString: D0 = LongInt, A0 = pointer to the Str255 to fill
 *   1  StringToNum: A0 = pointer to the Str255; the LongInt returns in D0
 * Only the selector word is on the stack and the package pops just that;
 * A0 and the other registers are preserved. Other selectors halt, named.
 *
 * NumToString: optional '-', decimal digits, no leading zeros, no plus sign,
 * including -2147483648. StringToNum: the public documentation defines the
 * result only for a string of digits with an optional leading minus; this
 * implementation also accepts a leading '+', stops at the first other
 * character, wraps modulo 2^32 on overflow, and yields 0 for no digits.
 * Neither routine returns a result code. All guest accesses are checked
 * against RAM before anything is written or the stack is popped. */

static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why);
    return -1;
}

static int span_ok(const MacRuntime *rt, uint32_t p, uint32_t n) {
    size_t size = mac_cpu_ram_size(rt->cpu);
    return p <= size && n <= size - p;
}

static int num_to_string(MacRuntime *rt, uint32_t sp) {
    uint32_t dest = mac_cpu_get(rt->cpu, MAC_A0);
    int32_t value = (int32_t)mac_cpu_get(rt->cpu, MAC_D0);
    char digits[12];
    unsigned n = 0, len = 0;
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    do { digits[n++] = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    if (value < 0) digits[n++] = '-';
    if (!span_ok(rt, dest, 1 + n)) return fail(rt, "NumToString destination lies outside guest RAM.");
    mac_cpu_write(rt->cpu, dest, 1, n);
    while (n) mac_cpu_write(rt->cpu, dest + 1 + len++, 1, (uint8_t)digits[--n]);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}

static int string_to_num(MacRuntime *rt, uint32_t sp) {
    uint32_t src = mac_cpu_get(rt->cpu, MAC_A0);
    if (!span_ok(rt, src, 1)) return fail(rt, "StringToNum source lies outside guest RAM.");
    uint32_t len = mac_cpu_read(rt->cpu, src, 1);
    if (!span_ok(rt, src + 1, len)) return fail(rt, "StringToNum source runs past the end of guest RAM.");
    uint32_t i = 0, acc = 0;
    int negative = 0;
    if (i < len) {
        uint8_t c = (uint8_t)mac_cpu_read(rt->cpu, src + 1, 1);
        if (c == '-' || c == '+') { negative = c == '-'; i++; }
    }
    for (; i < len; i++) {
        uint8_t c = (uint8_t)mac_cpu_read(rt->cpu, src + 1 + i, 1);
        if (c < '0' || c > '9') break;
        acc = acc * 10u + (uint32_t)(c - '0');
    }
    if (negative) acc = 0u - acc;
    mac_cpu_set(rt->cpu, MAC_D0, acc);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}

int mac_numconv_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9ee) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || !span_ok(rt, sp, 2)) return fail(rt, "Invalid Pack7 stack.");
    unsigned sel = (unsigned)mac_cpu_read(rt->cpu, sp, 2);
    if (sel == 0) return num_to_string(rt, sp);
    if (sel == 1) return string_to_num(rt, sp);
    char msg[64];
    snprintf(msg, sizeof msg, "Unsupported Pack7 selector $%04x.", sel);
    return fail(rt, msg);
}
