#include "mac_runtime.h"

#include <string.h>

/* Independent basic QuickDraw ABI implementation. Layout/behavior references:
 * https://www2.seas.gwu.edu/~adagroup/adalib_html/mac-toolbox-html/quickdraw.html
 * https://www.sysecol2.ethz.ch/RAMSES/Objects/AuxLibE/DMQuickDraw.html
 * Public interface descriptions only; no Apple implementation or cursor asset.
 * qd_globals points to the guest variable thePort, not the struct's first byte.
 */
static int range(MacRuntime *rt, uint32_t p, size_t n) {
    size_t size = mac_cpu_ram_size(rt->cpu);
    if ((p & 1) || p > size || n > size - p) {
        mac_cpu_halt(rt->cpu, "QuickDraw structure is outside aligned guest RAM.");
        return 0;
    }
    return 1;
}

static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }

static void rect(MacRuntime *rt, uint32_t p, int t, int l, int b, int r) {
    wr(rt, p, 2, (uint16_t)t); wr(rt, p + 2, 2, (uint16_t)l);
    wr(rt, p + 4, 2, (uint16_t)b); wr(rt, p + 6, 2, (uint16_t)r);
}

static void bitmap(MacRuntime *rt, uint32_t p) {
    wr(rt, p, 4, rt->screen_base); wr(rt, p + 4, 2, rt->screen_row_bytes);
    rect(rt, p + 6, 0, 0, rt->screen_height, rt->screen_width);
}

static uint32_t region(MacRuntime *rt, int t, int l, int b, int r) {
    size_t slot = 0;
    while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    if (slot == MAC_RUNTIME_HANDLES) return 0;
    uint32_t h = mac_runtime_alloc(rt, 4), p = mac_runtime_alloc(rt, 10);
    if (!h || !p) return 0;
    if (slot == rt->handle_count) rt->handle_count++;
    rt->handles[slot] = (MacHandle){.handle = h, .pointer = p, .size = 10};
    wr(rt, h, 4, p); wr(rt, p, 2, 10); rect(rt, p + 2, t, l, b, r);
    return h;
}

static int set_port(MacRuntime *rt, uint32_t port) {
    if (!rt->qd_globals || !range(rt, rt->qd_globals, 4) || (port && !range(rt, port, 108))) return -1;
    rt->current_port = port;
    wr(rt, rt->qd_globals, 4, port);
    return 1;
}

static int init_port(MacRuntime *rt, uint32_t port, int open) {
    if (!rt->qd_globals || !port || !range(rt, port, 108)) return -1;
    uint32_t visible = open ? region(rt, 0, 0, rt->screen_height, rt->screen_width) : rd(rt, port + 24, 4);
    uint32_t clip = open ? region(rt, -32767, -32767, 32767, 32767) : rd(rt, port + 28, 4);
    if (!visible || !clip || !range(rt, visible, 4) || !range(rt, clip, 4)) {
        mac_cpu_halt(rt->cpu, "QuickDraw port region allocation failed."); return -1;
    }
    uint32_t vp = rd(rt, visible, 4), cp = rd(rt, clip, 4);
    if (!vp || !cp || !range(rt, vp, 10) || !range(rt, cp, 10)) return -1;
    memset(mac_cpu_ram(rt->cpu) + port, 0, 108);
    bitmap(rt, port + 2);
    rect(rt, port + 16, 0, 0, rt->screen_height, rt->screen_width);
    wr(rt, port + 24, 4, visible); wr(rt, port + 28, 4, clip);
    wr(rt, vp, 2, 10); rect(rt, vp + 2, 0, 0, rt->screen_height, rt->screen_width);
    wr(rt, cp, 2, 10); rect(rt, cp + 2, -32767, -32767, 32767, 32767);
    memset(mac_cpu_ram(rt->cpu) + port + 40, 255, 8); /* fillPat */
    wr(rt, port + 52, 2, 1); wr(rt, port + 54, 2, 1);
    wr(rt, port + 56, 2, 8); /* patCopy */
    memset(mac_cpu_ram(rt->cpu) + port + 58, 255, 8); /* pnPat */
    wr(rt, port + 72, 2, 1); /* srcOr */
    wr(rt, port + 80, 4, 33); wr(rt, port + 84, 4, 30); /* black/white */
    return set_port(rt, port);
}

