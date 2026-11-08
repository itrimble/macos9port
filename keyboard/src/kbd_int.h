/* Internal state shared by the keyboard modules. Not a public interface. */
#ifndef OS999_KBD_INT_H
#define OS999_KBD_INT_H

#include "kbd.h"

enum { KBD_MAX_KEYS = 96, KBD_MAX_TOUCH = 10, KBD_RING = 256, KBD_MAX_ALTS = 8,
       KBD_MAX_SHORTCUTS = 16 };

typedef enum {
    KK_NONE, KK_GRAVE, KK_1, KK_2, KK_3, KK_4, KK_5, KK_6, KK_7, KK_8, KK_9, KK_0,
    KK_MINUS, KK_EQUAL, KK_DELETE, KK_TAB,
    KK_Q, KK_W, KK_E, KK_R, KK_T, KK_Y, KK_U, KK_I, KK_O, KK_P,
    KK_LBRACKET, KK_RBRACKET, KK_BACKSLASH, KK_CAPS,
    KK_A, KK_S, KK_D, KK_F, KK_G, KK_H, KK_J, KK_K, KK_L, KK_SEMI, KK_QUOTE, KK_RETURN,
    KK_SHIFT, KK_Z, KK_X, KK_C, KK_V, KK_B, KK_N, KK_M, KK_COMMA, KK_PERIOD, KK_SLASH,
    KK_RSHIFT, KK_CTRL, KK_OPT, KK_CMD, KK_SPACE, KK_SPACE2, KK_RCMD, KK_ROPT,
    KK_LEFT, KK_RIGHT, KK_UP, KK_DOWN, KK_ESC,
    KK_LAYER_NUM, KK_LAYER_SYM, KK_LAYER_ABC, KK_HIDE, KK_GLYPH, KK_COUNT
} KbdKeyId;

typedef enum { KIND_CHAR, KIND_MOD, KIND_ACTION, KIND_SPACE, KIND_LAYER } KbdKind;

/* Option-layer dead keys, as on the classic US layout. */
enum { DEAD_NONE, DEAD_ACUTE, DEAD_GRAVE, DEAD_CIRC, DEAD_UML, DEAD_TILDE };

typedef struct {
    const char *label;     /* printed legend; lowercase words for actions */
    const char *shift;     /* shifted legend (NULL: uppercase of label) */
    int32_t code;          /* SDL-compatible keycode */
    uint8_t kind;
    uint8_t mod;           /* KBD_MOD_* for modifiers */
    uint8_t dead;          /* Option makes this a dead key */
    const char *opt;       /* Option output / legend */
    const char *optshift;  /* Shift-Option output */
} KbdKeyDef;

typedef struct { float x, y, w, h; } KRect;

typedef struct {
    uint16_t id;
    uint8_t row, col;
    char glyph[8];         /* KK_GLYPH slots: legend and output */
    KRect r;
} KbdSlot;

typedef struct {
    KbdSlot keys[KBD_MAX_KEYS];
    int n;
    KRect deck;            /* keyboard body */
    KRect strip;           /* accessory strip, if any (w == 0: none) */
    KRect pad, pad_button; /* trackpad (w == 0: none) */
    KRect crease;          /* fold to keep clear (w == 0: none) */
    float key_h, unit, gap;
} KbdLayout;

typedef struct { float x, v; } Spring;

typedef struct {
    KRect from;            /* rect where this key began its morph */
    float t;               /* 0..1 morph progress (spring) */
    float tv;
    float delay;           /* seconds before the morph starts (stagger) */
    float press, pv;       /* press depth 0..1, velocity */
    float flip;            /* legend flip 0..1 (layer change) */
    float glow;            /* release flash */
    uint8_t born;          /* appeared in this relayout: fade in */
} KbdAnim;

typedef struct { KbdSlot slot; float alpha; } KbdGhost;

enum { ROLE_NONE, ROLE_KEY, ROLE_PAD, ROLE_PAD_BUTTON, ROLE_CURSOR, ROLE_ALT, ROLE_STRIP };

typedef struct { float x, dx; int init; } KEuro;   /* 1-euro filter, one axis */

typedef struct {
    int active;
    int64_t fid;
    float x, y, x0, y0, lx, ly;
    uint32_t t0;
    int role, slot, committed, moved;
    uint32_t next_repeat;
    int repeats;
    /* trackpad motion */
    KEuro ex, ey;
    float fx, fy, speed;   /* filtered position, speed in px/s */
    uint32_t lt;           /* time of the last motion sample */
    float pressure, ps;    /* raw and smoothed contact pressure */
    int stage;             /* clickpad stage: 0 up, 1 clicked, 2 force-clicked */
    int forced;            /* this touch clicked by pressure: no tap-click */
} KbdTouch;

