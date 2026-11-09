/* Index2Color / Color2Index on the 8-bit indexed device. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define RAM 0x10000u
#define SP 0xe000u
static MacRuntime fresh(void) {
    MacRuntime r = {0}; r.cpu = mac_cpu_create(RAM); CHECK(r.cpu != NULL); r.screen_depth = 8;
    for (unsigned i = 0; i < 256; i++) r.palette[i] = 0x010101u * (i / 2);
    r.palette[7] = 0xff8040u; r.palette[200] = 0x102030u; return r;
}
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { const uint8_t *m = mac_cpu_ram(r->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | m[a + i]; return v; }
static void w(MacRuntime *r, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, a, n, v); }
static int i2c(MacRuntime *r, uint32_t idx, uint32_t out) { mac_cpu_set(r->cpu, MAC_A7, SP); w(r, SP, 4, out); w(r, SP + 4, 4, idx); return mac_colorindex_service(r, 0xaa34); }
static int c2i(MacRuntime *r, uint16_t R, uint16_t G, uint16_t B) { w(r, 0x500, 2, R); w(r, 0x502, 2, G); w(r, 0x504, 2, B); mac_cpu_set(r->cpu, MAC_A7, SP); w(r, SP, 4, 0x500); w(r, SP + 4, 4, 0x77777777); return mac_colorindex_service(r, 0xaa33); }
int main(void) {
    MacRuntime r = fresh();
    CHECK(mac_colorindex_service(&r, 0xaa35) == 0 && mac_colorindex_service(&r, 0xaa32) == 0 && mac_colorindex_service(&r, 0xa800) == 0);
    memset(mac_cpu_ram(r.cpu) + 0x600, 0xee, 8);
    CHECK(i2c(&r, 7, 0x600) == 1 && rd(&r, 0x600, 2) == 0xff * 257 && rd(&r, 0x602, 2) == 0x80 * 257 && rd(&r, 0x604, 2) == 0x40 * 257 && rd(&r, 0x606, 1) == 0xee && mac_cpu_get(r.cpu, MAC_A7) == SP + 8);
    CHECK(i2c(&r, 255, 0x600) == 1 && rd(&r, 0x600, 2) == 127 * 257 && i2c(&r, 0, 0x600) == 1 && rd(&r, 0x600, 2) == 0);
    CHECK(c2i(&r, 0xff * 257, 0x80 * 257, 0x40 * 257) == 1 && rd(&r, SP + 4, 4) == 7 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4);               /* the exact entry */
    CHECK(c2i(&r, 0x10 * 257, 0x20 * 257, 0x30 * 257) == 1 && rd(&r, SP + 4, 4) == 200);
    CHECK(c2i(&r, 0, 0, 0) == 1 && rd(&r, SP + 4, 4) == 0);                                                                              /* ties: the first entry */
    CHECK(c2i(&r, 65535, 65535, 65535) == 1 && rd(&r, SP + 4, 4) == 254);                                                                /* nearest to white: the first of the two equal 127-grays */
    mac_cpu_destroy(r.cpu);
    {   static const uint32_t badptr[] = { 0, 1, 0x3ff, RAM - 4, RAM, 0xfffffffeu };
        for (unsigned k = 0; k < sizeof badptr / sizeof *badptr; k++) { r = fresh(); CHECK(i2c(&r, 3, badptr[k]) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu); }
        for (unsigned k = 0; k < sizeof badptr / sizeof *badptr; k++) { r = fresh(); mac_cpu_set(r.cpu, MAC_A7, SP); w(&r, SP, 4, badptr[k]); CHECK(mac_colorindex_service(&r, 0xaa33) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu); }
        r = fresh(); CHECK(i2c(&r, 256, 0x600) == -1 && mac_cpu_halted(r.cpu) && rd(&r, 0x600, 1) == 0); mac_cpu_destroy(r.cpu);               /* index past the table */
        r = fresh(); CHECK(i2c(&r, 0x80000000u, 0x600) == -1); mac_cpu_destroy(r.cpu);
        r = fresh(); r.screen_depth = 4; CHECK(i2c(&r, 1, 0x600) == -1 && mac_cpu_halted(r.cpu)); mac_cpu_destroy(r.cpu);                      /* not the 8-bit device */
        r = fresh(); mac_cpu_set(r.cpu, MAC_A7, SP + 1); CHECK(mac_colorindex_service(&r, 0xaa34) == -1); mac_cpu_destroy(r.cpu);
        r = fresh(); mac_cpu_set(r.cpu, MAC_A7, RAM - 6); CHECK(mac_colorindex_service(&r, 0xaa34) == -1); mac_cpu_destroy(r.cpu);
        r = fresh(); mac_cpu_set(r.cpu, MAC_A7, RAM - 4); CHECK(mac_colorindex_service(&r, 0xaa33) == -1); mac_cpu_destroy(r.cpu);
        r = fresh(); mac_cpu_halt(r.cpu, "x"); CHECK(i2c(&r, 1, 0x600) == -1); mac_cpu_destroy(r.cpu); }
    if (!fails) puts("Index2Color/Color2Index checks passed");
    return fails ? 1 : 0;
}
