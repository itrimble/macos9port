#ifndef MAC_PIXPAT_H
#define MAC_PIXPAT_H
#include "mac_cicn.h"
#include "mac_shape.h"
/* Pixel patterns (mac_pixpat.c). */
int mac_pixpat_service(MacRuntime *rt, uint16_t opcode);
/* What a PixPat handle paints with. 0: an old-style pattern, *pat1 is the guest address of its 8 bytes (painted in the port's colours). 1: a colour pattern,
 * expanded into *tile for the current device colours. -1 after halting (not a pixel pattern, or a form that is not supported). */
int mac_pixpat_read(MacRuntime *rt, uint32_t handle, uint32_t *pat1, MacPixTile *tile);
int mac_color_tile(MacRuntime *rt, uint32_t port, int t, int l, int b, int r, const MacPixTile *tile, const MacShape *outer); /* mac_color_raster.c */
#endif
