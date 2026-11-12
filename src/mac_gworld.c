#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Offscreen graphics worlds (QDOffscreen, trap $AB1D, selector in D0 as a long: the high word is the argument bytes, the low word the routine).
 * A GWorld is a colour port (a CGrafPort made by the existing OpenCPort service) whose PixMap points at pixel storage taken from the Memory
 * Manager. Public interface only; the routine numbers are from memory of the QDOffscreen interface.
 *
 *   0 NewGWorld  1 LockPixels  2 UnlockPixels  3 UpdateGWorld  4 DisposeGWorld  5 GetGWorld  6 SetGWorld  7 CTabChanged  8 PixPatChanged
 *   9 PortChanged  A GDeviceChanged  B AllowPurgePixels  C NoPurgePixels  D GetPixelsState  E SetPixelsState  F GetPixBaseAddr
 *   12 GetGWorldDevice  13 QDDone  16 PixMap32Bit  17 GetGWorldPixMap
 *
 * Depths 1, 2, 4, 8, 16 and 32 can be created (direct pixels are 5-5-5 at 16 and 8-8-8 at 32, rows padded to 4 bytes). What can then be drawn
 * into them is whatever the rest of QuickDraw supports on that format: indexed 8-bit ports for drawing, and CopyBits in every depth here.
 * Limits, all named when hit: a GWorld has no GDevice of its own (the main device stands in for it), purging and keepLocal flags are ignored,
 * the pixels are cleared to zero (not to any colour), and DisposeGWorld hands the pixels and the port block back to the Memory Manager but
 * that allocator is a bump allocator, so the address range is not reused (neither are the port's records). A GWorld's colour table is a copy of the screen's at 8 bits (same seed, so CopyBits between them moves indices untouched),
 * the caller's table when one is given (a fresh seed), and a gray ramp at the other indexed depths. */
#define MAX_GWORLDS 64
#define PARAM_ERR (-50)
#define MEM_FULL (-108)
typedef struct { uint32_t port, pixels, size; int depth, width, height; } GW;
struct MacGWorlds { GW w[MAX_GWORLDS]; uint32_t seed; };

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
static int ok_range(MacRuntime *rt, uint32_t p, uint32_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }
void mac_gworld_destroy(MacRuntime *rt) { free(rt->gworlds); rt->gworlds = NULL; }

static GW *find(MacRuntime *rt, uint32_t port) {
    if (!rt->gworlds || !port) return NULL;
    for (unsigned i = 0; i < MAX_GWORLDS; i++) if (rt->gworlds->w[i].port == port) return &rt->gworlds->w[i];
    return NULL;
}
/* Memory Manager NewPtr/DisposePtr through the service, so the allocator's own limits and bookkeeping apply. */
static uint32_t new_ptr(MacRuntime *rt, uint32_t size) {
    uint32_t a0 = mac_cpu_get(rt->cpu, MAC_A0), d0 = mac_cpu_get(rt->cpu, MAC_D0);
    mac_cpu_set(rt->cpu, MAC_D0, size);
    mac_memory_service(rt, 0xa11e);
    uint32_t p = mac_cpu_get(rt->cpu, MAC_A0); int16_t err = (int16_t)rd(rt, 0x220, 2);
    mac_cpu_set(rt->cpu, MAC_A0, a0); mac_cpu_set(rt->cpu, MAC_D0, d0);
    return err ? 0 : p;
}
static void dispose_ptr(MacRuntime *rt, uint32_t p) {
    if (!p) return;
    uint32_t a0 = mac_cpu_get(rt->cpu, MAC_A0);
    mac_cpu_set(rt->cpu, MAC_A0, p); mac_memory_service(rt, 0xa01f); mac_cpu_set(rt->cpu, MAC_A0, a0);
}
static int is_gw_pixmap(MacRuntime *rt, uint32_t h) {
    if (!rt->gworlds || !h) return 0;
    for (unsigned i = 0; i < MAX_GWORLDS; i++) if (rt->gworlds->w[i].port && rd(rt, rt->gworlds->w[i].port + 2, 4) == h) return 1;
    return 0;
}
static unsigned free_slots(MacRuntime *rt) { unsigned n = 0; for (unsigned i = 0; i < MAC_RUNTIME_HANDLES; i++) if (!rt->handles[i].handle && !rt->handles[i].pointer) n++; return n; }

typedef struct { int depth, width, height; unsigned row; uint32_t bytes; int top, left; } Geo;
/* Reads the request; 0 on success, else a QDErr. `bounds` points at a Rect. */
static int geometry(MacRuntime *rt, int depth, uint32_t bounds, Geo *g) {
    if (!ok_range(rt, bounds, 8)) return PARAM_ERR;
    int t = (int16_t)rd(rt, bounds, 2), l = (int16_t)rd(rt, bounds + 2, 2), b = (int16_t)rd(rt, bounds + 4, 2), r = (int16_t)rd(rt, bounds + 6, 2);
    if (!depth) depth = rt->screen_depth ? (int)rt->screen_depth : 8;
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 32) return PARAM_ERR;
    if (b <= t || r <= l) return PARAM_ERR;
    g->depth = depth; g->width = r - l; g->height = b - t; g->top = t; g->left = l;
    g->row = (((unsigned)g->width * (unsigned)depth + 31u) / 32u) * 4u;
    uint64_t bytes = (uint64_t)g->row * (unsigned)g->height;
    if (bytes > 64u * 1024u * 1024u || g->row > 0x3fff) return MEM_FULL;
    g->bytes = (uint32_t)bytes;
    return 0;
}
/* Colour table of the PixMap `pm` for a GWorld of this depth: the caller's table, the screen's copy (8 bits), or a gray ramp. 0 done, 1 the caller's table is unusable, -1 halted. */
static int set_table(MacRuntime *rt, GW *w, uint32_t pm, uint32_t ctab) {
    uint32_t th = rd(rt, pm + 42, 4), tp = th ? rd(rt, th, 4) : 0;
    if (!tp || !ok_range(rt, tp, 2056)) return fail(rt, "A GWorld's colour table is missing.");
    if (!rt->gworlds->seed) rt->gworlds->seed = 0x40000000u;
    if (w->depth > 8) { wr(rt, tp + 4, 2, 0); wr(rt, tp + 6, 2, 0xffff); return 0; }
    if (ctab) {
        uint32_t cp = ok_range(rt, ctab, 4) ? rd(rt, ctab, 4) : 0;
        if (!cp || !ok_range(rt, cp, 8)) return 1;
        unsigned n = rd(rt, cp + 6, 2) + 1u;
        if (n > 256 || n > (1u << w->depth) || !ok_range(rt, cp, 8 + n * 8)) return 1;
        memset(mac_cpu_ram(rt->cpu) + tp + 8, 0, 2048);
        memcpy(mac_cpu_ram(rt->cpu) + tp + 8, mac_cpu_ram(rt->cpu) + cp + 8, n * 8);
        wr(rt, tp, 4, rt->gworlds->seed++); wr(rt, tp + 4, 2, 0); wr(rt, tp + 6, 2, n - 1);
        return 0;
    }
    if (w->depth == 8) return 0;                                    /* the copy of the screen's table OpenCPort made, same seed */
    unsigned n = 1u << w->depth;
    for (unsigned i = 0; i < n; i++) {
        unsigned v = 0xffff - (n > 1 ? i * 0xffffu / (n - 1) : 0);
        wr(rt, tp + 8 + i * 8, 2, i);
        for (unsigned k = 0; k < 3; k++) wr(rt, tp + 10 + i * 8 + k * 2, 2, v);
    }
    wr(rt, tp, 4, rt->gworlds->seed++); wr(rt, tp + 4, 2, 0); wr(rt, tp + 6, 2, n - 1);
    return 0;
}
static void set_pixmap(MacRuntime *rt, uint32_t port, uint32_t pm, const Geo *g, uint32_t pixels) {
    int dir = g->depth > 8;
    wr(rt, pm, 4, pixels); wr(rt, pm + 4, 2, 0x8000u | g->row);
    wr(rt, pm + 6, 2, (uint16_t)g->top); wr(rt, pm + 8, 2, (uint16_t)g->left);
    wr(rt, pm + 10, 2, (uint16_t)(g->top + g->height)); wr(rt, pm + 12, 2, (uint16_t)(g->left + g->width));
    wr(rt, pm + 14, 2, 0); wr(rt, pm + 16, 2, 0); wr(rt, pm + 18, 4, 0);
    wr(rt, pm + 22, 4, 0x00480000); wr(rt, pm + 26, 4, 0x00480000);
    wr(rt, pm + 30, 2, dir ? 16 : 0); wr(rt, pm + 32, 2, (unsigned)g->depth);
    wr(rt, pm + 34, 2, dir ? 3 : 1); wr(rt, pm + 36, 2, dir ? (g->depth == 16 ? 5 : 8) : (unsigned)g->depth);
    wr(rt, pm + 38, 4, 0); wr(rt, pm + 46, 4, 0);
    wr(rt, port + 16, 2, (uint16_t)g->top); wr(rt, port + 18, 2, (uint16_t)g->left);
    wr(rt, port + 20, 2, (uint16_t)(g->top + g->height)); wr(rt, port + 22, 2, (uint16_t)(g->left + g->width));
    uint32_t vr = rd(rt, port + 24, 4), vp = vr ? rd(rt, vr, 4) : 0;
    if (vp && ok_range(rt, vp, 10)) { wr(rt, vp + 2, 2, (uint16_t)g->top); wr(rt, vp + 4, 2, (uint16_t)g->left); wr(rt, vp + 6, 2, (uint16_t)(g->top + g->height)); wr(rt, vp + 8, 2, (uint16_t)(g->left + g->width)); }
}
static int open_color_port(MacRuntime *rt, uint32_t port) {   /* OpenCPort makes the new port current; NewGWorld must not */
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), cur = rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0, saved = rt->current_port;
    wr(rt, sp - 4, 4, port); mac_cpu_set(rt->cpu, MAC_A7, sp - 4);
    int rc = mac_color_port_service(rt, 0xaa00);
    mac_cpu_set(rt->cpu, MAC_A7, sp);
    if (rt->qd_globals) wr(rt, rt->qd_globals, 4, cur);
    rt->current_port = saved;
    return rc;
}
static int close_color_port(MacRuntime *rt, uint32_t port) {
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    wr(rt, sp - 4, 4, port); mac_cpu_set(rt->cpu, MAC_A7, sp - 4);
    int rc = mac_color_port_service(rt, 0xaa02);
    mac_cpu_set(rt->cpu, MAC_A7, sp);
    return rc;
}
static void finish(MacRuntime *rt, uint32_t sp, unsigned pop) { mac_cpu_set(rt->cpu, MAC_A7, sp + pop); }

