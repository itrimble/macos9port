#include "ospi_icon_set.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    OspiIconSet set;
    uint8_t icon[1024], mask[128];
    int y, x;

    if (argc < 2 || !ospi_icon_set_load(&set, argv[1])) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    if (!ospi_icon_copy(&set, "trash_full", icon, mask)) {
        fprintf(stderr, "trash_full missing\n");
        return 1;
    }
    printf("%zu icons\n", set.count);
    for (y = 0; y < 32; y += 2) {
        for (x = 0; x < 32; x++) {
            putchar((mask[y * 4 + x / 8] >> (7 - x % 8)) & 1 ? (icon[y * 32 + x] == 215 ? '#' : '+') : '.');
        }
        putchar('\n');
    }
    ospi_icon_set_free(&set);
    return 0;
}
