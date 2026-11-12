#include "mac_pixpat.h"
#include "mac_handles.h"
#include <stdio.h>
#include <string.h>

/* Pixel patterns, from the public Color QuickDraw interface.
 *   NewPixPat $AA07, DisposePixPat $AA08, GetPixPat $AA0C, MakeRGBPat $AA0D, FillCRect $AA0E, FillCOval $AA0F, FillCRoundRect $AA10.
 * A PixPat is 28 bytes: patType (0 old-style 1-bit pattern, 1 colour pattern, 2 RGB pattern), patMap (PixMapHandle), patData (Handle), patXData (Handle),
 * patXValid (-1: no expanded copy), patXMap, pat1Data (the 8-byte pattern). A 'ppat' resource is that record with patMap and patData as offsets from the
 * start of the resource, the PixMap (50 bytes, pmTable an offset), the pixel data and the colour table (ctSeed, ctFlags, ctSize, ColorSpecs).
 * What is and is not here:
 * - The handles are ordinary Memory Manager blocks; the guest may read and change them, so every use parses them again.
 * - Colour patterns: unpacked chunky indexed pixels of 1, 2, 4 or 8 bits, at most 64 x 64. Each colour is matched to the nearest entry of the current
 *   8-bit device table when the pattern is used; no expanded copy is cached (patXValid stays -1, patXData stays empty).
 * - An RGB pattern paints the single nearest device colour. A real Macintosh dithers between up to four colours; that is not done.
 * - The fills are plain copies clipped like FillRect; on a black-and-white port, and for an old-style pattern, they paint pat1Data in the port's colours.
 * - PenPixPat, BackPixPat, CopyPixPat and the other FillC calls are not implemented and halt as unhandled traps. */
enum { REC = 28, PIXMAP = 50, MAX_SIDE = 64 };
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
static int ram_ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && !(p & 1) && p <= z && n <= z - p; }
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

static unsigned nearest(const MacRuntime *rt, unsigned r8, unsigned g8, unsigned b8) {
    unsigned best = 0, bestd = UINT32_MAX;
    for (unsigned k = 0; k < 256; k++) {
        uint32_t pc = rt->palette[k];
        int dr = (int)((pc >> 16) & 255) - (int)r8, dg = (int)((pc >> 8) & 255) - (int)g8, db = (int)(pc & 255) - (int)b8;
        unsigned dist = (unsigned)(dr * dr + dg * dg + db * db);
        if (dist < bestd) { bestd = dist; best = k; }
    }
    return best;
}

/* The record and its two blocks, zeroed: an old-style pattern of all white. */
static uint32_t new_pixpat(MacRuntime *rt, uint32_t map_bytes, uint32_t data_bytes, uint32_t table_bytes) {
    uint32_t h = mac_handle_new(rt, REC), map = mac_handle_new(rt, map_bytes), data = mac_handle_new(rt, data_bytes), xdata = mac_handle_new(rt, 0);
    uint32_t table = mac_handle_new(rt, table_bytes), p, n, mp;
    if (!h || !map || !data || !xdata || !table || !mac_handle_block(rt, h, &p, &n) || !mac_handle_block(rt, map, &mp, &n)) {
        mac_handle_dispose(rt, h); mac_handle_dispose(rt, map); mac_handle_dispose(rt, data); mac_handle_dispose(rt, xdata); mac_handle_dispose(rt, table);
        return 0;
    }
    wr(rt, p + 2, 4, map); wr(rt, p + 6, 4, data); wr(rt, p + 10, 4, xdata); wr(rt, p + 14, 2, 0xffff);
    wr(rt, mp + 42, 4, table);
    return h;
}

