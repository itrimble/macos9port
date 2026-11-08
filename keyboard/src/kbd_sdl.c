#include "kbd_sdl.h"
#include "kbd_audio.h"
#include "kbd_skin.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KBD_MARK 0x4Bu   /* padding2 of synthetic keyboard events */

_Static_assert((int)KBD_K_LEFT == (int)SDLK_LEFT && (int)KBD_K_CAPSLOCK == (int)SDLK_CAPSLOCK &&
               (int)KBD_K_LGUI == (int)SDLK_LGUI && (int)KBD_K_LALT == (int)SDLK_LALT &&
               (int)KBD_K_RETURN == (int)SDLK_RETURN && (int)KBD_K_BACKSPACE == (int)SDLK_BACKSPACE,
               "kbd key codes must equal SDL keycodes");

struct KbdAudio {
    SDL_AudioDeviceID dev;
    KbdSoundBank *bank;
    KbdMixer mix;
    int muted;
    int own_subsystem;
};

static void callback(void *ud, Uint8 *stream, int len) {
    KbdAudio *a = ud;
    kbd_mixer_render(&a->mix, (int16_t *)stream, len / 4);
}

KbdAudio *kbd_audio_open(void) {
    KbdAudio *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->bank = kbd_bank_create();
    kbd_mixer_init(&a->mix);
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) { kbd_audio_close(a); return NULL; }
        a->own_subsystem = 1;
    }
    SDL_AudioSpec want = {0}, got;
    want.freq = KBD_SYNTH_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 256;
    want.callback = callback;
    want.userdata = a;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!a->dev) { kbd_audio_close(a); return NULL; }
    SDL_PauseAudioDevice(a->dev, 0);
    return a;
}

