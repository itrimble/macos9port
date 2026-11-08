#include "mac_runtime.h"

#include <limits.h>
#include <string.h>

/* Independent cursor state and original replacement artwork. ABI reference:
 * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-391.html
 * Cursor is 32 data bytes, 32 mask bytes, then signed (vertical,horizontal).
 * The frontend must consume this state; this module does not draw a cursor. */
static int fail(MacRuntime *rt, const char *message) {
    mac_cpu_halt(rt->cpu, message);
    return -1;
}

static int range(MacRuntime *rt, uint32_t p, size_t n) {
    size_t size = mac_cpu_ram_size(rt->cpu);
    return !(p & 1) && p <= size && n <= size - p;
}

/* The standard cursors a program expects the System to supply: 1 I-beam, 2 crosshair, 3 plus, 4 watch (hourglass). The pixels are authored here
 * (simple geometric shapes), not taken from a Macintosh asset; hot spots are the shape centres. */
static uint32_t standard_cursor(MacRuntime *rt, int16_t id) {
    const uint32_t type = MAC_RESOURCE_TYPE('C','U','R','S');
    size_t slot = 0;
    while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    if (slot == MAC_RUNTIME_HANDLES) return 0;
    uint32_t saved = rt->heap_next;
    uint32_t h = mac_runtime_alloc(rt, 4), p = mac_runtime_alloc(rt, 68);
    if (!h || !p) { rt->heap_next = saved; return 0; }
    /* Original geometric hourglass: two horizontal bars and converging sides.
     * These pixels were authored here, not taken from a Macintosh asset. */
    uint8_t *cursor = mac_cpu_ram(rt->cpu) + p;
    for (unsigned y = 0; y < 16; y++) {
        uint16_t bits = 0;
        if (id == 4) {
            if (y >= 2 && y <= 13) {
                unsigned inset = y <= 7 ? y - 2 : 13 - y;
                bits = y == 2 || y == 13 ? 0x3ffc : (uint16_t)((0x2000u >> inset) | (4u << inset));
            }
        } else if (id == 1) bits = y == 0 || y == 15 ? 0x07e0 : 0x0180;                              /* I-beam: serifs and a stem */
        else if (id == 2) bits = y == 7 ? 0x7ffe : (y >= 1 && y <= 14) ? 0x0100 : 0;                  /* crosshair: thin cross */
        else bits = (y >= 6 && y <= 9) ? 0x3ffc : (y >= 2 && y <= 13) ? 0x0180 : 0;                   /* plus: thick cross */
        cursor[y * 2] = (uint8_t)(bits >> 8);
        cursor[y * 2 + 1] = (uint8_t)bits;
    }
    memcpy(cursor + 32, cursor, 32);
    cursor[65] = 7; cursor[67] = 7;
    mac_cpu_write(rt->cpu, h, 4, p);
    rt->handles[slot] = (MacHandle){.handle=h, .pointer=p, .size=68,
        .type=type, .id=id, .resource=1};
    if (slot == rt->handle_count) rt->handle_count++;
    return h;
}

int mac_cursor_service(MacRuntime *rt, uint16_t opcode) {
    unsigned args;
    switch (opcode) {
    case 0xa9b9: args = 2; break; /* GetCursor */
    case 0xa851: args = 4; break; /* SetCursor */
    case 0xa852: case 0xa853: case 0xa856: args = 0; break;
    case 0xa855: args = 8; break; /* ShieldCursor(shieldRect: Rect pointer; offsetPt: Point) */
    default: return 0;
    }
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!range(rt, sp, args + (opcode == 0xa9b9 ? 4 : 0)))
        return fail(rt, "Cursor call has an invalid stack range.");
    if (opcode == 0xa9b9) {
        int16_t id = (int16_t)mac_cpu_read(rt->cpu, sp, 2);
        uint32_t type = MAC_RESOURCE_TYPE('C','U','R','S');
        uint32_t h = mac_runtime_resource(rt, type, id);
        MacResource resource;
        int absent = mac_resource_find(&rt->fork, type, id, &resource) != 0;
        int standard = id >= 1 && id <= 4;
        if (!h && absent && standard) h = standard_cursor(rt, id);
        int error = h ? 0 : absent && !standard ? -192 : -108;
        mac_cpu_write(rt->cpu, sp + 2, 4, h);
        mac_cpu_write(rt->cpu, 0xa60, 2, (uint16_t)error);
    } else if (opcode == 0xa851) {
        uint32_t p = mac_cpu_read(rt->cpu, sp, 4);
        if (!p || !range(rt, p, sizeof(rt->cursor)))
            return fail(rt, "SetCursor requires a valid 68-byte Cursor pointer.");
        memcpy(rt->cursor, mac_cpu_ram(rt->cpu) + p, sizeof(rt->cursor));
        rt->cursor_valid = 1;
    } else if (opcode == 0xa855) {
        /* The cursor is composed by the host above the guest's screen memory and never drawn into it, so there are no pixels to
         * protect while the guest draws under the pointer: this validates the rectangle pointer and changes nothing (the hide
         * level and the obscured flag are untouched). */
        uint32_t r = mac_cpu_read(rt->cpu, sp + 4, 4);
        if (!r || !range(rt, r, 8)) return fail(rt, "ShieldCursor requires a valid Rect pointer.");
    } else if (opcode == 0xa856) {
        rt->cursor_obscured = 1;
    } else {
        int level = (int16_t)mac_cpu_read(rt->cpu, 0x8d0, 2);
        if (opcode == 0xa852) {
            if (level == INT16_MIN) return fail(rt, "Cursor hide nesting overflow.");
            level--;
        } else {
            if (level < 0) level++;
        }
        mac_cpu_write(rt->cpu, 0x8d0, 2, (uint16_t)level);
    }
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : 1;
}
