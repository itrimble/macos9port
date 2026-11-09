/* mac_guest_call: native services calling guest Pascal routines and resuming after the trap. */
#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40, TOTAL = MAP + 62, PROGRAM = 0x10000, ADD = 0x10100, BAD = 0x10200, NORES = 0x10300, TRAP = 0xa9fd };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }
/* One CODE resource (ID 1; its entry point is never run: the test supplies its own program). */
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
static void emit(MacCpu *c, uint32_t *p, uint16_t w) { CHECK(mac_cpu_write(c, *p, 2, w) == 0); *p += 2; }
static unsigned mode, calls;
static int last_rc; static uint32_t a7_before, a7_after, pc_before, pc_after;
static uint32_t results[8];
static int done(void *rtp, void *ctx, uint32_t result) {
    MacRuntime *rt = rtp; CHECK(ctx == (void *)&calls);
    results[calls++] = result;
    if (mode == 0 && calls < 3) { /* chain: add(result, 10) */
        uint8_t args[4] = { (uint8_t)(result >> 8), (uint8_t)result, 0, 10 };
        return mac_guest_call(rt, ADD, args, 4, 2, done, ctx);
    }
    mac_cpu_set(rt->cpu, MAC_D2, result);
    return 0;
}
static int hook(MacCpu *cpu, uint16_t opcode, uint32_t pc, void *user) {
    MacRuntime *rt = user;
    if (opcode != TRAP) return mac_runtime_trap(cpu, opcode, pc, user);
    uint8_t args[4] = { 0, 3, 0, 4 };
    a7_before = mac_cpu_get(cpu, MAC_A7); pc_before = mac_cpu_get(cpu, MAC_PC);
    switch (mode) {
    case 0: case 1: return mac_guest_call(rt, ADD, args, 4, 2, done, &calls);
    case 2: CHECK(mac_guest_call(rt, ADD, args, 4, 2, done, &calls) == 0);
            last_rc = mac_guest_call(rt, ADD, args, 4, 2, done, &calls); /* nested: refused */
            CHECK(last_rc == -1); return -1;
    case 3: last_rc = mac_guest_call(rt, ADD + 1, args, 4, 2, done, &calls); break;  /* odd target */
    case 4: last_rc = mac_guest_call(rt, 0x100, args, 4, 2, done, &calls); break;    /* low memory */
    case 5: last_rc = mac_guest_call(rt, ADD, args, 3, 2, done, &calls); break;      /* odd argument size */
    case 6: last_rc = mac_guest_call(rt, ADD, args, 4, 3, done, &calls); break;      /* bad result size */
    case 7: return mac_guest_call(rt, BAD, args, 4, 2, done, &calls);                /* routine leaves its arguments */
    case 8: mac_cpu_set(cpu, MAC_A7, 0x1008); a7_before = 0x1008;
            last_rc = mac_guest_call(rt, ADD, args, 4, 2, done, &calls); break;      /* no stack */
    case 9: memset(mac_cpu_ram(cpu) + mac_cpu_get(cpu, MAC_A7) - 64, 0xff, 64);      /* dirty stack below */
            return mac_guest_call(rt, NORES, args, 4, 2, done, &calls);              /* routine writes no result */
    }
    a7_after = mac_cpu_get(cpu, MAC_A7); pc_after = mac_cpu_get(cpu, MAC_PC);
    return -1;
}
/* mode 0: chain of three calls; 1: a single call; 2..8: each must halt before the program continues */
static void run(unsigned m) {
    uint8_t b[TOTAL]; MacResourceFork fork; char err[240]; MacRuntime rt;
    fixture(b); mode = m; calls = 0; last_rc = 0; memset(results, 0, sizeof results);
    if (mac_resource_open(&fork, b, TOTAL, err, sizeof err) || mac_runtime_init(&rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); CHECK(0); return; }
    MacCpu *c = rt.cpu;
    uint32_t p = ADD; /* Pascal Add(a, b): INTEGER */
    emit(c, &p, 0x302f); emit(c, &p, 6);  /* MOVE.W 6(A7),D0 */
    emit(c, &p, 0xd06f); emit(c, &p, 4);  /* ADD.W 4(A7),D0  */
    emit(c, &p, 0x3f40); emit(c, &p, 8);  /* MOVE.W D0,8(A7) */
    emit(c, &p, 0x205f); emit(c, &p, 0x588f); emit(c, &p, 0x4ed0); /* pop return, pop args, JMP (A0) */
    p = BAD; emit(c, &p, 0x4e75);           /* RTS, arguments still on the stack */
    p = NORES; emit(c, &p, 0x205f); emit(c, &p, 0x588f); emit(c, &p, 0x4ed0); /* pops, returns, writes nothing */
    p = PROGRAM;
    emit(c, &p, 0x7200);                    /* MOVEQ #0,D1 */
    emit(c, &p, TRAP);
    emit(c, &p, 0x7205);                    /* MOVEQ #5,D1: runs only after the trap completes */
    emit(c, &p, 0xa9f4);                    /* ExitToShell */
    mac_cpu_set(c, MAC_PC, PROGRAM);
    mac_cpu_set_trap(c, hook, &rt);
    uint32_t sp = mac_cpu_get(c, MAC_A7);
    mac_runtime_run(&rt, 2000);
    if (m == 0) {
        CHECK(calls == 3 && results[0] == 7 && results[1] == 17 && results[2] == 27);
        CHECK(mac_cpu_get(c, MAC_D2) == 27 && mac_cpu_get(c, MAC_D1) == 5 && rt.exited && mac_cpu_get(c, MAC_A7) == sp);
    } else if (m == 1) {
        CHECK(calls == 1 && results[0] == 7 && mac_cpu_get(c, MAC_D1) == 5 && rt.exited && mac_cpu_get(c, MAC_A7) == sp);
    } else if (m == 9) {
        CHECK(calls == 1 && results[0] == 0 && mac_cpu_get(c, MAC_D1) == 5 && rt.exited && mac_cpu_get(c, MAC_A7) == sp);
    } else {
        if (m == 3 || m == 4 || m == 5 || m == 6 || m == 8)  /* refused before touching A7 or PC */
            CHECK(last_rc == -1 && a7_after == a7_before && pc_after == pc_before && !rt.guest.active);
        CHECK(mac_cpu_halted(c) && !rt.exited && mac_cpu_get(c, MAC_D1) != 5);
    }
    mac_runtime_destroy(&rt);
}
int main(void) {
    for (unsigned m = 0; m <= 9; m++) run(m);
    if (!fails) puts("Guest call checks passed");
    return fails ? 1 : 0;
}
