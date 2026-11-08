#ifndef OS999_MOBILE_COMPOSE_H
#define OS999_MOBILE_COMPOSE_H

#include <stdint.h>
#include "mobile.h"

typedef struct {
    uint32_t *px;   /* device-resolution ARGB8888, opaque */
    int w, h;
    int *xmap;      /* scratch: destination column -> desktop column */
} MobileCanvas;

int mobile_canvas_resize(MobileCanvas *c, int w, int h);
void mobile_canvas_free(MobileCanvas *c);
/* Scale the visible part of the desktop (nearest neighbour) into the canvas
 * and paint everything else `backdrop`. */
void mobile_compose_desk(MobileCanvas *c, const MobileLayout *l, const MobileView *v,
                         const uint32_t *desk, uint32_t backdrop);
/* Vertical gradient behind the laptop deck before the keyboard draws on it. */
void mobile_compose_deck(MobileCanvas *c, Rect deck, uint32_t top, uint32_t bottom);

#endif
