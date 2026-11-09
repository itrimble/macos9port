#ifndef OS999_EXT_BOOT_H
#define OS999_EXT_BOOT_H

#include <SDL.h>
#include <stdint.h>

/* Startup extension override. Space held at launch (or OSPI_EXT_MANAGER=1, or a
 * crash-looped previous startup) pauses here and shows the Extensions Manager;
 * Shift alone skips extensions for this startup. Tells ospi-runtime children via
 * OSPI_EXT_ROOT / OSPI_EXT_MODE. Closing the window while paused exits. */
void ext_boot_run(const char *volume, SDL_Window *win, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *frame);

#endif
