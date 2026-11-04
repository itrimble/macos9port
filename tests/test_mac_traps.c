#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40,
       TWO = DATA + 54, TOTAL = MAP + 74,
       SETUP = 0x10000, WRAPPER = 0x10100, HELPER = 0x10200, MARK = 0x12000 };
static int fails;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; \
} } while (0)

static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }

/* Three independently authored CODE resources. CODE1 calls CODE2 through the
 * jump table, then exits. CODE2 sets D0=11 and returns. No application bytes. */
static void fixture(uint8_t *b) {
    memset(b, 0, TOTAL);
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 62); put32(b + 12, 74);
    put32(b + DATA, 32);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100);
    put32(b + ZERO + 8, 16); put32(b + ZERO + 12, 32);
    for (unsigned i = 0; i < 2; i++) {
        put16(b + ZERO + 16 + i * 8, 0);
        put16(b + ZERO + 18 + i * 8, 0x3f3c);
        put16(b + ZERO + 20 + i * 8, (uint16_t)(i + 1));
        put16(b + ZERO + 22 + i * 8, 0xa9f0);
    }
    put32(b + DATA + 36, 10);
    put16(b + ONE, 0); put16(b + ONE + 2, 1);
    put16(b + ONE + 4, 0x4ead); put16(b + ONE + 6, 42); /* JSR 42(A5) */
    put16(b + ONE + 8, 0xa9f4);
    put32(b + DATA + 50, 8);
    put16(b + TWO, 8); put16(b + TWO + 2, 1);
    put16(b + TWO + 4, 0x700b); put16(b + TWO + 6, 0x4e75);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 74);
    put16(b + MAP + 28, 0); put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 2); put16(b + MAP + 36, 10);
    for (unsigned i = 0; i < 3; i++) {
        put16(b + MAP + 38 + i * 12, (uint16_t)i);
        put16(b + MAP + 40 + i * 12, 0xffff);
    }
    b[MAP + 57] = 36; b[MAP + 69] = 50;
}

/* Inspection remains available after the CPU intentionally halts at exit. */
static uint32_t peek(MacCpu *cpu, uint32_t address, unsigned width) {
    if (address > mac_cpu_ram_size(cpu) || width > mac_cpu_ram_size(cpu) - address) {
        CHECK(0);
        return 0;
    }
    const uint8_t *p = mac_cpu_ram(cpu) + address;
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++) value = (value << 8) | p[i];
    return value;
}

static int load(MacRuntime *rt, uint8_t *b) {
    MacResourceFork fork;
    char err[240];
    fixture(b);
    CHECK(mac_resource_open(&fork, b, TOTAL, err, sizeof err) == 0);
    int result = mac_runtime_init(rt, &fork, err, sizeof err);
    if (result) fprintf(stderr, "init: %s\n", err);
    CHECK(result == 0);
    return result;
}

static uint32_t emit16(MacCpu *cpu, uint32_t p, uint16_t word) {
    CHECK(mac_cpu_write(cpu, p, 2, word) == 0);
    return p + 2;
}
static uint32_t emit32(MacCpu *cpu, uint32_t p, uint32_t value) {
    CHECK(mac_cpu_write(cpu, p, 4, value) == 0);
    return p + 4;
}

static void setup_patch(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    uint32_t p = SETUP;
    p = emit16(cpu, p, 0x303c); p = emit16(cpu, p, 0xa9f0); /* trap ID -> D0 */
    p = emit16(cpu, p, 0xa146); /* GetTrapAddress -> A0 */
    p = emit16(cpu, p, 0x23c8); p = emit32(cpu, p, WRAPPER + 8); /* save predecessor */
    p = emit16(cpu, p, 0x41f9); p = emit32(cpu, p, WRAPPER); /* LEA wrapper,A0 */
    p = emit16(cpu, p, 0x303c); p = emit16(cpu, p, 0xa9f0);
    p = emit16(cpu, p, 0xa047); /* SetTrapAddress */
    p = emit16(cpu, p, 0x4ef9); emit32(cpu, p, rt->jump_table + 2);
    p = WRAPPER;
    p = emit16(cpu, p, 0x4eb9); p = emit32(cpu, p, HELPER); /* JSR helper */
    p = emit16(cpu, p, 0x4ef9); emit32(cpu, p, 0); /* JMP predecessor */
    p = HELPER;
    p = emit16(cpu, p, 0x48e7); p = emit16(cpu, p, 0xfff8); /* save 13 registers */
    p = emit16(cpu, p, 0x302f); p = emit16(cpu, p, 60); /* segment ID */
    p = emit16(cpu, p, 0x33c0); p = emit32(cpu, p, MARK);
    p = emit16(cpu, p, 0x222f); p = emit16(cpu, p, 56); /* original continuation */
    p = emit16(cpu, p, 0x23c1); p = emit32(cpu, p, MARK + 12);
    p = emit16(cpu, p, 0x42a7); /* reserve Handle result */
    p = emit16(cpu, p, 0x2f3c); p = emit32(cpu, p, MAC_CODE_TYPE);
    p = emit16(cpu, p, 0x3f00); /* segment ID parameter */
    p = emit16(cpu, p, 0xa9a0); /* nested GetResource */
    p = emit16(cpu, p, 0x205f); /* MOVEA.L (SP)+,A0 */
    p = emit16(cpu, p, 0x23c8); p = emit32(cpu, p, MARK + 4);
    p = emit16(cpu, p, 0x2050); /* master pointer */
    p = emit16(cpu, p, 0x23c8); p = emit32(cpu, p, MARK + 8);
    p = emit16(cpu, p, 0x317c); p = emit16(cpu, p, 0x702a); p = emit16(cpu, p, 4);
    /* Mutation above changes CODE2's MOVEQ to 42. Execution must see it. */
    p = emit16(cpu, p, 0x4cdf); p = emit16(cpu, p, 0x1fff);
    emit16(cpu, p, 0x4e75);
    mac_cpu_set(cpu, MAC_PC, SETUP);
}

