/* OS Pi icon set loader (ospi-icons.icl8, format OSPIICN1).
 *
 * Each record: 24-byte name, 32x32 8-bit icon (system CLUT indices),
 * 128-byte 32x32 mask (MSB first), 16x16 8-bit icon, 32-byte 16x16 mask.
 * The 32x32 data matches Item.guest_icon[1024] / guest_icon_mask[128].
 */
#ifndef OSPI_ICON_SET_H
#define OSPI_ICON_SET_H

#include <stddef.h>
#include <stdint.h>

#define OSPI_ICON_NAME_LEN 24

typedef struct {
    char name[OSPI_ICON_NAME_LEN];
    uint8_t icl8[1024];
    uint8_t mask32[128];
    uint8_t ics8[256];
    uint8_t mask16[32];
} OspiIcon;

typedef struct {
    OspiIcon *icons;
    size_t count;
} OspiIconSet;

int ospi_icon_set_load(OspiIconSet *set, const char *path);
void ospi_icon_set_free(OspiIconSet *set);
const OspiIcon *ospi_icon_find(const OspiIconSet *set, const char *name);
/* Copies a 32x32 icon into guest-icon style buffers; returns 1 if found. */
int ospi_icon_copy(const OspiIconSet *set, const char *name,
                   uint8_t icon[1024], uint8_t mask[128]);

#endif
