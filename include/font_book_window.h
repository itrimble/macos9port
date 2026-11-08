#ifndef OS999_FONT_BOOK_WINDOW_H
#define OS999_FONT_BOOK_WINDOW_H

#include "draw.h"
#include "ospi.h"

int font_book_window_open(Session *session);
void font_book_window_release(Window *window);
void font_book_window_paint(Fb *fb, const Session *session, const Window *window, const Layout *layout);
int font_book_window_down(Session *session, Window *window, const Layout *layout, int x, int y);
int font_book_window_key(Session *session, Window *window, int key, int mods);
int font_book_window_wheel(Window *window, int delta);
int font_book_preview_draw(Fb *fb, const char *path, int face_index, Rect bounds, uint32_t color,
                           const char *sample);

#endif
