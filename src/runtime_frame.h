/* SDL-free guest framebuffer conversion, shared by the SDL presenter, the
 * headless probe and tests. */
#ifndef RUNTIME_FRAME_H
#define RUNTIME_FRAME_H
#include "mac_runtime.h"
static int frame_dimensions(const MacRuntime *rt, size_t *pixels) {
    size_t w = rt->screen_width, h = rt->screen_height;
    if (!w || !h || w > 4096 || h > 4096 ||
        (rt->screen_depth != 1 && rt->screen_depth != 8)) return -1;
    *pixels = w * h;
    return 0;
}

static int copy_frame(MacRuntime *rt, uint32_t *out, size_t capacity) {
    size_t count;
    if (!rt || !rt->cpu || !out || frame_dimensions(rt, &count) || count > capacity) return -1;
    size_t width = rt->screen_width, height = rt->screen_height;
    size_t required_row = rt->screen_depth == 1 ? (width + 7) / 8 : width;
    size_t stride = rt->screen_row_bytes;
    size_t ram_size = mac_cpu_ram_size(rt->cpu);
    size_t base = rt->screen_base;
    if (stride < required_row || base > ram_size ||
        (height - 1) * stride + required_row > ram_size - base) return -1;
    const uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (size_t y = 0; y < height; y++) {
        const uint8_t *row = ram + base + y * stride;
        for (size_t x = 0; x < width; x++) {
            uint32_t rgb;
            if (rt->screen_depth == 1) rgb = row[x / 8] & (0x80u >> (x % 8)) ? 0 : 0xffffff;
            else rgb = rt->palette[row[x]] & 0xffffffu;
            out[y * width + x] = 0xff000000u | rgb;
        }
    }
    return 0;
}

#endif
