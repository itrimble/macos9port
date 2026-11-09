/* Guest code runs in supervisor mode, as classic Mac OS applications do: the critical-section idiom MOVE SR,-(A7) / ORI #$0200,SR
 * / MOVE (A7)+,SR must execute (Castle of Dr. Brain's heap code uses it) instead of raising a privilege violation. */
#include "mac_runtime.h"
#include "mac_resource_chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40, TOTAL = MAP + 62, CODE_AT = 0x10000 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }
static uint8_t image[TOTAL];
static void fixture(uint8_t *b) {
    memset(b, 0, TOTAL);
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 62); put32(b + 12, 62);
    put32(b + DATA, 32);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100); put32(b + ZERO + 8, 8); put32(b + ZERO + 12, 32);
    put16(b + ZERO + 16, 0); put16(b + ZERO + 18, 0x3f3c); put16(b + ZERO + 20, 1); put16(b + ZERO + 22, 0xa9f0);
    put32(b + DATA + 36, 10); put16(b + ONE, 0); put16(b + ONE + 2, 1); put16(b + ONE + 4, 0xa9f4);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 62);
    put16(b + MAP + 28, 0); put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    for (unsigned i = 0; i < 2; i++) { put16(b + MAP + 38 + i * 12, (uint16_t)i); put16(b + MAP + 40 + i * 12, 0xffff); }
    b[MAP + 57] = 36;
}
int main(void) {
    MacRuntime rt = {0}; char err[240]; MacResourceFork fork; fixture(image);
    if (mac_resource_open(&fork, image, TOTAL, err, sizeof err) || mac_runtime_init(&rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); return 1; }
    uint32_t sr = mac_cpu_get(rt.cpu, MAC_SR), sp = mac_cpu_get(rt.cpu, MAC_A7);
    CHECK(sr & 0x2000);                         /* supervisor */
    CHECK((sr & 0x0700) == 0);                  /* interrupts enabled */
    CHECK(sp < rt.a5);                         /* A7 below A5, not swapped to a stack pointer of 0 */
    CHECK(sp > 0x1000);
    uint8_t *ram = mac_cpu_ram(rt.cpu);
    static const uint8_t code[] = { 0x40, 0xe7,  0x00, 0x7c, 0x02, 0x00,  0x46, 0xdf,  0x4e, 0x71 };  /* move sr,-(a7); ori #$200,sr; move (a7)+,sr; nop */
    memcpy(ram + CODE_AT, code, sizeof code);
    mac_cpu_set(rt.cpu, MAC_PC, CODE_AT);
    CHECK(mac_cpu_run(rt.cpu, 1) == 1 && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_A7) == sp - 2);       /* the old SR is pushed */
    CHECK(mac_cpu_run(rt.cpu, 1) == 1 && !mac_cpu_halted(rt.cpu) && (mac_cpu_get(rt.cpu, MAC_SR) & 0x0700) == 0x0200);   /* mask raised */
    CHECK(mac_cpu_run(rt.cpu, 1) == 1 && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_A7) == sp && (mac_cpu_get(rt.cpu, MAC_SR) & 0x2700) == 0x2000);  /* restored */
    CHECK(mac_cpu_run(rt.cpu, 1) == 1 && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) == CODE_AT + sizeof code);
    mac_runtime_destroy(&rt);
    if (!fails) puts("Supervisor mode checks passed");
    return fails ? 1 : 0;
}
