/* Folding-phone simulator for the OS999 keyboards. */
#ifndef OS999_KBD_DEMO_H
#define OS999_KBD_DEMO_H

#include "kbd.h"
#include "../src/kbd_gfx.h"

enum { DEMO_TEXT = 4096 };

typedef struct {
    char text[DEMO_TEXT];
    int len;
    float scroll;          /* lines scrolled up */
    float px, py;          /* OS pointer, screen px */
    int pointer_down;
    uint32_t blink;
    int focused;
} DemoEditor;

typedef struct { float x, y, w, h; } DRect;

typedef struct {
    const char *label;
    int id;
    DRect r;
    int on;                /* toggles: lit state */
} DemoButton;

void demo_editor_insert(DemoEditor *e, const char *utf8);
void demo_editor_backspace(DemoEditor *e, int word);

/* The OS999 screen: menu bar, a SimpleText-style window, the pointer.
 * area_h is the part of the screen not covered by the keyboard. */
void demo_draw_os(Fb *fb, float dp, const DemoEditor *e, float area_h, uint32_t now, const char *title);
/* Phone body around the screen at (x, y, w, h); posture 0 cover, 1 open, 2 flex. */
void demo_draw_device(Fb *fb, float dp, DRect screen, int posture);
void demo_draw_panel(Fb *fb, float dp, DemoButton *b, int n, const char *skin, const char *blurb);
void demo_blit(Fb *dst, const Fb *src, int x, int y);

#endif
