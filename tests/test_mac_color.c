#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define SP 0x8000u

static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { CHECK(!mac_cpu_write(rt->cpu, p, n, v)); }
static MacRuntime fresh(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(rt.cpu);
    rt.heap_next = 0x10000; rt.heap_limit = 0x100000;
    mac_cpu_set(rt.cpu, MAC_A7, SP); return rt;
}
static void layout(void) {
    MacRuntime rt = fresh();
    CHECK(mac_color_init(&rt) == 1);
    CHECK(rt.screen_width == 640 && rt.screen_height == 480 && rt.screen_depth == 8);
    CHECK(rt.screen_row_bytes == 640 && rt.screen_base == 0xe00000);
    uint32_t gd = rd(&rt, rt.main_device, 4), pm = rd(&rt, rt.screen_pixmap, 4);
    uint32_t ct = rd(&rt, rt.screen_colors, 4);
    CHECK(rd(&rt, gd + 22, 4) == rt.screen_pixmap && rd(&rt, gd + 30, 4) == 0);
    CHECK(rd(&rt, gd + 34, 4) == 0 && rd(&rt, gd + 38, 2) == 480 && rd(&rt, gd + 40, 2) == 640);
    CHECK(rd(&rt, gd + 20, 2) == 0xe801);
    CHECK(rd(&rt, pm, 4) == rt.screen_base && rd(&rt, pm + 4, 2) == 0x8280);
    CHECK(rd(&rt, pm + 22, 4) == 72u << 16 && rd(&rt, pm + 26, 4) == 72u << 16);
    CHECK(rd(&rt, pm + 30, 2) == 0 && rd(&rt, pm + 32, 2) == 8);
    CHECK(rd(&rt, pm + 34, 2) == 1 && rd(&rt, pm + 36, 2) == 8);
    CHECK(rd(&rt, pm + 42, 4) == rt.screen_colors && rd(&rt, pm + 46, 4) == 0);
    CHECK(rd(&rt, ct, 4) > 1023 && rd(&rt, ct + 4, 2) == 0x8000 && rd(&rt, ct + 6, 2) == 255);
    CHECK(rt.palette[0] == 0xffffff && rt.palette[255] == 0);
    for (unsigned i = 0; i < 256; i++) {
        CHECK(rd(&rt, ct + 10 + i * 8, 2) == ((rt.palette[i] >> 16) & 255) * 257);
        CHECK(rd(&rt, ct + 12 + i * 8, 2) == ((rt.palette[i] >> 8) & 255) * 257);
        CHECK(rd(&rt, ct + 14 + i * 8, 2) == (rt.palette[i] & 255) * 257);
    }
    uint32_t heap = rt.heap_next;
    CHECK(mac_color_init(&rt) == 1 && heap == rt.heap_next);
    const uint16_t gets[] = {0xaa29, 0xaa2a, 0xaa32};
    for (unsigned i = 0; i < 3; i++) {
        CHECK(mac_color_service(&rt, gets[i]) == 1);
        CHECK(rd(&rt, SP, 4) == rt.main_device && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    }
    wr(&rt, SP, 4, rt.main_device);
    CHECK(mac_color_service(&rt, 0xaa2b) == 1 && rd(&rt, SP + 4, 4) == 0);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 4);
    for (unsigned bit = 0; bit < 18; bit++) {
        mac_cpu_set(rt.cpu, MAC_A7, SP);
        wr(&rt, SP, 2, bit); wr(&rt, SP + 2, 4, rt.main_device); wr(&rt, SP + 6, 2, 0x55aa);
        CHECK(mac_color_service(&rt, 0xaa2c) == 1);
        unsigned yes = bit < 16 && (0xe801 & (1u << bit));
        CHECK(rd(&rt, SP + 6, 2) == (yes ? 0x01aa : 0x00aa));
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 6);
    }
    mac_cpu_set(rt.cpu, MAC_A7, SP); wr(&rt, SP, 4, rt.main_device);
    CHECK(mac_color_service(&rt, 0xaa31) == 1 && rt.current_device == rt.main_device);
    CHECK(mac_color_service(&rt, 0xaa00) == 0); /* Color ports remain unsupported. */
    mac_cpu_destroy(rt.cpu);
}
static void faults(void) {
    MacRuntime rt = fresh();
    rt.heap_limit = rt.heap_next;
    CHECK(mac_color_init(&rt) == -1 && !rt.main_device && !rt.handle_count);
    mac_cpu_destroy(rt.cpu);
    rt = fresh(); CHECK(mac_color_init(&rt) == 1);
    mac_cpu_set(rt.cpu, MAC_A7, 0xfffffffc);
    CHECK(mac_color_service(&rt, 0xaa2b) == -1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    rt = fresh(); CHECK(mac_color_init(&rt) == 1);
    wr(&rt, SP, 4, MAC_RUNTIME_RAM - 2); wr(&rt, SP + 4, 4, 0xcafebabe);
    CHECK(mac_color_service(&rt, 0xaa2b) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_ram(rt.cpu)[SP + 4] == 0xca && mac_cpu_get(rt.cpu, MAC_A7) == SP);
    mac_cpu_destroy(rt.cpu);
}
int main(void) { layout(); faults(); puts("mac color device tests passed"); return 0; }
