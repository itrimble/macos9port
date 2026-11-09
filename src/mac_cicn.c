#include "mac_cicn.h"
#include <string.h>

/* Colour icons. A 'cicn' resource (layout from memory of Inside Macintosh, checked against the 906-byte icons in Glider PRO) is: a PixMap record (50
 * bytes), a mask BitMap (14), an icon BitMap (14), the iconData handle slot (4), the mask bits, the icon bitmap bits, a colour table (ctSeed 4, ctFlags 2,
 * ctSize 2, then ctSize+1 ColorSpecs of 8 bytes) and the pixel data. GetCIcon keeps the resource's own bytes in a guest handle; the handle's contents are
 * UNTRUSTED (the program can write them), so every PlotCIcon parses and bounds-checks them again. Differences: only unpacked chunky indexed pixels of 1, 2, 4
 * or 8 bits; a mask of rowBytes 0 means no mask; PlotCIcon scales by nearest neighbour; only the colour pixels are drawn (the 1-bit iconBMap is ignored);
 * colours are matched to the nearest entry of the 8-bit palette (squared RGB distance, first of equals); drawing is a plain copy (srcCopy where the mask is
 * set), into the current port, which must be an indexed-8 colour port. */
#define CICN_TYPE UINT32_C(0x4349434e)
#define CICN_MAX_SIDE 256
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
static int ram_ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }

int mac_cicn_parse(const uint8_t *d, size_t n, MacCicn *o, const char **why) {
#define BAD(s) do { *why = s; return -1; } while (0)
    if (n < 82) BAD("The colour icon resource is shorter than its header.");
    int t = (int16_t)be16(d + 6), l = (int16_t)be16(d + 8), b = (int16_t)be16(d + 10), r = (int16_t)be16(d + 12);
    int w = r - l, h = b - t;
    if (w < 1 || h < 1 || w > CICN_MAX_SIDE || h > CICN_MAX_SIDE) BAD("The colour icon has an unsupported size.");
    if (!(be16(d + 4) & 0x8000)) BAD("The colour icon's PixMap lacks the PixMap flag.");
    unsigned depth = be16(d + 32);
    if (be16(d + 16) != 0 || be16(d + 30) != 0 || be16(d + 34) != 1 || be16(d + 36) != depth || (depth != 1 && depth != 2 && depth != 4 && depth != 8))
        BAD("Only unpacked chunky indexed colour icons of 1, 2, 4 or 8 bits are supported.");
    unsigned pix_row = be16(d + 4) & 0x3fff, mask_row = be16(d + 54);
    if (pix_row < ((unsigned)w * depth + 7) / 8 || pix_row > 4096) BAD("The colour icon's pixel rowBytes is wrong.");
    if (mask_row && (mask_row < ((unsigned)w + 7) / 8 || mask_row > 4096)) BAD("The colour icon's mask rowBytes is wrong.");
    unsigned bmap_row = be16(d + 68);
    if (bmap_row > 4096) BAD("The colour icon's bitmap rowBytes is wrong.");
    size_t at = 82, mask_bytes = (size_t)mask_row * (size_t)h, bmap_bytes = (size_t)bmap_row * (size_t)h;
    if (mask_bytes > n - at) BAD("The colour icon's mask runs past the resource.");
    o->mask = mask_row ? d + at : NULL; at += mask_bytes;
    if (bmap_bytes > n - at) BAD("The colour icon's bitmap runs past the resource.");
    at += bmap_bytes;
    if (n - at < 8) BAD("The colour icon has no colour table.");
    unsigned entries = be16(d + at + 6) + 1u;
    if (entries > 256) BAD("The colour icon's colour table is too long.");
    o->device_table = (be16(d + at + 4) & 0x8000) != 0;
    at += 8;
    if ((size_t)entries * 8 > n - at) BAD("The colour icon's colour table runs past the resource.");
    o->table = d + at; o->colors = entries; at += (size_t)entries * 8;
    size_t pix_bytes = (size_t)pix_row * (size_t)h;
    if (pix_bytes > n - at) BAD("The colour icon's pixel data runs past the resource.");
    o->pixels = d + at;
    o->width = w; o->height = h; o->depth = (int)depth; o->pix_row = pix_row; o->mask_row = mask_row;
    return 0;
#undef BAD
}

static const uint8_t *icon_bytes(MacRuntime *rt, uint32_t h, size_t *size) {
    if (!h || (h & 1) || !ram_ok(rt, h, 4)) { mac_cpu_halt(rt->cpu, "A colour icon handle is nil or invalid."); return NULL; }
    for (size_t i = 0; i < rt->handle_count; i++) {
        const MacHandle *m = &rt->handles[i];
        if (m->handle != h || m->type != CICN_TYPE) continue;
        uint32_t p = rd(rt, h, 4);
        if (p != m->pointer || !ram_ok(rt, p, m->size)) break;
        *size = m->size;
        return mac_cpu_ram(rt->cpu) + p;
    }
    mac_cpu_halt(rt->cpu, "A colour icon handle is not a live colour icon (forged, disposed or its master pointer changed).");
    return NULL;
}

