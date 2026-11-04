#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

static int call(MacRuntime *rt, uint16_t opcode, uint32_t argument) {
    mac_cpu_set(rt->cpu, MAC_A7, 0xf00000);
    mac_cpu_write(rt->cpu, 0xf00000, 4, argument);
    return mac_quickdraw_service(rt, opcode);
}

static void indexed_startup(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return;
    rt.heap_next = 0x100000; rt.heap_limit = 0x700000;
    rt.indexed_requested = 1;
    mac_traps_init(&rt);
    mac_cpu_set(rt.cpu, MAC_A5, 0x800000);
    CHECK(call(&rt, 0xa86e, 0x7ffffc) == 1);
    CHECK(rt.screen_depth == 8 && rt.screen_row_bytes == 640);
    CHECK(rt.main_device != 0 && rt.screen_pixmap != 0);
    mac_cpu_set(rt.cpu, MAC_A7, 0xf00000);
    mac_cpu_write(rt.cpu, 0xf00000, 4, 0x700000);
    CHECK(mac_runtime_trap(rt.cpu, 0xaa00, 0x100, &rt) == 0);
    CHECK(rt.current_port == 0x700000);
    /* Exercise the real dispatcher after InitGraf, rather than fabricated
     * display records: a black 2x2 rectangle must change exactly four pixels. */
    mac_cpu_write(rt.cpu, 0x600000, 4, 0);
    mac_cpu_write(rt.cpu, 0x600004, 4, 0x00020002);
    mac_cpu_set(rt.cpu, MAC_A7, 0xf00000);
    mac_cpu_write(rt.cpu, 0xf00000, 4, 0x600000);
    CHECK(mac_runtime_trap(rt.cpu, 0xa8a2, 0x102, &rt) == 0);
    uint8_t *pixels = mac_cpu_ram(rt.cpu) + rt.screen_base;
    CHECK(pixels[0] == 255 && pixels[1] == 255 && pixels[2] == 0);
    CHECK(pixels[640] == 255 && pixels[641] == 255 && pixels[642] == 0);
    CHECK(pixels[1280] == 0 && !mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

int main(void) {
    indexed_startup();
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    rt.heap_next = 0x900000; rt.heap_limit = 0xd00000;
    const uint32_t a5 = 0x800000, globals = a5 - 4, port = 0x700000;
    mac_cpu_set(rt.cpu, MAC_A5, a5);
    CHECK(call(&rt, 0xa86e, globals) == 1);
    CHECK(!mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == 0xf00004);
    CHECK(mac_cpu_read(rt.cpu, a5, 4) == globals);
    CHECK(mac_cpu_read(rt.cpu, globals, 4) == 0);
    CHECK(mac_cpu_read(rt.cpu, globals - 126, 4) == 1);
    CHECK(mac_cpu_read(rt.cpu, globals - 122, 4) == 0xe00000);
    CHECK(mac_cpu_read(rt.cpu, globals - 118, 2) == 80);
    CHECK(mac_cpu_read(rt.cpu, globals - 112, 2) == 480);
    CHECK(mac_cpu_read(rt.cpu, globals - 110, 2) == 640);
    CHECK(rt.screen_depth == 1 && rt.palette[0] == 0xffffff && rt.palette[1] == 0);
    CHECK(mac_cpu_read(rt.cpu, globals - 16, 4) == 0xffffffff);
    CHECK(mac_cpu_read(rt.cpu, globals - 8, 4) == 0);

    CHECK(call(&rt, 0xa86f, port) == 1);
    CHECK(rt.current_port == port && mac_cpu_read(rt.cpu, globals, 4) == port);
    CHECK(mac_cpu_read(rt.cpu, port + 2, 4) == 0xe00000);
    CHECK(mac_cpu_read(rt.cpu, port + 6, 2) == 80);
    CHECK(mac_cpu_read(rt.cpu, port + 20, 2) == 480);
    CHECK(mac_cpu_read(rt.cpu, port + 22, 2) == 640);
    uint32_t vis = mac_cpu_read(rt.cpu, port + 24, 4), clip = mac_cpu_read(rt.cpu, port + 28, 4);
    uint32_t vr = mac_cpu_read(rt.cpu, vis, 4), cr = mac_cpu_read(rt.cpu, clip, 4);
    CHECK(vis != 0 && clip != 0 && vis != clip);
    CHECK(mac_cpu_read(rt.cpu, vr, 2) == 10 && mac_cpu_read(rt.cpu, cr, 2) == 10);
    CHECK(mac_cpu_read(rt.cpu, vr + 6, 2) == 480);
    CHECK(mac_cpu_read(rt.cpu, cr + 2, 2) == (uint16_t)-32767);
    CHECK(mac_cpu_read(rt.cpu, cr + 6, 2) == 32767);
    CHECK(mac_cpu_read(rt.cpu, port + 32, 4) == 0);
    CHECK(mac_cpu_read(rt.cpu, port + 40, 4) == 0xffffffff);
    CHECK(mac_cpu_read(rt.cpu, port + 52, 4) == 0x00010001);
    CHECK(mac_cpu_read(rt.cpu, port + 56, 2) == 8);
    CHECK(mac_cpu_read(rt.cpu, port + 72, 2) == 1);
    CHECK(mac_cpu_read(rt.cpu, port + 80, 4) == 33);
    CHECK(mac_cpu_read(rt.cpu, port + 84, 4) == 30);

    uint32_t heap = rt.heap_next;
    mac_cpu_write(rt.cpu, port + 52, 2, 4);
    CHECK(call(&rt, 0xa86d, port) == 1);
    CHECK(rt.heap_next == heap);
    CHECK(mac_cpu_read(rt.cpu, port + 24, 4) == vis);
    CHECK(mac_cpu_read(rt.cpu, port + 52, 2) == 1);
    CHECK(call(&rt, 0xa873, 0) == 1);
    CHECK(call(&rt, 0xa874, 0x600000) == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x600000, 4) == 0);
    CHECK(call(&rt, 0xa873, port) == 1);
    CHECK(call(&rt, 0xa874, 0x600000) == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x600000, 4) == port);

    mac_cpu_set(rt.cpu, MAC_A7, 0xf00000);
    mac_cpu_write(rt.cpu, 0xf00000, 2, 50); /* bottom */
    mac_cpu_write(rt.cpu, 0xf00002, 2, 60); /* right */
    mac_cpu_write(rt.cpu, 0xf00004, 2, (uint16_t)-10); /* top */
    mac_cpu_write(rt.cpu, 0xf00006, 2, 20); /* left */
    mac_cpu_write(rt.cpu, 0xf00008, 4, 0x600010);
    CHECK(mac_quickdraw_service(&rt, 0xa8a7) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == 0xf0000c);
    CHECK(mac_cpu_read(rt.cpu, 0x600010, 2) == (uint16_t)-10);
    CHECK(mac_cpu_read(rt.cpu, 0x600012, 2) == 20);
    CHECK(mac_cpu_read(rt.cpu, 0x600014, 2) == 50);
    CHECK(mac_cpu_read(rt.cpu, 0x600016, 2) == 60);
    CHECK(call(&rt, 0xa850, 0) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == 0xf00000);
    CHECK(mac_cpu_read(rt.cpu, 0x8d0, 2) == 0);
    CHECK(call(&rt, 0xa8ec, 0) == 0); /* CopyBits belongs to its own service. */
    CHECK(call(&rt, 0xa87d, port) == 1);
    CHECK(mac_cpu_read(rt.cpu, vis, 4) == 0 && mac_cpu_read(rt.cpu, clip, 4) == 0);
    CHECK(mac_cpu_read(rt.cpu, port + 24, 4) == 0);
    CHECK(call(&rt, 0xa86e, 2) == -1); /* Underflow before globals. */
    CHECK(call(&rt, 0xa86f, 0xffffff) == -1);
    CHECK(mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
    printf("QuickDraw initialization checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
