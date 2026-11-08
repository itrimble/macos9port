#ifndef OS999_KBD_DRAW_INT_H
#define OS999_KBD_DRAW_INT_H
#include "kbd_int.h"
#include "kbd_gfx.h"
#include "kbd_skin.h"

float kbd_yoff(const Kbd *k);
int kbd_desk(const Kbd *k);
void kbd_draw_cmd(Fb *fb, float cx, float cy, float s, uint32_t c, float alpha);
void kbd_draw_cap(Kbd *k, Fb *fb, const KbdSkin *sk, const KbdSlot *s, KRect r, float press,
                  float alpha, float glow, float flip);
void kbd_draw_keys(Kbd *k, Fb *fb);
#endif
