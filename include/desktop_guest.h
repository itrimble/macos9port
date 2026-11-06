#ifndef DESKTOP_GUEST_H
#define DESKTOP_GUEST_H

#include "ospi.h"

typedef struct { int x, y, w, h, image_x, image_y, image_w, image_h; } GuestWindow;

void guest_window_layout(GuestWindow *window, int width, int height,
                         int initial, int old_width, int old_height);
int guest_window_drag(GuestWindow *window, int x, int y, int press_x, int press_y);
void guest_pixel_point(const GuestWindow *window, int width, int height,
                       int x, int y, int *guest_x, int *guest_y);
void draw_guest_window(uint32_t *frame, const GuestWindow *window,
                       const uint32_t *pixels, int width, int height,
                       uint32_t state, const char *title, const char *message);

#endif