static int get_cicon(MacRuntime *rt, uint32_t sp) {
    int16_t id = (int16_t)rd(rt, sp, 2); MacResource res; MacCicn c; const char *why;
    uint32_t out = 0;
    if (mac_resource_find(&rt->fork, 0x6369636e, id, &res)) wr(rt, 0xa60, 2, (uint16_t)-192);
    else if (res.size > 0x20000 || mac_cicn_parse(res.data, res.size, &c, &why)) return fail(rt, res.size > 0x20000 ? "The colour icon resource is too large." : why);
    else {
        unsigned slot = 0; while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
        uint32_t saved = rt->heap_next, h = 0, p = 0;
        if (slot < MAC_RUNTIME_HANDLES) { h = mac_runtime_alloc(rt, 4); p = h ? mac_runtime_alloc_data(rt, (uint32_t)res.size) : 0; }
        if (!p) { rt->heap_next = saved; wr(rt, 0xa60, 2, (uint16_t)-108); }
        else {
            if (slot == rt->handle_count) rt->handle_count++;
            rt->handles[slot] = (MacHandle){ .handle = h, .pointer = p, .size = (uint32_t)res.size, .type = CICN_TYPE };
            memcpy(mac_cpu_ram(rt->cpu) + p, res.data, res.size); wr(rt, h, 4, p);
            wr(rt, 0xa60, 2, 0); out = h;
        }
    }
    wr(rt, sp + 2, 4, out);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}

static int dispose(MacRuntime *rt, uint32_t sp) {
    uint32_t h = rd(rt, sp, 4);
    if (h) {                                                               /* DisposeCIcon(nil) does nothing */
        size_t size; if (!icon_bytes(rt, h, &size)) return -1;
        for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == h && rt->handles[i].type == CICN_TYPE) {
            mac_runtime_free_data(rt, rt->handles[i].pointer); wr(rt, h, 4, 0); memset(&rt->handles[i], 0, sizeof rt->handles[i]); break;
        }
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
    return 1;
}

static int plot(MacRuntime *rt, uint32_t sp) {
    uint32_t rect = rd(rt, sp + 4, 4), h = rd(rt, sp, 4);
    if (!ram_ok(rt, rect, 8) || (rect & 1)) return fail(rt, "PlotCIcon's rectangle is outside guest RAM.");
    size_t size; const uint8_t *d = icon_bytes(rt, h, &size); if (!d) return -1;
    MacCicn c; const char *why;
    if (mac_cicn_parse(d, size, &c, &why)) return fail(rt, why);
    int rt_ = (int16_t)rd(rt, rect, 2), rl = (int16_t)rd(rt, rect + 2, 2), rb = (int16_t)rd(rt, rect + 4, 2), rr = (int16_t)rd(rt, rect + 6, 2);
    if (rb > rt_ && rr > rl) {
        MacColorTarget tg; int rc = mac_color_target(rt, rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0, &tg);
        if (rc < 0) return -1;
        if (rc == 0) return fail(rt, "PlotCIcon needs a colour port as the current port.");
        uint8_t map[256]; unsigned seen[256 / 32 + 1] = {0};
        for (unsigned i = 0; i < c.colors; i++) {                          /* each table entry -> nearest palette index, by the entry's value (or position) */
            const uint8_t *e = c.table + i * 8; unsigned key = c.device_table ? i : be16(e);
            if (key > 255) continue;
            unsigned r8 = e[2], g8 = e[4], b8 = e[6], best = 0, bestd = UINT32_MAX;
            for (unsigned k = 0; k < 256; k++) {
                uint32_t pc = rt->palette[k]; int dr = (int)((pc >> 16) & 255) - (int)r8, dg = (int)((pc >> 8) & 255) - (int)g8, db = (int)(pc & 255) - (int)b8;
                unsigned dist = (unsigned)(dr * dr + dg * dg + db * db);
                if (dist < bestd) { bestd = dist; best = k; }
            }
            map[key] = (uint8_t)best; seen[key / 32] |= 1u << (key % 32);
        }
        int rw = rr - rl, rh = rb - rt_;
        for (int y = rt_; y < rb; y++) {
            if (y < tg.ct || y >= tg.cb) continue;
            int sy = (int)((int64_t)(y - rt_) * c.height / rh);
            for (int x = rl; x < rr; x++) {
                if (x < tg.cl || x >= tg.cr) continue;
                int sx = (int)((int64_t)(x - rl) * c.width / rw);
                if (c.mask && !(c.mask[(size_t)sy * c.mask_row + (unsigned)sx / 8] & (0x80u >> (sx & 7)))) continue;
                const uint8_t *row = c.pixels + (size_t)sy * c.pix_row;
                unsigned bit = (unsigned)sx * (unsigned)c.depth, v = (row[bit / 8] >> (8 - c.depth - (bit & 7))) & ((1u << c.depth) - 1);
                if (!(seen[v / 32] & (1u << (v % 32)))) continue;
                tg.pixels[(size_t)(y - tg.bt) * (size_t)tg.row + (size_t)(x - tg.bl)] = map[v];
            }
        }
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 8);
    return 1;
}

int mac_cicn_service(MacRuntime *rt, uint16_t op) {
    unsigned args = op == 0xaa1e ? 2 : op == 0xaa1f ? 8 : op == 0xaa25 ? 4 : 0, result = op == 0xaa1e ? 4 : 0;
    if (!args) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || !ram_ok(rt, sp, args + result)) return fail(rt, "Colour icon call has an invalid stack.");
    return op == 0xaa1e ? get_cicon(rt, sp) : op == 0xaa1f ? plot(rt, sp) : dispose(rt, sp);
}