static int init_graf(MacRuntime *rt, uint32_t globals) {
    uint32_t a5 = mac_cpu_get(rt->cpu, MAC_A5);
    if (globals < 126 || !range(rt, globals - 126, 130) || !range(rt, a5, 4)) return -1;
    rt->qd_globals = globals;
    rt->current_port = 0;
    rt->screen_base = 0xe00000;
    rt->screen_width = 640; rt->screen_height = 480; rt->screen_row_bytes = 80;
    rt->screen_depth = 1;
    rt->palette[0] = 0xffffff; rt->palette[1] = 0;
    if (!range(rt, rt->screen_base, 80 * 480)) return -1;
    memset(mac_cpu_ram(rt->cpu) + globals - 126, 0, 130);
    wr(rt, globals - 126, 4, 1);
    bitmap(rt, globals - 122);
    /* Authored triangular cursor, not the Apple cursor bitmap. */
    for (unsigned y = 0; y < 12; y++) {
        uint16_t bits = (uint16_t)(0xffffu << (15 - y / 2));
        wr(rt, globals - 108 + y * 2, 2, bits);
        wr(rt, globals - 76 + y * 2, 2, bits);
    }
    for (unsigned y = 0; y < 8; y++) {
        wr(rt, globals - 40 + y, 1, y & 1 ? 0x77 : 0xdd);
        wr(rt, globals - 32 + y, 1, y & 1 ? 0x22 : 0x88);
        wr(rt, globals - 24 + y, 1, y & 1 ? 0x55 : 0xaa);
        wr(rt, globals - 16 + y, 1, 0xff);
    }
    wr(rt, a5, 4, globals);
    if (rt->indexed_requested) return mac_color_init(rt);
    return 1;
}

/* Address of the rectangle that maps a port's local coordinates to its pixels. A classic GrafPort keeps it in
 * portBits.bounds (port+8); a color port (CGrafPort, version bits 0xC000 at port+6) keeps it in the PixMap that
 * portPixMap (port+2) points to, at pmBounds (+6). 0 with the CPU halted if the structure is unusable. */
static uint32_t port_bounds(MacRuntime *rt, uint32_t port) {
    if (!port) { mac_cpu_halt(rt->cpu, "QuickDraw has no current port."); return 0; }
    if (!range(rt, port, 108)) return 0;
    if ((rd(rt, port + 6, 2) & 0xc000) != 0xc000) return port + 8;
    uint32_t handle = rd(rt, port + 2, 4);
    if (!handle || !range(rt, handle, 4)) { mac_cpu_halt(rt->cpu, "QuickDraw color port has no valid PixMap handle."); return 0; }
    uint32_t pixmap = rd(rt, handle, 4);
    if (!pixmap || !range(rt, pixmap, 50)) { mac_cpu_halt(rt->cpu, "QuickDraw color port PixMap is invalid."); return 0; }
    return pixmap + 6;
}

static void shift_rect(MacRuntime *rt, uint32_t r, int dv, int dh) { /* 16-bit wraparound, as QuickDraw's arithmetic */
    wr(rt, r, 2, (uint16_t)(rd(rt, r, 2) + dv)); wr(rt, r + 2, 2, (uint16_t)(rd(rt, r + 2, 2) + dh));
    wr(rt, r + 4, 2, (uint16_t)(rd(rt, r + 4, 2) + dv)); wr(rt, r + 6, 2, (uint16_t)(rd(rt, r + 6, 2) + dh));
}

/* SetOrigin(h, v): make (h, v) the top-left of the current port's portRect. As the public description says, the
 * local coordinate system moves: portBits.bounds (or pmBounds), portRect and visRgn are translated by the same
 * amount and the clipRgn is NOT, so a program keeps its clip in step by calling ClipRect itself. Everything is
 * validated before anything is written. Only rectangular regions exist in this runtime. */
static int set_origin(MacRuntime *rt, uint32_t sp) {
    int v = (int16_t)rd(rt, sp, 2), h = (int16_t)rd(rt, sp + 2, 2);
    if (!rt->qd_globals || !range(rt, rt->qd_globals, 4)) return -1;
    uint32_t port = rd(rt, rt->qd_globals, 4), bounds = port_bounds(rt, port);
    if (!bounds || !range(rt, bounds, 8)) return -1;
    int dv = v - (int16_t)rd(rt, port + 16, 2), dh = h - (int16_t)rd(rt, port + 18, 2);
    uint32_t vis = rd(rt, port + 24, 4), visrect = 0;
    if (!vis || !range(rt, vis, 4) || !rd(rt, vis, 4) || !range(rt, rd(rt, vis, 4), 10)) {
        mac_cpu_halt(rt->cpu, "SetOrigin needs a valid visible region."); return -1;
    }
    if (rd(rt, rd(rt, vis, 4), 2) != 10) { mac_cpu_halt(rt->cpu, "SetOrigin on a complex visible region is unsupported."); return -1; }
    visrect = rd(rt, vis, 4) + 2;
    if (dv || dh) { shift_rect(rt, bounds, dv, dh); shift_rect(rt, port + 16, dv, dh); shift_rect(rt, visrect, dv, dh); }
    return 1;
}

