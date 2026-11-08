#ifndef MOVIE_WINDOW_H
#define MOVIE_WINDOW_H

#include "draw.h"
#include "ospi.h"

int movie_is_name(const char *name);
int movie_window_launcher(Session *s);
/* 0 when the file was handled, including a dialog for a bad movie.
 * -1 only when every window slot is already in use. */
int movie_window_open(Session *s, const char *title, const char *rel);
void movie_window_release(Window *w);
void movie_windows_tick(Session *s, uint32_t now_ms);
void movie_window_toggle(Window *w);
void movie_window_click(Window *w, int x, int y);
int movie_window_key(Window *w, int key);
int movie_window_seek(Window *w, uint32_t milliseconds);
void movie_window_paint(Fb *fb, const Window *w);
int movie_window_picture_rect(const Window *w, Rect *out);
int movie_window_state(const Window *w);
uint32_t movie_window_time(const Window *w);

#endif
