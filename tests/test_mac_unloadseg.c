/* UnloadSeg ($A9F1): validated purgeability hint; the segment stays usable. */
#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40, TWO = DATA + 54, TOTAL = MAP + 74, SETUP = 0x10000 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }
/* Same two-segment shape as test_mac_traps: CODE2 is `MOVEQ #11,D0; RTS`. */
static void fixture(uint8_t *b) {
    memset(b, 0, TOTAL);
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 62); put32(b + 12, 74);
    put32(b + DATA, 32);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100); put32(b + ZERO + 8, 16); put32(b + ZERO + 12, 32);
    for (unsigned i = 0; i < 2; i++) {
        put16(b + ZERO + 16 + i * 8, 0); put16(b + ZERO + 18 + i * 8, 0x3f3c);
        put16(b + ZERO + 20 + i * 8, (uint16_t)(i + 1)); put16(b + ZERO + 22 + i * 8, 0xa9f0);
    }
    put32(b + DATA + 36, 10); put16(b + ONE, 0); put16(b + ONE + 2, 1);
    put16(b + ONE + 4, 0x4ead); put16(b + ONE + 6, 42); put16(b + ONE + 8, 0xa9f4);
    put32(b + DATA + 50, 8); put16(b + TWO, 8); put16(b + TWO + 2, 1);
    put16(b + TWO + 4, 0x700b); put16(b + TWO + 6, 0x4e75);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 74);
    put16(b + MAP + 28, 0); put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 2); put16(b + MAP + 36, 10);
    for (unsigned i = 0; i < 3; i++) { put16(b + MAP + 38 + i * 12, (uint16_t)i); put16(b + MAP + 40 + i * 12, 0xffff); }
    b[MAP + 57] = 36; b[MAP + 69] = 50;
}
static uint32_t emit16(MacCpu *c, uint32_t p, uint16_t w) { CHECK(mac_cpu_write(c, p, 2, w) == 0); return p + 2; }
static uint32_t emit32(MacCpu *c, uint32_t p, uint32_t v) { CHECK(mac_cpu_write(c, p, 4, v) == 0); return p + 4; }
/* push `address`; _UnloadSeg; MOVEQ #7,D0; _ExitToShell */
static int run(uint32_t (*address)(const MacRuntime *), uint32_t *d0, uint32_t *sp_before, uint32_t *sp_after, int *halted, int *exited) {
    uint8_t b[TOTAL]; MacResourceFork fork; char err[240]; MacRuntime rt;
    fixture(b);
    if (mac_resource_open(&fork, b, TOTAL, err, sizeof err) || mac_runtime_init(&rt, &fork, err, sizeof err)) { CHECK(0); return -1; }
    MacCpu *c = rt.cpu; uint32_t p = SETUP;
    p = emit16(c, p, 0x2f3c); p = emit32(c, p, address(&rt));
    p = emit16(c, p, 0xa9f1); p = emit16(c, p, 0x7007); emit16(c, p, 0xa9f4);
    mac_cpu_set(c, MAC_PC, SETUP);
    *sp_before = mac_cpu_get(c, MAC_A7);
    mac_runtime_run(&rt, 100);
    *d0 = mac_cpu_get(c, MAC_D0); *sp_after = mac_cpu_get(c, MAC_A7);
    *halted = mac_cpu_halted(c); *exited = rt.exited;
    /* CODE2 was never loaded by this call: its entry is still the lazy LoadSeg stub. */
    CHECK(mac_cpu_ram(c)[rt.jump_table + 10] == 0x3f && mac_cpu_ram(c)[rt.jump_table + 11] == 0x3c);
    mac_runtime_destroy(&rt);
    return 0;
}
static uint32_t entry0(const MacRuntime *r) { return r->jump_table + 2; }
static uint32_t entry1(const MacRuntime *r) { return r->jump_table + 10; }
static uint32_t odd(const MacRuntime *r) { return r->jump_table + 3; }
static uint32_t off_by_entry(const MacRuntime *r) { return r->jump_table + 6; }
static uint32_t past_end(const MacRuntime *r) { return r->jump_table + 18; }
static uint32_t before_table(const MacRuntime *r) { return r->jump_table - 6; }
int main(void) {
    uint32_t d0, a, z; int halted, exited;
    uint32_t (*good[])(const MacRuntime *) = { entry0, entry1 };
    for (unsigned i = 0; i < 2; i++) {
        CHECK(run(good[i], &d0, &a, &z, &halted, &exited) == 0);
        CHECK(exited && d0 == 7); /* the code after the trap ran */
        CHECK(z == a);            /* the 4-byte argument was popped */
    }
    uint32_t (*bad[])(const MacRuntime *) = { odd, off_by_entry, past_end, before_table };
    for (unsigned i = 0; i < 4; i++) {
        CHECK(run(bad[i], &d0, &a, &z, &halted, &exited) == 0);
        CHECK(halted && !exited && d0 != 7);
    }
    if (!fails) puts("UnloadSeg checks passed");
    return fails ? 1 : 0;
}
