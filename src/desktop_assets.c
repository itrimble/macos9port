#include "desktop_assets.h"
#include "mac_icon.h"
#include "ospi_icon_set.h"

#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static OspiIconSet icons;
static int attempted;

int desktop_assets_load(const char *path) {
    OspiIconSet next = {0};
    if (!path || !*path || !ospi_icon_set_load(&next, path)) return 0;
    ospi_icon_set_free(&icons);
    icons = next;
    attempted = 1;
    return 1;
}

void desktop_assets_reset(void) {
    ospi_icon_set_free(&icons);
    attempted = 0;
}

static void load_default(void) {
    char executable[4096], path[4096];
    attempted = 1;
#if defined(__APPLE__)
    uint32_t size = sizeof executable;
    int found = _NSGetExecutablePath(executable, &size) == 0;
#else
    ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
    int found = size > 0 && (size_t)size < sizeof executable - 1;
    if (found) executable[size] = 0;
#endif
    if (found) {
        char *slash = strrchr(executable, '/');
        if (slash) {
            *slash = 0;
            const char *suffixes[] = {
                "/../Resources/ospi-icons.icl8",
                "/../assets/icons/ospi-icons.icl8"
            };
            for (unsigned i = 0; i < sizeof suffixes / sizeof *suffixes; i++) {
                int n = snprintf(path, sizeof path, "%s%s", executable, suffixes[i]);
                if (n > 0 && (size_t)n < sizeof path && desktop_assets_load(path)) return;
            }
        }
    }
    (void)desktop_assets_load("assets/icons/ospi-icons.icl8");
}

int desktop_asset_draw(Fb *fb, int x, int y, const char *name) {
    if (!fb || !fb->px || !name) return 0;
    if (!attempted) load_default();
    const OspiIcon *icon = ospi_icon_find(&icons, name);
    if (!icon) return 0;
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int bit = row * 32 + col;
            if (icon->mask32[bit >> 3] & (0x80u >> (bit & 7))) {
                fb_fill(fb, x + col, y + row, 1, 1, mac_icon_color(icon->icl8[bit]));
            }
        }
    }
    return 1;
}
