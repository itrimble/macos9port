/* 24-bit machines ignore the high byte of an address; the Memory Manager traps must too. In 32-bit mode every bit counts.
 * Headless, no guest software. */
#include "mac_runtime.h"
#include "guest_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

enum { DATA = 256, MAP = 384, ZERO = DATA + 4, SEG = DATA + 32, TOTAL = MAP + 62 };
static void fill_fork(uint8_t *b) {
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
    put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    put16(b + MAP + 40, 0xffff);
    put16(b + MAP + 50, 1); put16(b + MAP + 52, 0xffff);
    b[MAP + 57] = 28;
}

static int call(MacRuntime *rt, uint16_t op, uint32_t a0, uint32_t d0) {
    mac_cpu_set(rt->cpu, MAC_A0, a0); mac_cpu_set(rt->cpu, MAC_D0, d0);
    return mac_memory_service(rt, op);
}
static int16_t memerr(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0x220, 2); }
static uint32_t ra0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_A0); }
static uint32_t rd0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_D0); }

#define FLAG 0xa5000000u
static int start(MacRuntime *rt, const char *mode) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    unsetenv("OS999_GUEST_RAM_MB"); unsetenv("OS999_GUEST_HEAP_MB"); unsetenv("OS999_GUEST_GUARD");
    setenv("OS999_GUEST_ADDRESSING", mode, 1);
    memset(bytes, 0, sizeof bytes); fill_fork(bytes);
    return mac_resource_open(&f, bytes, TOTAL, err, sizeof err) == 0 && mac_runtime_init(rt, &f, err, sizeof err) == 0;
}

static void machine24(void) {
    MacRuntime rt;
    CHECK(start(&rt, "24") && !rt.addr32);
    CHECK(call(&rt, 0xa122, 0, 64) == 1 && memerr(&rt) == 0);   /* NewHandle: the result is a clean address */
    uint32_t h = ra0(&rt), hf = h | FLAG;
    CHECK(h != 0 && (h & 0xff000000u) == 0);
    CHECK(call(&rt, 0xa025, hf, 0) == 1 && memerr(&rt) == 0 && rd0(&rt) == 64);   /* GetHandleSize through a flagged handle */
    CHECK(call(&rt, 0xa029, hf, 0) == 1 && memerr(&rt) == 0);                    /* HLock */
    CHECK(call(&rt, 0xa069, hf, 0) == 1 && (rd0(&rt) & 0x80));                   /* HGetState sees the lock */
    CHECK(call(&rt, 0xa02a, hf, 0) == 1 && memerr(&rt) == 0);                    /* HUnlock */
    CHECK(call(&rt, 0xa024, hf, 128) == 1 && memerr(&rt) == 0);                  /* SetHandleSize */
    CHECK(call(&rt, 0xa025, h, 0) == 1 && rd0(&rt) == 128);
    CHECK(call(&rt, 0xa023, hf, 0) == 1 && memerr(&rt) == 0);                    /* DisposeHandle */
    CHECK(call(&rt, 0xa025, h, 0) == 1 && memerr(&rt) == -109);                  /* gone */
    CHECK(call(&rt, 0xa11e, 0, 32) == 1 && memerr(&rt) == 0);                    /* NewPtr */
    uint32_t p = ra0(&rt), pf = p | FLAG;
    CHECK(p != 0 && (p & 0xff000000u) == 0);
    CHECK(call(&rt, 0xa021, pf, 0) == 1 && memerr(&rt) == 0 && rd0(&rt) == 32);  /* GetPtrSize */
    mac_cpu_write(rt.cpu, p, 4, 0x11223344);
    mac_cpu_set(rt.cpu, MAC_A1, (p + 16) | 0x40000000u);
    CHECK(call(&rt, 0xa02e, pf, 4) == 1 && !mac_cpu_halted(rt.cpu));            /* BlockMove, both addresses flagged */
    CHECK(mac_cpu_read(rt.cpu, p + 16, 4) == 0x11223344);
    mac_cpu_set(rt.cpu, MAC_A1, p + 20);
    CHECK(call(&rt, 0xa22e, pf, 4) == 1 && mac_cpu_read(rt.cpu, p + 20, 4) == 0x11223344);   /* BlockMoveData */
    CHECK(call(&rt, 0xa01f, pf, 0) == 1 && memerr(&rt) == 0);                    /* DisposePtr */
    CHECK(call(&rt, 0xa04b, 0x82001000u, 0) == 1 && mac_cpu_read(rt.cpu, 0x1f4, 4) == 0x82001000u);   /* GZProc is a code address: untouched */
    CHECK(call(&rt, 0xa055, 0xa0123456u, 0) == 1 && ra0(&rt) == 0x123456u);      /* StripAddress still strips */
    CHECK(call(&rt, 0xa025, 0, 0) == 1 && memerr(&rt) == -109);                  /* nil stays nil */
    CHECK(call(&rt, 0xa025, 0x04000000u, 0) == 1 && memerr(&rt) == -109);        /* bits above 24 alone are a nil address, not RAM */
    mac_runtime_destroy(&rt);
}

static void machine32(void) {
    MacRuntime rt;
    CHECK(start(&rt, "32") && rt.addr32);
    CHECK(call(&rt, 0xa122, 0, 64) == 1 && memerr(&rt) == 0);
    uint32_t h = ra0(&rt);
    CHECK(call(&rt, 0xa025, h | FLAG, 0) == 1 && memerr(&rt) == -109);          /* every bit is address: not stripped */
    CHECK(call(&rt, 0xa025, h, 0) == 1 && memerr(&rt) == 0 && rd0(&rt) == 64);
    CHECK(call(&rt, 0xa11e, 0, 32) == 1);
    uint32_t p = ra0(&rt);
    mac_cpu_set(rt.cpu, MAC_A1, p + 16);
    CHECK(call(&rt, 0xa02e, p | FLAG, 4) == -1 && mac_cpu_halted(rt.cpu));   /* BlockMove from an address outside RAM halts, as before */
    mac_runtime_destroy(&rt);
}

int main(void) {
    machine24(); machine32();
    unsetenv("OS999_GUEST_ADDRESSING");
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("legacy pointers: all checks passed");
    return 0;
}