int mac_quickdraw_service(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    unsigned args = 0;
    switch (opcode) {
        case 0xa870: case 0xa871: case 0xa878: args = 4; break;
        case 0xa8a9: args = 8; break;
        case 0xa86e: case 0xa86d: case 0xa86f: case 0xa873: case 0xa874: case 0xa87d: args = 4; break;
        case 0xa8a7: args = 12; break;
        case 0xa850: args = 0; break;
        default: return 0;
    }
    if (!range(rt, sp, args)) return -1;
    uint32_t p = args == 4 && opcode != 0xa878 ? rd(rt, sp, 4) : 0;
    int rc = 1;
    switch (opcode) {
        case 0xa878: rc = set_origin(rt, sp); break;
        case 0xa870: case 0xa871: {
            /* The bounds rectangle is in local coordinates and its top-left is the bitmap's first pixel, so a point's
             * pixel position is local - bounds.topLeft: LocalToGlobal subtracts it and GlobalToLocal adds it. */
            uint32_t port = rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0, bounds = port_bounds(rt, port);
            if (!bounds || !range(rt, bounds, 8)) return -1;
            int16_t bounds_top = (int16_t)rd(rt, bounds, 2);
            int16_t bounds_left = (int16_t)rd(rt, bounds + 2, 2);
            uint32_t point = p;
            if (!point || !range(rt, point, 4)) return -1;
            int16_t v = (int16_t)rd(rt, point, 2);
            int16_t h = (int16_t)rd(rt, point + 2, 2);
            int delta_v = opcode == 0xa870 ? -bounds_top : bounds_top;
            int delta_h = opcode == 0xa870 ? -bounds_left : bounds_left;
            wr(rt, point, 2, (uint16_t)(int16_t)(v + delta_v));
            wr(rt, point + 2, 2, (uint16_t)(int16_t)(h + delta_h));
            break;
        }
        case 0xa86e: rc = init_graf(rt, p); break;
        case 0xa86f: case 0xa86d: rc = init_port(rt, p, opcode == 0xa86f); break;
        case 0xa873: rc = set_port(rt, p); break;
        case 0xa874:
            if (!rt->qd_globals || !p || !range(rt, p, 4)) return -1;
            rt->current_port = rd(rt, rt->qd_globals, 4);
            wr(rt, p, 4, rt->current_port); break;
        case 0xa87d:
            if (!p || !range(rt, p, 108)) return -1;
            for (unsigned offset = 24; offset <= 28; offset += 4) {
                uint32_t h = rd(rt, p + offset, 4);
                for (size_t i = 0; i < rt->handle_count; i++) {
                    if (h && rt->handles[i].handle == h) {
                        wr(rt, h, 4, 0);
                        memset(&rt->handles[i], 0, sizeof rt->handles[i]); break;
                    }
                }
                wr(rt, p + offset, 4, 0);
            }
            break;
        case 0xa8a7:
            p = rd(rt, sp + 8, 4);
            if (!p || !range(rt, p, 8)) return -1;
            rect(rt, p, (int16_t)rd(rt, sp + 4, 2), (int16_t)rd(rt, sp + 6, 2),
                 (int16_t)rd(rt, sp, 2), (int16_t)rd(rt, sp + 2, 2)); break;
        case 0xa8a9: {
            p = rd(rt, sp + 4, 4);
            if (!p || !range(rt, p, 8)) return -1;
            int16_t dv = (int16_t)rd(rt, sp, 2);
            int16_t dh = (int16_t)rd(rt, sp + 2, 2);
            int16_t top = (int16_t)rd(rt, p, 2);
            int16_t left = (int16_t)rd(rt, p + 2, 2);
            int16_t bottom = (int16_t)rd(rt, p + 4, 2);
            int16_t right = (int16_t)rd(rt, p + 6, 2);
            int32_t new_top = (int32_t)top + dv;
            int32_t new_left = (int32_t)left + dh;
            int32_t new_bottom = (int32_t)bottom - dv;
            int32_t new_right = (int32_t)right - dh;
            if (new_bottom - new_top < 1 || new_right - new_left < 1) {
                rect(rt, p, 0, 0, 0, 0);
            } else {
                rect(rt, p, (int16_t)new_top, (int16_t)new_left,
                     (int16_t)new_bottom, (int16_t)new_right);
            }
            break;
        }
        case 0xa850:
            if (rt->qd_globals >= 108 && range(rt, rt->qd_globals - 108, 68)) {
                memcpy(rt->cursor, mac_cpu_ram(cpu) + rt->qd_globals - 108, 68);
                rt->cursor_valid = 1;
            }
            rt->cursor_obscured = 0;
            wr(rt, 0x8d0, 2, 0); break;
    }
    if (rc > 0 && !mac_cpu_halted(cpu)) mac_cpu_set(cpu, MAC_A7, sp + args);
    return mac_cpu_halted(cpu) ? -1 : rc;
}
