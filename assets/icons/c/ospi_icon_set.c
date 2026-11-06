#include "ospi_icon_set.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_exact(FILE *f, void *buf, size_t n)
{
    return fread(buf, 1, n, f) == n;
}

int ospi_icon_set_load(OspiIconSet *set, const char *path)
{
    unsigned char head[12];
    FILE *f;
    size_t count;
    size_t i;

    set->icons = NULL;
    set->count = 0;
    f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    if (!read_exact(f, head, sizeof head) || memcmp(head, "OSPIICN1", 8) != 0) {
        fclose(f);
        return 0;
    }
    count = ((size_t)head[8] << 24) | ((size_t)head[9] << 16) |
            ((size_t)head[10] << 8) | (size_t)head[11];
    if (count == 0 || count > 4096) {
        fclose(f);
        return 0;
    }
    set->icons = calloc(count, sizeof *set->icons);
    if (!set->icons) {
        fclose(f);
        return 0;
    }
    for (i = 0; i < count; i++) {
        OspiIcon *ic = &set->icons[i];
        if (!read_exact(f, ic->name, OSPI_ICON_NAME_LEN) ||
            !read_exact(f, ic->icl8, sizeof ic->icl8) ||
            !read_exact(f, ic->mask32, sizeof ic->mask32) ||
            !read_exact(f, ic->ics8, sizeof ic->ics8) ||
            !read_exact(f, ic->mask16, sizeof ic->mask16)) {
            ospi_icon_set_free(set);
            fclose(f);
            return 0;
        }
        ic->name[OSPI_ICON_NAME_LEN - 1] = '\0';
    }
    set->count = count;
    fclose(f);
    return 1;
}

void ospi_icon_set_free(OspiIconSet *set)
{
    free(set->icons);
    set->icons = NULL;
    set->count = 0;
}

const OspiIcon *ospi_icon_find(const OspiIconSet *set, const char *name)
{
    size_t i;

    for (i = 0; i < set->count; i++) {
        if (strcmp(set->icons[i].name, name) == 0) {
            return &set->icons[i];
        }
    }
    return NULL;
}

int ospi_icon_copy(const OspiIconSet *set, const char *name,
                   uint8_t icon[1024], uint8_t mask[128])
{
    const OspiIcon *ic = ospi_icon_find(set, name);

    if (!ic) {
        return 0;
    }
    memcpy(icon, ic->icl8, sizeof ic->icl8);
    memcpy(mask, ic->mask32, sizeof ic->mask32);
    return 1;
}
