#include "mac_dlg_int.h"
#include "mac_pict.h"
#include <string.h>

/* A PICT item of a DITL: the picture resource the item names, scaled to the item rectangle (as DrawPicture scales to its destination) onto the 8-bit screen the
 * native dialog drawing uses. Version 1 and indexed version 2 pictures only (mac_pict_decode); anything else, a missing resource or a palette that cannot be matched
 * leaves the item blank, which is what the dialog showed before this existed. The picture is clipped to the screen. Not copied from a real system: the scaling is the decoder's. */
void mac_dlg_draw_pict(MacRuntime *rt, int l, int t, int r, int b, int16_t id) {
    int w = r - l, h = b - t;
    if (w <= 0 || h <= 0 || w > 640 || h > 480 || rt->screen_depth != 8 || !rt->screen_base) return;
    MacResource res;
    if (mac_resource_find(&rt->fork, MAC_RESOURCE_TYPE('P', 'I', 'C', 'T'), id, &res) || res.size < 14) return;
    MacPictPalette pal; memset(&pal, 0, sizeof pal);
    if (mac_palette_match_mask(rt, pal.allowed) < 0) return;
    for (unsigned i = 0; i < 256; i++) for (unsigned c = 0; c < 3; c++) pal.rgb[i][c] = (uint16_t)(((rt->palette[i] >> (16 - c * 8)) & 255) * 257);
    MacPictImage img; char err[160];
    if (mac_pict_decode(res.data, res.size, (unsigned)w, (unsigned)h, &pal, &img, err, sizeof err)) return;
    uint8_t *ram = mac_cpu_ram(rt->cpu); size_t z = mac_cpu_ram_size(rt->cpu);
    if ((size_t)rt->screen_base + 640u * 480u <= z)
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int sx = l + x, sy = t + y;
            if (sx < 0 || sy < 0 || sx >= 640 || sy >= 480 || !img.covered[(size_t)y * (unsigned)w + (unsigned)x]) continue;
            ram[rt->screen_base + (size_t)sy * 640 + (size_t)sx] = img.pixels[(size_t)y * (unsigned)w + (unsigned)x];
        }
    mac_pict_destroy(&img);
}
