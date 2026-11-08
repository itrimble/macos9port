#ifndef OS999_DESKTOP_CONTEXT_H
#define OS999_DESKTOP_CONTEXT_H
#include "ospi.h"
#include "draw.h"
enum { CONTEXT_KEY_UP = 1073741906, CONTEXT_KEY_DOWN = 1073741905 };
int context_open(Session *s, int x, int y);
int context_active(void);
int context_bounds(const Session *s, Rect *out);
void context_draw(Session *s, Fb *fb);
int context_down(Session *s, int x, int y);
void context_move(Session *s, int x, int y);
/* Returns consumed; Cmd-Q closes and passes through. */
int context_key(Session *s, int key, int mods);
void context_close(void);
#endif
