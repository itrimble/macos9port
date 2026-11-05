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

int mac_quickdraw_service(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    unsigned args = 0;
    switch (opcode) {
        case 0xa870: case 0xa871: args = 4; break;
        case 0xa86e: case 0xa86d: case 0xa86f: case 0xa873: case 0xa874: case 0xa87d: args = 4; break;
        case 0xa8a7: args = 12; break;
        case 0xa850: args = 0; break;
        default: return 0;
    }
    if (!range(rt, sp, args)) return -1;
    uint32_t p = args == 4 ? rd(rt, sp, 4) : 0;
    int rc = 1;
    switch (opcode) {
        case 0xa870: case 0xa871: {
            uint32_t port = rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0;
            if (!port || !range(rt, port, 108)) return -1;
            uint32_t bits = port + 2;
            int16_t bounds_top = (int16_t)rd(rt, bits + 10, 2);
            int16_t bounds_left = (int16_t)rd(rt, bits + 12, 2);
            uint32_t point = p;
            if (!point || !range(rt, point, 4)) return -1;
            int16_t v = (int16_t)rd(rt, point, 2);
            int16_t h = (int16_t)rd(rt, point + 2, 2);
            int delta_v = opcode == 0xa870 ? bounds_top : -bounds_top;
            int delta_h = opcode == 0xa870 ? bounds_left : -bounds_left;
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
