/* Fingers -> classic mouse.
 *
 * Direct mode (default): the desktop is under your finger.
 *   tap ............................ click
 *   double-tap ..................... double-click (open)
 *   touch, then move ............... press and drag (windows, icons, menus)
 *   touch and hold (0.3 s) ......... the loupe appears; slide to aim, lift to click
 *   keep holding (1 s) ............. contextual menu (Control-click)
 *   two fingers drag ............... scroll (pan when zoomed in)
 *   pinch .......................... zoom the desktop, 1x to 4x
 *   two-finger tap ................. contextual menu
 *
 * Trackpad mode: the screen is a trackpad and a pointer is drawn.
 *   one finger moves the pointer, tap clicks, double-tap opens,
 *   tap then touch-and-move drags, two fingers scroll, two-finger tap
 *   is a contextual click.
 */
#ifndef OS999_MOBILE_TOUCH_H
#define OS999_MOBILE_TOUCH_H

#include <stdint.h>
#include "mobile.h"

typedef enum {
    MA_NONE,
    MA_MOVE,       /* x, y: pointer moved (desktop px) */
    MA_DOWN,       /* x, y, clicks */
    MA_UP,         /* x, y */
    MA_CONTEXT,    /* x, y: Control-click */
    MA_WHEEL,      /* lines: + = content moves down (SDL wheel.y) */
    MA_HAPTIC,     /* haptic: a KbdHaptic type (kbd.h), same vocabulary as the keyboard */
    MA_REDRAW      /* view, loupe or ring changed */
} MobileActionKind;

typedef struct {
    MobileActionKind kind;
    int x, y, clicks, lines;
    float strength;
    int haptic;
} MobileAction;

typedef enum { MT_DIRECT, MT_TRACKPAD } MobileTouchMode;

enum { MT_QUEUE = 64, MT_FINGERS = 5 };

typedef struct {
    int64_t id;
    float x, y, x0, y0;
    int active;
} MobileFinger;

typedef struct {
    MobileTouchMode mode;
    const MobileLayout *layout;
    MobileView *view;
    /* tunables in dp / ms */
    float slop_dp, double_dp;
    uint32_t aim_ms, context_ms, tap_ms, double_ms;
    float tracking;

    MobileFinger f[MT_FINGERS];
    int nf;
    int state;                 /* internal state machine */
    uint32_t down_ms, last_tap_ms, aim_start_ms;
    float aim_x0, aim_y0;
    int last_tap_x, last_tap_y, tap_count;
    int dragging, context_sent;
    /* two fingers */
    float g_cx, g_cy, g_dist, g_zoom0;
    int g_mode;                /* 0 undecided, 1 scroll, 2 pinch/pan */
    float wheel_acc;
    uint32_t g_down_ms;
    float g_travel;
    /* trackpad pointer, desktop px */
    float px, py;
    /* loupe / hold ring for the overlay */
    int loupe;                 /* 1 while aiming */
    float loupe_sx, loupe_sy;  /* finger on screen */
    float ring;                /* 0..1 progress toward the contextual click */

    MobileAction q[MT_QUEUE];
    int qh, qt;
} MobileTouch;

void mobile_touch_init(MobileTouch *t, const MobileLayout *l, MobileView *v);
void mobile_touch_set_mode(MobileTouch *t, MobileTouchMode mode);
void mobile_touch_down(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms);
void mobile_touch_move(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms);
void mobile_touch_up(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms);
void mobile_touch_cancel(MobileTouch *t);
void mobile_touch_tick(MobileTouch *t, uint32_t ms);
int mobile_touch_poll(MobileTouch *t, MobileAction *out);
int mobile_touch_busy(const MobileTouch *t);

#endif
