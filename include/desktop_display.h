#ifndef OS999_DESKTOP_DISPLAY_H
#define OS999_DESKTOP_DISPLAY_H

#include "ospi.h"
#include <SDL.h>

/* The host window, renderer, streaming texture and the two desktop-sized
 * buffers. They follow the logical desktop size (OSPI_W x OSPI_H) and the
 * Screen size percent chosen in Monitors & Display. */
typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    uint32_t *frame;      /* what is presented */
    uint32_t *backdrop;   /* the desktop behind the startup animation */
    int w, h, scale;      /* what the buffers and the window were made for */
} DesktopDisplay;

/* Restore the saved mode for `volume`, then open the window at that size.
 * Returns 0, or -1 after printing why and releasing everything. */
int desktop_display_open(DesktopDisplay *d, const char *volume);
void desktop_display_close(DesktopDisplay *d);
/* Follow a mode or scale change. Returns 1 when the desktop size changed and
 * frame, backdrop and tex were replaced; the caller re-places host-drawn
 * surfaces. If the new buffers cannot be made the old mode is restored. */
int desktop_display_sync(DesktopDisplay *d, Session *s);
/* Upload d->frame and present it stretched over the whole window. */
void desktop_display_present(DesktopDisplay *d);
void desktop_display_point(SDL_Window *win, int x, int y, int *out_x, int *out_y);

#endif
