#ifndef OS999_DESKTOP_GESTURE_H
#define OS999_DESKTOP_GESTURE_H
#include "ospi.h"
struct Fb;
enum { DESKTOP_GESTURE_REGIONS = 12 };
int session_gesture_regions(const Session *s, Rect out[DESKTOP_GESTURE_REGIONS]);
void session_gesture_paint(struct Fb *fb, const Session *s);
#endif
