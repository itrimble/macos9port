#ifndef MAC_CICN_H
#define MAC_CICN_H
#include "mac_runtime.h"
/* The drawing target of a colour port: its indexed-8 pixels (base of the PixMap's bounds), and the clip box (portRect, PixMap bounds, visRgn and clipRgn
 * rectangles intersected). Defined in mac_color_raster.c. Returns 1, 0 when the port is not a colour port, -1 after halting (complex regions, other depths). */
typedef struct { uint8_t *pixels; int row; int bt, bl, bb, br; int ct, cl, cb, cr; } MacColorTarget;
int mac_color_target(MacRuntime *rt, uint32_t port, MacColorTarget *out);

/* Colour icons ('cicn'): GetCIcon $AA1E, PlotCIcon $AA1F, DisposeCIcon $AA25 (mac_cicn.c). 1 handled, 0 not ours, -1 halted. */
int mac_cicn_service(MacRuntime *rt, uint16_t opcode);
/* A validated view of a 'cicn' resource (its own bytes; every size and offset comes from the resource and is checked against `n`). */
typedef struct {
    int width, height, depth;               /* pixels and bits per pixel (1, 2, 4 or 8) */
    unsigned pix_row, mask_row;             /* bytes per row of the pixel data and of the 1-bit mask (0: no mask, everything is opaque) */
    const uint8_t *pixels, *mask;
    unsigned colors; const uint8_t *table;  /* ColorSpec entries: value (2), red, green, blue (2 each) */
    int device_table;                       /* ctFlags bit 15: entries are indexed by position, not by value */
} MacCicn;
int mac_cicn_parse(const uint8_t *d, size_t n, MacCicn *out, const char **why);   /* 0 ok, -1 with *why */
#endif
