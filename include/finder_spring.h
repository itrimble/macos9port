#ifndef OS999_FINDER_SPRING_H
#define OS999_FINDER_SPRING_H
#include "ospi.h"
void finder_spring_tick(Session *s, uint32_t now_ms);
void finder_spring_finish(Session *s, int x, int y, int keep_destination);
#endif
