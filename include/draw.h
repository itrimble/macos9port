#ifndef OSPI_DRAW_H
#define OSPI_DRAW_H

#include <stdint.h>

enum {
    C_BLACK = 0xFF000000u,
    C_WHITE = 0xFFFFFFFFu,
    C_MENU = 0xFFEEEEEE,
    C_DESK = 0xFF5B71A2u,
    C_DESK2 = 0xFF56709Cu,
    C_FOLDER = 0xFF9A98EEu,
    C_FOLDER_EDGE = 0xFF2A2858u,
    C_FOLDER_TAB = 0xFFC4C2FFu,
    C_GRAY = 0xFFB0B0B0u,
    C_TRACK = 0xFFD2D2D2u,
    C_DISABLED = 0xFF8A8A8Au,
    C_STRIPE_A = 0xFFBEBEBEu,
    C_STRIPE_B = 0xFFA3A3A3u
};

typedef struct Fb {
    uint32_t *px;
    int w, h;
    int clip;
    int cx, cy, cw, ch;
} Fb;

int font_next(const char *s, int *advance, const uint8_t **rows);

void fb_init(Fb *fb, uint32_t *px, int w, int h);
void fb_clip(Fb *fb, int x, int y, int w, int h);
void fb_unclip(Fb *fb);
void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t c);
void fb_frame(Fb *fb, int x, int y, int w, int h, uint32_t c);
void fb_invert(Fb *fb, int x, int y, int w, int h);
void fb_hline(Fb *fb, int x, int y, int w, uint32_t c);
void fb_text(Fb *fb, int x, int y, uint32_t c, const char *s);
void fb_text_n(Fb *fb, int x, int y, uint32_t c, const char *s, int nbytes);
int fb_text_width(const char *s);
int fb_text_width_n(const char *s, int nbytes);
void fb_desktop(Fb *fb);

#endif
