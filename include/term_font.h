#ifndef OS999_TERM_FONT_H
#define OS999_TERM_FONT_H
#include "draw.h"
int term_font_has_glyph(uint32_t cp);
void term_font_draw(Fb *fb, int x, int y, uint32_t cp, uint32_t color, int bold);
#endif
