#include "mac_runtime.h"

static int valid(MacCpu *cpu, uint32_t address, size_t size) {
    if ((address & 1) || address > mac_cpu_ram_size(cpu) ||
        size > mac_cpu_ram_size(cpu) - address) {
        mac_cpu_halt(cpu, "Geometry argument exceeds guest RAM or is unaligned.");
        return 0;
    }
    return 1;
}

/* Independently implemented from the public PtInRect interface. Points are
 * passed by value (vertical, horizontal); rectangles are passed by address.
 * Bounds enclose pixels: top/left inclusive, bottom/right exclusive. */
int mac_geometry_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa8aa && opcode != 0xa8ab && opcode != 0xa8ad && opcode != 0xa8a8 && opcode != 0xa880 && opcode != 0xa8a6) return 0;
    MacCpu *cpu = rt->cpu;
    if (mac_cpu_halted(cpu)) return -1;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    if (opcode == 0xa8aa) { /* SectRect(src1,src2,VAR dst): Pascal reverse order.
        * Public contract: QuickDraw Drawing, rectangle calculations. */
        if (!valid(cpu,sp,14)) return -1;
        uint32_t dst=mac_cpu_read(cpu,sp,4), b=mac_cpu_read(cpu,sp+4,4), a=mac_cpu_read(cpu,sp+8,4);
        if (!a || !b || !dst) {mac_cpu_halt(cpu,"SectRect requires non-NIL rectangles.");return -1;}
        if (!valid(cpu,a,8) || !valid(cpu,b,8) || !valid(cpu,dst,8)) return -1;
        int16_t edges[4];
        for(unsigned i=0;i<4;i++) {
            int16_t av=(int16_t)mac_cpu_read(cpu,a+2*i,2),bv=(int16_t)mac_cpu_read(cpu,b+2*i,2);
            edges[i]=i<2?(av>bv?av:bv):(av<bv?av:bv);
        }
        unsigned intersects=edges[0]<edges[2] && edges[1]<edges[3];
        /* Snapshot all source edges before writing; destination may be source. */
        for(unsigned i=0;i<4;i++)mac_cpu_write(cpu,dst+2*i,2,intersects?(uint16_t)edges[i]:0);
        mac_cpu_write(cpu,sp+12,1,intersects);mac_cpu_set(cpu,MAC_A7,sp+12);return 1;
    }
    if (opcode == 0xa8ab) { /* UnionRect(src1,src2,VAR dst): the smallest rectangle enclosing both, empty ones included (my reading of the
        * public contract: no special case for an empty source). dst may be a source. Pascal reverse order, no result. */
        if (!valid(cpu,sp,12)) return -1;
        uint32_t dst=mac_cpu_read(cpu,sp,4), b=mac_cpu_read(cpu,sp+4,4), a=mac_cpu_read(cpu,sp+8,4);
        if (!a || !b || !dst) {mac_cpu_halt(cpu,"UnionRect requires non-NIL rectangles.");return -1;}
        if (!valid(cpu,a,8) || !valid(cpu,b,8) || !valid(cpu,dst,8)) return -1;
        int16_t edges[4];
        for(unsigned i=0;i<4;i++) {
            int16_t av=(int16_t)mac_cpu_read(cpu,a+2*i,2),bv=(int16_t)mac_cpu_read(cpu,b+2*i,2);
            edges[i]=i<2?(av<bv?av:bv):(av>bv?av:bv);
        }
        for(unsigned i=0;i<4;i++)mac_cpu_write(cpu,dst+2*i,2,(uint16_t)edges[i]);
        mac_cpu_set(cpu,MAC_A7,sp+12);return 1;
    }
    if (opcode == 0xa880) { /* SetPt(VAR pt; h,v), Pascal reverse order.
        * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-57.html */
        if (!valid(cpu, sp, 8)) return -1;
        uint32_t point = mac_cpu_read(cpu, sp + 4, 4);
        uint32_t coordinates = mac_cpu_read(cpu, sp, 4);
        if (!point) { mac_cpu_halt(cpu, "SetPt requires a non-NIL point."); return -1; }
        if (!valid(cpu, point, 4)) return -1;
        /* Snapshot before writing: the destination may overlap arguments. */
        mac_cpu_write(cpu, point, 4, coordinates);
        mac_cpu_set(cpu, MAC_A7, sp + 8);
        return 1;
    }
    if (opcode == 0xa8a6) { /* EqualRect(rect1, rect2): BOOLEAN. True when all four edges are equal, so two empty rectangles
        * with different edges are not equal. Stack: rect2, rect1, result slot (Pascal reverse order). */
        if (!valid(cpu, sp, 10)) return -1;
        uint32_t b = mac_cpu_read(cpu, sp, 4), a = mac_cpu_read(cpu, sp + 4, 4);
        if (!a || !b) { mac_cpu_halt(cpu, "EqualRect requires non-NIL rectangles."); return -1; }
        if (!valid(cpu, a, 8) || !valid(cpu, b, 8)) return -1;
        unsigned same = 1;
        for (unsigned i = 0; i < 4; i++)
            if (mac_cpu_read(cpu, a + i * 2, 2) != mac_cpu_read(cpu, b + i * 2, 2)) same = 0;
        mac_cpu_write(cpu, sp + 8, 1, same);
        mac_cpu_set(cpu, MAC_A7, sp + 8);
        return 1;
    }
    if (opcode == 0xa8a8) { /* OffsetRect(VAR r; dh,dv): Pascal reverse order. */
        if (!valid(cpu, sp, 8)) return -1;
        uint16_t dv = (uint16_t)mac_cpu_read(cpu, sp, 2);
        uint16_t dh = (uint16_t)mac_cpu_read(cpu, sp + 2, 2);
        uint32_t rect = mac_cpu_read(cpu, sp + 4, 4);
        if (!valid(cpu, rect, 8)) return -1;
        uint16_t edges[4];
        for (unsigned i = 0; i < 4; i++)
            edges[i] = (uint16_t)mac_cpu_read(cpu, rect + i * 2, 2);
        /* Word addition wraps exactly at 16 bits, including negative deltas.
         * Snapshot all arguments/edges before writing, allowing stack overlap. */
        for (unsigned i = 0; i < 4; i++)
            mac_cpu_write(cpu, rect + i * 2, 2, (uint16_t)(edges[i] + (i & 1 ? dh : dv)));
        mac_cpu_set(cpu, MAC_A7, sp + 8);
        return 1;
    }

    if (!valid(cpu, sp, 10)) return -1;
    uint32_t rect = mac_cpu_read(cpu, sp, 4);
    if (!valid(cpu, rect, 8)) return -1;
    int16_t y = (int16_t)mac_cpu_read(cpu, sp + 4, 2);
    int16_t x = (int16_t)mac_cpu_read(cpu, sp + 6, 2);
    int16_t top = (int16_t)mac_cpu_read(cpu, rect, 2);
    int16_t left = (int16_t)mac_cpu_read(cpu, rect + 2, 2);
    int16_t bottom = (int16_t)mac_cpu_read(cpu, rect + 4, 2);
    int16_t right = (int16_t)mac_cpu_read(cpu, rect + 6, 2);
    unsigned inside = y >= top && y < bottom && x >= left && x < right;
    mac_cpu_write(cpu, sp + 8, 1, inside);
    mac_cpu_set(cpu, MAC_A7, sp + 8);
    return 1;
}
