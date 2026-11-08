/* OS999 for folding phones: the SDL host.
 *
 * On iOS and Android this is the app. On a desk it is a simulator: the window
 * shows a device profile at its real pixel count, the mouse is a finger, and
 *   F5  next posture (cover, unfolded, laptop, book)
 *   F6  next device profile
 *   F7  pretend a Bluetooth keyboard is attached / detached
 *   F8  the mouse is a finger (default) or a trackpad/mouse pointer
 * Tablet profiles start with a keyboard and pointer attached.
 *   Option-drag        second finger mirrored through the centre (pinch)
 *   Shift-Option-drag  second finger alongside (two-finger scroll / tap)
 */
#include "mobile_shell.h"
#include "mobile_platform.h"
#include "os999_mobile.h"
#include "kbd_sdl.h"
#include "desktop_host.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tex_w, tex_h;
    KbdAudio *audio;
    KbdHaptics *haptics;
    MobileShell shell;
    int sim, sim_device, sim_hw, sim_pointer, paused;
    int saw_pointer;             /* a real mouse/trackpad has been used (device builds) */
    int text_on;                 /* iOS: SDL text input running for a hardware keyboard */
    MobilePosture sim_posture;
    unsigned seen_seq;
    int out_w, out_h;
    int mirror;                  /* simulator second finger: 0 none, 1 mirror, 2 alongside */
} Host;

enum { SIM_SECOND = 1001 };

static void on_haptic(void *ctx, int type, float strength, float sharpness, int ms) {
    Host *h = ctx;
    if (MOBILE_PHONE) { os999_mobile_haptic(type, strength, sharpness, ms); return; }
    KbdEvent e = {.kind = KBD_EV_HAPTIC, .haptic = type, .strength = strength,
                  .sharpness = sharpness, .duration_ms = ms};
    kbd_haptics_play(h->haptics, &e);
}

static void on_kbd_event(void *ctx, const KbdEvent *e) {
    Host *h = ctx;
    if (e->kind == KBD_EV_SOUND && h->shell.kbd) kbd_audio_play(h->audio, e, kbd_skin(h->shell.kbd));
}

static char *on_clipboard(void *ctx) {
    (void)ctx;
    char *t = SDL_GetClipboardText();
    char *copy = t ? strdup(t) : NULL;
    SDL_free(t);
    return copy;
}

static void sim_window_size(Host *h, const MobileScreen *s) {
    SDL_DisplayMode dm;
    int max_h = 900;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) max_h = dm.h * 85 / 100;
    float k = (float)max_h / s->h;
    if (k > 0.5f) k = 0.5f;
    SDL_SetWindowSize(h->win, (int)(s->w * k), (int)(s->h * k));
    SDL_RenderSetLogicalSize(h->ren, s->w, s->h);
}

/* The screen as the platform reports it right now. */
static void read_screen(Host *h, MobileScreen *s) {
    if (h->sim) {
        mobile_device_screen(mobile_device_at(h->sim_device), h->sim_posture, s);
        s->hw_keyboard = h->sim_hw;
        s->pointer = h->sim_pointer;
        return;
    }
    Os999MobileReport r;
    os999_mobile_snapshot(&r, 0);
    memset(s, 0, sizeof *s);
    s->w = h->out_w;
    s->h = h->out_h;
    s->ppi = r.ppi;
    if (s->ppi <= 0) {
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0) s->ppi = ddpi;
    }
    s->inset_l = r.inset_l; s->inset_t = r.inset_t; s->inset_r = r.inset_r; s->inset_b = r.inset_b;
    s->inner = r.inner;
    s->fold_state = (MobileFoldState)r.fold_state;
    s->fold_axis = (MobileFoldAxis)r.fold_axis;
    s->fold = (Rect){r.fold_l, r.fold_t, r.fold_r - r.fold_l, r.fold_b - r.fold_t};
    s->hw_keyboard = r.hw_keyboard;
    s->pointer = h->saw_pointer;
    s->tablet = r.tablet;
}

