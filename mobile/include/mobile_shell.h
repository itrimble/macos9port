/* The phone shell around one OS999 desktop session: layout per posture,
 * fingers, the on-screen keyboard, the Control Strip and the overlays,
 * composed into one device-resolution canvas. SDL-free, so the SDL host and
 * the screenshot tool run exactly the same code.
 */
#ifndef OS999_MOBILE_SHELL_H
#define OS999_MOBILE_SHELL_H

#include <stdint.h>
#include "kbd.h"
#include "mobile.h"
#include "mobile_compose.h"
#include "mobile_touch.h"
#include "mobile_ui.h"

typedef struct {
    /* KbdHaptic type with its spec (kbd_haptic_spec), already scaled. */
    void (*haptic)(void *ctx, int type, float strength, float sharpness, int duration_ms);
    void (*kbd_event)(void *ctx, const KbdEvent *e);   /* switch sounds */
    char *(*clipboard)(void *ctx);                     /* malloc'd UTF-8 or NULL */
    void *ctx;
} MobileShellHost;

enum { MSH_FINGERS = 16 };
enum { MSH_DOWN, MSH_MOVE, MSH_UP, MSH_CANCEL };
enum { MSH_BUTTON_PRIMARY = 1, MSH_BUTTON_SECONDARY = 2 };
enum { OWN_NONE, OWN_DESK, OWN_KBD, OWN_STRIP, OWN_COACH };

typedef struct {
    Session *s;
    Kbd *kbd;
    float kbd_dp;
    MobileShellHost host;
    MobileScreen screen;
    MobileLayout layout;
    MobileView view;
    MobileTouch touch;
    MobileStrip strip;
    MobileToast toast;
    MobileCoach coach;
    MobileContinuity cont;
    MobileCanvas canvas;
    uint32_t *desk, *prev_desk;
    size_t desk_cap;
    MobilePosture locked;          /* MP_AUTO unless chosen on the strip */
    int scale_pref;
    int64_t finger_id[MSH_FINGERS];
    int owner[MSH_FINGERS];
    int dirty, pointer_visible, focus_y, last_lift, configured;
    int mouse_owner, mouse_down;   /* trackpad / mouse button state */
    int hw_seen;                   /* a hardware key has been pressed: show the hardware strip */
    int hw_reserve;                /* screen pixels kept clear below the desktop for that strip */
    int hw_reported;               /* the platform's last hardware-keyboard report */
    int was_editing, kbd_for_edit;  /* an icon name is being typed; we raised the keyboard */
    float wheel_acc;
    uint32_t now;
    char guide_path[OSPI_PATH + 32];
} MobileShell;

/* volume: the desktop volume root (the guide remembers it was dismissed there).
 * The shell owns the keyboard: it is rebuilt when the screen density changes
 * (cover and inner screens differ), keeping skin, settings and visibility. */
int mobile_shell_init(MobileShell *m, Session *s, MobileShellHost host, const char *volume);
void mobile_shell_free(MobileShell *m);
void mobile_shell_set_screen(MobileShell *m, const MobileScreen *screen, uint32_t ms);
void mobile_shell_lock_posture(MobileShell *m, MobilePosture p, uint32_t ms);
void mobile_shell_finger(MobileShell *m, int phase, int64_t id, float x, float y, uint32_t ms);
/* Touch pressure 0..1 for a finger already down (SDL tfinger.pressure); the
 * keyboard's trackpad uses it for a two-stage clickpad and Force Click. */
void mobile_shell_finger_force(MobileShell *m, int64_t id, float pressure, uint32_t ms);
/* Committed text from the platform (hardware keyboards, IMEs). */
void mobile_shell_text(MobileShell *m, const char *utf8);
/* Per frame: raise the keyboard while an icon name is edited, keep the name
 * above it, and lower it again afterwards. */
void mobile_shell_follow_edit(MobileShell *m);
/* A trackpad or mouse (iPad keyboard cases, Bluetooth mice, the desk
 * simulator): screen pixels, MSH_DOWN/MOVE/UP, MSH_BUTTON_*. The OS999
 * pointer follows it exactly; the secondary button is a contextual click. */
void mobile_shell_mouse(MobileShell *m, int phase, float x, float y, int button, int clicks,
                        uint32_t ms);
void mobile_shell_wheel(MobileShell *m, float lines);
/* Trackpad pinch (or Command/Control + wheel): zoom the view by `factor`
 * around the pointer, or the middle of the desktop with no pointer shown. */
void mobile_shell_magnify(MobileShell *m, float factor);
/* Hardware keyboard key (SDL keycode + MOD_CMD bits) for the desktop. */
void mobile_shell_key(MobileShell *m, int key, int mods);
/* Advance, paint the desktop, recompose when needed. Returns 1 when the
 * canvas changed and should be presented. */
int mobile_shell_frame(MobileShell *m, uint32_t ms);
void mobile_shell_strip_action(MobileShell *m, int tile, uint32_t ms);
void mobile_shell_show_guide(MobileShell *m, int show);
void mobile_shell_haptic(MobileShell *m, int type);   /* KbdHaptic */
void mobile_shell_rescale(MobileShell *m, int scale, uint32_t ms);
/* Where the Control Strip lives: above the keyboard, or in the laptop's top half. */
Rect mobile_shell_strip_area(const MobileShell *m);

#endif
