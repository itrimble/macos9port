#ifndef OS999_KBD_SKIN_H
#define OS999_KBD_SKIN_H

#include <stdint.h>

/* Switch families the synthesizer models. */
enum { KBD_SW_DOME, KBD_SW_ALPS, KBD_SW_MITSUMI, KBD_SW_SCISSOR, KBD_SW_COUNT };
enum { TEX_NONE, TEX_PINSTRIPE, TEX_SPECKLE, TEX_DOMES };

typedef struct {
    const char *name;
    const char *blurb;
    uint32_t deck_top, deck_bot, deck_edge;
    uint32_t cap_top, cap_bot, skirt, skirt_dark;
    uint32_t mod_top, mod_bot;
    uint32_t legend, legend_mod;
    uint32_t accent;          /* latched / caps LED */
    uint32_t pad_top, pad_bot, pad_edge, button_top, button_bot;
    float cap_alpha;          /* < 1: translucent caps show the domes below */
    float radius;             /* corner radius, fraction of key height */
    int texture;
    int sound;                /* KBD_SW_* */
    uint32_t row_tint[6];     /* nonzero: per-row cap tint */
} KbdSkin;

const KbdSkin *kbd_skin_get(int skin);
int kbd_skin_sound(int skin);

#endif
