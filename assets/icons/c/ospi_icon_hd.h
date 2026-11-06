/* OS Pi HiDPI icon set loader (ospi-icons@2x.rgba / @4x.rgba, OSPIHD01).
 *
 * Header: "OSPIHD01", count (u32 BE), size (u32 BE).
 * Each record: 24-byte name, then size*size RGBA pixels (straight alpha),
 * row-major. @2x is 64 px and @4x is 128 px, for 32-point desktop icons on
 * 2x and 4x framebuffers.
 */
#ifndef OSPI_ICON_HD_H
#define OSPI_ICON_HD_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[24];
    const uint8_t *rgba;
} OspiHdIcon;

typedef struct {
    OspiHdIcon *icons;
    uint8_t *pixels;
    size_t count;
    int size;
} OspiHdIconSet;

int ospi_hd_icon_set_load(OspiHdIconSet *set, const char *path);
void ospi_hd_icon_set_free(OspiHdIconSet *set);
/* Returns size*size RGBA pixels, or NULL if the name is unknown. */
const uint8_t *ospi_hd_icon_find(const OspiHdIconSet *set, const char *name);
/* Packs one pixel as 0xAARRGGBB, matching the 1x framebuffer colors. */
uint32_t ospi_hd_icon_argb(const uint8_t *rgba, int size, int x, int y);

#endif
