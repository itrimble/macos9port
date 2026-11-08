/* OS999 mobile chrome: the Control Strip, the aiming loupe, posture balloons,
 * the first-run guide and the trackpad pointer. Everything draws into an Fb at
 * device resolution with the keyboard's anti-aliased primitives, in the same
 * Platinum greys as the desktop.
 */
#ifndef OS999_MOBILE_UI_H
#define OS999_MOBILE_UI_H

#include <stdint.h>
#include "draw.h"
#include "mobile.h"
#include "mobile_touch.h"

/* Platinum */
enum {
    MUI_INK = 0xFF000000u, MUI_WHITE = 0xFFFFFFFFu, MUI_PANEL = 0xFFDDDDDDu,
    MUI_LIGHT = 0xFFEEEEEEu, MUI_SHADOW = 0xFF888888u, MUI_DARK = 0xFF555555u,
    MUI_ACCENT = 0xFF6666CCu, MUI_SELECT = 0xFFCCCCFFu, MUI_BALLOON = 0xFFFFFFE6u
};

typedef enum {
    MS_NONE = -1,
    MS_KEYBOARD, MS_POINTER, MS_ZOOM, MS_DISPLAY, MS_POSTURE, MS_SKIN, MS_SOUND, MS_HELP,
    MS_TILE_COUNT,
    MS_TAB = 100
} MobileStripTile;

typedef struct {
    int keyboard;            /* on-screen keyboard showing */
    int trackpad;            /* MT_TRACKPAD */
    float zoom;
    int desk_w, desk_h;
    MobilePosture posture;   /* in use */
    int posture_locked;      /* user picked it on the strip */
    const char *skin;
    int sound;
    int tablet;              /* posture tile reads "Tablet" */
    int hw_keyboard;         /* keyboard tile reads "HW Keys" */
} MobileStripState;

typedef struct {
    int expanded;
    float dp;
    Rect bar;                /* whole strip as drawn */
    Rect tab;
    Rect tiles[MS_TILE_COUNT];
    int pressed;             /* tile under a finger, for the pressed look */
} MobileStrip;

/* Anchor the strip at the bottom-left of `area` (screen px). */
void mobile_strip_place(MobileStrip *s, Rect area, float dp);
int mobile_strip_hit(const MobileStrip *s, int x, int y);
void mobile_strip_draw(const MobileStrip *s, Fb *fb, const MobileStripState *st);
const char *mobile_strip_label(MobileStripTile t, const MobileStripState *st, char *buf, int n);

/* Glyphs, centred in a box of side `size` at (x, y). Shared with the icon tool. */
void mobile_glyph(Fb *fb, MobileStripTile t, float x, float y, float size,
                  const MobileStripState *st, uint32_t ink);
void mobile_glyph_phone(Fb *fb, MobilePosture p, float x, float y, float size, uint32_t ink,
                        uint32_t screen);
void mobile_platinum_box(Fb *fb, float x, float y, float w, float h, float r, int pressed);

/* Loupe: magnifies the desktop under the finger while aiming. `desk` is the
 * desktop frame (l->desk_w x l->desk_h). */
void mobile_loupe_draw(Fb *fb, const MobileLayout *l, const MobileView *v,
                       const MobileTouch *t, const uint32_t *desk);

/* Posture balloon. Returns 1 while still visible. */
typedef struct {
    char title[48], body[128];
    uint32_t start_ms;
    int active;
} MobileToast;

void mobile_toast_show(MobileToast *t, const char *title, const char *body, uint32_t ms);
int mobile_toast_draw(MobileToast *t, Fb *fb, Rect area, float dp, uint32_t ms);

/* First-run guide. */
typedef struct {
    int visible;
    Rect window, button;
    MobilePosture posture;
    int tablet;
} MobileCoach;

void mobile_coach_layout(MobileCoach *c, Rect area, float dp);
void mobile_coach_draw(const MobileCoach *c, Fb *fb, float dp);
int mobile_coach_hit_button(const MobileCoach *c, int x, int y);

/* Trackpad pointer: classic 16x16 cursor data + mask (68-byte .curs file). */
int mobile_pointer_load(const char *curs_path);
void mobile_pointer_draw(Fb *fb, const MobileLayout *l, const MobileView *v, float dx, float dy);

#endif
