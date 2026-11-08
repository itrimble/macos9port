/* Anti-aliased drawing into an OS999 Fb (ARGB8888, opaque destination). */
#ifndef OS999_KBD_GFX_H
#define OS999_KBD_GFX_H

#include <stdint.h>
#include "draw.h"

uint32_t g_mix(uint32_t a, uint32_t b, float t);
uint32_t g_shade(uint32_t c, float k);   /* k > 1 lighter, < 1 darker */

/* Rounded rect, vertical gradient top->bottom, AA edges. */
void g_rrect(Fb *fb, float x, float y, float w, float h, float r,
             uint32_t top, uint32_t bottom, float alpha);
void g_rrect_stroke(Fb *fb, float x, float y, float w, float h, float r,
                    float thick, uint32_t c, float alpha);
/* Soft shadow of a rounded rect (blur in px). */
void g_shadow(Fb *fb, float x, float y, float w, float h, float r,
              float blur, uint32_t c, float alpha);
void g_line(Fb *fb, float x0, float y0, float x1, float y1, float thick,
            uint32_t c, float alpha);
void g_disc(Fb *fb, float cx, float cy, float r, uint32_t c, float alpha);
/* Ring arc from a0 to a1 radians (screen y down, 0 = +x). */
void g_arc(Fb *fb, float cx, float cy, float r, float thick, float a0, float a1,
           uint32_t c, float alpha);
void g_triangle(Fb *fb, float x0, float y0, float x1, float y1, float x2, float y2,
                uint32_t c, float alpha);

/* Text. Fonts load once; size is pixel height of the em. */
int g_font_init(const char *primary, const char *fallback);
float g_text_width(const char *s, float px);
/* y is the top of the em box. */
void g_text(Fb *fb, float x, float y, float px, uint32_t c, float alpha, const char *s);
void g_text_center(Fb *fb, float cx, float cy, float px, uint32_t c, float alpha, const char *s);
/* Largest size <= px whose width fits max_w. */
float g_fit(const char *s, float px, float max_w);

#endif
