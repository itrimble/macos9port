#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

/* SetPortBits ($A875): the current port's portBits become the given BitMap; drawing then lands in that bitmap and the
 * screen is untouched. Color ports are refused. Bad pointers halt without changing the port. */
int mac_raster_service(MacRuntime *rt, uint16_t opcode);
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
enum { PORT = 0x700000, RECT = 0x600000, BITMAP = 0x600100, OFF = 0x610000, STACK = 0xf00000 };

static void rd_rect(MacRuntime *rt, uint32_t p, int t, int l, int b, int r) {
    mac_cpu_write(rt->cpu, p, 2, (uint16_t)t); mac_cpu_write(rt->cpu, p + 2, 2, (uint16_t)l);
    mac_cpu_write(rt->cpu, p + 4, 2, (uint16_t)b); mac_cpu_write(rt->cpu, p + 6, 2, (uint16_t)r);
}
static int quickdraw(MacRuntime *rt, uint16_t op, uint32_t arg) {
    mac_cpu_set(rt->cpu, MAC_A7, STACK); mac_cpu_write(rt->cpu, STACK, 4, arg);
    return mac_quickdraw_service(rt, op);
}
static int raster(MacRuntime *rt, uint16_t op, uint32_t arg) {
    mac_cpu_set(rt->cpu, MAC_A7, STACK); mac_cpu_write(rt->cpu, STACK, 4, arg);
    return mac_raster_service(rt, op);
}
static int init(MacRuntime *rt) {
    memset(rt, 0, sizeof *rt); rt->cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    if (!rt->cpu) return -1;
    rt->heap_next = 0x900000; rt->heap_limit = 0xd00000;
    mac_cpu_set(rt->cpu, MAC_A5, 0x800000); mac_cpu_set(rt->cpu, MAC_A7, STACK); mac_cpu_write(rt->cpu, STACK, 4, 0x7ffffc);
    CHECK(mac_quickdraw_service(rt, 0xa86e) == 1);
    CHECK(quickdraw(rt, 0xa86f, PORT) == 1);
    return 0;
}
/* An offscreen 1-bit bitmap: 16 pixels wide (rowBytes 2), 8 rows, bounds (4,6)-(12,22), in guest RAM at OFF. */
static void make_bitmap(MacRuntime *rt) {
    mac_cpu_write(rt->cpu, BITMAP, 4, OFF); mac_cpu_write(rt->cpu, BITMAP + 4, 2, 2);
    rd_rect(rt, BITMAP + 6, 4, 6, 12, 22); memset(mac_cpu_ram(rt->cpu) + OFF, 0, 16);
}
/* mac_cpu_read gives nothing once the CPU has halted, so checks after a halt read raw guest RAM (big-endian). */
static uint32_t raw32(MacRuntime *rt, uint32_t at) {
    const uint8_t *m = mac_cpu_ram(rt->cpu) + at; return (uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3];
}
static unsigned screen_nonzero(MacRuntime *rt) {
    unsigned n = 0; for (unsigned i = 0; i < 80u * 480u; i++) n += mac_cpu_ram(rt->cpu)[rt->screen_base + i] != 0; return n;
}

int main(void) {
    MacRuntime rt;
    if (init(&rt)) return 1;
    uint32_t vis_before = mac_cpu_read(rt.cpu, PORT + 24, 4), clip_before = mac_cpu_read(rt.cpu, PORT + 28, 4);
    uint32_t rect_before = mac_cpu_read(rt.cpu, PORT + 16, 4), rect_before2 = mac_cpu_read(rt.cpu, PORT + 20, 4);
    make_bitmap(&rt);
    CHECK(quickdraw(&rt, 0xa875, BITMAP) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == STACK + 4);                       /* the trap pops its one pointer argument */
    CHECK(mac_cpu_read(rt.cpu, PORT + 2, 4) == OFF && mac_cpu_read(rt.cpu, PORT + 6, 2) == 2);
    CHECK(mac_cpu_read(rt.cpu, PORT + 8, 4) == 0x00040006 && mac_cpu_read(rt.cpu, PORT + 12, 4) == 0x000c0016);
    CHECK(mac_cpu_read(rt.cpu, PORT + 24, 4) == vis_before && mac_cpu_read(rt.cpu, PORT + 28, 4) == clip_before);
    CHECK(mac_cpu_read(rt.cpu, PORT + 16, 4) == rect_before && mac_cpu_read(rt.cpu, PORT + 20, 4) == rect_before2);
    /* Draw a 2x2 black rectangle at local (4,6): pixel (0,0) of the offscreen bitmap, the top-left bit of byte 0. */
    rd_rect(&rt, RECT, 4, 6, 6, 8);
    uint32_t visrect = mac_cpu_read(rt.cpu, mac_cpu_read(rt.cpu, PORT + 24, 4), 4) + 2;
    rd_rect(&rt, visrect, 4, 6, 12, 22);
    uint32_t cliprect = mac_cpu_read(rt.cpu, mac_cpu_read(rt.cpu, PORT + 28, 4), 4) + 2;
    rd_rect(&rt, cliprect, 4, 6, 12, 22);
    CHECK(raster(&rt, 0xa8a2, RECT) == 1);                                   /* PaintRect */
    CHECK(mac_cpu_ram(rt.cpu)[OFF] == 0xc0 && mac_cpu_ram(rt.cpu)[OFF + 2] == 0xc0 && mac_cpu_ram(rt.cpu)[OFF + 4] == 0);
    CHECK(screen_nonzero(&rt) == 0);                                         /* the screen was not touched */
    mac_runtime_destroy(&rt);

    /* Bad pointers halt and leave the port as it was. */
    if (init(&rt)) return 1;
    uint32_t base_before = mac_cpu_read(rt.cpu, PORT + 2, 4);
    CHECK(quickdraw(&rt, 0xa875, 0) == -1 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu), "nil") != NULL);
    CHECK(raw32(&rt, PORT + 2) == base_before);
    mac_runtime_destroy(&rt);
    if (init(&rt)) return 1;
    CHECK(quickdraw(&rt, 0xa875, 0xfffff8) == -1 && mac_cpu_halted(rt.cpu));  /* 14 bytes would run past RAM */
    CHECK(raw32(&rt, PORT + 2) == base_before);
    mac_runtime_destroy(&rt);
    if (init(&rt)) return 1;
    make_bitmap(&rt);
    CHECK(quickdraw(&rt, 0xa875, BITMAP + 1) == -1 && mac_cpu_halted(rt.cpu)); /* odd pointer */
    mac_runtime_destroy(&rt);

    /* A color port keeps a PixMap handle where portBits sits, so it is refused and unchanged. */
    if (init(&rt)) return 1;
    make_bitmap(&rt);
    mac_cpu_write(rt.cpu, PORT + 6, 2, 0xc000);
    uint32_t handle_before = mac_cpu_read(rt.cpu, PORT + 2, 4);
    CHECK(quickdraw(&rt, 0xa875, BITMAP) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(strstr(mac_cpu_error(rt.cpu), "color port") != NULL && raw32(&rt, PORT + 2) == handle_before);
    mac_runtime_destroy(&rt);

    /* No current port at all. */
    if (init(&rt)) return 1;
    make_bitmap(&rt);
    mac_cpu_write(rt.cpu, 0x7ffffc, 4, 0);
    CHECK(quickdraw(&rt, 0xa875, BITMAP) == -1 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);

    printf("SetPortBits checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
