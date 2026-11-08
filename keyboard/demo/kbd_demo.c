/* OS999 keyboard simulator.
 *
 * A folding phone on your Mac: folded (cover screen), open flat, or bent
 * into Flex. The mouse is a finger; Shift-drag is two fingers; a right
 * click is a two-finger tap. Typing on the Mac's own keyboard exercises the
 * Bluetooth path: the on-screen keyboard steps aside for the hardware strip,
 * and holding Command shows the shortcut sheet. Everything the keyboard
 * produces travels through kbd_sdl_push as ordinary SDL events, exactly as it
 * would into the OS999 desktop.
 *
 * Hold F8 (press) or F9 (force press) while touching the trackpad to feel
 * the clickpad; on a Mac with a Force Touch trackpad every haptic plays
 * through it.
 *
 *   F1 folded  F2 open  F3 flex  F4 split  F5/F6 skin  F7 sounds
 *   ./build/kbd_demo [--font ../fonts/ChicagoFLF.ttf] [--touch]
 */
#include "demo.h"
#include "kbd_sdl.h"
#include "../src/kbd_skin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIN_W = 1280, WIN_H = 860, COVER_W = 360, COVER_H = 780, INNER_W = 880, INNER_H = 740, PANEL_W = 280 };
enum { B_COVER = 1, B_OPEN, B_FLEX, B_SPLIT, B_SKIN_PREV, B_SKIN_NEXT, B_SOUND, B_CLICK, B_PC, B_MOTION,
       B_HAPTICS, B_TFD, B_LIGHT, B_MEDIUM, B_FIRM };

typedef struct {
    Kbd *k;
    KbdAudio *audio;
    KbdHaptics *haptics;
    KbdSdlTarget target;
    float sim_pressure;
    const char *last_feel;
    uint32_t feel_at;
    DemoEditor ed;
    int posture, running, touch_input, mouse_finger, two_finger;
    float hd;
    DRect screen;           /* in drawable pixels */
    DemoButton bt[32];
    int nbt;
} Demo;

static void layout_buttons(Demo *d) {
    float dp = d->hd, x = 24 * dp, y = 22 * dp, w = 232 * dp, h = 26 * dp, g = 6 * dp;
    KbdSettings *s = kbd_settings(d->k);
    d->nbt = 0;
#define LABEL(t) d->bt[d->nbt++] = (DemoButton){ t, -1, { x, y, w, 18 * dp }, 0 }, y += 20 * dp
#define BTN(t, id, bx, bw, on) d->bt[d->nbt++] = (DemoButton){ t, id, { bx, y, bw, h }, on }
    float tw = (w - 2 * g) / 3, hw = (w - g) / 2;
    LABEL("Posture");
    BTN("Folded", B_COVER, x, tw, d->posture == 0);
    BTN("Open", B_OPEN, x + tw + g, tw, d->posture == 1);
    BTN("Flex", B_FLEX, x + 2 * (tw + g), tw, d->posture == 2);
    y += h + g;
    BTN("Split for thumbs", B_SPLIT, x, w, kbd_split(d->k));
    y += h + 2 * g;
    LABEL("Skin");
    BTN("<  Previous", B_SKIN_PREV, x, hw, 0);
    BTN("Next  >", B_SKIN_NEXT, x + hw + g, hw, 0);
    y += h + 2 * g;
    LABEL("Trackpad");
    BTN("Haptics", B_HAPTICS, x, hw, s->haptics);
    BTN("3-finger drag", B_TFD, x + hw + g, hw, s->three_finger_drag);
    y += h + g;
    BTN("Light", B_LIGHT, x, tw, s->click_pressure == 0);
    BTN("Medium", B_MEDIUM, x + tw + g, tw, s->click_pressure == 1);
    BTN("Firm", B_FIRM, x + 2 * (tw + g), tw, s->click_pressure == 2);
    y += h + 2 * g;
    LABEL("Sound");
    BTN("Key sounds", B_SOUND, x, hw, s->sounds);
    BTN("Click along", B_CLICK, x + hw + g, hw, s->hw_click_along);
    y += h + 2 * g;
    LABEL("Bluetooth keyboard and motion");
    BTN("PC modifiers", B_PC, x, hw, s->pc_modifiers);
    BTN("Reduce motion", B_MOTION, x + hw + g, hw, s->reduce_motion);
    y += h + 3 * g;
    LABEL("Mouse is a finger; Shift-drag is two.");
    LABEL("Right-click: two-finger tap.");
    LABEL("Hold space: steer the pointer.");
    LABEL("Hold F8 / F9 on the pad: press /");
    LABEL("force press. Mac keys: Bluetooth.");
#undef LABEL
#undef BTN
}

