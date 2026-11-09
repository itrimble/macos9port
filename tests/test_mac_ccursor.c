#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

/* GetCCursor / SetCCursor / AllocCursor / DisposeCCursor against synthetic 'crsr' resources. */
enum { SP = 0xf000, CRSR = 0x63727372, MAXRES = 8, GOOD = 444 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }

typedef struct { uint8_t bytes[GOOD]; size_t size; } Res;
static void good(Res *r) {
    memset(r, 0, sizeof *r); r->size = GOOD;
    uint8_t *p = r->bytes;
    put16(p, 0x8001); put32(p + 2, 96); put32(p + 6, 188);
    for (unsigned i = 0; i < 32; i++) { p[20 + i] = (uint8_t)(0xa0 + i); p[52 + i] = (uint8_t)(0x10 + i); }
    put16(p + 84, 3); put16(p + 86, 5); /* hot spot v=3 h=5 */
    uint8_t *pm = p + 96;
    put16(pm + 4, 0x8000 | 16); put16(pm + 10, 16); put16(pm + 12, 16); put16(pm + 32, 8);
    put32(pm + 42, 148);
    put32(p + 148, 1234); put16(p + 154, 3); /* 4 entries */
    for (unsigned i = 0; i < 4; i++) put16(p + 156 + i * 8 + 2, (uint16_t)(0x1111 * i));
    for (unsigned i = 0; i < 256; i++) p[188 + i] = (uint8_t)i;
}

/* Resource fork with up to MAXRES 'crsr' resources, ids 128+i. */
static uint8_t fork[8192];
static size_t build(const Res *res, unsigned n) {
    memset(fork, 0, sizeof fork);
    size_t data = 256, off = data;
    uint32_t offs[MAXRES];
    for (unsigned i = 0; i < n; i++) {
        offs[i] = (uint32_t)(off - data);
        put32(fork + off, (uint32_t)res[i].size); memcpy(fork + off + 4, res[i].bytes, res[i].size);
        off += 4 + res[i].size;
    }
    size_t map = off, types = map + 28, refs = types + 10, end = refs + n * 12;
    put32(fork, (uint32_t)data); put32(fork + 4, (uint32_t)map);
    put32(fork + 8, (uint32_t)(map - data)); put32(fork + 12, (uint32_t)(end - map));
    put16(fork + map + 24, 28); put16(fork + map + 26, (uint16_t)(end - map));
    put16(fork + types, 0); put32(fork + types + 2, CRSR);
    put16(fork + types + 6, (uint16_t)(n - 1)); put16(fork + types + 8, 10);
    for (unsigned i = 0; i < n; i++) { put16(fork + refs + i * 12, (uint16_t)(128 + i)); put16(fork + refs + i * 12 + 2, 0xffff); put32(fork + refs + i * 12 + 4, offs[i]); }
    return end;
}
static int open_rt(MacRuntime *rt, const Res *res, unsigned n, uint32_t heap_limit) {
    memset(rt, 0, sizeof *rt);
    size_t size = build(res, n);
    if (mac_resource_open(&rt->fork, fork, size, NULL, 0)) return -1;
    rt->cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    if (!rt->cpu) return -1;
    rt->heap_next = 0x100000; rt->heap_limit = heap_limit;
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    return 0;
}
static uint32_t get(MacRuntime *rt, int16_t id) {
    mac_cpu_set(rt->cpu, MAC_A7, SP);
    mac_cpu_write(rt->cpu, SP, 2, (uint16_t)id); mac_cpu_write(rt->cpu, SP + 2, 4, 0xdeadbeef);
    CHECK(mac_ccursor_service(rt, 0xaa1b) == 1);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == SP + 2);
    return mac_cpu_read(rt->cpu, SP + 2, 4);
}
static int err(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0xa60, 2); }
static int call(MacRuntime *rt, uint16_t op, uint32_t h) {
    mac_cpu_set(rt->cpu, MAC_A7, SP); mac_cpu_write(rt->cpu, SP, 4, h);
    return mac_ccursor_service(rt, op);
}
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }

