#include "mac_runtime.h"
#include "mac_shape.h"
#include <stdio.h>

/* Ovals and round rectangles: FrameRoundRect $A8B0, PaintRoundRect $A8B1, EraseRoundRect $A8B2, InvertRoundRect $A8B3, FillRoundRect $A8B4,
 * FrameOval $A8B7, PaintOval $A8B8, EraseOval $A8B9, InvertOval $A8BA, FillOval $A8BB, drawn on black-and-white ports and on indexed
 * colour ports with the same pattern, mode, pen-size, hidden-pen and clip rules as the rectangle calls. The shapes come from mac_shape.c,
 * which is our own scan conversion and not QuickDraw's. A frame is the shape minus the same shape with the rectangle inset by the pen size
 * and the corner ellipse reduced by twice the pen size. Stack, Pascal order: RoundRect (rect, ovalW, ovalH): ovalH at SP, ovalW, then the
 * rectangle pointer; FillRoundRect adds the pattern pointer at SP; Oval takes the rectangle pointer, FillOval a pattern pointer above... below it. */
int mac_raster_shape(MacRuntime *rt, uint32_t port, int t, int l, int b, int r, uint32_t pattern, int mode, int invert, int frame, const MacShape *outer, const MacShape *inner);
int mac_color_shape(MacRuntime *rt, uint32_t port, int t, int l, int b, int r, uint16_t op, uint32_t fillpat, const MacShape *outer, const MacShape *inner);

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n, int even) { size_t z = mac_cpu_ram_size(rt->cpu); return (!even || !(p & 1)) && p != 0 && p <= z && n <= z - p; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }

int mac_shapes_service(MacRuntime *rt, uint16_t opcode) {
    int round = opcode >= 0xa8b0 && opcode <= 0xa8b4, oval = opcode >= 0xa8b7 && opcode <= 0xa8bb;
    if (!round && !oval) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    unsigned kind = opcode - (round ? 0xa8b0 : 0xa8b7);                 /* 0 frame, 1 paint, 2 erase, 3 invert, 4 fill */
    unsigned args = (round ? 8 : 4) + (kind == 4 ? 4 : 0);
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!valid(rt, sp, args, 1)) return fail(rt, "A shape call has an invalid stack.");
    uint32_t at = sp, pat = 0; int oh = 0, ow = 0;
    if (kind == 4) { pat = rd(rt, at, 4); at += 4; }
    if (round) { oh = (int16_t)rd(rt, at, 2); ow = (int16_t)rd(rt, at + 2, 2); at += 4; }
    uint32_t rect = rd(rt, at, 4);
    if (!valid(rt, rect, 8, 1)) return fail(rt, "A shape call's rectangle is outside guest RAM.");
    if (!rt->qd_globals || !valid(rt, rt->qd_globals, 4, 1)) return fail(rt, "QuickDraw globals are not initialized.");
    uint32_t port = rd(rt, rt->qd_globals, 4);
    if (!valid(rt, port, 108, 1)) return fail(rt, "A shape call has no valid current port.");
    int t = (int16_t)rd(rt, rect, 2), l = (int16_t)rd(rt, rect + 2, 2), b = (int16_t)rd(rt, rect + 4, 2), r = (int16_t)rd(rt, rect + 6, 2);
    if (!round) { ow = r - l; oh = b - t; }                                  /* an oval: the corner ellipse is the whole rectangle */
    int color = (rd(rt, port + 6, 2) & 0xc000) == 0xc000;
    int pw = (int16_t)rd(rt, port + 54, 2), ph = (int16_t)rd(rt, port + 52, 2);
    MacShape outer, inner; mac_shape_init(&outer); mac_shape_init(&inner);
    int rc = 1;
    if (mac_shape_build(&outer, t, l, b, r, ow, oh)) rc = fail(rt, "A shape is taller than the supported 2048 rows.");
    else if (kind == 0 && pw > 0 && ph > 0 && mac_shape_build(&inner, t + ph, l + pw, b - ph, r - pw, ow - 2 * pw, oh - 2 * ph))
        rc = fail(rt, "A shape is taller than the supported 2048 rows.");
    if (rc > 0) {
        int mode = (kind <= 1) ? (int)rd(rt, port + 56, 2) : 8;
        if (color) {
            static const uint16_t op[5] = {0xa8a1, 0xa8a2, 0xa8a3, 0xa8a4, 0xa8a5};
            rc = mac_color_shape(rt, port, t, l, b, r, op[kind], pat, &outer, kind == 0 ? &inner : NULL);
        } else {
            uint32_t pattern = kind == 4 ? pat : port + (kind == 2 ? 32 : 58);
            rc = mac_raster_shape(rt, port, t, l, b, r, pattern, mode, kind == 3, kind == 0, &outer, kind == 0 ? &inner : NULL);
        }
    }
    mac_shape_free(&outer); mac_shape_free(&inner);
    if (rc > 0 && !mac_cpu_halted(rt->cpu)) mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : rc;
}