static void set_posture(Demo *d, int p) {
    d->posture = p;
    int w = p == 0 ? COVER_W : INNER_W, h = p == 0 ? COVER_H : INNER_H;
    float sw = w * d->hd, sh = h * d->hd;
    float area_x = PANEL_W * d->hd, area_w = WIN_W * d->hd - area_x;
    d->screen = (DRect){ area_x + (area_w - sw) / 2, (WIN_H * d->hd - sh) / 2, sw, sh };
    kbd_set_surface(d->k, (int)sw, (int)sh);
    kbd_set_posture(d->k, p == 0 ? KBD_COVER : p == 1 ? KBD_OPEN : KBD_FLEX);
    d->target.width = (int)sw;
    d->target.height = (int)sh;
    if (d->ed.px <= 0 || d->ed.px > sw || d->ed.py > sh) { d->ed.px = sw / 2; d->ed.py = sh / 3; }
    d->target.cursor_x = d->ed.px;
    d->target.cursor_y = d->ed.py;
    layout_buttons(d);
}

static void press_button(Demo *d, int id) {
    KbdSettings *s = kbd_settings(d->k);
    switch (id) {
    case B_COVER: set_posture(d, 0); break;
    case B_OPEN: set_posture(d, 1); break;
    case B_FLEX: set_posture(d, 2); break;
    case B_SPLIT: kbd_set_split(d->k, !kbd_split(d->k)); break;
    case B_SKIN_PREV: kbd_set_skin(d->k, kbd_skin(d->k) - 1); break;
    case B_SKIN_NEXT: kbd_set_skin(d->k, kbd_skin(d->k) + 1); break;
    case B_SOUND: s->sounds = !s->sounds; break;
    case B_CLICK: s->hw_click_along = !s->hw_click_along; break;
    case B_PC: s->pc_modifiers = !s->pc_modifiers; break;
    case B_MOTION: s->reduce_motion = !s->reduce_motion; break;
    case B_HAPTICS: s->haptics = !s->haptics; break;
    case B_TFD: s->three_finger_drag = !s->three_finger_drag; break;
    case B_LIGHT: s->click_pressure = 0; break;
    case B_MEDIUM: s->click_pressure = 1; break;
    case B_FIRM: s->click_pressure = 2; break;
    }
    layout_buttons(d);
}

static float area_height(Demo *d) {
    if (d->posture == 2) return d->screen.h * 0.5f;
    return d->screen.h - (float)kbd_occluded_height(d->k);
}

/* Clicks the OS receives (from a trackpad tap, or a mouse finger that missed
 * the keyboard): inside the text window focuses it and brings the keyboard. */
static void os_click(Demo *d, float x, float y) {
    float top = 28 * d->hd;
    int in_window = y > top && y < area_height(d) && x > 8 * d->hd && x < d->screen.w - 8 * d->hd;
    d->ed.focused = in_window;
    kbd_show(d->k, in_window || d->posture == 2);
}

static void finger(Demo *d, int phase, float wx, float wy, int64_t id) {
    float x = wx * d->hd - d->screen.x, y = wy * d->hd - d->screen.y;
    uint32_t now = SDL_GetTicks();
    int used = phase == 0 ? kbd_touch_down(d->k, id, x, y, now)
             : phase == 1 ? kbd_touch_move(d->k, id, x, y, now) : kbd_touch_up(d->k, id, x, y, now);
    if (used && phase < 2 && d->sim_pressure > 0) kbd_touch_force(d->k, id, d->sim_pressure, now);
    if (!used && phase == 0 && x >= 0 && y >= 0 && x < d->screen.w && y < d->screen.h) {
        d->ed.px = d->target.cursor_x = x;
        d->ed.py = d->target.cursor_y = y;
        os_click(d, x, y);
    }
}

/* F8 and F9 stand in for finger pressure on a desk Mac. */
static void sim_press(Demo *d, float p) {
    d->sim_pressure = p;
    if (d->mouse_finger) kbd_touch_force(d->k, 1, p, SDL_GetTicks());
}

