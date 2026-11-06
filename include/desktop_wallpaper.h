#ifndef OS999_DESKTOP_WALLPAPER_H
#define OS999_DESKTOP_WALLPAPER_H
#include "draw.h"
/* Explicit trusted resource directory; preference is a per-volume file. */
int desktop_wallpaper_init(const char *directory, const char *preference_file);
int desktop_wallpaper_count(void);
const char *desktop_wallpaper_name(int index);
int desktop_wallpaper_current(void);
/* -1 selects the existing gradient. Returns 1=picture, 0=fallback, -1=error. */
int desktop_wallpaper_select(int index);
/* Cached cover fit; no I/O or decode per frame. Returns 0 for normal desktop fallback. */
int desktop_wallpaper_draw(Fb *fb);
void desktop_wallpaper_shutdown(void);
#endif
