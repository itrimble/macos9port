#ifndef OS999_FINDER_DESKTOP_POSITIONS_H
#define OS999_FINDER_DESKTOP_POSITIONS_H
#include "ospi.h"
void finder_desktop_positions_load(Session *s);
void finder_desktop_positions_clean(Session *s);
void finder_desktop_positions_move(Session *s, int dx, int dy);
int finder_desktop_position_item(Session *s, const char *name, int x, int y);
#endif
