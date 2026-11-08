/* The application's resource file as code that reads it directly sees it: CurApRefNum, CurMap, TopMapHndl, PBRead, PBGetEOF. */
#include "mac_runtime.h"
#include "mac_resource_chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, ONE = DATA + 40, TOTAL = MAP + 62, PB = 0x5000, BUF = 0x6000, MAP_HANDLE = 0xe60000 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }
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
    put32(b + MAP + 16, 0xdeadbeef);  /* junk where the in-memory map keeps its next-map handle: must be cleared in the copy */
    b[MAP + 61] = 0x77;               /* the last byte of the map is not zero: a copy one byte short would show */
    for (unsigned i = 0; i < 16; i++) b[DATA + 4 + 100 + i] = (uint8_t)(0xa0 + i); /* recognisable bytes inside the data area */
}
static uint8_t image[TOTAL];
static uint32_t raw(const MacRuntime *rt, uint32_t p, unsigned n) { const uint8_t *r = mac_cpu_ram(rt->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | r[p + i]; return v; }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { CHECK(mac_cpu_write(rt->cpu, p, n, v) == 0); }
static int init(MacRuntime *rt) {
    char err[240]; MacResourceFork fork; fixture(image);
    if (mac_resource_open(&fork, image, TOTAL, err, sizeof err) || mac_runtime_init(rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); CHECK(0); return -1; }
    return 0;
}
/* A PBRead (or PBGetEOF) parameter block for the application fork. Returns the service result. */
static int call(MacRuntime *rt, uint16_t op, int ref, uint32_t buffer, uint32_t count, unsigned mode, int32_t offset) {
    memset(mac_cpu_ram(rt->cpu) + PB, 0xee, 64);
    wr(rt, PB + 16, 2, 0); wr(rt, PB + 24, 2, (uint16_t)ref); wr(rt, PB + 32, 4, buffer); wr(rt, PB + 36, 4, count);
    wr(rt, PB + 44, 2, mode); wr(rt, PB + 46, 4, (uint32_t)offset); mac_cpu_set(rt->cpu, MAC_A0, PB);
    return mac_appfork_service(rt, op);
}
static int result(const MacRuntime *rt) { return (int16_t)raw(rt, PB + 16, 2); }

int main(void) {
    MacRuntime rt = {0};
    if (init(&rt)) return 1;
    const uint8_t *fork = rt.fork.bytes; uint32_t size = (uint32_t)rt.fork.size;
    /* Published low memory. */
    CHECK(raw(&rt, 0x900, 2) == MAC_APP_RES_REF && raw(&rt, 0xa5a, 2) == MAC_APP_RES_REF);
    uint32_t handle = raw(&rt, 0xa50, 4), map = raw(&rt, handle, 4);
    CHECK(handle == MAP_HANDLE && map > handle && map + rt.fork.map_size <= 0x1000000);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + map, fork + rt.fork.map_offset, 16));              /* header copy first, as in the file */
    CHECK(raw(&rt, map + 16, 4) == 0 && raw(&rt, map + 20, 2) == MAC_APP_RES_REF);          /* next map none; file reference filled in */
    CHECK(mac_cpu_ram(rt.cpu)[map + rt.fork.map_size - 1] == 0x77);                         /* the whole map was copied */
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + map + 22, fork + rt.fork.map_offset + 22, rt.fork.map_size - 22)); /* the rest is the file's map */
    CHECK(raw(&rt, map + 24, 2) == 28);                                                    /* type list offset */
    CHECK(map < 0xe00000 || map >= 0xe4b000);                                              /* not on the screen */
    /* Reads: from the start, at the mark, from the mark, from the end. */
    CHECK(call(&rt, 0xa002, 1, BUF, 16, 1, 0) == 1 && result(&rt) == 0 && raw(&rt, PB + 40, 4) == 16 && raw(&rt, PB + 46, 4) == 16);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + BUF, fork, 16));
    CHECK(call(&rt, 0xa002, 1, BUF, 8, 0, 0) == 1 && result(&rt) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + BUF, fork + 16, 8) && raw(&rt, PB + 46, 4) == 24);
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 3, 4) == 1 && result(&rt) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + BUF, fork + 28, 4) && raw(&rt, PB + 46, 4) == 32);
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 2, -4) == 1 && result(&rt) == 0 && !memcmp(mac_cpu_ram(rt.cpu) + BUF, fork + size - 4, 4) && raw(&rt, PB + 46, 4) == size);
    CHECK(call(&rt, 0xa002, 1, BUF, 16, 1, 104) == 1 && !memcmp(mac_cpu_ram(rt.cpu) + BUF, fork + 104, 16)); /* an offset in the data area */
    /* Short read at the end: the bytes that exist, then eofErr; at the end: nothing, eofErr. */
    memset(mac_cpu_ram(rt.cpu) + BUF, 0x55, 32);
    CHECK(call(&rt, 0xa002, 1, BUF, 10, 1, (int32_t)size - 3) == 1 && result(&rt) == -39 && raw(&rt, PB + 40, 4) == 3);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + BUF, fork + size - 3, 3) && raw(&rt, BUF + 3, 1) == 0x55);
    CHECK(call(&rt, 0xa002, 1, BUF, 10, 1, (int32_t)size) == 1 && result(&rt) == -39 && raw(&rt, PB + 40, 4) == 0);
    CHECK(call(&rt, 0xa002, 1, BUF, 10, 1, (int32_t)size + 100) == 1 && result(&rt) == -39 && raw(&rt, PB + 40, 4) == 0);
    CHECK(call(&rt, 0xa002, 1, BUF, 0, 1, 5) == 1 && result(&rt) == 0 && raw(&rt, PB + 40, 4) == 0);   /* zero-length read succeeds */
    CHECK(call(&rt, 0xa002, 1, BUF, 0, 1, (int32_t)size) == 1 && result(&rt) == 0);                       /* ... even at the end */
    /* Errors and the cases that are not ours. */
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 1, -1) == 1 && result(&rt) == -40);                              /* posErr */
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 2, -((int32_t)size + 1)) == 1 && result(&rt) == -40);
    CHECK(call(&rt, 0xa402, 1, BUF, 4, 1, 0) == 1 && result(&rt) == -50);                               /* asynchronous */
    CHECK(call(&rt, 0xa002, 2, BUF, 4, 1, 0) == 0 && call(&rt, 0xa002, 64, BUF, 4, 1, 0) == 0 && call(&rt, 0xa002, 128, BUF, 4, 1, 0) == 0); /* other references */
    CHECK(call(&rt, 0xa003, 1, BUF, 4, 1, 0) == 0 && call(&rt, 0xa001, 1, BUF, 4, 1, 0) == 0);          /* writes and closes are not claimed */
    /* The condition codes follow the result like the File Manager's. */
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 1, 0) == 1 && (mac_cpu_get(rt.cpu, MAC_SR) & 15) == 4 && mac_cpu_get(rt.cpu, MAC_D0) == 0);
    CHECK(call(&rt, 0xa002, 1, BUF, 4, 1, -1) == 1 && (mac_cpu_get(rt.cpu, MAC_SR) & 15) == 8 && (int32_t)mac_cpu_get(rt.cpu, MAC_D0) == -40);
    /* PBGetEOF. */
    CHECK(call(&rt, 0xa011, 1, 0, 0, 0, 0) == 1 && result(&rt) == 0 && raw(&rt, PB + 28, 4) == size);
    CHECK(call(&rt, 0xa011, 2, 0, 0, 0, 0) == 0);
    /* A buffer that does not fit guest RAM halts, writing nothing. */
    CHECK(call(&rt, 0xa002, 1, 0xfffff0, 64, 1, 0) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
    if (!fails) puts("Application resource fork checks passed");
    return fails ? 1 : 0;
}
