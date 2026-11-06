#include "ospi_icon_hd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t be32(const unsigned char *p)
{
    return ((size_t)p[0] << 24) | ((size_t)p[1] << 16) | ((size_t)p[2] << 8) | (size_t)p[3];
}

int ospi_hd_icon_set_load(OspiHdIconSet *set, const char *path)
{
    unsigned char head[16];
    size_t count, size, px_bytes, i;
    FILE *f;

    memset(set, 0, sizeof *set);
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (fread(head, 1, sizeof head, f) != sizeof head || memcmp(head, "OSPIHD01", 8) != 0) {
        fclose(f);
        return 0;
    }
    count = be32(head + 8);
    size = be32(head + 12);
    if (count == 0 || count > 4096 || size == 0 || size > 1024) {
        fclose(f);
        return 0;
    }
    px_bytes = size * size * 4;
    set->icons = calloc(count, sizeof *set->icons);
    set->pixels = malloc(count * px_bytes);
    if (!set->icons || !set->pixels) {
        ospi_hd_icon_set_free(set);
        fclose(f);
        return 0;
    }
    for (i = 0; i < count; i++) {
        uint8_t *dst = set->pixels + i * px_bytes;
        if (fread(set->icons[i].name, 1, 24, f) != 24 || fread(dst, 1, px_bytes, f) != px_bytes) {
            ospi_hd_icon_set_free(set);
            fclose(f);
            return 0;
        }
        set->icons[i].name[23] = '\0';
        set->icons[i].rgba = dst;
    }
    fclose(f);
    set->count = count;
    set->size = (int)size;
    return 1;
}

void ospi_hd_icon_set_free(OspiHdIconSet *set)
{
    free(set->icons);
    free(set->pixels);
    memset(set, 0, sizeof *set);
}

const uint8_t *ospi_hd_icon_find(const OspiHdIconSet *set, const char *name)
{
    size_t i;

    for (i = 0; i < set->count; i++) {
        if (strcmp(set->icons[i].name, name) == 0) {
            return set->icons[i].rgba;
        }
    }
    return NULL;
}

uint32_t ospi_hd_icon_argb(const uint8_t *rgba, int size, int x, int y)
{
    const uint8_t *p = rgba + ((size_t)y * (size_t)size + (size_t)x) * 4;

    return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}
