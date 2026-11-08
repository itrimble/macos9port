#ifndef OS999_FINDER_DESKTOP_SURFACE_H
#define OS999_FINDER_DESKTOP_SURFACE_H
#include "ospi.h"
struct Fb;
int finder_desktop_init(Session *s);
void finder_desktop_layout(Session *s);
int finder_desktop_overflow(const Session *s);
void finder_desktop_overflow_paint(struct Fb *fb, const Session *s);
int finder_desktop_overflow_down(Session *s, int x, int y);
void finder_desktop_reveal_selected(Session *s);
#endif