/* NewGWorld(VAR gworld; depth; bounds; cTable; gdevice; flags): QDErr. Stack from SP: flags, gdevice, cTable, bounds, depth, VAR, result. */
static int new_gworld(MacRuntime *rt, uint32_t sp) {
    uint32_t ctab = rd(rt, sp + 8, 4), bounds = rd(rt, sp + 12, 4), var = rd(rt, sp + 18, 4);
    int depth = (int16_t)rd(rt, sp + 16, 2);
    if (!ok_range(rt, var, 4)) return fail(rt, "NewGWorld: the result pointer is invalid.");
    wr(rt, var, 4, 0);
    if (!rt->gworlds && !(rt->gworlds = calloc(1, sizeof *rt->gworlds))) return fail(rt, "NewGWorld: no memory for the registry.");
    Geo g; int err = geometry(rt, depth, bounds, &g);
    GW *slot = NULL;
    for (unsigned i = 0; !err && i < MAX_GWORLDS; i++) if (!rt->gworlds->w[i].port) { slot = &rt->gworlds->w[i]; break; }
    if (!err && !slot) err = MEM_FULL;
    if (!err && free_slots(rt) < 14) err = MEM_FULL;
    uint32_t port = 0, pixels = 0;
    if (!err) {
        port = new_ptr(rt, 108); pixels = port ? new_ptr(rt, g.bytes) : 0;
        if (!port || !pixels) err = MEM_FULL;
    }
    if (!err) {
        if (!rt->qd_globals) return fail(rt, "NewGWorld needs initialised QuickDraw.");
        memset(mac_cpu_ram(rt->cpu) + port, 0, 108);
        if (open_color_port(rt, port) != 1) return -1;
        uint32_t pm = rd(rt, rd(rt, port + 2, 4), 4);
        GW w = {.port = port, .pixels = pixels, .size = g.bytes, .depth = g.depth, .width = g.width, .height = g.height};
        set_pixmap(rt, port, pm, &g, pixels);
        int terr = set_table(rt, &w, pm, ctab);
        if (terr < 0) return -1;
        if (terr) { err = PARAM_ERR; if (close_color_port(rt, port) != 1) return -1; }
        else { memset(mac_cpu_ram(rt->cpu) + pixels, 0, g.bytes); *slot = w; wr(rt, var, 4, port); }
    }
    if (err) { dispose_ptr(rt, pixels); dispose_ptr(rt, port); }
    wr(rt, sp + 22, 2, (uint16_t)err); wr(rt, 0x220, 2, (uint16_t)err);
    finish(rt, sp, 22); return 1;
}
/* UpdateGWorld(VAR gworld; depth; bounds; cTable; gdevice; flags): GWorldFlags. Stack as NewGWorld, result is a long. New pixels replace the
 * old ones when the shape changes (the contents are not carried over). The answer is 0, or $80000000 (gwFlagErr) when it could not be done. */