static void lazy_and_canonical(int preload) {
    uint8_t b[TOTAL]; MacRuntime rt;
    if (load(&rt, b)) return;
    uint32_t sp = mac_cpu_get(rt.cpu, MAC_A7);
    CHECK(rt.handle_count == 1);
    CHECK(peek(rt.cpu, rt.jump_table + 2, 2) == 0x4ef9);
    CHECK(peek(rt.cpu, rt.jump_table + 10, 2) == 0x3f3c);
    CHECK(peek(rt.cpu, rt.jump_table + 14, 2) == 0xa9f0);
    uint32_t h1 = mac_runtime_resource(&rt, MAC_CODE_TYPE, 1);
    CHECK(peek(rt.cpu, rt.jump_table + 4, 4) == peek(rt.cpu, h1, 4) + 4);
    uint32_t h2 = 0;
    if (preload) {
        h2 = mac_runtime_resource(&rt, MAC_CODE_TYPE, 2);
        CHECK(h2 != 0);
        CHECK(peek(rt.cpu, rt.jump_table + 10, 2) == 0x3f3c);
        mac_cpu_write(rt.cpu, peek(rt.cpu, h2, 4) + 4, 2, 0x702a);
    }
    CHECK(mac_runtime_run(&rt, 100) < 100);
    CHECK(rt.exited && mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == (preload ? 42u : 11u));
    CHECK(rt.traps == 2);
    CHECK(peek(rt.cpu, rt.jump_table + 10, 2) == 0x4ef9);
    uint32_t handle = mac_runtime_resource(&rt, MAC_CODE_TYPE, 2);
    CHECK(!preload || handle == h2);
    CHECK(peek(rt.cpu, rt.jump_table + 12, 4) == peek(rt.cpu, handle, 4) + 4);
    mac_runtime_destroy(&rt);
}

static void guest_patch(void) {
    uint8_t b[TOTAL]; MacRuntime rt;
    if (load(&rt, b)) return;
    uint32_t sp = mac_cpu_get(rt.cpu, MAC_A7);
    uint32_t predecessor = rt.trap_addresses[0x9f0];
    setup_patch(&rt);
    CHECK(mac_runtime_run(&rt, 500) < 500);
    if (!rt.exited) fprintf(stderr, "patch halted: %s PC=%08x\n",
                            mac_cpu_error(rt.cpu), mac_cpu_get(rt.cpu, MAC_PC));
    CHECK(rt.exited);
    CHECK(rt.trap_addresses[0x9f0] == WRAPPER);
    CHECK(peek(rt.cpu, WRAPPER + 8, 4) == predecessor);
    CHECK(peek(rt.cpu, MARK, 2) == 2);
    CHECK(peek(rt.cpu, MARK + 12, 4) == rt.jump_table + 16);
    uint32_t h = mac_runtime_resource(&rt, MAC_CODE_TYPE, 2);
    CHECK(peek(rt.cpu, MARK + 4, 4) == h);
    CHECK(peek(rt.cpu, MARK + 8, 4) == peek(rt.cpu, h, 4));
    CHECK(peek(rt.cpu, rt.jump_table + 12, 4) == peek(rt.cpu, h, 4) + 4);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 42);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp);
    CHECK(rt.traps == 6); /* get/set, guest LoadSeg, GetResource, native LoadSeg, exit */
    mac_runtime_destroy(&rt);
}

static void hfs_predecessor(uint16_t opcode) {
    uint8_t b[TOTAL]; MacRuntime rt;
    if (load(&rt, b)) return;
    unsigned key = opcode & 0xff;
    uint32_t sp = mac_cpu_get(rt.cpu, MAC_A7), p = SETUP;
    p = emit16(rt.cpu, p, 0x303c); p = emit16(rt.cpu, p, opcode);
    p = emit16(rt.cpu, p, 0xa346); /* GetOSTrapAddress */
    p = emit16(rt.cpu, p, 0x23c8); p = emit32(rt.cpu, p, MARK);
    p = emit16(rt.cpu, p, 0x2248); /* MOVEA.L A0,A1: saved predecessor */
    p = emit16(rt.cpu, p, 0x41f9); p = emit32(rt.cpu, p, 0x14000);
    p = emit16(rt.cpu, p, 0x7007); /* GetWDInfo selector */
    p = emit16(rt.cpu, p, 0x4e91); /* JSR (A1) */
    emit16(rt.cpu, p, 0xa9f4);
    mac_cpu_set(rt.cpu, MAC_PC, SETUP);
    CHECK(mac_runtime_run(&rt, 30) < 30 && rt.exited);
    CHECK(peek(rt.cpu, MARK, 4) == rt.trap_addresses[key]);
    CHECK(peek(rt.cpu, rt.trap_addresses[key], 2) == opcode);
    /* No volume mounted: the real service must return nsvErr, not halt at an
     * unhandled A060 or mistake the return address for the parameter block. */
    CHECK((int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -35);
    CHECK(peek(rt.cpu, 0x14010, 2) == (uint16_t)-35);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp);
    mac_runtime_destroy(&rt);
}

int main(void) {
    hfs_predecessor(0xa260);
    hfs_predecessor(0xa207);
    lazy_and_canonical(0);
    lazy_and_canonical(1);
    guest_patch();
    printf("Trap and lazy-segment checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