void kbd_audio_close(KbdAudio *a) {
    if (!a) return;
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    if (a->own_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    kbd_bank_destroy(a->bank);
    free(a);
}

void kbd_audio_set_volume(KbdAudio *a, float v) {
    if (!a) return;
    SDL_LockAudioDevice(a->dev);
    a->mix.master = v < 0 ? 0 : v > 1 ? 1 : v;
    SDL_UnlockAudioDevice(a->dev);
}

void kbd_audio_set_muted(KbdAudio *a, int muted) { if (a) a->muted = muted; }

void kbd_audio_play(KbdAudio *a, const KbdEvent *e, int skin) {
    if (!a || !a->dev || a->muted || e->kind != KBD_EV_SOUND) return;
    const KbdSample *s = kbd_bank_get(a->bank, kbd_skin_sound(skin), e->sound, e->variant);
    SDL_LockAudioDevice(a->dev);
    kbd_mixer_trigger(&a->mix, s, e->pitch, e->gain, e->pan);
    SDL_UnlockAudioDevice(a->dev);
}

static Uint16 sdl_mods(int m) {
    Uint16 o = 0;
    if (m & KBD_MOD_SHIFT) o |= KMOD_LSHIFT;
    if (m & KBD_MOD_CTRL) o |= KMOD_LCTRL;
    if (m & KBD_MOD_OPT) o |= KMOD_LALT;
    if (m & KBD_MOD_CMD) o |= KMOD_LGUI;
    if (m & KBD_MOD_CAPS) o |= KMOD_CAPS;
    return o;
}

static int push_key(KbdSdlTarget *t, SDL_Keycode sym, int down, Uint16 mod) {
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.timestamp = SDL_GetTicks();
    ev.key.windowID = t->window_id;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.sym = sym;
    ev.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
    ev.key.keysym.mod = mod;
    ev.key.padding2 = KBD_MARK;
    return SDL_PushEvent(&ev) > 0;
}

static int push_button(KbdSdlTarget *t, int down, int clicks) {
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.timestamp = SDL_GetTicks();
    ev.button.windowID = t->window_id;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.which = KBD_SDL_MOUSE_ID;
    ev.button.clicks = (Uint8)(clicks > 0 ? clicks : 1);
    ev.button.x = (Sint32)lrintf(t->cursor_x);
    ev.button.y = (Sint32)lrintf(t->cursor_y);
    t->buttons = down;
    return SDL_PushEvent(&ev) > 0;
}

int kbd_sdl_push(KbdSdlTarget *t, const KbdEvent *e) {
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    float sc = t->pointer_scale > 0 ? t->pointer_scale : 1;
    switch (e->kind) {
    case KBD_EV_KEY:
        return push_key(t, (SDL_Keycode)e->key, e->down, sdl_mods(e->mods));
    case KBD_EV_TEXT:
        ev.type = SDL_TEXTINPUT;
        ev.text.timestamp = SDL_GetTicks();
        ev.text.windowID = t->window_id;
        strncpy(ev.text.text, e->text, sizeof ev.text.text - 1);
        return SDL_PushEvent(&ev) > 0;
    case KBD_EV_POINTER: {
        float nx = fminf(fmaxf(t->cursor_x + e->dx * sc, 0), (float)(t->width - 1));
        float ny = fminf(fmaxf(t->cursor_y + e->dy * sc, 0), (float)(t->height - 1));
        ev.type = SDL_MOUSEMOTION;
        ev.motion.timestamp = SDL_GetTicks();
        ev.motion.windowID = t->window_id;
        ev.motion.which = KBD_SDL_MOUSE_ID;
        ev.motion.x = (Sint32)lrintf(nx);
        ev.motion.y = (Sint32)lrintf(ny);
        ev.motion.xrel = (Sint32)lrintf(nx - t->cursor_x);
        ev.motion.yrel = (Sint32)lrintf(ny - t->cursor_y);
        ev.motion.state = t->buttons ? SDL_BUTTON_LMASK : 0;
        t->cursor_x = nx;
        t->cursor_y = ny;
        return SDL_PushEvent(&ev) > 0;
    }
    case KBD_EV_BUTTON: {
        int n = 0, ctrl = (e->mods & KBD_MOD_CTRL) != 0;
        if (ctrl && e->down) n += push_key(t, SDLK_LCTRL, 1, KMOD_LCTRL);
        n += push_button(t, e->down, e->clicks);
        if (ctrl && !e->down) n += push_key(t, SDLK_LCTRL, 0, 0);
        return n;
    }
    case KBD_EV_SCROLL: {
        t->wheel_x += e->dx;
        t->wheel_y += e->dy;
        int ix = (int)t->wheel_x, iy = (int)t->wheel_y;
        if (!ix && !iy) return 0;
        t->wheel_x -= (float)ix;
        t->wheel_y -= (float)iy;
        ev.type = SDL_MOUSEWHEEL;
        ev.wheel.timestamp = SDL_GetTicks();
        ev.wheel.windowID = t->window_id;
        ev.wheel.which = KBD_SDL_MOUSE_ID;
        ev.wheel.x = ix;
        ev.wheel.y = iy;
        ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        return SDL_PushEvent(&ev) > 0;
    }
    default:
        return 0;
    }
}

static void swap_pc(SDL_Event *ev) {
    SDL_Keysym *k = &ev->key.keysym;
    static const SDL_Keycode from[] = { SDLK_LALT, SDLK_RALT, SDLK_LGUI, SDLK_RGUI };
    static const SDL_Keycode to[] = { SDLK_LGUI, SDLK_RGUI, SDLK_LALT, SDLK_RALT };
    for (int i = 0; i < 4; i++)
        if (k->sym == from[i]) { k->sym = to[i]; k->scancode = SDL_GetScancodeFromKey(to[i]); break; }
    Uint16 m = k->mod, o = m & ~(KMOD_ALT | KMOD_GUI);
    if (m & KMOD_LALT) o |= KMOD_LGUI;
    if (m & KMOD_RALT) o |= KMOD_RGUI;
    if (m & KMOD_LGUI) o |= KMOD_LALT;
    if (m & KMOD_RGUI) o |= KMOD_RALT;
    k->mod = o;
}

int kbd_sdl_handle(Kbd *k, SDL_Event *ev, int sw, int sh) {
    switch (ev->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        if (ev->key.padding2 == KBD_MARK) return 0;          /* our own synthetic key */
        if (ev->key.repeat) return 0;
        kbd_hw_key(k, (int)ev->key.keysym.scancode, ev->type == SDL_KEYDOWN, ev->key.timestamp);
        if (kbd_settings(k)->pc_modifiers) swap_pc(ev);
        return 0;                                             /* host still handles it */
    case SDL_FINGERDOWN: {
        int used = kbd_touch_down(k, (int64_t)ev->tfinger.fingerId, ev->tfinger.x * sw, ev->tfinger.y * sh,
                                  ev->tfinger.timestamp);
        if (used) kbd_touch_force(k, (int64_t)ev->tfinger.fingerId, ev->tfinger.pressure, ev->tfinger.timestamp);
        return used;
    }
    case SDL_FINGERMOTION: {
        int used = kbd_touch_move(k, (int64_t)ev->tfinger.fingerId, ev->tfinger.x * sw, ev->tfinger.y * sh,
                                  ev->tfinger.timestamp);
        if (used) kbd_touch_force(k, (int64_t)ev->tfinger.fingerId, ev->tfinger.pressure, ev->tfinger.timestamp);
        return used;
    }
    case SDL_FINGERUP:
        return kbd_touch_up(k, (int64_t)ev->tfinger.fingerId, ev->tfinger.x * sw, ev->tfinger.y * sh,
                            ev->tfinger.timestamp);
    default:
        return 0;
    }
}
