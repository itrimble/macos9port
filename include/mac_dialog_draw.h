#ifndef MAC_DIALOG_DRAW_H
#define MAC_DIALOG_DRAW_H
#include "mac_runtime.h"
/* Drawing primitives for the native modal dialogs (Standard File, Alert): our own chrome, clipped to the dialog
 * rectangle and the physical screen, on the 640x480 8-bit indexed display. `dark` and `light` are the palette's darkest
 * and lightest entries. Not Apple's definition procedures or art. */
typedef struct { MacRuntime *rt; int left, top, width, height; uint8_t dark, light; } MacPen;
void mac_pen_init(MacPen *pen, MacRuntime *rt, int left, int top, int width, int height);
void mac_pen_fill(const MacPen *p, int l, int t, int r, int b, uint8_t ink);
void mac_pen_box(const MacPen *p, int l, int t, int r, int b, uint8_t ink);
/* 8x8 text at (x, y); `dim` draws every other pixel (a disabled look). */
void mac_pen_text(const MacPen *p, int x, int y, const char *s, unsigned n, uint8_t ink, int dim);
/* Text wrapped to the rectangle on spaces and carriage returns, 10 pixels per line, clipped at the bottom. */
void mac_pen_wrap(const MacPen *p, int l, int t, int r, int b, const char *s, unsigned n);
/* A push button with rounded corners; `is_default` adds the heavy outline; `pressed` inverts it. */
void mac_pen_button(const MacPen *p, int l, int t, int r, int b, const uint8_t *text, unsigned n, int enabled, int pressed, int is_default);
/* Paper-filled dialog area with the double border. */
void mac_pen_frame(const MacPen *p);
/* A 1-bit 32x32 icon (128 bytes, 1 = dark) with its top-left at (x, y). */
void mac_pen_icon(const MacPen *p, int x, int y, const uint8_t bits[128]);
/* Screen pixels under the pen's rectangle, for restoring after the dialog closes. */
void mac_pen_save(const MacPen *p, uint8_t *buffer);
void mac_pen_restore(const MacPen *p, const uint8_t *buffer);
#endif
