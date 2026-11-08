#ifndef OS999_STARTUP_SPRITE_H
#define OS999_STARTUP_SPRITE_H

#include "draw.h"

/* Original pixel-art Granny Smith fruit for the OS999 boot screen.
   The artwork is authored as indexed rows in os999_startup_sprite.c and uses
   only colours from the project 256-colour icon cube.

   `angle` is in radians. The hand-drawn silhouette never changes; the
   shading bands rotate so the fruit turns while work is pending. `scale` is
   an integer pixel multiplier. */
void os999_startup_draw_fruit(Fb *fb, int cx, int cy, int scale, double angle);

int os999_startup_fruit_width(int scale);
int os999_startup_fruit_height(int scale);

#endif
