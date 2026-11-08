/* OS999 touch keyboards and trackpads for folding phones.
 *
 * The core is SDL-free and deterministic: feed it touches, hardware key
 * events and a millisecond clock; drain KbdEvent records; draw into an Fb.
 * kbd_sdl.h turns those events into SDL events and plays the switch sounds.
 */
#ifndef OS999_KBD_H
#define OS999_KBD_H

#include <stdint.h>
#include "draw.h"

typedef struct Kbd Kbd;

/* Device posture. COVER is the folded outer screen. OPEN is the inner screen
 * held flat (split thumb layout or a full desk layout). FLEX is the inner
 * screen bent like a laptop: top half shows OS999, bottom half becomes a
 * portable-style deck with a full keyboard and a trackpad with one button. */
typedef enum { KBD_COVER, KBD_OPEN, KBD_FLEX, KBD_POSTURE_COUNT } KbdPosture;
typedef enum { KBD_LAYER_LETTERS, KBD_LAYER_NUMBERS, KBD_LAYER_SYMBOLS } KbdLayer;

/* Key codes are numerically identical to SDL2 SDL_Keycode values, so a host
 * can push them as SDL_KEYDOWN without a table. Printable keys are ASCII. */
#define KBD_SC(x) ((int32_t)((x) | (1 << 30)))
enum {
    KBD_K_BACKSPACE = 8, KBD_K_TAB = 9, KBD_K_RETURN = 13, KBD_K_ESCAPE = 27,
    KBD_K_SPACE = 32, KBD_K_DELETE = 127,
    KBD_K_CAPSLOCK = KBD_SC(57), KBD_K_RIGHT = KBD_SC(79), KBD_K_LEFT = KBD_SC(80),
    KBD_K_DOWN = KBD_SC(81), KBD_K_UP = KBD_SC(82),
    KBD_K_LCTRL = KBD_SC(224), KBD_K_LSHIFT = KBD_SC(225),
    KBD_K_LALT = KBD_SC(226), KBD_K_LGUI = KBD_SC(227)
};
enum { KBD_MOD_SHIFT = 1, KBD_MOD_CTRL = 2, KBD_MOD_OPT = 4, KBD_MOD_CMD = 8, KBD_MOD_CAPS = 16 };

/* Switch sounds. The host maps these to synthesized samples (kbd_synth.h). */
enum { KBD_SND_PRESS, KBD_SND_RELEASE, KBD_SND_WIDE_PRESS, KBD_SND_WIDE_RELEASE,
       KBD_SND_PAD_CLICK, KBD_SND_PAD_RELEASE, KBD_SND_COUNT };

typedef enum {
    KBD_EV_NONE,
    KBD_EV_KEY,        /* key (SDL keycode), down, mods */
    KBD_EV_TEXT,       /* text: committed UTF-8 */
    KBD_EV_POINTER,    /* dx, dy: relative pointer motion in surface px */
    KBD_EV_BUTTON,     /* button 1, down, clicks, mods (CTRL = contextual click) */
    KBD_EV_SCROLL,     /* dx, dy: scroll in lines, fractional */
    KBD_EV_SOUND,      /* sound, variant, pan -1..1, gain, pitch */
    KBD_EV_HAPTIC,     /* haptic type, strength 0..1, sharpness 0..1, duration_ms */
    KBD_EV_VISIBILITY, /* down = keyboard now shown (1) or hidden (0) */
    KBD_EV_FORCE       /* force click (second-stage press); host decides: Get Info, Quick Look... */
} KbdEventKind;

/* Haptic vocabulary. Each type has a fixed intensity/sharpness/duration
 * (kbd_haptic_spec) so every platform renders the same feel: a Force Touch
 * trackpad, an Android vibrator, iOS Core Haptics, or a controller. */
typedef enum {
    KBD_HAPTIC_KEY,        /* character key press: light crisp tick */
    KBD_HAPTIC_KEY_WIDE,   /* space, return, delete: a little rounder */
    KBD_HAPTIC_MODIFIER,   /* modifier latched or unlatched, caps lock */
    KBD_HAPTIC_SELECTION,  /* moving across accent choices, layer change */
    KBD_HAPTIC_TAP,        /* tap-to-click, two-finger tap */
    KBD_HAPTIC_CLICK,      /* clickpad press or the pad button going down */
    KBD_HAPTIC_RELEASE,    /* clickpad release: lighter and sharper than the press */
    KBD_HAPTIC_FORCE,      /* force click: deep second stage */
    KBD_HAPTIC_MODE,       /* the keyboard became a trackpad (space-bar hold) */
    KBD_HAPTIC_COUNT
} KbdHaptic;

