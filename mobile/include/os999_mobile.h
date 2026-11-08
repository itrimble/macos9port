/* Platform -> OS999 bridge. Android (Kotlin, via JNI) and iOS (Objective-C)
 * report what the hardware is doing; the SDL host reads a snapshot each frame.
 * Every call is thread-safe and cheap.
 */
#ifndef OS999_MOBILE_BRIDGE_H
#define OS999_MOBILE_BRIDGE_H

typedef struct {
    unsigned seq;              /* bumps on every change */
    int fold_state;            /* MobileFoldState */
    int fold_axis;             /* MobileFoldAxis */
    int fold_l, fold_t, fold_r, fold_b;   /* hinge bounds, window pixels */
    int inner;                 /* 1 inner screen, 0 cover, -1 unknown */
    float ppi;                 /* 0 unknown */
    int inset_l, inset_t, inset_r, inset_b;
    int hw_keyboard;
    int tablet;                /* one large screen and no hinge (iPad, Android tablets) */
} Os999MobileReport;

/* state/axis use the MobileFoldState / MobileFoldAxis numbers from mobile.h. */
void os999_mobile_report_fold(int state, int axis, int l, int t, int r, int b);
void os999_mobile_report_display(int inner, float ppi);
void os999_mobile_report_insets(int l, int t, int r, int b);
void os999_mobile_report_hw_keyboard(int attached);
void os999_mobile_report_tablet(int tablet);
/* Trackpad gestures the platform recognises itself (iPadOS pinch on a
 * keyboard case's trackpad, Android 14+ touchpad pinch and two-finger swipe).
 * They accumulate until the host takes them, once a frame: factor multiplies,
 * lines add (positive scrolls up, as SDL's wheel). */
void os999_mobile_report_magnify(float factor);
void os999_mobile_report_scroll(float lines);
/* Returns 1 when something was pending; otherwise factor 1 and lines 0. */
int os999_mobile_take_gestures(float *factor, float *lines);
/* Copies the latest report; returns 1 when it changed since `seen_seq`. */
int os999_mobile_snapshot(Os999MobileReport *out, unsigned seen_seq);

/* Haptics use the keyboard's vocabulary (KbdHaptic in kbd.h, docs/HAPTICS.md). */
typedef void (*Os999HapticFn)(int type, float strength, float sharpness, int duration_ms);
void os999_mobile_set_haptics(Os999HapticFn fn);
void os999_mobile_haptic(int type, float strength, float sharpness, int duration_ms);

#endif
