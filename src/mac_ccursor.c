#include "mac_runtime.h"
#include "mac_resource_chain.h"

#include <string.h>

/* Color cursors: GetCCursor ($AA1B), SetCCursor ($AA1C), AllocCursor ($AA1D),
 * DisposeCCursor ($AA26). Independent implementation from the public Imaging
 * With QuickDraw description of the 'crsr' resource and the CCrsr record:
 *   crsr resource (offsets from its start, big endian):
 *     0 crsrType (0x8000 b/w, 0x8001 color)  2 pixmap offset  6 pixel-data offset
 *     10 expanded data  14 expanded depth  16 reserved  20 1-bit data[32]
 *     52 mask[32]  84 hot spot (v,h)  88 reserved  92 cursor id   (96 bytes)
 *     then a 50-byte PixMap at the pixmap offset, whose pmTable is an offset to
 *     a color table (seed, flags, size-1, then 8-byte entries), then the pixels.
 *   CCrsr record in memory (96 bytes, same field order): crsrMap becomes a
 *     PixMapHandle, crsrData a Handle to the pixels, pmTable a CTabHandle.
 * LIMITATION: the runtime has one cursor path (the 1-bit Cursor in rt->cursor,
 * consumed by the frontend). SetCCursor therefore installs the resource's 1-bit
 * data, mask and hot spot; the color pixels are loaded, validated and kept in
 * the guest-visible record but are not drawn. */
#define CCRS MAC_RESOURCE_TYPE('c','C','U','R') /* tag for our handle slots */
#define CRSR MAC_RESOURCE_TYPE('c','r','s','r')
enum { HDR = 96, PIXMAP = 50, ERR_NOTFOUND = -192, ERR_MAP = -199, ERR_MEM = -108 };

static uint32_t be(const uint8_t *p, unsigned n) {
    uint32_t v = 0;
    while (n--) v = v << 8 | *p++;
    return v;
}
static void put(uint8_t *p, unsigned n, uint32_t v) {
    while (n--) *p++ = (uint8_t)(v >> (8 * n));
}
static int fail(MacRuntime *rt, const char *message) {
    mac_cpu_halt(rt->cpu, message);
    return -1;
}
static int range(MacRuntime *rt, uint32_t p, size_t n) {
    size_t size = mac_cpu_ram_size(rt->cpu);
    return !(p & 1) && p <= size && n <= size - p;
}

/* Returns 0 if the resource is well formed; fills the geometry used to build the record. */
typedef struct { uint32_t map, data, table, rows, row_bytes, depth, entries, pixels; } Layout;
static int parse(const uint8_t *r, size_t size, Layout *l) {
    if (size < HDR) return -1;
    unsigned type = be(r, 2);
    if (type != 0x8000 && type != 0x8001) return -1;
    l->map = be(r + 2, 4); l->data = be(r + 6, 4);
    if (l->map > size - PIXMAP) return -1;
    const uint8_t *pm = r + l->map;
    l->row_bytes = be(pm + 4, 2) & 0x3fff;
    int16_t top = (int16_t)be(pm + 6, 2), left = (int16_t)be(pm + 8, 2);
    int16_t bottom = (int16_t)be(pm + 10, 2), right = (int16_t)be(pm + 12, 2);
    l->depth = be(pm + 32, 2);
    if (bottom <= top || right <= left || bottom - top > 32 || right - left > 32) return -1;
    if (l->depth != 1 && l->depth != 2 && l->depth != 4 && l->depth != 8) return -1;
    l->rows = (uint32_t)(bottom - top);
    if (l->row_bytes < ((uint32_t)(right - left) * l->depth + 7) / 8) return -1;
    l->pixels = l->row_bytes * l->rows;
    if (l->data > size || l->pixels > size - l->data) return -1;
    l->table = be(pm + 42, 4);
    if (l->table > size || size - l->table < 8) return -1;
    l->entries = be(r + l->table + 6, 2) + 1u;
    if (l->entries > 256 || l->entries * 8u > size - l->table - 8) return -1;
    return 0;
}

static unsigned free_slots(MacRuntime *rt, size_t *slots, unsigned want) {
    unsigned found = 0;
    for (size_t i = 0; i < MAC_RUNTIME_HANDLES && found < want; i++)
        if (!rt->handles[i].handle && !rt->handles[i].pointer) slots[found++] = i;
    return found;
}