static int get_pixpat(MacRuntime *rt, uint32_t sp) {
    int16_t id = (int16_t)rd(rt, sp, 2);
    MacResource res;
    uint32_t out = 0;
    int16_t err = 0;
    if (mac_resource_find(&rt->fork, MAC_RESOURCE_TYPE('p', 'p', 'a', 't'), id, &res)) err = -192;
    else {
        const uint8_t *d = res.data;
        if (res.size < REC) return fail(rt, "The pixel pattern resource is shorter than its record.");
        unsigned type = be16(d);
        uint32_t map_at = be32(d + 2), data_at = be32(d + 6), table_at = 0, table_bytes = 8, data_bytes = 0;
        if (type == 1) {
            if (map_at > res.size || res.size - map_at < PIXMAP) return fail(rt, "The pixel pattern's PixMap runs past the resource.");
            const uint8_t *m = d + map_at;
            unsigned row = be16(m + 4) & 0x3fff;
            int height = (int16_t)be16(m + 10) - (int16_t)be16(m + 6);
            table_at = be32(m + 42);
            if (height < 1 || height > MAX_SIDE || row > 4096) return fail(rt, "The pixel pattern has an unsupported size.");
            data_bytes = row * (unsigned)height;
            if (data_at > res.size || res.size - data_at < data_bytes) return fail(rt, "The pixel pattern's pixels run past the resource.");
            if (table_at > res.size || res.size - table_at < 8) return fail(rt, "The pixel pattern has no colour table.");
            unsigned entries = be16(d + table_at + 6) + 1u;
            if (entries > 256 || res.size - table_at - 8 < entries * 8) return fail(rt, "The pixel pattern's colour table runs past the resource.");
            table_bytes = 8 + entries * 8;
        } else if (type != 0) return fail(rt, "Only old-style and colour 'ppat' resources (patType 0 and 1) are supported.");
        out = new_pixpat(rt, PIXMAP, data_bytes, table_bytes);
        if (!out) err = -108;
        else {
            uint32_t p, n, mh, mp, dh, dp, th, tp;
            mac_handle_block(rt, out, &p, &n);
            uint8_t *ram = mac_cpu_ram(rt->cpu);
            wr(rt, p, 2, type); memcpy(ram + p + 20, d + 20, 8);
            mh = rd(rt, p + 2, 4); dh = rd(rt, p + 6, 4);
            if (type == 1 && mac_handle_block(rt, mh, &mp, &n) && mac_handle_block(rt, dh, &dp, &n)) {
                th = rd(rt, mp + 42, 4);
                memcpy(ram + mp, d + map_at, PIXMAP);
                wr(rt, mp, 4, 0); wr(rt, mp + 42, 4, th);
                memcpy(ram + dp, d + data_at, data_bytes);
                if (mac_handle_block(rt, th, &tp, &n)) memcpy(ram + tp, d + table_at, table_bytes);
            }
        }
    }
    wr(rt, 0xa60, 2, (uint16_t)err);
    wr(rt, sp + 2, 4, out);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 2);
    return 1;
}

static int record(MacRuntime *rt, uint32_t h, uint32_t *p) {
    uint32_t n;
    if (!mac_handle_block(rt, h, p, &n) || n < REC) return fail(rt, "A pixel pattern handle is nil or not a live pixel pattern.");
    return 1;
}

static int dispose(MacRuntime *rt, uint32_t sp) {
    uint32_t h = rd(rt, sp, 4), p, mp, n;
    if (h) {
        if (record(rt, h, &p) < 0) return -1;
        uint32_t map = rd(rt, p + 2, 4), data = rd(rt, p + 6, 4), xdata = rd(rt, p + 10, 4);
        if (mac_handle_block(rt, map, &mp, &n) && n >= PIXMAP) mac_handle_dispose(rt, rd(rt, mp + 42, 4));
        mac_handle_dispose(rt, map); mac_handle_dispose(rt, data); mac_handle_dispose(rt, xdata); mac_handle_dispose(rt, h);
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + 4);
    return 1;
}

/* MakeRGBPat(pp, myColor): the pattern becomes an RGB pattern. The colour is kept where the public description puts it, entry 4 of a five-entry colour
 * table; the 8 x 8, 2-bit PixMap that a real system fills with a dither is zeroed. */
