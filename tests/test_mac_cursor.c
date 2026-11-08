#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failures++; } } while (0)
static void setup(MacRuntime *rt) {
    memset(rt, 0, sizeof(*rt));
    rt->cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    rt->heap_next = 0x100000; rt->heap_limit = 0x200000;
    mac_cpu_set(rt->cpu, MAC_A7, 0x8000);
}
static uint32_t get(MacRuntime *rt, int16_t id) {
    mac_cpu_set(rt->cpu, MAC_A7, 0x8000);
    mac_cpu_write(rt->cpu, 0x8000, 2, (uint16_t)id);
    CHECK(mac_cursor_service(rt, 0xa9b9) == 1);
    CHECK(mac_cpu_get(rt->cpu, MAC_A7) == 0x8002);
    return mac_cpu_read(rt->cpu, 0x8002, 4);
}
static void cursor_state(void) {
    MacRuntime rt; setup(&rt);
    uint8_t expected[68];
    for (unsigned i = 0; i < 68; i++) expected[i] = (uint8_t)(i * 3);
    expected[64] = 0; expected[65] = 5; expected[66] = 0; expected[67] = 9;
    memcpy(mac_cpu_ram(rt.cpu) + 0x4000, expected, 68);
    mac_cpu_write(rt.cpu, 0x8000, 4, 0x4000);
    CHECK(mac_cursor_service(&rt, 0xa851) == 1);
    CHECK(rt.cursor_valid && memcmp(rt.cursor, expected, 68) == 0);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == 0x8004);
    memset(mac_cpu_ram(rt.cpu) + 0x4000, 0, 68);
    CHECK(memcmp(rt.cursor, expected, 68) == 0);
    CHECK(mac_cursor_service(&rt, 0xa852) == 1);
    CHECK(mac_cursor_service(&rt, 0xa852) == 1);
    CHECK((int16_t)mac_cpu_read(rt.cpu, 0x8d0, 2) == -2);
    CHECK(mac_cursor_service(&rt, 0xa856) == 1 && rt.cursor_obscured);
    CHECK((int16_t)mac_cpu_read(rt.cpu, 0x8d0, 2) == -2);
    for (int i = 0; i < 3; i++) CHECK(mac_cursor_service(&rt, 0xa853) == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x8d0, 2) == 0);
    CHECK(rt.cursor_obscured); /* Independent of the nesting level. */
    /* ShieldCursor(rect pointer, offset point): pops 8, changes no cursor state; a bad rectangle pointer halts. */
    {int16_t lvl = (int16_t)mac_cpu_read(rt.cpu, 0x8d0, 2); int ob = rt.cursor_obscured; uint8_t keep[68]; memcpy(keep, rt.cursor, 68);
     mac_cpu_set(rt.cpu, MAC_A7, 0x8000); mac_cpu_write(rt.cpu, 0x8000, 4, 0x00050003); mac_cpu_write(rt.cpu, 0x8004, 4, 0x4000);
     CHECK(mac_cursor_service(&rt, 0xa855) == 1 && mac_cpu_get(rt.cpu, MAC_A7) == 0x8008);
     CHECK((int16_t)mac_cpu_read(rt.cpu, 0x8d0, 2) == lvl && rt.cursor_obscured == ob && !memcmp(keep, rt.cursor, 68));
     for (unsigned k = 0; k < 3; k++) {
         MacRuntime bad; setup(&bad); mac_cpu_set(bad.cpu, MAC_A7, 0x8000);
         mac_cpu_write(bad.cpu, 0x8004, 4, k == 0 ? 0 : k == 1 ? 0xfffffffc : 0x3);
         if (k == 2) mac_cpu_write(bad.cpu, 0x8004, 4, (uint32_t)mac_cpu_ram_size(bad.cpu) - 4);
         CHECK(mac_cursor_service(&bad, 0xa855) == -1 && mac_cpu_halted(bad.cpu));
     }
     MacRuntime bs; setup(&bs); mac_cpu_set(bs.cpu, MAC_A7, (uint32_t)mac_cpu_ram_size(bs.cpu) - 4);
     CHECK(mac_cursor_service(&bs, 0xa855) == -1 && mac_cpu_halted(bs.cpu));}
    mac_cpu_destroy(rt.cpu);
}
static void resources(void) {
    MacRuntime rt; setup(&rt);
    CHECK(get(&rt, -1) == 0);
    CHECK((int16_t)mac_cpu_read(rt.cpu, 0xa60, 2) == -192);
    uint32_t h = get(&rt, 4), saved = rt.heap_next;
    CHECK(h != 0 && rt.handle_count == 1);
    CHECK(get(&rt, 4) == h && rt.heap_next == saved);
    CHECK(mac_cpu_read(rt.cpu, 0xa60, 2) == 0);
    uint32_t p = mac_cpu_read(rt.cpu, h, 4);
    CHECK(mac_cpu_read(rt.cpu, p + 4, 2) == 0x3ffc);
    CHECK(mac_cpu_read(rt.cpu, p + 26, 2) == 0x3ffc);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu) + p, mac_cpu_ram(rt.cpu) + p + 32, 32));
    CHECK(mac_cpu_read(rt.cpu, p + 64, 4) == 0x00070007);
    mac_cpu_set(rt.cpu, MAC_A7, 0x8000);
    mac_cpu_write(rt.cpu, 0x8000, 4, p);
    CHECK(mac_cursor_service(&rt, 0xa851) == 1 && rt.cursor_valid);
    /* A loaded guest CURS takes precedence; no replacement or mutation. */
    rt.handles[0].id = -9;
    CHECK(get(&rt, -9) == h && rt.heap_next == saved);
    mac_cpu_destroy(rt.cpu);
    /* the other standard cursors (1 I-beam, 2 crosshair, 3 plus): authored shapes, mask = data, centre hot spot, one handle each, cached */
    setup(&rt);
    for (int16_t id = 1; id <= 3; id++) {
        uint32_t c = get(&rt, id), at = rt.heap_next; CHECK(c != 0 && get(&rt, id) == c && rt.heap_next == at);
        uint32_t cp = mac_cpu_read(rt.cpu, c, 4);
        CHECK(!memcmp(mac_cpu_ram(rt.cpu) + cp, mac_cpu_ram(rt.cpu) + cp + 32, 32) && mac_cpu_read(rt.cpu, cp + 64, 4) == 0x00070007);
        CHECK(mac_cpu_read(rt.cpu, 0xa60, 2) == 0);
    }
    {   uint32_t a = get(&rt, 1), b = get(&rt, 2), c = get(&rt, 3), w = get(&rt, 4);
        CHECK(a != b && b != c && c != a && w != a && rt.handle_count == 4);
        uint32_t pa = mac_cpu_read(rt.cpu, a, 4), pb = mac_cpu_read(rt.cpu, b, 4), pc = mac_cpu_read(rt.cpu, c, 4);
        CHECK(mac_cpu_read(rt.cpu, pa, 2) == 0x07e0 && mac_cpu_read(rt.cpu, pa + 2, 2) == 0x0180 && mac_cpu_read(rt.cpu, pa + 30, 2) == 0x07e0);   /* I-beam */
        CHECK(mac_cpu_read(rt.cpu, pb + 14, 2) == 0x7ffe && mac_cpu_read(rt.cpu, pb + 2, 2) == 0x0100 && mac_cpu_read(rt.cpu, pb, 2) == 0);         /* crosshair */
        CHECK(mac_cpu_read(rt.cpu, pc + 12, 2) == 0x3ffc && mac_cpu_read(rt.cpu, pc + 4, 2) == 0x0180 && mac_cpu_read(rt.cpu, pc, 2) == 0 && mac_cpu_read(rt.cpu, pc + 18, 2) == 0x3ffc && mac_cpu_read(rt.cpu, pc + 20, 2) == 0x0180 && mac_cpu_read(rt.cpu, pc + 26, 2) == 0x0180 && mac_cpu_read(rt.cpu, pc + 28, 2) == 0);}       /* plus */
    CHECK(get(&rt, 0) == 0 && (int16_t)mac_cpu_read(rt.cpu, 0xa60, 2) == -192);                    /* 0 and 5 are not standard cursors */
    CHECK(get(&rt, 5) == 0 && (int16_t)mac_cpu_read(rt.cpu, 0xa60, 2) == -192);
    mac_cpu_destroy(rt.cpu);
    setup(&rt); saved = rt.heap_next; rt.heap_limit = saved + 4;
    CHECK(get(&rt, 4) == 0);
    CHECK(rt.heap_next == saved && rt.handle_count == 0);
    CHECK((int16_t)mac_cpu_read(rt.cpu, 0xa60, 2) == -108);
    mac_cpu_destroy(rt.cpu);
}
static void malformed(void) {
    uint32_t bad[] = {0, 0x4001, MAC_RUNTIME_RAM - 66, UINT32_MAX};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        MacRuntime rt; setup(&rt);
        memset(rt.cursor, 0x5a, sizeof(rt.cursor));
        mac_cpu_write(rt.cpu, 0x8000, 4, bad[i]);
        CHECK(mac_cursor_service(&rt, 0xa851) == -1);
        CHECK(mac_cpu_halted(rt.cpu) && !rt.cursor_valid);
        CHECK(rt.cursor[0] == 0x5a && rt.cursor[67] == 0x5a);
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == 0x8000);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt; setup(&rt);
    mac_cpu_set(rt.cpu, MAC_A7, MAC_RUNTIME_RAM - 4);
    CHECK(mac_cursor_service(&rt, 0xa9b9) == -1 && rt.handle_count == 0);
    mac_cpu_destroy(rt.cpu);
    setup(&rt); mac_cpu_write(rt.cpu, 0x8d0, 2, 0x8000);
    CHECK(mac_cursor_service(&rt, 0xa852) == -1);
    CHECK(mac_cpu_ram(rt.cpu)[0x8d0] == 0x80 && mac_cpu_ram(rt.cpu)[0x8d1] == 0);
    mac_cpu_destroy(rt.cpu);
}
int main(void) {
    cursor_state(); resources(); malformed();
    if (!failures) puts("mac_cursor: all checks passed");
    return failures ? 1 : 0;
}
