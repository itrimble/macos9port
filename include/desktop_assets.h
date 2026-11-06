#ifndef OS999_DESKTOP_ASSETS_H
#define OS999_DESKTOP_ASSETS_H

#include "draw.h"

/* Logical 32px icons. This does not claim a HiDPI framebuffer path. */
int desktop_assets_load(const char *path);
void desktop_assets_reset(void);
int desktop_asset_draw(Fb *fb, int x, int y, const char *name);

#endif