static void handle_key(Demo *d, SDL_Event *ev) {
    SDL_Keycode sym = ev->key.keysym.sym;
    Uint16 mod = ev->key.keysym.mod;
    if (sym == SDLK_F8 || sym == SDLK_F9) {
        if (!ev->key.repeat) sim_press(d, sym == SDLK_F8 ? 0.6f : 1.0f);
        return;
    }
    if (sym >= SDLK_F1 && sym <= SDLK_F7) {
        static const int map[] = { B_COVER, B_OPEN, B_FLEX, B_SPLIT, B_SKIN_PREV, B_SKIN_NEXT, B_SOUND };
        press_button(d, map[sym - SDLK_F1]);
        return;
    }
    if ((mod & KMOD_GUI) && sym == 'q') { d->running = 0; return; }
    if (mod & (KMOD_GUI | KMOD_CTRL)) return;
    if (sym == SDLK_BACKSPACE) demo_editor_backspace(&d->ed, (mod & KMOD_ALT) != 0);
    else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) demo_editor_insert(&d->ed, "\n");
    else if (sym == SDLK_TAB) demo_editor_insert(&d->ed, "    ");
}

static void handle(Demo *d, SDL_Event *ev) {
    int sw = (int)d->screen.w, sh = (int)d->screen.h;
    switch (ev->type) {
    case SDL_QUIT: d->running = 0; return;
    case SDL_KEYDOWN:
        kbd_sdl_handle(d->k, ev, sw, sh);
        handle_key(d, ev);
        return;
    case SDL_KEYUP:
        kbd_sdl_handle(d->k, ev, sw, sh);
        if (ev->key.keysym.sym == SDLK_F8 || ev->key.keysym.sym == SDLK_F9) sim_press(d, 0.08f);
        return;
    case SDL_TEXTINPUT: demo_editor_insert(&d->ed, ev->text.text); return;
    case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
        if (d->touch_input) {
            int ph = ev->type == SDL_FINGERDOWN ? 0 : ev->type == SDL_FINGERMOTION ? 1 : 2;
            finger(d, ph, ev->tfinger.x * WIN_W, ev->tfinger.y * WIN_H, (int64_t)ev->tfinger.fingerId + 100);
        }
        return;
    case SDL_MOUSEMOTION:
        if (ev->motion.which == KBD_SDL_MOUSE_ID) { d->ed.px = (float)ev->motion.x; d->ed.py = (float)ev->motion.y; return; }
        if (d->mouse_finger) {
            finger(d, 1, (float)ev->motion.x, (float)ev->motion.y, 1);
            if (d->two_finger) finger(d, 1, (float)ev->motion.x + 40, (float)ev->motion.y, 2);
        }
        return;
    case SDL_MOUSEBUTTONDOWN:
        if (ev->button.which == KBD_SDL_MOUSE_ID) { os_click(d, d->ed.px, d->ed.py); return; }
        if (ev->button.which == SDL_TOUCH_MOUSEID) return;
        for (int i = 0; i < d->nbt; i++) {
            DRect r = d->bt[i].r;
            float x = ev->button.x * d->hd, y = ev->button.y * d->hd;
            if (d->bt[i].id > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) {
                press_button(d, d->bt[i].id);
                return;
            }
        }
        d->two_finger = ev->button.button == SDL_BUTTON_RIGHT || (SDL_GetModState() & KMOD_SHIFT);
        d->mouse_finger = 1;
        finger(d, 0, (float)ev->button.x, (float)ev->button.y, 1);
        if (d->two_finger) finger(d, 0, (float)ev->button.x + 40, (float)ev->button.y, 2);
        return;
    case SDL_MOUSEBUTTONUP:
        if (ev->button.which == KBD_SDL_MOUSE_ID || ev->button.which == SDL_TOUCH_MOUSEID) return;
        if (!d->mouse_finger) return;
        finger(d, 2, (float)ev->button.x, (float)ev->button.y, 1);
        if (d->two_finger) finger(d, 2, (float)ev->button.x + 40, (float)ev->button.y, 2);
        d->mouse_finger = d->two_finger = 0;
        return;
    case SDL_MOUSEWHEEL:
        if (ev->wheel.which == KBD_SDL_MOUSE_ID) d->ed.scroll = SDL_max(0, d->ed.scroll + (float)ev->wheel.y);
        return;
    }
}

