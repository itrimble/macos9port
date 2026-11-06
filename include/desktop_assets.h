#ifndef OS999_DESKTOP_ASSETS_H
#define OS999_DESKTOP_ASSETS_H

#include "draw.h"
#include "ospi.h"

/* Logical 32px icons. This does not claim a HiDPI framebuffer path. */
int desktop_assets_load(const char *path);
void desktop_assets_reset(void);
int desktop_asset_draw(Fb *fb, int x, int y, const char *name);

/* Semantic fallback artwork only; caller must prefer a real guest icon. */
const char *desktop_asset_name(const Item *item, const char *parent_rel);

#endif
