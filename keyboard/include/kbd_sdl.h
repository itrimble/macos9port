/* SDL2 glue for the OS999 keyboards.
 *
 * Integration in a host loop (see docs/INTEGRATION.md):
 *   SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");  // fingers are not mice
 *   while (SDL_PollEvent(&ev)) {
 *       if (kbd_sdl_handle(k, &ev, surface_w, surface_h)) continue;  // keyboard took it
 *       ...existing handling...
 *   }
 *   kbd_tick(k, SDL_GetTicks());
 *   while (kbd_poll_event(k, &e)) {
 *       kbd_audio_play(audio, &e, kbd_skin(k)); kbd_haptics_play(haptics, &e); kbd_sdl_push(&target, &e);
 *   }
 *   kbd_draw(k, &fb);
 *
 * Pushed events look exactly like a hardware keyboard and mouse: KEYDOWN,
 * TEXTINPUT, KEYUP, MOUSEMOTION, MOUSEBUTTON (Control-click is wrapped in a
 * Control key press), MOUSEWHEEL. They carry a marker so kbd_sdl_handle never
 * mistakes them for a Bluetooth keyboard.
 */
#ifndef OS999_KBD_SDL_H
#define OS999_KBD_SDL_H

#include <SDL.h>
#include "kbd.h"

/* `which` of pointer events pushed by kbd_sdl_push, so a host can tell the
 * trackpad's pointer from a real mouse if it wants to. */
#define KBD_SDL_MOUSE_ID 0x4B4244u

typedef struct KbdAudio KbdAudio;

/* Opens its own low-latency device (48 kHz stereo, 256-frame callback). */
KbdAudio *kbd_audio_open(void);
void kbd_audio_close(KbdAudio *a);
void kbd_audio_set_volume(KbdAudio *a, float volume);   /* 0..1 */
void kbd_audio_set_muted(KbdAudio *a, int muted);       /* e.g. while a guest owns audio */
/* Plays KBD_EV_SOUND events with the skin's switch family; ignores others. */
void kbd_audio_play(KbdAudio *a, const KbdEvent *e, int skin);

/* Haptics for SDL hosts: the Force Touch trackpad on macOS (AppKit), else the
 * first SDL haptic device with rumble, else a silent no-op. Native phone
 * hosts map KBD_EV_HAPTIC themselves (docs/HAPTICS.md). */
typedef struct KbdHaptics KbdHaptics;
KbdHaptics *kbd_haptics_open(void);
void kbd_haptics_close(KbdHaptics *h);
const char *kbd_haptics_backend(const KbdHaptics *h);
void kbd_haptics_play(KbdHaptics *h, const KbdEvent *e);   /* ignores non-haptic events */

typedef struct {
    Uint32 window_id;
    float cursor_x, cursor_y;     /* window coordinates, kept by the glue */
    int width, height;            /* window size for clamping */
    float pointer_scale;          /* surface px -> window px, default 1 */
    int buttons;                  /* internal */
    float wheel_x, wheel_y;       /* internal fractional accumulators */
} KbdSdlTarget;

/* Push the SDL events equivalent to one keyboard event. Returns events pushed. */
int kbd_sdl_push(KbdSdlTarget *t, const KbdEvent *e);

/* Route an incoming SDL event. Touches go to the keyboard (coordinates are
 * scaled to the surface); physical keys are observed for auto-hide, the
 * shortcut sheet and click-along, and PC modifier order is swapped in place
 * when KbdSettings.pc_modifiers is set. Returns 1 if the event was consumed. */
int kbd_sdl_handle(Kbd *k, SDL_Event *ev, int surface_w, int surface_h);

#endif