static void render(Demo *d, Fb *win, Fb *scr) {
    float dp = d->hd;
    g_rrect(win, 0, 0, (float)win->w, (float)win->h, 0, 0xFF3A3D47u, 0xFF23252Cu, 1);
    g_rrect(win, 0, 0, PANEL_W * dp, (float)win->h, 0, 0xFF2E3039u, 0xFF262830u, 1);
    const KbdSkin *sk = kbd_skin_get(kbd_skin(d->k));
    demo_draw_panel(win, dp, d->bt, d->nbt, sk->name, sk->blurb);
    char feel[128];
    SDL_snprintf(feel, sizeof feel, "Haptics: %s", kbd_haptics_backend(d->haptics));
    g_text(win, 24 * dp, win->h - 64 * dp, 11 * dp, 0xFFC8C8D2u, 1, feel);
    uint32_t age = SDL_GetTicks() - d->feel_at;
    if (d->last_feel && age < 700) {
        float a = 1.0f - (float)age / 700.0f;
        g_disc(win, 30 * dp, win->h - 38 * dp, 5 * dp, 0xFF8C8CF0u, a);
        g_text(win, 42 * dp, win->h - 45 * dp, 12 * dp, 0xFFFFFFFFu, a, d->last_feel);
    }
    demo_draw_device(win, dp, d->screen, d->posture);
    scr->w = (int)d->screen.w;
    scr->h = (int)d->screen.h;
    demo_draw_os(scr, dp, &d->ed, area_height(d), SDL_GetTicks(), "Untitled 1");
    kbd_draw(d->k, scr);
    demo_blit(win, scr, (int)d->screen.x, (int)d->screen.y);
}

int main(int argc, char **argv) {
    const char *font = "../fonts/ChicagoFLF.ttf", *shot = NULL;
    int start_posture = 0, start_skin = 0, frames = 0;
    Demo d;
    memset(&d, 0, sizeof d);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--font") && i + 1 < argc) font = argv[++i];
        else if (!strcmp(argv[i], "--touch")) d.touch_input = 1;
        else if (!strcmp(argv[i], "--posture") && i + 1 < argc) start_posture = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--skin") && i + 1 < argc) start_skin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];   /* render, save PPM, exit */
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("OS999 Keyboards", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    int dw, dh;
    SDL_GetRendererOutputSize(ren, &dw, &dh);
    d.hd = (float)dw / WIN_W;
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, dw, dh);
    uint32_t *wpx = calloc((size_t)dw * dh, 4), *spx = calloc((size_t)dw * dh, 4);
    if (!tex || !wpx || !spx) return 1;
    Fb wfb = { .px = wpx, .w = dw, .h = dh }, sfb = { .px = spx, .w = dw, .h = dh };
    if (!kbd_load_fonts(font)) fprintf(stderr, "No outline font at %s; using the bitmap face.\n", font);
    d.k = kbd_create(d.hd);
    d.audio = kbd_audio_open();
    d.haptics = kbd_haptics_open();
    if (!d.audio) fprintf(stderr, "No audio device: %s\n", SDL_GetError());
    d.target.window_id = SDL_GetWindowID(win);
    d.target.pointer_scale = 1;
    static const char *const shortcuts[][2] = { { "N", "New Folder" }, { "O", "Open" }, { "W", "Close Window" },
                                                { "F", "Find" }, { "Q", "Quit" } };
    kbd_set_shortcuts(d.k, shortcuts, 5);
    demo_editor_insert(&d.ed, "Welcome to OS999 on a folding phone. ");
    d.ed.focused = 1;
    kbd_set_skin(d.k, start_skin);
    set_posture(&d, start_posture < 0 || start_posture > 2 ? 0 : start_posture);
    kbd_show(d.k, 1);
    SDL_StartTextInput();
    d.running = 1;
    while (d.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handle(&d, &ev);
        kbd_tick(d.k, SDL_GetTicks());
        KbdEvent e;
        static const char *const feel[KBD_HAPTIC_COUNT] = { "key", "wide key", "modifier", "detent",
                                                             "tap", "click", "release", "force click", "trackpad mode" };
        while (kbd_poll_event(d.k, &e)) {
            kbd_audio_play(d.audio, &e, kbd_skin(d.k));
            kbd_haptics_play(d.haptics, &e);
            if (e.kind == KBD_EV_HAPTIC && e.haptic >= 0 && e.haptic < KBD_HAPTIC_COUNT) {
                d.last_feel = feel[e.haptic];
                d.feel_at = SDL_GetTicks();
            }
            kbd_sdl_push(&d.target, &e);
        }
        while (SDL_PollEvent(&ev)) handle(&d, &ev);   /* deliver the keyboard's events this frame */
        render(&d, &wfb, &sfb);
        if (shot && ++frames == 90) {
            FILE *f = fopen(shot, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", dw, dh);
                for (int i = 0; i < dw * dh; i++) {
                    unsigned char c[3] = { (unsigned char)(wpx[i] >> 16), (unsigned char)(wpx[i] >> 8), (unsigned char)wpx[i] };
                    fwrite(c, 1, 3, f);
                }
                fclose(f);
            }
            d.running = 0;
        }
        SDL_UpdateTexture(tex, NULL, wpx, dw * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    kbd_audio_close(d.audio);
    kbd_haptics_close(d.haptics);
    kbd_destroy(d.k);
    free(wpx);
    free(spx);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