static int get_ccursor(MacRuntime *rt, uint32_t sp) {
    int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp, 2);
    MacResource res; int16_t owner = 0;
    uint32_t result = 0; int error = ERR_NOTFOUND;
    if (mac_resource_chain_find(rt, CRSR, id, 0, &res, &owner) == 0) {
        mac_resedit_apply(rt, owner, &res);
        Layout l; size_t slots[4];
        if (parse(res.data, res.size, &l)) error = ERR_MAP;
        else if (free_slots(rt, slots, 4) != 4) error = ERR_MEM;
        else {
            uint32_t table_size = 8 + l.entries * 8, pm_off = 16 + HDR;
            uint32_t table_off = pm_off + 52, pix_off = table_off + ((table_size + 3) & ~3u);
            uint32_t total = pix_off + ((l.pixels + 3) & ~3u);
            uint32_t base = mac_runtime_alloc_data(rt, total);
            if (!base || !range(rt, base, total)) { mac_runtime_free_data(rt, base); error = ERR_MEM; }
            else {
                uint8_t *m = mac_cpu_ram(rt->cpu) + base;
                memset(m, 0, total);
                uint32_t rec = base + 16, pm = base + pm_off, ct = base + table_off, px = base + pix_off;
                /* masters: ccursor, pixmap, pixel data, color table */
                uint32_t ptr[4] = {rec, pm, px, ct}, size[4] = {HDR, PIXMAP, l.pixels, table_size};
                for (unsigned i = 0; i < 4; i++) {
                    put(m + i * 4, 4, ptr[i]);
                    rt->handles[slots[i]] = (MacHandle){.handle = base + i * 4, .pointer = ptr[i],
                        .size = size[i], .type = i ? 0 : CCRS, .state = 0x80};
                    if (rt->handle_count <= slots[i]) rt->handle_count = slots[i] + 1;
                }
                memcpy(m + 16, res.data, HDR);
                put(m + 16 + 2, 4, base + 4);  /* crsrMap: PixMapHandle */
                put(m + 16 + 6, 4, base + 8);  /* crsrData: Handle */
                put(m + 16 + 10, 4, 0); put(m + 16 + 14, 2, 0); put(m + 16 + 16, 4, 0); /* no expanded data */
                put(m + 16 + 88, 4, 0); put(m + 16 + 92, 4, (uint32_t)(int32_t)id);
                memcpy(m + pm_off, res.data + l.map, PIXMAP);
                put(m + pm_off, 4, 0);          /* baseAddr is unused */
                put(m + pm_off + 42, 4, base + 12); /* pmTable: CTabHandle */
                memcpy(m + table_off, res.data + l.table, table_size);
                memcpy(m + pix_off, res.data + l.data, l.pixels);
                result = base + 0; error = 0;
            }
        }
    }
    mac_cpu_write(rt->cpu, sp + 2, 4, result);
    mac_cpu_write(rt->cpu, 0xa60, 2, (uint16_t)error);
    return 1;
}

static MacHandle *find(MacRuntime *rt, uint32_t handle) {
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].type == CCRS && rt->handles[i].handle == handle && handle) return &rt->handles[i];
    return NULL;
}

int mac_ccursor_service(MacRuntime *rt, uint16_t opcode) {
    unsigned args, result = 0;
    switch (opcode) {
    case 0xaa1b: args = 2; result = 4; break; /* GetCCursor */
    case 0xaa1c: case 0xaa26: args = 4; break; /* SetCCursor, DisposeCCursor */
    case 0xaa1d: args = 0; break;              /* AllocCursor */
    default: return 0;
    }
    if (mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!range(rt, sp, args + result)) return fail(rt, "Color cursor call has an invalid stack range.");
    if (opcode == 0xaa1b) get_ccursor(rt, sp);
    else if (opcode != 0xaa1d) {
        uint32_t h = mac_cpu_read(rt->cpu, sp, 4);
        MacHandle *rec = find(rt, h);
        if (!rec) {
            if (opcode == 0xaa26 && !h) { mac_cpu_set(rt->cpu, MAC_A7, sp + args); return 1; } /* nil is a no-op */
            return fail(rt, opcode == 0xaa1c ? "SetCCursor needs a handle from GetCCursor."
                                              : "DisposeCCursor needs a handle from GetCCursor.");
        }
        if (opcode == 0xaa1c) {
            if (!range(rt, rec->pointer, HDR)) return fail(rt, "SetCCursor record is outside guest RAM.");
            /* 1-bit fallback: data[32], mask[32], then the hot spot (v,h) already in Cursor order. */
            memcpy(rt->cursor, mac_cpu_ram(rt->cpu) + rec->pointer + 20, 68);
            rt->cursor_valid = 1;
        } else {
            uint32_t base = h; /* the four masters sit at the start of one block */
            for (size_t i = 0; i < rt->handle_count; i++)
                if (rt->handles[i].handle >= base && rt->handles[i].handle < base + 16)
                    rt->handles[i] = (MacHandle){0};
            mac_runtime_free_data(rt, base);
        }
    }
    /* AllocCursor: the real call reserves cursor buffers; this runtime holds none, so there is nothing to do. */
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : 1;
}