static void happy(void) {
    Res r; good(&r); MacRuntime rt;
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    uint32_t h = get(&rt, 128);
    CHECK(h && err(&rt) == 0);
    uint32_t rec = rd(&rt, h, 4);
    CHECK(rd(&rt, rec, 2) == 0x8001 && rd(&rt, rec + 92, 4) == 128);
    uint32_t pmh = rd(&rt, rec + 2, 4), pm = rd(&rt, pmh, 4);
    CHECK(pmh && pm && rd(&rt, pm, 4) == 0 && rd(&rt, pm + 4, 2) == (0x8000u | 16) && rd(&rt, pm + 32, 2) == 8);
    uint32_t cth = rd(&rt, pm + 42, 4), ct = rd(&rt, cth, 4);
    CHECK(ct && rd(&rt, ct, 4) == 1234 && rd(&rt, ct + 6, 2) == 3 && rd(&rt, ct + 8 + 3 * 8 + 2, 2) == 0x3333);
    uint32_t dh = rd(&rt, rec + 6, 4), d = rd(&rt, dh, 4);
    CHECK(d && rd(&rt, d, 1) == 0 && rd(&rt, d + 255, 1) == 255);
    CHECK(rd(&rt, rec + 20, 1) == 0xa0 && rd(&rt, rec + 52 + 31, 1) == 0x2f);
    /* SetCCursor installs the 1-bit data, mask and hot spot. */
    CHECK(call(&rt, 0xaa1c, h) == 1 && rt.cursor_valid);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 4);
    CHECK(rt.cursor[0] == 0xa0 && rt.cursor[31] == 0xbf && rt.cursor[32] == 0x10 && rt.cursor[63] == 0x2f);
    CHECK(rt.cursor[65] == 3 && rt.cursor[67] == 5);
    /* Second call returns a distinct handle; AllocCursor pops nothing. */
    uint32_t h2 = get(&rt, 128);
    CHECK(h2 && h2 != h);
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    CHECK(mac_ccursor_service(&rt, 0xaa1d) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    /* Dispose frees the slots; the installed cursor survives. */
    unsigned used = 0;
    for (size_t i = 0; i < rt.handle_count; i++) used += rt.handles[i].handle != 0;
    CHECK(used == 8);
    CHECK(call(&rt, 0xaa26, h) == 1);
    used = 0;
    for (size_t i = 0; i < rt.handle_count; i++) used += rt.handles[i].handle != 0;
    CHECK(used == 4 && rt.cursor[0] == 0xa0);
    CHECK(call(&rt, 0xaa26, 0) == 1); /* nil is a no-op */
    CHECK(call(&rt, 0xaa26, h2) == 1);
    mac_runtime_destroy(&rt);
}

static void bad_resources(void) {
    Res r[MAXRES]; MacRuntime rt;
    for (unsigned i = 0; i < MAXRES; i++) good(&r[i]);
    r[0].size = 95;                              /* truncated header */
    put16(r[1].bytes, 0x1234);                   /* bad crsrType */
    put32(r[2].bytes + 2, GOOD);                 /* pixmap offset past the end */
    put16(r[3].bytes + 96 + 32, 3);              /* depth that is not 1, 2, 4 or 8 */
    put32(r[4].bytes + 6, GOOD - 100);           /* pixel data runs past the end */
    put32(r[5].bytes + 96 + 42, GOOD - 4);       /* color table offset too close to the end */
    put16(r[6].bytes + 154, 255);                /* 256 entries do not fit in the resource */
    put16(r[7].bytes + 96 + 4, 4);               /* rowBytes too small for a 16-pixel row */
    CHECK(open_rt(&rt, r, MAXRES, 0x200000) == 0);
    for (unsigned i = 0; i < MAXRES; i++) {
        CHECK(get(&rt, (int16_t)(128 + i)) == 0);
        CHECK(err(&rt) == -199);
    }
    CHECK(get(&rt, 999) == 0 && err(&rt) == -192);
    CHECK(get(&rt, 128 + 8) == 0 && err(&rt) == -192);
    CHECK(rt.handle_count == 0);
    CHECK(!mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static void exhaustion(void) {
    Res r; good(&r); MacRuntime rt;
    CHECK(open_rt(&rt, &r, 1, 0x100100) == 0); /* 256 bytes of bump space: not enough */
    CHECK(get(&rt, 128) == 0 && err(&rt) == -108 && rt.handle_count == 0);
    mac_runtime_destroy(&rt);
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    for (size_t i = 0; i < MAC_RUNTIME_HANDLES - 3; i++) rt.handles[i].pointer = 1; /* only 3 slots left */
    rt.handle_count = MAC_RUNTIME_HANDLES;
    CHECK(get(&rt, 128) == 0 && err(&rt) == -108);
    mac_runtime_destroy(&rt);
}

static void invalid_use(void) {
    Res r; good(&r); MacRuntime rt;
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    uint32_t h = get(&rt, 128);
    CHECK(h);
    CHECK(call(&rt, 0xaa1c, 0) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    h = get(&rt, 128);
    CHECK(call(&rt, 0xaa1c, h + 4) == -1 && mac_cpu_halted(rt.cpu)); /* a pixmap handle is not a CCrsrHandle */
    mac_runtime_destroy(&rt);
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    h = get(&rt, 128);
    CHECK(call(&rt, 0xaa26, h) == 1);
    CHECK(call(&rt, 0xaa26, h) == -1 && mac_cpu_halted(rt.cpu)); /* double dispose */
    mac_runtime_destroy(&rt);
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    mac_cpu_set(rt.cpu, MAC_A7, 0x3fffffff);
    CHECK(mac_ccursor_service(&rt, 0xaa1b) == -1 && mac_cpu_halted(rt.cpu)); /* stack outside RAM */
    mac_runtime_destroy(&rt);
    CHECK(open_rt(&rt, &r, 1, 0x200000) == 0);
    CHECK(mac_ccursor_service(&rt, 0xaa1e) == 0); /* GetCIcon is not ours */
    mac_runtime_destroy(&rt);
}

int main(void) {
    happy(); bad_resources(); exhaustion(); invalid_use();
    if (fails) fprintf(stderr, "%d failure(s)\n", fails);
    else puts("test_mac_ccursor: ok");
    return fails != 0;
}