static int update_gworld(MacRuntime *rt, uint32_t sp) {
    uint32_t ctab = rd(rt, sp + 8, 4), bounds = rd(rt, sp + 12, 4), var = rd(rt, sp + 18, 4);
    int depth = (int16_t)rd(rt, sp + 16, 2);
    if (!ok_range(rt, var, 4)) return fail(rt, "UpdateGWorld: the GWorld pointer is invalid.");
    GW *w = find(rt, rd(rt, var, 4));
    if (!w) return fail(rt, "UpdateGWorld: the pointer is not a GWorld made by NewGWorld.");
    Geo g; uint32_t result = 0;
    if (geometry(rt, depth, bounds, &g)) result = 0x80000000u;
    else if (g.depth != w->depth || g.width != w->width || g.height != w->height || g.bytes != w->size || ctab) {
        uint32_t pixels = new_ptr(rt, g.bytes);
        if (!pixels) result = 0x80000000u;
        else {
            memset(mac_cpu_ram(rt->cpu) + pixels, 0, g.bytes);
            uint32_t pm = rd(rt, rd(rt, w->port + 2, 4), 4);
            GW n = *w; n.depth = g.depth;
            set_pixmap(rt, w->port, pm, &g, pixels);
            int terr = set_table(rt, &n, pm, ctab);
            if (terr < 0) return -1;
            dispose_ptr(rt, w->pixels);
            w->pixels = pixels; w->size = g.bytes; w->depth = g.depth; w->width = g.width; w->height = g.height;
            if (terr) result = 0x80000000u;
        }
    } else {                                                       /* same shape: only the origin may move */
        uint32_t pm = rd(rt, rd(rt, w->port + 2, 4), 4);
        set_pixmap(rt, w->port, pm, &g, w->pixels);
    }
    wr(rt, sp + 22, 4, result); finish(rt, sp, 22); return 1;
}
static int dispose_gworld(MacRuntime *rt, uint32_t sp) {
    uint32_t port = rd(rt, sp, 4); GW *w = find(rt, port);
    if (!w) return fail(rt, "DisposeGWorld: the pointer is not a GWorld made by NewGWorld.");
    uint32_t cur = rt->qd_globals ? rd(rt, rt->qd_globals, 4) : 0;
    if (cur == port) return fail(rt, "DisposeGWorld: the GWorld is the current port.");
    if (close_color_port(rt, port) != 1) return -1;
    dispose_ptr(rt, w->pixels); dispose_ptr(rt, port);
    memset(w, 0, sizeof *w);
    wr(rt, 0x220, 2, 0); finish(rt, sp, 4); return 1;
}
static int set_gworld(MacRuntime *rt, uint32_t sp) {
    uint32_t gd = rd(rt, sp, 4), port = rd(rt, sp + 4, 4);
    if (!ok_range(rt, port, 108) || !rt->qd_globals) return fail(rt, "SetGWorld: the port is invalid or QuickDraw is not initialised.");
    if (!find(rt, port) && gd && gd != rt->main_device) return fail(rt, "SetGWorld: only the main graphics device exists here.");
    wr(rt, rt->qd_globals, 4, port); rt->current_port = port;
    if (rt->main_device) rt->current_device = rt->main_device;
    finish(rt, sp, 8); return 1;
}
static int get_gworld(MacRuntime *rt, uint32_t sp) {
    uint32_t gd = rd(rt, sp, 4), port = rd(rt, sp + 4, 4);
    if (!ok_range(rt, gd, 4) || !ok_range(rt, port, 4) || !rt->qd_globals) return fail(rt, "GetGWorld: invalid result pointers or QuickDraw is not initialised.");
    wr(rt, port, 4, rd(rt, rt->qd_globals, 4)); wr(rt, gd, 4, rt->current_device);
    finish(rt, sp, 8); return 1;
}