static void refresh_screen(Host *h, int force) {
    SDL_GetRendererOutputSize(h->ren, &h->out_w, &h->out_h);
    Os999MobileReport r;
    int changed = os999_mobile_snapshot(&r, h->seen_seq);
    h->seen_seq = r.seq;
    if (!changed && !force) return;
    MobileScreen s;
    read_screen(h, &s);
    if (h->sim) sim_window_size(h, &s);
    else SDL_RenderSetLogicalSize(h->ren, s.w, s.h);   /* mouse events arrive in pixels */
    mobile_shell_set_screen(&h->shell, &s, SDL_GetTicks());
    if (h->tex_w != s.w || h->tex_h != s.h) {
        if (h->tex) SDL_DestroyTexture(h->tex);
        h->tex = SDL_CreateTexture(h->ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, s.w, s.h);
        h->tex_w = s.w;
        h->tex_h = s.h;
    }
}

static void sim_key(Host *h, SDL_Keycode k) {
    if (k == SDLK_F5) h->sim_posture = (MobilePosture)((h->sim_posture + 1) % MP_POSTURE_COUNT);
    if (k == SDLK_F6) h->sim_device = (h->sim_device + 1) % mobile_device_count();
    if (k == SDLK_F7) h->sim_hw = !h->sim_hw;
    if (k == SDLK_F8) h->sim_pointer = !h->sim_pointer;
    SDL_ShowCursor(h->sim_pointer ? SDL_DISABLE : SDL_ENABLE);   /* OS999 draws its own arrow */
    if (k == SDLK_F6 && mobile_device_at(h->sim_device)->tablet) h->sim_hw = h->sim_pointer = 1;
    fprintf(stderr, "OS999 simulator: %s, %s%s%s\n", mobile_device_at(h->sim_device)->name,
            mobile_posture_name(h->sim_posture), h->sim_hw ? ", hardware keyboard" : "",
            h->sim_pointer ? ", pointer" : "");
    refresh_screen(h, 1);
}

static void mouse(Host *h, const SDL_Event *ev) {
    /* Touches synthesise mouse events on some platforms; only real pointers count.
     * In the simulator the mouse is a finger unless F8 made it a pointer. */
    Uint32 which = ev->type == SDL_MOUSEWHEEL ? ev->wheel.which :
                   ev->type == SDL_MOUSEMOTION ? ev->motion.which : ev->button.which;
    if (which == SDL_TOUCH_MOUSEID || which == KBD_SDL_MOUSE_ID) return;
    if (h->sim && !h->sim_pointer) return;
    /* iPadOS reports a hover at (0,0) before any real pointer exists; a
     * pointer counts once it moves somewhere or a button is pressed. */
    if (!h->sim && !h->saw_pointer) {
        if (ev->type == SDL_MOUSEMOTION && ev->motion.x == 0 && ev->motion.y == 0) return;
        h->saw_pointer = 1;
        h->shell.screen.pointer = 1;
    }
    MobileShell *m = &h->shell;
    if (ev->type == SDL_MOUSEWHEEL) {
        float y = ev->wheel.preciseY != 0 ? ev->wheel.preciseY : (float)ev->wheel.y;
        /* Command or Control with the wheel zooms, as browsers do; ChromeOS
         * and some Android touchpads send a pinch this way too. */
        if (SDL_GetModState() & (KMOD_GUI | KMOD_CTRL)) mobile_shell_magnify(m, SDL_powf(1.1f, y));
        else mobile_shell_wheel(m, y);
        return;
    }
    if (ev->type == SDL_MOUSEMOTION) {
        mobile_shell_mouse(m, MSH_MOVE, (float)ev->motion.x, (float)ev->motion.y, 0, 0, ev->motion.timestamp);
        return;
    }
    int button = ev->button.button == SDL_BUTTON_RIGHT ||
                 (ev->button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_CTRL))
                     ? MSH_BUTTON_SECONDARY : MSH_BUTTON_PRIMARY;
    mobile_shell_mouse(m, ev->type == SDL_MOUSEBUTTONDOWN ? MSH_DOWN : MSH_UP,
                       (float)ev->button.x, (float)ev->button.y, button, ev->button.clicks,
                       ev->button.timestamp);
}

