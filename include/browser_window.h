#ifndef OS999_BROWSER_WINDOW_H
#define OS999_BROWSER_WINDOW_H

#include <SDL.h>
#include <stdint.h>
#include "ospi.h"

/* Embedded Chromium window drawn by OS999 (checklist B01/B04).
 * browser_window_event returns 1 when it consumed the event. */
int browser_window_event(Session *s, SDL_Window *win, const SDL_Event *ev);
void browser_window_frame(Session *s, SDL_Window *win, uint32_t *frame, int hidden);
void browser_window_close(void);

#endif
