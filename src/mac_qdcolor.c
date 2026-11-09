#include "mac_qdcolor.h"

int mac_qd_color_constant(uint32_t color, unsigned rgb[3]) {
    static const struct { uint32_t c; unsigned r, g, b; } t[] = { {33,0,0,0},{30,255,255,255},{205,255,0,0},{341,0,255,0},{409,0,0,255},{273,0,255,255},{137,255,0,255},{69,255,255,0} };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (t[i].c == color) { rgb[0] = t[i].r; rgb[1] = t[i].g; rgb[2] = t[i].b; return 1; }
    return 0;
}
