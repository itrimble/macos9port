/* The SDL path into a host: run with SDL_VIDEODRIVER=dummy. */
#include "kbd_sdl.h"
#include "../src/kbd_int.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    Kbd *k = kbd_create(1);
    kbd_set_surface(k, 390, 860);
    kbd_show(k, 1);
    uint32_t t = 1000;
    for (int i = 0; i < 100; i++) kbd_tick(k, t += 16);
    KbdEvent e;
    while (kbd_poll_event(k, &e)) {}
    int s = -1;
    for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == KK_A) s = i;
    KRect r = k->lay.keys[s].r;
    kbd_touch_down(k, 1, r.x + r.w / 2, r.y + r.h / 2, t);
    kbd_touch_up(k, 1, r.x + r.w / 2, r.y + r.h / 2, t + 40);
    KbdSdlTarget target = { .window_id = 0, .cursor_x = 100, .cursor_y = 100, .width = 1024, .height = 768 };
    while (kbd_poll_event(k, &e)) kbd_sdl_push(&target, &e);
    SDL_Event ev;
    int down = 0, up = 0, text = 0;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == 'a') {
            down++;
            CHECK(kbd_sdl_handle(k, &ev, 390, 860) == 0);
            CHECK(!kbd_hw_active(k));                  /* our own key is not a Bluetooth key */
        }
        if (ev.type == SDL_KEYUP && ev.key.keysym.sym == 'a') up++;
        if (ev.type == SDL_TEXTINPUT && !strcmp(ev.text.text, "a")) text++;
    }
    CHECK(down == 1 && up == 1 && text == 1);
    KbdEvent mv = { .kind = KBD_EV_POINTER, .dx = 5000, .dy = -20 };
    kbd_sdl_push(&target, &mv);
    KbdEvent cc = { .kind = KBD_EV_BUTTON, .button = 1, .down = 1, .clicks = 1, .mods = KBD_MOD_CTRL };
    kbd_sdl_push(&target, &cc);
    cc.down = 0;
    kbd_sdl_push(&target, &cc);
    int seq = 0;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_MOUSEMOTION) { CHECK(ev.motion.x == 1023 && ev.motion.y == 80); CHECK(ev.motion.which == KBD_SDL_MOUSE_ID); }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_LCTRL) seq = seq * 10 + 1;
        if (ev.type == SDL_MOUSEBUTTONDOWN) seq = seq * 10 + 2;
        if (ev.type == SDL_MOUSEBUTTONUP) seq = seq * 10 + 3;
        if (ev.type == SDL_KEYUP && ev.key.keysym.sym == SDLK_LCTRL) seq = seq * 10 + 4;
    }
    CHECK(seq == 1234);                                /* control-click, in order */
    KbdEvent sc = { .kind = KBD_EV_SCROLL, .dy = 0.6f };
    kbd_sdl_push(&target, &sc);
    kbd_sdl_push(&target, &sc);
    int wheels = 0;
    while (SDL_PollEvent(&ev)) if (ev.type == SDL_MOUSEWHEEL) wheels += ev.wheel.y;
    CHECK(wheels == 1);                                /* fractions accumulate */
    SDL_Event hw;
    memset(&hw, 0, sizeof hw);
    hw.type = SDL_KEYDOWN;
    hw.key.keysym.scancode = SDL_SCANCODE_LALT;
    hw.key.keysym.sym = SDLK_LALT;
    hw.key.keysym.mod = KMOD_LALT;
    hw.key.timestamp = t + 100;
    kbd_settings(k)->pc_modifiers = 1;
    kbd_sdl_handle(k, &hw, 390, 860);
    CHECK(hw.key.keysym.sym == SDLK_LGUI && (hw.key.keysym.mod & KMOD_LGUI));   /* PC Alt -> Command */
    hw.key.keysym.scancode = SDL_SCANCODE_J;
    hw.key.keysym.sym = 'j';
    kbd_sdl_handle(k, &hw, 390, 860);
    CHECK(kbd_hw_active(k));
    kbd_destroy(k);
    /* Pressure from real touch events drives the clickpad. */
    k = kbd_create(1);
    kbd_set_surface(k, 900, 760);
    kbd_set_posture(k, KBD_FLEX);
    kbd_show(k, 1);
    for (int i = 0; i < 100; i++) kbd_tick(k, t += 16);
    while (kbd_poll_event(k, &e)) {}
    SDL_Event f;
    memset(&f, 0, sizeof f);
    f.type = SDL_FINGERDOWN;
    f.tfinger.fingerId = 7;
    f.tfinger.x = (k->lay.pad.x + k->lay.pad.w / 2) / 900.0f;
    f.tfinger.y = (k->lay.pad.y + k->lay.pad.h / 2) / 760.0f;
    f.tfinger.pressure = 0.2f;
    f.tfinger.timestamp = t;
    CHECK(kbd_sdl_handle(k, &f, 900, 760) == 1);
    f.type = SDL_FINGERMOTION;
    int buttons = 0;
    for (int i = 0; i < 4; i++) {
        f.tfinger.pressure = 0.7f;
        f.tfinger.timestamp = t += 8;
        kbd_sdl_handle(k, &f, 900, 760);
    }
    while (kbd_poll_event(k, &e)) buttons += e.kind == KBD_EV_BUTTON && e.down;
    CHECK(buttons == 1);
    KbdHaptics *hp = kbd_haptics_open();                /* dummy drivers: a safe no-op */
    CHECK(kbd_haptics_backend(hp) != NULL);
    KbdEvent he = { .kind = KBD_EV_HAPTIC, .haptic = KBD_HAPTIC_CLICK, .strength = 0.8f, .duration_ms = 14 };
    kbd_haptics_play(hp, &he);
    kbd_haptics_close(hp);
    kbd_destroy(k);
    SDL_Quit();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