typedef struct { float intensity, sharpness; int duration_ms; } KbdHapticSpec;
void kbd_haptic_spec(int type, KbdHapticSpec *out);

typedef struct {
    KbdEventKind kind;
    int32_t key;
    int down, mods;
    char text[8];
    float dx, dy;
    int button, clicks;
    int sound, variant;
    float pan, gain, pitch;
    int haptic, duration_ms;          /* KBD_EV_HAPTIC */
    float strength, sharpness;
} KbdEvent;

typedef struct {
    int repeat_delay_ms;   /* classic Keyboard control panel: delay until repeat */
    int repeat_rate_ms;    /* interval between repeats */
    int tap_to_click;      /* trackpad taps click (default on) */
    int reduce_motion;     /* springs settle instantly, crossfades only */
    int sounds;            /* emit KBD_EV_SOUND */
    int pc_modifiers;      /* hardware keyboard has Ctrl/Win/Alt order: swap */
    int hw_click_along;    /* play switch sounds for hardware keys too */
    float tracking;        /* pointer speed 0.25..3, default 1 */
    int haptics;           /* emit KBD_EV_HAPTIC (default on) */
    float haptic_strength; /* 0..1.5, scales every haptic (default 1) */
    int click_pressure;    /* clickpad firmness: 0 light, 1 medium, 2 firm */
    int force_click;       /* emit KBD_EV_FORCE on a deep press (default on) */
    int three_finger_drag; /* three fingers drag, as in the accessibility option */
} KbdSettings;

/* px_per_dp: surface pixels per 160-dpi point (1 on a 1x desktop, 2.6 on a
 * Fold cover screen at native resolution). */
Kbd *kbd_create(float px_per_dp);
void kbd_destroy(Kbd *k);

void kbd_set_surface(Kbd *k, int width, int height);
void kbd_set_posture(Kbd *k, KbdPosture p);
/* The device turned (quarter_turns clockwise, e.g. a book fold turned on its
 * side for Flex): the screen is now w x h. Keys start where they physically
 * were, rotated with the glass, then flow into the new posture's layout. */
void kbd_rotate(Kbd *k, int w, int h, int quarter_turns, KbdPosture p);
void kbd_set_split(Kbd *k, int split);
void kbd_set_skin(Kbd *k, int skin);
void kbd_set_layer(Kbd *k, KbdLayer layer);
KbdSettings *kbd_settings(Kbd *k);
KbdPosture kbd_posture(const Kbd *k);
int kbd_split(const Kbd *k);
int kbd_skin(const Kbd *k);

/* Text field focus. With a hardware keyboard in use only the slim strip shows. */
void kbd_show(Kbd *k, int show);
int kbd_visible(const Kbd *k);
/* Pixels at the bottom of the surface currently covered (for window insets). */
int kbd_occluded_height(const Kbd *k);

/* Touches in surface pixels. Return 1 if the keyboard consumed the touch. */
int kbd_touch_down(Kbd *k, int64_t finger, float x, float y, uint32_t ms);
int kbd_touch_move(Kbd *k, int64_t finger, float x, float y, uint32_t ms);
int kbd_touch_up(Kbd *k, int64_t finger, float x, float y, uint32_t ms);
void kbd_touch_cancel_all(Kbd *k);
/* Contact pressure 0..1 (SDL tfinger.pressure, Android getPressure, iOS force
 * normalized). Devices that report a constant value are detected and fall
 * back to tap-to-click; devices with real pressure get a two-stage clickpad. */
void kbd_touch_force(Kbd *k, int64_t finger, float pressure, uint32_t ms);

/* Hardware (Bluetooth/USB) keyboard. usage = USB HID usage (== SDL scancode). */
void kbd_hw_attached(Kbd *k, int attached);
int kbd_hw_key(Kbd *k, int usage, int down, uint32_t ms);
int kbd_hw_active(const Kbd *k);
void kbd_set_shortcuts(Kbd *k, const char *const (*pairs)[2], int n);

/* Advance timers and animation. Call once per frame. */
void kbd_tick(Kbd *k, uint32_t ms);
int kbd_animating(const Kbd *k);
int kbd_poll_event(Kbd *k, KbdEvent *out);

/* Draw the keyboard, trackpad and overlays over whatever is in fb. */
void kbd_draw(Kbd *k, Fb *fb);

/* Legend font (ChicagoFLF from fonts/) plus a host fallback for symbols. */
int kbd_load_fonts(const char *legend_ttf);

/* Skins */
int kbd_skin_count(void);
const char *kbd_skin_name(int skin);

#endif