int mac_gworld_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xab1d) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sel = mac_cpu_get(rt->cpu, MAC_D0), sp = mac_cpu_get(rt->cpu, MAC_A7);
    unsigned routine = sel & 0xffff, args = sel >> 16;
    if ((sp & 1) || !ok_range(rt, sp, args + 4)) return fail(rt, "Offscreen call has an invalid stack.");
    /* Pixel-memory calls on a PixMap that is not a GWorld's belong to mac_pixels.c (NewPixMap with NewPtr storage). */
    if ((routine == 0x1 || routine == 0x2 || routine == 0xc || routine == 0xf) && args == 4 && !is_gw_pixmap(rt, rd(rt, sp, 4))) return 0;
    switch (routine) {
    case 0x0: if (args == 22) return new_gworld(rt, sp); break;
    case 0x3: if (args == 22) return update_gworld(rt, sp); break;
    case 0x4: if (args == 4) return dispose_gworld(rt, sp); break;
    case 0x5: if (args == 8) return get_gworld(rt, sp); break;
    case 0x6: if (args == 8) return set_gworld(rt, sp); break;
    case 0x1: case 0x13: case 0x16:                                       /* LockPixels, QDDone, PixMap32Bit: Boolean */
        if (args == 4) { wr(rt, sp + 4, 2, 0x0100); finish(rt, sp, 4); return 1; }
        break;
    case 0x2: case 0x7: case 0x8: case 0x9: case 0xa: case 0xb: case 0xc:   /* UnlockPixels, the *Changed calls, AllowPurge, NoPurge: nothing to do */
        if (args == 4) { finish(rt, sp, 4); return 1; }
        break;
    case 0xe: if (args == 8) { finish(rt, sp, 8); return 1; } break;       /* SetPixelsState */
    case 0xd: if (args == 4) { wr(rt, sp + 4, 4, 0); finish(rt, sp, 4); return 1; } break;   /* GetPixelsState: no flags */
    case 0xf: case 0x17: case 0x12:
        if (args == 4) {
            uint32_t a = rd(rt, sp, 4), v = 0;
            if (routine == 0x12) v = rt->main_device;
            else if (routine == 0x17) { if (!find(rt, a)) return fail(rt, "GetGWorldPixMap: the pointer is not a GWorld made by NewGWorld."); v = rd(rt, a + 2, 4); }
            else { uint32_t p = ok_range(rt, a, 4) ? rd(rt, a, 4) : 0; if (!p || !ok_range(rt, p, 50)) return fail(rt, "GetPixBaseAddr: the PixMap handle is invalid."); v = rd(rt, p, 4); }
            wr(rt, sp + 4, 4, v); finish(rt, sp, 4); return 1;
        }
        break;
    }
    char why[96];
    snprintf(why, sizeof why, "Offscreen selector %08X is unsupported.", (unsigned)sel);
    return fail(rt, why);
}
