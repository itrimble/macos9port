/* Haptic playback for SDL hosts.
 *
 * macOS: the Force Touch trackpad, through AppKit's NSHapticFeedbackManager
 *   default performer (called through the Objective-C runtime so this stays
 *   a C file). It offers three patterns and no intensity, so the vocabulary
 *   folds onto them: presses and taps are Generic, detents and modifier
 *   latches are Alignment, force clicks and mode changes are LevelChange.
 *   The user feels it while a finger is on the trackpad, which is exactly
 *   when the simulator's mouse-finger is down.
 * Elsewhere: the first SDL haptic device with rumble (a game controller, or a
 *   device vibrator where the platform port exposes one), with each type's
 *   strength and duration. Rumble motors can't make a crisp tick, so very
 *   short types are stretched to a perceptible minimum.
 * Native phone hosts should skip this file and map KbdEvent haptic types to
 *   the platform directly; see docs/HAPTICS.md.
 */
#include "kbd_sdl.h"
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <objc/message.h>
#include <objc/runtime.h>
#define KBD_APPKIT 1
#endif
#endif

enum { MIN_GAP_MS = 14, RUMBLE_MIN_MS = 14 };

struct KbdHaptics {
    int appkit;
    SDL_Haptic *rumble;
    int own_subsystem;
    uint32_t last;
    char name[96];
    float scale;
};

#ifdef KBD_APPKIT
static id appkit_performer(void) {
    Class cls = objc_getClass("NSHapticFeedbackManager");
    if (!cls) return NULL;
    id (*send)(Class, SEL) = (id (*)(Class, SEL))objc_msgSend;
    return send(cls, sel_registerName("defaultPerformer"));
}

static void appkit_perform(long pattern) {
    id performer = appkit_performer();
    if (!performer) return;
    /* NSHapticFeedbackPattern (NSInteger), NSHapticFeedbackPerformanceTimeNow = 1 */
    void (*send)(id, SEL, long, unsigned long) = (void (*)(id, SEL, long, unsigned long))objc_msgSend;
    send(performer, sel_registerName("performFeedbackPattern:performanceTime:"), pattern, 1UL);
}
#endif

KbdHaptics *kbd_haptics_open(void) {
    KbdHaptics *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->scale = 1;
    strcpy(h->name, "none");
#ifdef KBD_APPKIT
    if (appkit_performer()) {
        h->appkit = 1;
        strcpy(h->name, "Force Touch trackpad");
        return h;
    }
#endif
    if (!SDL_WasInit(SDL_INIT_HAPTIC)) {
        if (SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0) h->own_subsystem = 1;
        else return h;
    }
    for (int i = 0; i < SDL_NumHaptics() && !h->rumble; i++) {
        SDL_Haptic *d = SDL_HapticOpen(i);
        if (!d) continue;
        if (SDL_HapticRumbleSupported(d) == SDL_TRUE && SDL_HapticRumbleInit(d) == 0) {
            h->rumble = d;
            const char *n = SDL_HapticName(i);
            SDL_snprintf(h->name, sizeof h->name, "rumble: %s", n ? n : "haptic device");
        } else {
            SDL_HapticClose(d);
        }
    }
    return h;
}

void kbd_haptics_close(KbdHaptics *h) {
    if (!h) return;
    if (h->rumble) SDL_HapticClose(h->rumble);
    if (h->own_subsystem) SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    free(h);
}

const char *kbd_haptics_backend(const KbdHaptics *h) { return h ? h->name : "none"; }

void kbd_haptics_play(KbdHaptics *h, const KbdEvent *e) {
    if (!h || e->kind != KBD_EV_HAPTIC || e->strength <= 0) return;
    uint32_t now = SDL_GetTicks();
    /* Two pulses closer than the actuator can render feel like one mushy
     * buzz; keep the stronger rhythm by dropping the second. */
    if (h->last && now - h->last < MIN_GAP_MS && e->haptic != KBD_HAPTIC_RELEASE) return;
    h->last = now;
#ifdef KBD_APPKIT
    if (h->appkit) {
        long pattern = 0;   /* Generic */
        if (e->haptic == KBD_HAPTIC_MODIFIER || e->haptic == KBD_HAPTIC_SELECTION) pattern = 1;   /* Alignment */
        if (e->haptic == KBD_HAPTIC_FORCE || e->haptic == KBD_HAPTIC_MODE) pattern = 2;           /* LevelChange */
        appkit_perform(pattern);
        return;
    }
#endif
    if (h->rumble) {
        int ms = e->duration_ms < RUMBLE_MIN_MS ? RUMBLE_MIN_MS : e->duration_ms;
        float s = e->strength * h->scale;
        SDL_HapticRumblePlay(h->rumble, s > 1 ? 1 : s, (Uint32)ms);
    }
}