typedef struct {
    int fingers;           /* fingers on the pad surface */
    uint32_t first_down, last_tap_up;
    float travel;
    int dragging, button_down, two_finger, three_finger, tfd;
    int axis;              /* scroll axis lock: 0 deciding, 1 free, 2 vertical, 3 horizontal */
    float ax, ay;          /* travel while deciding */
    float vx, vy;          /* scroll velocity, lines/s */
    uint32_t last_scroll;
    int clicks;
    uint32_t last_click;
    int pressure_seen;     /* the device reports real, varying pressure */
} KbdPad;

struct Kbd {
    float dp;
    int sw, sh;
    KbdPosture posture;
    int split, skin;
    KbdLayer layer;
    KbdSettings set;
    KbdLayout lay;
    KbdAnim anim[KBD_MAX_KEYS];
    KbdGhost ghost[KBD_MAX_KEYS];
    int nghost;
    Spring morph;          /* deck edge and trackpad follow the keys */
    float deck_from;
    KRect pad_from, btn_from;
    Spring show;           /* 0 hidden .. 1 shown */
    int want_show;
    int latched, held, caps;
    uint32_t shift_tap;
    int dead;
    int mod_used;          /* a key was typed while a modifier was held */
    uint32_t last_space, last_char;
    Spring optfade;        /* Option legends crossfade */
    KbdTouch touch[KBD_MAX_TOUCH];
    int preview;           /* slot shown in the magnified preview, -1 none */
    Spring preview_s;
    int alt_open, alt_slot, alt_n, alt_sel;
    char alts[KBD_MAX_ALTS][8];
    KRect alt_rect;
    Spring alt_s;
    KbdPad pad;
    int cursor_mode;
    Spring cursor_s;
    int hw_attached, hw_active, hw_mods, hw_caps;
    uint32_t hw_last, hw_cmd_since;
    int sheet;             /* shortcut sheet visible */
    Spring hud_s, sheet_s;
    const char *shortcuts[KBD_MAX_SHORTCUTS][2];
    int nshort;
    uint32_t now;
    int started;
    uint32_t rng;
    KbdEvent ring[KBD_RING];
    int rhead, rtail;
};

extern const KbdKeyDef kbd_keydefs[KK_COUNT];

/* kbd_layout.c */
void kbd_layout_build(KbdLayout *L, KbdPosture p, int split, KbdLayer layer,
                      int w, int h, float dp);
int kbd_layout_hit(const KbdLayout *L, float x, float y, float slop);

/* kbd_text.c */
int kbd_utf8_upper(const char *in, char *out, int cap);
int kbd_compose(int dead, const char *base, char *out, int cap);
const char *kbd_dead_mark(int dead);
int kbd_alternates(const char *base, char out[][8], int max);
const char *kbd_slot_legend(const KbdSlot *s, int mods, int caps);
const char *kbd_slot_output(const KbdSlot *s, int mods, int caps, int *dead);

/* kbd_anim.c */
void spring_step(Spring *s, float target, float k, float c, float dt);
void kbd_anim_relayout(Kbd *k, const KbdLayout *old, int by_position);
void kbd_anim_step(Kbd *k, float dt);
KRect kbd_anim_rect(const Kbd *k, int slot);
float kbd_anim_alpha(const Kbd *k, int slot);
float kbd_anim_deck_y(const Kbd *k);
KRect kbd_anim_pad(const Kbd *k, int button, float *alpha);

/* kbd_core.c */
void kbd_emit(Kbd *k, const KbdEvent *e);
void kbd_emit_key(Kbd *k, int32_t code, int down, int mods);
void kbd_emit_text(Kbd *k, const char *utf8);
void kbd_emit_sound(Kbd *k, int sound, float x);
void kbd_emit_haptic(Kbd *k, int type);
int kbd_mods(const Kbd *k);
void kbd_relayout(Kbd *k, int by_position);
uint32_t kbd_rand(Kbd *k);
KRect kbd_hud_button(const Kbd *k);

/* kbd_touch.c */
int kbd_keys_down(Kbd *k, KbdTouch *t);
void kbd_keys_move(Kbd *k, KbdTouch *t);
void kbd_keys_up(Kbd *k, KbdTouch *t);
void kbd_keys_tick(Kbd *k);
void kbd_alt_release(Kbd *k);

/* kbd_pad.c */
int kbd_pad_down(Kbd *k, KbdTouch *t);
void kbd_pad_move(Kbd *k, KbdTouch *t);
void kbd_pad_force(Kbd *k, KbdTouch *t);

/* kbd_motion.c */
void kbd_euro_reset(KEuro *e, float x);
float kbd_euro(KEuro *e, float x, float dt, float dp);
float kbd_pointer_gain(const Kbd *k, float speed_px, float pad_w);
float kbd_motion_dt(uint32_t now, uint32_t last);
void kbd_pad_up(Kbd *k, KbdTouch *t);
void kbd_pad_tick(Kbd *k, float dt);
void kbd_cursor_begin(Kbd *k, KbdTouch *t);

static inline int krect_in(KRect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

#endif
