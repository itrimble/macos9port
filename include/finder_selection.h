#ifndef OS999_FINDER_SELECTION_H
#define OS999_FINDER_SELECTION_H
#include "ospi.h"
#include "draw.h"
void finder_selection_press(Session *s, Window *w, int index);
void finder_selection_marquee(Session *s, int x, int y);
void finder_selection_paint(Fb *fb, const Session *s);
#endif