static void finger(Host *h, const SDL_Event *ev) {
    MobileShell *m = &h->shell;
    if (h->sim && h->sim_pointer && ev->tfinger.touchId == SDL_MOUSE_TOUCHID) return;
    float x = ev->tfinger.x * m->screen.w, y = ev->tfinger.y * m->screen.h;
    int phase = ev->type == SDL_FINGERDOWN ? MSH_DOWN : ev->type == SDL_FINGERUP ? MSH_UP : MSH_MOVE;
    uint32_t ms = ev->tfinger.timestamp ? ev->tfinger.timestamp : SDL_GetTicks();
    int64_t id = (int64_t)ev->tfinger.fingerId;
    if (h->sim && phase == MSH_DOWN) {
        SDL_Keymod mod = SDL_GetModState();
        h->mirror = (mod & KMOD_ALT) ? ((mod & KMOD_SHIFT) ? 2 : 1) : 0;
    }
    mobile_shell_finger(m, phase, id, x, y, ms);
    if (phase != MSH_UP && ev->tfinger.pressure > 0)   /* clickpad stages and Force Click */
        mobile_shell_finger_force(m, id, ev->tfinger.pressure, ms);
    if (h->sim && h->mirror) {
        float ox = h->mirror == 1 ? m->screen.w - x : x + 140 * m->layout.dp;
        float oy = h->mirror == 1 ? m->screen.h - y : y;
        mobile_shell_finger(m, phase, SIM_SECOND, ox, oy, ms);
        if (phase == MSH_UP) h->mirror = 0;
    }
}

static void handle(Host *h, SDL_Event *ev, int *quit) {
    MobileShell *m = &h->shell;
    switch (ev->type) {
    case SDL_QUIT: *quit = 1; break;
    case SDL_APP_DIDENTERBACKGROUND: h->paused = 1; mobile_shell_finger(m, MSH_CANCEL, 0, 0, 0, 0); break;
    case SDL_APP_DIDENTERFOREGROUND: h->paused = 0; m->dirty = 1; break;
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED && !h->sim) refresh_screen(h, 1);
        m->dirty = 1;
        break;
    case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: finger(h, ev); break;
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
        mouse(h, ev);
        break;
    case SDL_TEXTINPUT:   /* composed text: capitals, symbols, Option letters, IMEs */
        mobile_shell_text(m, ev->text.text);
        break;
    case SDL_KEYDOWN: case SDL_KEYUP:
        if (m->kbd && kbd_sdl_handle(m->kbd, ev, m->screen.w, m->screen.h)) break;
        if (ev->type != SDL_KEYDOWN) break;
        if (h->sim && !ev->key.repeat && ev->key.keysym.sym >= SDLK_F5 && ev->key.keysym.sym <= SDLK_F7) {
            sim_key(h, ev->key.keysym.sym);
            break;
        }
        {
            int key = ev->key.keysym.sym == SDLK_KP_ENTER ? '\r' : ev->key.keysym.sym;
            if (key == SDLK_AC_BACK) key = SDLK_ESCAPE;   /* Android back: close menus and dialogs */
            mobile_shell_key(m, key, (ev->key.keysym.mod & KMOD_GUI) ? MOD_CMD : 0);
        }
        break;
    default: break;
    }
}

