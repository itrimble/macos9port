#ifndef OS999_BROWSER_OVERLAY_H
#define OS999_BROWSER_OVERLAY_H

/* Platform layer for embedded browser windows ("slots").
 * macOS: GPU frames arrive as IOSurface Mach ports and are presented
 * zero-copy by one click-through Core Animation layer per slot.
 * Linux: CPU frames arrive in one shared-memory surface per slot and are
 * copied into the OS999 framebuffer. OS999 always keeps all input. */

#include <SDL.h>
#include "frame_dump.h"
#include <stddef.h>
#include <stdint.h>

/* Own rect type: Cocoa headers still define a classic QuickDraw Rect. */
typedef struct { int x, y, w, h; } OverlayRect;

/* Emit disjoint visible rectangles after subtracting the union of all holes.
 * Returns -1 on invalid bounds/count; emits nothing in that case. */
int overlay_visible_regions(OverlayRect bounds, const OverlayRect *holes, int count,
                            void (*emit)(OverlayRect, void *), void *context);

int overlay_listen(char *name, size_t n);                 /* frame channel */
void overlay_window_open(int slot, char *target, size_t n); /* Linux: shm name */
void overlay_window_close(int slot);
void overlay_bind(int slot, int browser_id);              /* active tab */
void overlay_place(SDL_Window *win, int logical_w, int logical_h, int slot,
                   int visible, int order, OverlayRect content,
                   const OverlayRect *holes, int nholes);
void overlay_draw_cpu(int slot, uint32_t *frame, int fw, int fh, OverlayRect content);
float overlay_scale(SDL_Window *win, int logical_w);
uint64_t overlay_frames(int slot);
/* Developer test hook: samples the surface last presented for the slot (see frame_dump.h).
 * Returns 0 and fills the outputs, or -1 when no frame has arrived / not supported. */
int overlay_probe(int slot, int *w, int *h, SurfaceStats *out);

#endif
