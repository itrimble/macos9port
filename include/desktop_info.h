#ifndef OS999_DESKTOP_INFO_H
#define OS999_DESKTOP_INFO_H
#include "ospi.h"
#include "draw.h"
int desktop_info_open(Session *s, const char *rel);
int desktop_info_paint(Fb *fb, const Window *w, const Layout *lay);
int desktop_info_click(Session *s, Window *w, int x, int y);
int desktop_info_font_ready(int index);
#endif