static int parse_args(Host *h, int argc, char **argv) {
    h->sim = !MOBILE_PHONE;
    h->sim_posture = MP_COVER;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--device") == 0) {
            for (int d = 0; d < mobile_device_count(); d++)
                if (strcmp(mobile_device_at(d)->id, argv[i + 1]) == 0) h->sim_device = d;
            if (mobile_device_at(h->sim_device)->tablet) h->sim_hw = h->sim_pointer = 1;
        } else if (strcmp(argv[i], "--posture") == 0) {
            for (int p = 0; p < MP_POSTURE_COUNT; p++)
                if (SDL_strcasecmp(mobile_posture_name((MobilePosture)p), argv[i + 1]) == 0)
                    h->sim_posture = (MobilePosture)p;
        }
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--keyboard") == 0) h->sim_hw = 1;
        if (strcmp(argv[i], "--pointer") == 0) h->sim_pointer = 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    static Host host;
    Host *h = &host;
    parse_args(h, argc, argv);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, h->sim ? "1" : "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait PortraitUpsideDown LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    /* Back (gesture, button, mouse back) must never quit the desktop; it
     * arrives as AC_BACK and acts as Escape. */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "OS999: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    /* Hardware keyboards deliver letters as composed text (as the desktop
     * takes them). On Android that needs only the event, never the system
     * keyboard; iOS starts text input while a name is edited (see below). */
    SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
    if (mobile_platform_resources(argc, argv) != 0)
        fprintf(stderr, "OS999: resources not found; continuing with defaults\n");
    char volume[OSPI_PATH];
    if (mobile_platform_volume(argc, argv, volume, sizeof volume) != 0) {
        fprintf(stderr, "OS999: could not prepare the volume\n");
        return 1;
    }
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN |
                   (h->sim ? 0 : SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS);
    h->win = SDL_CreateWindow("OS999", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 540, 1100, flags);
    h->ren = h->win ? SDL_CreateRenderer(h->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!h->ren) {
        fprintf(stderr, "OS999: no renderer: %s\n", SDL_GetError());
        return 1;
    }
    mobile_platform_start();
    /* The system pointer (iPadOS's circle, Android's arrow) would sit on top of
     * OS999's own arrow: hide it. The simulator shows the Mac cursor as the finger. */
    SDL_ShowCursor(h->sim && !h->sim_pointer ? SDL_ENABLE : SDL_DISABLE);
    desktop_host_init(volume);
    Session *s = session_create(volume);
    if (!s) { fprintf(stderr, "OS999: could not open %s\n", volume); return 1; }
    kbd_load_fonts("fonts/ChicagoFLF.ttf");
    mobile_pointer_load("assets/icons/cursors/arrow.curs");
    h->audio = kbd_audio_open();
    h->haptics = h->sim ? kbd_haptics_open() : NULL;
    MobileShellHost cb = {on_haptic, on_kbd_event, on_clipboard, h};
    if (mobile_shell_init(&h->shell, s, cb, volume) != 0) return 1;
#if defined(__ANDROID__)
    /* Android hardware keyboards are PC-ordered (Ctrl, Win, Alt): swap so the
     * key beside the space bar, Alt, is Command and Win is Option, as on a Mac. */
    kbd_settings(h->shell.kbd)->pc_modifiers = 1;
#endif
    refresh_screen(h, 1);
    int quit = 0;
    while (!quit && !session_quit(s)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handle(h, &ev, &quit);
        if (h->paused) { SDL_Delay(50); continue; }
        mobile_platform_poll();
        refresh_screen(h, 0);
        {   /* trackpad pinch and two-finger swipes the platform recognised */
            float factor, lines;
            if (os999_mobile_take_gestures(&factor, &lines)) {
                if (factor != 1.0f) mobile_shell_magnify(&h->shell, factor);
                if (lines != 0) mobile_shell_wheel(&h->shell, lines);
            }
        }
#if defined(__APPLE__) && MOBILE_PHONE
        /* iOS gives hardware-keyboard text only to a text field: run one while
         * a name is being typed on an attached keyboard. Without a hardware
         * keyboard this would raise the system keyboard, so OS999's own keys
         * handle that case. */
        int want_text = session_editing(s) && (h->shell.screen.hw_keyboard || kbd_hw_active(h->shell.kbd));
        if (want_text != h->text_on) {
            if (want_text) SDL_StartTextInput(); else SDL_StopTextInput();
            h->text_on = want_text;
        }
#endif
        if (session_take_clip_out(s)) SDL_SetClipboardText(s->clip);
        desktop_host_tick(s, 0);
        kbd_audio_set_muted(h->audio, !s->host_sound_enabled);
        if (!mobile_shell_frame(&h->shell, SDL_GetTicks())) { SDL_Delay(8); continue; }
        SDL_UpdateTexture(h->tex, NULL, h->shell.canvas.px, h->shell.canvas.w * (int)sizeof(uint32_t));
        SDL_RenderClear(h->ren);
        SDL_RenderCopy(h->ren, h->tex, NULL, NULL);
        SDL_RenderPresent(h->ren);
    }
    mobile_shell_free(&h->shell);
    session_destroy(s);
    if (h->audio) kbd_audio_close(h->audio);
    if (h->haptics) kbd_haptics_close(h->haptics);
    desktop_host_shutdown();
    SDL_Quit();
    return 0;
}
