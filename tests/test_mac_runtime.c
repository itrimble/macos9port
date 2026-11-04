#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

enum { DATA = 256, MAP = 384, ZERO = DATA + 4, SEG = DATA + 32, TOTAL = MAP + 62 };

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

/* Entire fixture is authored here: two CODE resources, a near-model jump
 * entry, and MOVEQ #7,D0; ExitToShell; NOP; NOP. No guest assets or Apple code.
 */
static void fixture(uint8_t *b) {
    memset(b, 0, TOTAL);
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 44); put32(b + 12, 62);
    put32(b + DATA, 24);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100);
    put32(b + ZERO + 8, 8); put32(b + ZERO + 12, 32);
    put16(b + ZERO + 16, 0); put16(b + ZERO + 18, 0x3f3c);
    put16(b + ZERO + 20, 1); put16(b + ZERO + 22, 0xa9f0);
    put32(b + DATA + 28, 12);
    put16(b + SEG, 0); put16(b + SEG + 2, 1);
    put16(b + SEG + 4, 0x7007); put16(b + SEG + 6, 0xa9f4);
    put16(b + SEG + 8, 0x4e71); put16(b + SEG + 10, 0x4e71);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 62);
    put16(b + MAP + 28, 0);
    put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    put16(b + MAP + 38, 0); put16(b + MAP + 40, 0xffff);
    put16(b + MAP + 50, 1); put16(b + MAP + 52, 0xffff);
    b[MAP + 57] = 28;
}

static int load(MacRuntime *rt, uint8_t *b, char *error, size_t n) {
    MacResourceFork f;
    int rc = mac_resource_open(&f, b, TOTAL, error, n);
    CHECK(rc == 0);
    return rc ? rc : mac_runtime_init(rt, &f, error, n);
}

static void reject(uint8_t *b) {
    MacRuntime rt;
    char error[240];
    CHECK(load(&rt, b, error, sizeof error) == -1);
    CHECK(rt.cpu == NULL);
    CHECK(error[0] != 0);
    mac_runtime_destroy(&rt);
}

int main(void) {
    uint8_t b[TOTAL];
    char error[240];
    MacRuntime rt;
    fixture(b);
    CHECK(load(&rt, b, error, sizeof error) == 0);
    if (!rt.cpu) return 1;
    CHECK(rt.system_volume != NULL);
    CHECK(mac_cpu_read(rt.cpu, 0xa58, 2) == 64);
    CHECK(mac_cpu_get(rt.cpu, MAC_PC) == rt.jump_table + 2);
    CHECK(mac_cpu_get(rt.cpu, MAC_A5) == rt.a5);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == rt.a5 - 0x100);
    CHECK(mac_cpu_read(rt.cpu, 0x908, 4) == rt.a5 - 0x100);
    CHECK(mac_cpu_read(rt.cpu, rt.jump_table + 2, 2) == 0x4ef9);
    uint32_t code = mac_cpu_read(rt.cpu, mac_runtime_resource(&rt, MAC_CODE_TYPE, 1), 4);
    CHECK(mac_cpu_read(rt.cpu, rt.jump_table + 4, 4) == code + 4);
    int pinned = 0;
    for (size_t i = 0; i < rt.handle_count; i++)
        if (rt.handles[i].pointer == code) pinned = rt.handles[i].loaded_code;
    CHECK(pinned == 1);
    CHECK(mac_runtime_run(&rt, 0) == 0);
    CHECK(rt.instructions == 0);
    CHECK(mac_runtime_run(&rt, 100) == 3);
    CHECK(rt.exited == 1 && rt.traps == 1 && rt.last_trap == 0xa9f4);
    CHECK(rt.last_trap_pc == code + 6);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 7);
    CHECK(mac_cpu_halted(rt.cpu));
    CHECK(mac_runtime_run(&rt, 100) == 0);
    mac_runtime_destroy(&rt);
    CHECK(rt.system_volume == NULL);
    mac_runtime_destroy(&rt); /* Destruction remains idempotent. */

    fixture(b);
    put16(b + ZERO + 16, 2);
    CHECK(load(&rt, b, error, sizeof error) == 0);
    if (!rt.cpu) return 1;
    code = mac_cpu_read(rt.cpu, mac_runtime_resource(&rt, MAC_CODE_TYPE, 1), 4);
    CHECK(mac_cpu_read(rt.cpu, rt.jump_table + 4, 4) == code + 6);
    CHECK(mac_runtime_run(&rt, 100) == 2);
    CHECK(rt.exited == 1 && mac_cpu_get(rt.cpu, MAC_D0) == 0);
    mac_runtime_destroy(&rt);

    fixture(b);
    put16(b + SEG + 4, 0x60fe); /* Bounded infinite branch, not an exit. */
    CHECK(load(&rt, b, error, sizeof error) == 0);
    if (!rt.cpu) return 1;
    code = mac_cpu_read(rt.cpu, mac_runtime_resource(&rt, MAC_CODE_TYPE, 1), 4);
    CHECK(mac_runtime_run(&rt, 100) == 100);
    CHECK(rt.instructions == 100 && !rt.exited && rt.traps == 0);
    CHECK(!mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu, MAC_PC) == code + 4);
    CHECK(mac_runtime_run(&rt, 11) == 11 && rt.instructions == 111);
    mac_runtime_destroy(&rt);

    fixture(b); put32(b + DATA, 23); reject(b); /* Truncated CODE0. */
    fixture(b); put32(b + ZERO + 8, 7); reject(b); /* Non-entry length. */
    fixture(b); put32(b + ZERO + 8, 16); reject(b); /* Beyond payload. */
    fixture(b); put32(b + ZERO + 12, 0x101); reject(b); /* Beyond A5 area. */
    fixture(b); put32(b + ZERO + 12, 0xff); reject(b); /* Range overruns A5 area. */
    fixture(b); put32(b + ZERO + 12, 33); reject(b); /* Odd executable address. */
    fixture(b); put32(b + ZERO, 0xffffffff); reject(b); /* Overflow-sized globals. */
    fixture(b); put16(b + ZERO + 18, 0x4e71); reject(b); /* Unknown entry encoding. */
    fixture(b); put16(b + ZERO + 20, 2); reject(b); /* Missing segment. */
    fixture(b); put16(b + ZERO + 20, 0); reject(b); /* CODE0 is not a segment. */
    fixture(b); put16(b + ZERO + 16, 8); reject(b); /* Entry beyond CODE body. */
    fixture(b); put16(b + ZERO + 16, 1); reject(b); /* Odd entry offset. */
    fixture(b); put32(b + DATA + 28, 3); reject(b); /* Missing segment header. */
    fixture(b); put32(b + DATA + 28, 5); reject(b); /* One byte cannot be an opcode. */
    printf("Runtime checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
