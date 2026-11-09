#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

/* SetOrigin ($A878) and the coordinate conversions that must agree with it, on a classic GrafPort and on a color
 * port (CGrafPort, whose bounds live in the PixMap). Public description: SetOrigin translates portBits.bounds
 * (or pmBounds), portRect and visRgn by the same amount and leaves the clipRgn alone. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0xf00000u
#define A5 0x800000u
#define PORT 0x700000u
#define CPORT 0x710000u
enum { BOUNDS = 8, RECT = 16, VIS = 24, CLIP = 28 };

static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static int16_t s16(MacRuntime *r, uint32_t p) { return (int16_t)rd(r, p, 2); }
static int call(MacRuntime *r, uint16_t op, uint32_t arg) { mac_cpu_set(r->cpu, MAC_A7, SP); wr(r, SP, 4, arg); return mac_quickdraw_service(r, op); }
static int origin(MacRuntime *r, int h, int v) {                  /* SetOrigin(h, v): v on top of the stack */
    mac_cpu_set(r->cpu, MAC_A7, SP); wr(r, SP, 2, (uint16_t)v); wr(r, SP + 2, 2, (uint16_t)h);
    return mac_quickdraw_service(r, 0xa878);
}
static int convert(MacRuntime *r, uint16_t op, int v, int h, int16_t out[2]) {
    wr(r, 0x600020, 2, (uint16_t)v); wr(r, 0x600022, 2, (uint16_t)h);
    int rc = call(r, op, 0x600020); out[0] = s16(r, 0x600020); out[1] = s16(r, 0x600022); return rc;
}
static MacRuntime *make(void) {
    static MacRuntime rt;
    memset(&rt, 0, sizeof rt);
    rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(rt.cpu != NULL);
    rt.heap_next = 0x900000; rt.heap_limit = 0xd00000;
    mac_traps_init(&rt);
    mac_cpu_set(rt.cpu, MAC_A5, A5);
    return &rt;
}
static MacRuntime *classic(void) {
    MacRuntime *r = make();
    CHECK(call(r, 0xa86e, A5 - 4) == 1 && call(r, 0xa86f, PORT) == 1);
    return r;
}
static void rect4(MacRuntime *r, uint32_t p, int t, int l, int b, int rr) {
    wr(r, p, 2, (uint16_t)t); wr(r, p + 2, 2, (uint16_t)l); wr(r, p + 4, 2, (uint16_t)b); wr(r, p + 6, 2, (uint16_t)rr);
}
static int rect_is(MacRuntime *r, uint32_t p, int t, int l, int b, int rr) {
    return s16(r, p) == (int16_t)t && s16(r, p + 2) == (int16_t)l && s16(r, p + 4) == (int16_t)b && s16(r, p + 6) == (int16_t)rr;
}
static uint32_t vis_box(MacRuntime *r, uint32_t port) { return rd(r, rd(r, port + VIS, 4), 4) + 2; }
static uint32_t clip_box(MacRuntime *r, uint32_t port) { return rd(r, rd(r, port + CLIP, 4), 4) + 2; }

/* A hand-built color port: handle table at 0x720000, PixMap at 0x720100, regions at 0x720200 and 0x720300. */
static MacRuntime *color(int visible_size, int pixmap_ok, int vis_ok) {
    MacRuntime *r = make();
    CHECK(call(r, 0xa86e, A5 - 4) == 1);
    memset(mac_cpu_ram(r->cpu) + CPORT, 0, 108);
    wr(r, 0x720000, 4, pixmap_ok ? 0x720100 : 0);               /* PixMap master pointer */
    wr(r, 0x720010, 4, 0x720200); wr(r, 0x720020, 4, 0x720300);
    wr(r, CPORT + 2, 4, 0x720000); wr(r, CPORT + 6, 2, 0xc001);
    wr(r, CPORT + 8, 4, 0x11223344); wr(r, CPORT + 12, 2, 0x5566); wr(r, CPORT + 14, 2, 0x7788);   /* the classic-layout bounds slots */
    rect4(r, CPORT + RECT, 0, 0, 200, 300);
    wr(r, CPORT + VIS, 4, vis_ok ? 0x720010 : 0); wr(r, CPORT + CLIP, 4, 0x720020);
    wr(r, 0x720200, 2, (uint16_t)visible_size); rect4(r, 0x720202, 0, 0, 200, 300);
    wr(r, 0x720300, 2, 10); rect4(r, 0x720302, -32767, -32767, 32767, 32767);
    wr(r, 0x720100, 4, 0xe00000); wr(r, 0x720104, 2, 0x8000 | 640); rect4(r, 0x720106, 0, 0, 200, 300);
    memset(mac_cpu_ram(r->cpu) + 0x72010e, 0xab, 36);
    CHECK(call(r, 0xa873, CPORT) == 1);
    return r;
}

