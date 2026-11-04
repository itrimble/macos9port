#include "mac_runtime.h"
#include <string.h>

/* Public Color QuickDraw structures, independently implemented:
 * dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-202.html
 * leopard-adc.pepas.com/documentation/mac/QuickDraw/QuickDraw-275.html
 * One indexed screen only; no claim of complete Color QuickDraw support. */
static int valid(MacRuntime *rt, uint32_t p, size_t n) {
    if ((p & 1) || p > mac_cpu_ram_size(rt->cpu) || n > mac_cpu_ram_size(rt->cpu) - p) {
        mac_cpu_halt(rt->cpu, "Color QuickDraw range exceeds aligned guest RAM.");
        return 0;
    }
    return 1;
}
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static void bounds(MacRuntime *rt, uint32_t p) {
    wr(rt, p, 4, 0); wr(rt, p + 4, 2, 480); wr(rt, p + 6, 2, 640);
}

int mac_color_init(MacRuntime *rt) {
    if (mac_cpu_halted(rt->cpu)) return -1;
    if (rt->main_device) return 1;
    if (!valid(rt, 0xe00000, 640 * 480)) return -1;
    /* Allocate all records together before publishing any state. Handles are
     * locked for the runtime-owned display; applications must not dispose them. */
    unsigned slots[3], found = 0;
    for (unsigned i = 0; i < MAC_RUNTIME_HANDLES && found < 3; i++)
        if (!rt->handles[i].handle && !rt->handles[i].pointer) slots[found++] = i;
    if (found != 3) { mac_cpu_halt(rt->cpu, "No handles for display records."); return -1; }
    uint32_t block = mac_runtime_alloc(rt, 12 + 62 + 50 + 2056);
    if (!block) { mac_cpu_halt(rt->cpu, "No memory for display records."); return -1; }
    uint32_t gd = block + 12, pm = gd + 62, ct = pm + 50;
    uint32_t ptrs[3] = {gd, pm, ct}, sizes[3] = {62, 50, 2056};
    for (unsigned i = 0; i < 3; i++) {
        wr(rt, block + i * 4, 4, ptrs[i]);
        rt->handles[slots[i]] = (MacHandle){.handle = block + i * 4,
            .pointer = ptrs[i], .size = sizes[i], .state = 0x80};
        if (rt->handle_count <= slots[i]) rt->handle_count = slots[i] + 1;
    }
    wr(rt, ct, 4, 1024); wr(rt, ct + 4, 2, 0x8000); wr(rt, ct + 6, 2, 255);
    /* Authored uniform RGB332 cube, reversed so white=0 and black=255. */
    for (unsigned i = 0; i < 256; i++) {
        unsigned v = 255 - i;
        unsigned r = ((v >> 5) & 7) * 255 / 7;
        unsigned g = ((v >> 2) & 7) * 255 / 7;
        unsigned b = (v & 3) * 255 / 3;
        rt->palette[i] = r << 16 | g << 8 | b;
        wr(rt, ct + 8 + i * 8, 2, 0);
        wr(rt, ct + 10 + i * 8, 2, r * 257);
        wr(rt, ct + 12 + i * 8, 2, g * 257);
        wr(rt, ct + 14 + i * 8, 2, b * 257);
    }
    wr(rt, pm, 4, 0xe00000); wr(rt, pm + 4, 2, 0x8000 | 640); bounds(rt, pm + 6);
    wr(rt, pm + 22, 4, 72u << 16); wr(rt, pm + 26, 4, 72u << 16);
    wr(rt, pm + 32, 2, 8); wr(rt, pm + 34, 2, 1); wr(rt, pm + 36, 2, 8);
    wr(rt, pm + 42, 4, block + 8);
    wr(rt, gd + 20, 2, 0xe801); /* color, main, screen, no driver, active */
    wr(rt, gd + 22, 4, block + 4); bounds(rt, gd + 34);
    rt->main_device = rt->current_device = block;
    rt->screen_pixmap = block + 4; rt->screen_colors = block + 8;
    rt->screen_base = 0xe00000; rt->screen_width = 640; rt->screen_height = 480;
    rt->screen_row_bytes = 640; rt->screen_depth = 8;
    memset(mac_cpu_ram(rt->cpu) + rt->screen_base, 0, 640 * 480);
    return 1;
}

static int device(MacRuntime *rt, uint32_t h, uint32_t *out) {
    if (!h || !valid(rt, h, 4)) return 0;
    uint32_t p = rd(rt, h, 4);
    if (!p || !valid(rt, p, 62)) return 0;
    *out = p;
    return 1;
}

int mac_color_service(MacRuntime *rt, uint16_t opcode) {
    unsigned args, result;
    switch (opcode) {
        case 0xaa29: case 0xaa2a: case 0xaa32: args = 0; result = 4; break;
        case 0xaa2b: args = 4; result = 4; break;
        case 0xaa2c: args = 6; result = 2; break;
        case 0xaa31: args = 4; result = 0; break;
        default: return 0;
    }
    if (mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!valid(rt, sp, args + result)) return -1;
    if (!rt->main_device) {
        mac_cpu_halt(rt->cpu, "Indexed display has not been initialized."); return -1;
    }
    uint32_t p = 0, h = 0, answer = rt->main_device;
    if (args) {
        h = rd(rt, sp + (opcode == 0xaa2c ? 2 : 0), 4);
        if (!device(rt, h, &p)) {
            mac_cpu_halt(rt->cpu, "Invalid graphics device handle."); return -1;
        }
    }
    switch (opcode) {
        case 0xaa32: answer = rt->current_device; break;
        case 0xaa2b: answer = rd(rt, p + 30, 4); break;
        case 0xaa2c: {
            unsigned bit = rd(rt, sp, 2);
            answer = bit < 16 && (rd(rt, p + 20, 2) & (1u << bit));
            answer = !!answer; break;
        }
        case 0xaa31:
            if (h != rt->main_device) {
                mac_cpu_halt(rt->cpu, "Offscreen device selection is unsupported."); return -1;
            }
            rt->current_device = h; break;
    }
    if (result) wr(rt, sp + args, result == 2 ? 1 : 4, answer);
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return 1;
}
