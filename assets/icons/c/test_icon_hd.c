#include "ospi_icon_hd.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    OspiHdIconSet set;
    const uint8_t *px;
    int y, x;

    if (argc < 2 || !ospi_hd_icon_set_load(&set, argv[1])) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    px = ospi_hd_icon_find(&set, "alert_caution");
    if (!px) {
        fprintf(stderr, "alert_caution missing\n");
        return 1;
    }
    printf("%zu icons at %d px\n", set.count, set.size);
    for (y = 0; y < set.size; y += set.size / 16) {
        for (x = 0; x < set.size; x += set.size / 32) {
            uint32_t c = ospi_hd_icon_argb(px, set.size, x, y);
            putchar((c >> 24) < 128 ? '.' : ((c & 0xffffff) == 0 ? '#' : '+'));
        }
        putchar('\n');
    }
    ospi_hd_icon_set_free(&set);
    return 0;
}