static void classic_port(void) {
    MacRuntime *r = classic();
    uint8_t before[108]; memcpy(before, mac_cpu_ram(r->cpu) + PORT, 108);
    CHECK(rect_is(r, PORT + BOUNDS, 0, 0, 480, 640) && rect_is(r, PORT + RECT, 0, 0, 480, 640) && rect_is(r, vis_box(r, PORT), 0, 0, 480, 640));
    CHECK(origin(r, 100, 50) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 4);               /* h = 100, v = 50 */
    CHECK(rect_is(r, PORT + RECT, 50, 100, 530, 740));                                      /* top-left is now (v 50, h 100) */
    CHECK(rect_is(r, PORT + BOUNDS, 50, 100, 530, 740));                                    /* bounds moved with it */
    CHECK(rect_is(r, vis_box(r, PORT), 50, 100, 530, 740));                                 /* so did the visible region */
    CHECK(rect_is(r, clip_box(r, PORT), -32767, -32767, 32767, 32767));                    /* the clip region did NOT */
    uint8_t after[108]; memcpy(after, mac_cpu_ram(r->cpu) + PORT, 108);
    for (unsigned i = 0; i < 108; i++) {                                                     /* nothing else in the port changed */
        int moved = (i >= BOUNDS && i < BOUNDS + 8) || (i >= RECT && i < RECT + 8);
        if (!moved) CHECK(after[i] == before[i]);
    }
    int16_t g[2];
    CHECK(convert(r, 0xa870, 50, 100, g) == 1 && g[0] == 0 && g[1] == 0);                  /* the top-left pixel is still pixel (0,0) */
    CHECK(convert(r, 0xa870, 60, 130, g) == 1 && g[0] == 10 && g[1] == 30);
    CHECK(convert(r, 0xa871, 10, 30, g) == 1 && g[0] == 60 && g[1] == 130);                 /* and back */
    CHECK(origin(r, 100, 50) == 1 && rect_is(r, PORT + RECT, 50, 100, 530, 740));           /* same origin again: no drift */
    CHECK(origin(r, 7, 3) == 1 && s16(r, PORT + RECT) == 3 && s16(r, PORT + RECT + 2) == 7);   /* h and v are not swapped */
    CHECK(origin(r, 0, 0) == 1);
    CHECK(!memcmp(before, mac_cpu_ram(r->cpu) + PORT, 108));                                 /* the round trip restores the port exactly */
    CHECK(origin(r, 0, 0) == 1 && !memcmp(before, mac_cpu_ram(r->cpu) + PORT, 108));         /* a no-op changes nothing */
    CHECK(origin(r, -300, -200) == 1 && rect_is(r, PORT + RECT, -200, -300, 280, 340));    /* negative origins */
    CHECK(origin(r, 32767, 32767) == 1 && s16(r, PORT + RECT) == 32767 && s16(r, PORT + RECT + 4) == (int16_t)(480 + 32767));   /* 16-bit wraparound */
    CHECK(!mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
}

static void color_port(void) {
    MacRuntime *r = color(10, 1, 1);
    uint8_t port[108], pixmap[50]; memcpy(port, mac_cpu_ram(r->cpu) + CPORT, 108); memcpy(pixmap, mac_cpu_ram(r->cpu) + 0x720100, 50);
    CHECK(origin(r, 40, 60) == 1);
    CHECK(rect_is(r, 0x720106, 60, 40, 260, 340));                                           /* the PixMap's pmBounds moved */
    CHECK(rect_is(r, CPORT + RECT, 60, 40, 260, 340) && rect_is(r, 0x720202, 60, 40, 260, 340));
    CHECK(rect_is(r, 0x720302, -32767, -32767, 32767, 32767));                               /* clip untouched */
    CHECK(rd(r, CPORT + 8, 4) == 0x11223344 && rd(r, CPORT + 12, 2) == 0x5566 && rd(r, CPORT + 14, 2) == 0x7788);   /* CGrafPort fields at port+8 intact */
    CHECK(!memcmp(pixmap, mac_cpu_ram(r->cpu) + 0x720100, 6) && !memcmp(pixmap + 14, mac_cpu_ram(r->cpu) + 0x72010e, 36));   /* rest of the PixMap intact */
    int16_t g[2];
    CHECK(convert(r, 0xa870, 60, 40, g) == 1 && g[0] == 0 && g[1] == 0);
    CHECK(convert(r, 0xa871, 5, 5, g) == 1 && g[0] == 65 && g[1] == 45);
    CHECK(origin(r, 0, 0) == 1 && memcmp(port + 16, mac_cpu_ram(r->cpu) + CPORT + 16, 8) == 0 && memcmp(pixmap, mac_cpu_ram(r->cpu) + 0x720100, 50) == 0);
    CHECK(!mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
}

/* Anything unusable halts with a message, and nothing is written before the checks pass. */
static void fail_closed(void) {
    MacRuntime *r;
    r = color(14, 1, 1);                                                                      /* a complex visible region */
    uint8_t snap[108 + 64]; memcpy(snap, mac_cpu_ram(r->cpu) + CPORT, 108); memcpy(snap + 108, mac_cpu_ram(r->cpu) + 0x720100, 64);
    CHECK(origin(r, 5, 5) == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), "complex"));
    CHECK(!memcmp(snap, mac_cpu_ram(r->cpu) + CPORT, 108) && !memcmp(snap + 108, mac_cpu_ram(r->cpu) + 0x720100, 64));   /* no partial update */
    mac_cpu_destroy(r->cpu);
    r = color(10, 0, 1);                                                                      /* PixMap master pointer is nil */
    CHECK(origin(r, 5, 5) == -1 && mac_cpu_halted(r->cpu) && s16(r, CPORT + RECT) == 0);
    mac_cpu_destroy(r->cpu);
    r = color(10, 1, 0);                                                                      /* no visible region */
    CHECK(origin(r, 5, 5) == -1 && mac_cpu_halted(r->cpu) && s16(r, 0x720106) == 0);
    mac_cpu_destroy(r->cpu);
    r = color(10, 1, 1); wr(r, CPORT + 2, 4, 0xfffffff0);                                    /* PixMap handle outside RAM */
    CHECK(origin(r, 5, 5) == -1 && mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
    r = make(); CHECK(call(r, 0xa86e, A5 - 4) == 1);                                          /* no port set */
    CHECK(origin(r, 1, 1) == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), "no current port"));
    mac_cpu_destroy(r->cpu);
    r = classic(); wr(r, A5 - 4, 4, 0xfffffff0);                                              /* thePort points outside RAM */
    CHECK(origin(r, 1, 1) == -1 && mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
    r = classic(); wr(r, A5 - 4, 4, PORT + 1);                                                /* misaligned port */
    CHECK(origin(r, 1, 1) == -1 && mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
    r = classic(); mac_cpu_set(r->cpu, MAC_A7, 0xfffffff0u);                                  /* stack outside RAM */
    CHECK(mac_quickdraw_service(r, 0xa878) == -1);
    mac_cpu_destroy(r->cpu);
    r = classic(); wr(r, PORT + VIS, 4, 0);                                                   /* classic port with no visRgn */
    uint8_t keep[8]; memcpy(keep, mac_cpu_ram(r->cpu) + PORT + RECT, 8);
    CHECK(origin(r, 1, 1) == -1 && mac_cpu_halted(r->cpu));
    CHECK(!memcmp(keep, mac_cpu_ram(r->cpu) + PORT + RECT, 8));                              /* portRect untouched */
    mac_cpu_destroy(r->cpu);
}

int main(void) {
    classic_port();
    color_port();
    fail_closed();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("SetOrigin checks ok");
    return 0;
}