static int make_rgb(MacRuntime *rt, uint32_t sp) {
    uint32_t color = rd(rt, sp, 4), h = rd(rt, sp + 4, 4), p, mp, tp, n;
    if (!ram_ok(rt, color, 6)) return fail(rt, "MakeRGBPat's colour pointer is invalid.");
    if (record(rt, h, &p) < 0) return -1;
    uint32_t map = rd(rt, p + 2, 4);
    if (!mac_handle_block(rt, map, &mp, &n) || n < PIXMAP) return fail(rt, "MakeRGBPat's pattern has no PixMap.");
    uint32_t r = rd(rt, color, 2), g = rd(rt, color + 2, 2), b = rd(rt, color + 4, 2);
    uint32_t table = rd(rt, mp + 42, 4), fresh = mac_handle_new(rt, 8 + 5 * 8);
    if (!fresh || !mac_handle_block(rt, fresh, &tp, &n)) return fail(rt, "No memory for MakeRGBPat's colour table.");
    mac_handle_dispose(rt, table);
    wr(rt, tp + 6, 2, 4);
    for (unsigned i = 0; i < 5; i++) wr(rt, tp + 8 + i * 8, 2, i);
    wr(rt, tp + 8 + 4 * 8 + 2, 2, r); wr(rt, tp + 8 + 4 * 8 + 4, 2, g); wr(rt, tp + 8 + 4 * 8 + 6, 2, b);
    memset(mac_cpu_ram(rt->cpu) + mp, 0, PIXMAP);
    wr(rt, mp + 4, 2, 0x8002); wr(rt, mp + 10, 2, 8); wr(rt, mp + 12, 2, 8); wr(rt, mp + 22, 4, 72u << 16); wr(rt, mp + 26, 4, 72u << 16);
    wr(rt, mp + 32, 2, 2); wr(rt, mp + 34, 2, 1); wr(rt, mp + 36, 2, 2); wr(rt, mp + 42, 4, fresh);
    wr(rt, p, 2, 2); wr(rt, p + 14, 2, 0xffff);
    mac_cpu_set(rt->cpu, MAC_A7, sp + 8);
    return 1;
}

int mac_pixpat_read(MacRuntime *rt, uint32_t handle, uint32_t *pat1, MacPixTile *tile) {
    uint32_t p, mp, tp, dp, n, tn, dn;
    if (record(rt, handle, &p) < 0) return -1;
    unsigned type = rd(rt, p, 2);
    *pat1 = p + 20;
    if (type == 0) return 0;
    if (type > 2) return fail(rt, "A pixel pattern has an unknown patType.");
    if (!mac_handle_block(rt, rd(rt, p + 2, 4), &mp, &n) || n < PIXMAP) return fail(rt, "A pixel pattern's PixMap handle is invalid.");
    if (!mac_handle_block(rt, rd(rt, mp + 42, 4), &tp, &tn) || tn < 8) return fail(rt, "A pixel pattern's colour table handle is invalid.");
    const uint8_t *ram = mac_cpu_ram(rt->cpu), *table = ram + tp;
    unsigned entries = be16(table + 6) + 1u;
    if (entries > 256 || tn < 8 + entries * 8) return fail(rt, "A pixel pattern's colour table is truncated.");
    if (type == 2) {
        if (entries < 5) return fail(rt, "An RGB pixel pattern's colour table has no colour.");
        const uint8_t *e = table + 8 + 4 * 8;
        tile->w = tile->h = 1; tile->px[0] = (uint8_t)nearest(rt, e[2], e[4], e[6]);
        return 1;
    }
    const uint8_t *m = ram + mp;
    unsigned row = be16(m + 4) & 0x3fff, depth = be16(m + 32);
    int w = (int16_t)be16(m + 12) - (int16_t)be16(m + 8), h = (int16_t)be16(m + 10) - (int16_t)be16(m + 6);
    if (w < 1 || h < 1 || w > MAX_SIDE || h > MAX_SIDE) return fail(rt, "A pixel pattern larger than 64 x 64 is unsupported.");
    if (be16(m + 16) != 0 || be16(m + 30) != 0 || be16(m + 34) != 1 || be16(m + 36) != depth || (depth != 1 && depth != 2 && depth != 4 && depth != 8))
        return fail(rt, "Only unpacked chunky indexed pixel patterns of 1, 2, 4 or 8 bits are supported.");
    if (row < ((unsigned)w * depth + 7) / 8) return fail(rt, "A pixel pattern's rowBytes is too small.");
    if (!mac_handle_block(rt, rd(rt, p + 6, 4), &dp, &dn) || dn < row * (unsigned)h) return fail(rt, "A pixel pattern's data handle is invalid or too short.");
    int map[256];
    int by_position = (be16(table + 4) & 0x8000) != 0;
    for (unsigned i = 0; i < 256; i++) map[i] = -1;
    for (unsigned i = 0; i < entries; i++) {
        const uint8_t *e = table + 8 + i * 8;
        unsigned key = by_position ? i : be16(e);
        if (key < 256 && map[key] < 0) map[key] = (int)nearest(rt, e[2], e[4], e[6]);
    }
    tile->w = w; tile->h = h;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const uint8_t *line = ram + dp + (size_t)y * row;
        unsigned bit = (unsigned)x * depth, v = (line[bit / 8] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
        if (map[v] < 0) return fail(rt, "A pixel pattern uses a pixel value its colour table does not define.");
        tile->px[y * w + x] = (uint8_t)map[v];
    }
    return 1;
}

/* FillCRect(r, pp), FillCOval(r, pp), FillCRoundRect(r, ovalWidth, ovalHeight, pp): the pattern handle is at SP. */
static int fill(MacRuntime *rt, uint32_t sp, uint16_t opcode) {
    static MacPixTile tile;
    uint32_t pp = rd(rt, sp, 4), pat1;
    int round = opcode == 0xaa10, kind = mac_pixpat_read(rt, pp, &pat1, &tile);
    if (kind < 0) return -1;
    if (!rt->qd_globals || !ram_ok(rt, rt->qd_globals, 4)) return fail(rt, "QuickDraw globals are not initialized.");
    uint32_t port = rd(rt, rt->qd_globals, 4);
    if (!ram_ok(rt, port, 108)) return fail(rt, "A pixel pattern fill has no valid current port.");
    if (kind == 0 || (rd(rt, port + 6, 2) & 0xc000) != 0xc000) { /* the 1-bit pattern in the port's colours: exactly FillRect, FillOval or FillRoundRect */
        wr(rt, sp, 4, pat1);
        uint16_t plain = opcode == 0xaa0e ? 0xa8a5 : opcode == 0xaa0f ? 0xa8bb : 0xa8b4;
        int rc = plain == 0xa8a5 ? mac_color_raster_service(rt, plain) : mac_shapes_service(rt, plain);
        if (!rc && plain == 0xa8a5) rc = mac_raster_service(rt, plain);
        if (rc <= 0 && !mac_cpu_halted(rt->cpu)) { wr(rt, sp, 4, pp); return fail(rt, "A pixel pattern fill on this port is unsupported."); }
        return rc;
    }
    uint32_t rect = rd(rt, sp + (round ? 8 : 4), 4);
    if (!ram_ok(rt, rect, 8)) return fail(rt, "A pixel pattern fill's rectangle is outside guest RAM.");
    int t = (int16_t)rd(rt, rect, 2), l = (int16_t)rd(rt, rect + 2, 2), b = (int16_t)rd(rt, rect + 4, 2), r = (int16_t)rd(rt, rect + 6, 2);
    int rc;
    if (opcode == 0xaa0e) rc = mac_color_tile(rt, port, t, l, b, r, &tile, NULL);
    else {
        int ow = round ? (int16_t)rd(rt, sp + 6, 2) : r - l, oh = round ? (int16_t)rd(rt, sp + 4, 2) : b - t;
        MacShape shape; mac_shape_init(&shape);
        rc = mac_shape_build(&shape, t, l, b, r, ow, oh) ? fail(rt, "A shape is taller than the supported 2048 rows.") : mac_color_tile(rt, port, t, l, b, r, &tile, &shape);
        mac_shape_free(&shape);
    }
    if (rc > 0) mac_cpu_set(rt->cpu, MAC_A7, sp + (round ? 12 : 8));
    return rc;
}

int mac_pixpat_service(MacRuntime *rt, uint16_t opcode) {
    unsigned need;
    switch (opcode) {
    case 0xaa07: need = 4; break;
    case 0xaa08: need = 4; break;
    case 0xaa0c: need = 6; break;
    case 0xaa0d: case 0xaa0e: case 0xaa0f: need = 8; break;
    case 0xaa10: need = 12; break;
    default: return 0;
    }
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!ram_ok(rt, sp, need)) return fail(rt, "A pixel pattern call has an invalid stack.");
    switch (opcode) {
    case 0xaa07: { /* NewPixPat: PixPatHandle */
        uint32_t h = new_pixpat(rt, PIXMAP, 0, 8);
        wr(rt, 0x220, 2, h ? 0 : (uint16_t)-108);
        wr(rt, sp, 4, h);
        return 1;
    }
    case 0xaa08: return dispose(rt, sp);
    case 0xaa0c: return get_pixpat(rt, sp);
    case 0xaa0d: return make_rgb(rt, sp);
    default: return fill(rt, sp, opcode);
    }
}
