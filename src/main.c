#include "notes.h"
#include "osa_session.h"
#include "finder_apps.h"
#include "finder_print.h"
#include "ospi.h"
#include "app_switcher.h"
#include "draw.h"
#include "native_launch.h"
#include "os999_startup.h"
#include "desktop_guest.h"
#include "desktop_host.h"
#include "desktop_shortcuts.h"
#include "browser_window.h"
#include "term.h"
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
typedef struct SessionInitTask {
    const char *volume;
    Session *session;
    atomic_int done;
} SessionInitTask;
static void *create_session_worker(void *context) {
    SessionInitTask *task = context;
    task->session = session_create(task->volume);
    if (task->session) task->session->guest_menu_command = native_surface_command;
    atomic_store_explicit(&task->done, 1, memory_order_release);
    return NULL;
}
static const char *flag_arg(int argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], flag) == 0 && i + 1 < argc) return argv[i + 1];
    return NULL;
}
static void take_clip(Session *s) {
    char *text = SDL_GetClipboardText();
    session_set_clip(s, text ? text : "");
    SDL_free(text);
}
static void logical_point(SDL_Window *win, int x, int y, int *ox, int *oy) {
    int ww = 1, wh = 1;
    SDL_GetWindowSize(win, &ww, &wh);
    if (ww < 1) ww = 1;
    if (wh < 1) wh = 1;
    *ox = x * OSPI_W / ww;
    *oy = y * OSPI_H / wh;
}
static int inside(int x, int y, int left, int top, int width, int height) {
    return x >= left && y >= top && x < left + width && y < top + height;
}
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--seed") == 0) {
        return volume_seed(argv[2]) == 0 ? 0 : 1;
    }
    char volume[OSPI_PATH];
    if (desktop_host_resolve_volume(argc, argv, volume, sizeof volume) != 0) {
        return 1;
    }
    fprintf(stderr, "OS999 Stylized 0.1  %s\n", volume);
    const char *install_url = flag_arg(argc, argv, "--install");
    if (install_url) {
        char rel[OSPI_PATH];
        char err[240];
        err[0] = 0;
        if (install_garden(volume, install_url, rel, sizeof rel, err, sizeof err) != 0) {
            fprintf(stderr, "OS999 Stylized: %s\n", err[0] ? err : "install failed");
            return 1;
        }
        fprintf(stderr, "OS999 Stylized installed %s\n", rel);
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "OS999 Stylized: SDL_Init failed: %s\nInstall SDL2 (brew install sdl2, or apt install libsdl2-dev).\n",
                SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *win = SDL_CreateWindow("OS999 Stylized", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, OSPI_W, OSPI_H,
                                       SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "OS999 Stylized: could not open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    desktop_shortcuts_init();
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, OSPI_W, OSPI_H);
    if (!ren || !tex) {
        fprintf(stderr, "OS999 Stylized: could not create the screen: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    int ww = 0, wh = 0, rw = 0, rh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &rw, &rh);
    fprintf(stderr, "OS999 Stylized window %dx%d  drawable %dx%d\n", ww, wh, rw, rh);
    Session *s = NULL;
    uint32_t *frame = calloc((size_t)OSPI_W * OSPI_H, sizeof *frame);
    if (!frame) {
        SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }
    int dumped = 0;
    const char *dump = getenv("OSPI_DUMP");
    uint32_t *guest_pixels = calloc((size_t)RUNTIME_BRIDGE_MAX_WIDTH *
                                    RUNTIME_BRIDGE_MAX_HEIGHT, sizeof *guest_pixels);
    uint32_t *startup_desktop = malloc((size_t)OSPI_W * OSPI_H * sizeof *startup_desktop);
    Os999Startup startup = {0};
    const char *boot_animation = getenv("OSPI_BOOT_ANIMATION");
    const char *preview_mode = getenv("OSPI_BOOT_PREVIEW");
    const char *reduce_motion = getenv("OSPI_REDUCE_MOTION");
    int boot_preview = preview_mode && strcmp(preview_mode, "1") == 0;
    int boot_enabled = os999_startup_pref_read(volume);
    if (boot_animation && strcmp(boot_animation, "0") == 0) boot_enabled = 0;
    else if (boot_animation && strcmp(boot_animation, "1") == 0) boot_enabled = 1;
    if (!guest_pixels || !startup_desktop) {
        free(startup_desktop); free(guest_pixels); free(frame); SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 1;
    }
    desktop_host_init(volume);
    boot_enabled = startup_desktop && boot_enabled;
    if (boot_enabled)
        os999_startup_begin(&startup, SDL_GetTicks(), reduce_motion && strcmp(reduce_motion, "1") == 0);
    else
        startup.phase = OS999_STARTUP_INACTIVE;
    SessionInitTask init = {.volume = volume, .session = NULL};
    atomic_init(&init.done, 0);
    pthread_t init_thread;
    if (pthread_create(&init_thread, NULL, create_session_worker, &init) != 0) {
        desktop_host_shutdown();
        fprintf(stderr, "OS999 Stylized: could not start desktop initialization\n");
        free(startup_desktop); free(guest_pixels); free(frame);
        SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }
    for (size_t i = 0; i < (size_t)OSPI_W * OSPI_H; i++) startup_desktop[i] = 0xFF3C4F70u;
    int startup_quit = 0, startup_failed = 0, session_ready = 0;
    while (!session_ready || os999_startup_active(&startup)) {
        int retry_requested = 0;
        SDL_Event boot_event;
        while (SDL_PollEvent(&boot_event)) {
            if (boot_event.type == SDL_QUIT) { startup_quit = 1; continue; }
            if (startup_failed && startup.phase == OS999_STARTUP_ERROR) {
                if (boot_event.type == SDL_MOUSEBUTTONDOWN && boot_event.button.button == SDL_BUTTON_LEFT) {
                    int x, y; logical_point(win, boot_event.button.x, boot_event.button.y, &x, &y);
                    int left = OSPI_W / 2 - 275, top = OSPI_H / 2 - 104;
                    if (inside(x, y, left + 82, top + 142, 150, 40)) retry_requested = 1;
                    else if (inside(x, y, left + 318, top + 142, 150, 40)) startup_quit = 1;
                } else if (boot_event.type == SDL_KEYDOWN) {
                    if (boot_event.key.keysym.sym == SDLK_r || boot_event.key.keysym.sym == SDLK_RETURN)
                        retry_requested = 1;
                    else if (boot_event.key.keysym.sym == SDLK_ESCAPE) startup_quit = 1;
                }
            }
        }
        if (retry_requested && startup_failed) {
            startup_failed = 0; session_ready = 0; s = NULL; init.session = NULL;
            atomic_store_explicit(&init.done, 0, memory_order_release);
            if (boot_enabled) os999_startup_begin(&startup, SDL_GetTicks(), reduce_motion && strcmp(reduce_motion, "1") == 0);
            else startup.phase = OS999_STARTUP_INACTIVE;
            if (pthread_create(&init_thread, NULL, create_session_worker, &init) != 0) {
                session_ready = startup_failed = 1;
                os999_startup_fail(&startup, SDL_GetTicks());
            }
        }
        if (atomic_load_explicit(&init.done, memory_order_acquire) && !session_ready) {
            pthread_join(init_thread, NULL);
            session_ready = 1;
            s = init.session;
            if (!s) {
                fprintf(stderr, "OS999 Stylized: could not open %s\n", volume);
                startup_failed = 1;
                os999_startup_fail(&startup, SDL_GetTicks());
            } else {
                desktop_host_tick(s, 0);
                session_paint(s, startup_desktop, OSPI_W, OSPI_H);
                startup.ready = 1;
            }
        }
        if (startup_quit) break;
        if (startup_failed && !os999_startup_active(&startup)) break;
        if (session_ready && !os999_startup_active(&startup)) break;
        if (os999_startup_active(&startup))
            os999_startup_frame(&startup, SDL_GetTicks(), session_ready && !startup_failed,
                                frame, startup_desktop, OSPI_W, OSPI_H);
        else memcpy(frame, startup_desktop, (size_t)OSPI_W * OSPI_H * sizeof *frame);
        SDL_UpdateTexture(tex, NULL, frame, OSPI_W * (int)sizeof *frame);
        SDL_RenderClear(ren);
        SDL_GetRendererOutputSize(ren, &rw, &rh);
        SDL_Rect boot_dst = {0, 0, rw, rh};
        SDL_RenderCopy(ren, tex, NULL, &boot_dst);
        SDL_RenderPresent(ren);
        SDL_Delay(12);
    }
    if (!session_ready) {
        while (!atomic_load_explicit(&init.done, memory_order_acquire)) SDL_Delay(1);
        pthread_join(init_thread, NULL);
        s = init.session;
    }
    if (startup_quit || startup_failed || !s) {
        desktop_host_end_session(s);
        os999_startup_release();
        free(startup_desktop); free(guest_pixels); free(frame);
        SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return startup_failed || !s ? 1 : 0;
    }
    /* --open REL uses desktop open semantics and reports errors on stderr. */
    osa_ipc_start_from_env();
    const char *open_rel = flag_arg(argc, argv, "--open");
    if (open_rel && open_rel[0] && !strstr(open_rel, "..")) {
        char full[OSPI_PATH], err[240] = "";
        const char *slash = strrchr(open_rel, '/');
        if (snprintf(full, sizeof full, "%s/%s", volume, open_rel) < (int)sizeof full &&
            classic_open(volume, full, slash ? slash + 1 : open_rel, err, sizeof err) != 0)
            fprintf(stderr, "OS999 Stylized: --open %s: %s\n", open_rel, err[0] ? err : "could not open");
    }
    GuestWindow guest_window = {0};
    uint64_t guest_sequence = 0;
    int guest_width = 0, guest_height = 0, guest_placed = 0;
    int guest_drag = 0, guest_mouse_capture = 0, guest_focus = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quake-demo")) session_command(s, CMD_QUAKE3_DEMO);
        if (!strcmp(argv[i], "--tailscale")) session_command(s, CMD_TAILSCALE);
        if (!strcmp(argv[i], "--terminal")) session_command(s, CMD_TERMINAL_SHELL);
    }
    const char *play_path = flag_arg(argc, argv, "--play");
    if (play_path && session_media_hooks() && session_media_hooks()->open_file)
        session_media_hooks()->open_file(s, play_path);
    while (!session_quit(s)) {
        uint32_t surface_state = RUNTIME_BRIDGE_STARTING;
        char surface_message[256] = {0};
        if (native_surface_active()) {
            int old_guest_width = guest_width, old_guest_height = guest_height;
            int read = native_surface_read(guest_pixels,
                (size_t)RUNTIME_BRIDGE_MAX_WIDTH * RUNTIME_BRIDGE_MAX_HEIGHT,
                &guest_width, &guest_height, &guest_sequence);
            if (read > 0 && (!guest_placed || guest_width != old_guest_width ||
                             guest_height != old_guest_height)) {
                int initial = !guest_placed;
                guest_window_layout(&guest_window, guest_width, guest_height,
                                    initial, old_guest_width, old_guest_height);
                guest_placed = 1;
            }
            native_surface_state(&surface_state, surface_message, sizeof surface_message);
            if (surface_state == RUNTIME_BRIDGE_CLOSED) {
                native_surface_close();
                guest_placed = guest_drag = guest_mouse_capture = guest_focus = 0;
                guest_sequence = 0; guest_width = guest_height = 0;
            }
        } else guest_placed = guest_drag = guest_mouse_capture = guest_focus = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (os999_startup_active(&startup)) {
                if (ev.type == SDL_QUIT) s->quit = 1;
                continue;
            }
            if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && finder_apps_key(s, ev.key.keysym.sym,
                ((ev.key.keysym.mod & KMOD_GUI) ? MOD_CMD : 0) | ((ev.key.keysym.mod & KMOD_SHIFT) ? MOD_SHIFT : 0) |
                ((ev.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) ? MOD_OTHER : 0), ev.type == SDL_KEYDOWN, ev.key.repeat)) continue;
            if (browser_window_event(s, win, &ev)) continue;
            if (!guest_focus && !session_overlay_active(s) && ev.type == SDL_KEYDOWN && !ev.key.repeat &&
                (ev.key.keysym.sym == SDLK_F9 || ev.key.keysym.sym == SDLK_F10)) {
                session_command(s, ev.key.keysym.sym == SDLK_F9 ?
                                CMD_STARTUP_TOGGLE : CMD_STARTUP_PREVIEW);
                continue;
            }
            if (boot_preview && !session_overlay_active(s) && ev.type == SDL_KEYDOWN &&
                (ev.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) == (KMOD_CTRL | KMOD_ALT) &&
                ev.key.keysym.sym == SDLK_9) {
                os999_startup_begin(&startup, SDL_GetTicks(), reduce_motion && strcmp(reduce_motion, "1") == 0);
                continue;
            }
            int x = 0;
            int y = 0;
            if (ev.type == SDL_QUIT) {
                s->quit = 1;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN &&
                       (ev.button.button == SDL_BUTTON_LEFT || ev.button.button == SDL_BUTTON_RIGHT)) {
                logical_point(win, ev.button.x, ev.button.y, &x, &y);
                s->mouse_mods = (SDL_GetModState() & KMOD_SHIFT ? MOD_SHIFT : 0) |
                                (SDL_GetModState() & KMOD_GUI ? MOD_CMD : 0) |
                                (SDL_GetModState() & KMOD_ALT ? MOD_OPTION : 0);
                if (finder_apps_modal(s) || app_switcher_hit(s,x,y)) {
                    if (guest_focus && finder_apps_modal(s)) native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST});
                    guest_mouse_capture = 0;
                    take_clip(s);
                    s->input_now_ms = ev.button.timestamp ? ev.button.timestamp : SDL_GetTicks();
                    if (ev.button.button == SDL_BUTTON_RIGHT || (SDL_GetModState() & KMOD_CTRL))
                        session_context(s, x, y);
                    else session_down(s, x, y, ev.button.clicks);
                    continue;
                }
                int active = native_surface_active() && guest_placed && finder_apps_guest_hit(s, guest_focus, x, y);
                if (active && inside(x, y, guest_window.x, guest_window.y,
                                     guest_window.w, guest_window.h)) {
                    if (ev.button.button == SDL_BUTTON_RIGHT) continue;
                    int close_hit = inside(x, y, guest_window.x + 10,
                                           guest_window.y + 7, 13, 13);
                    if (close_hit) {
                        finder_apps_close_guest_window(s);
                        guest_drag = guest_mouse_capture = 0; guest_focus = s->guest_focused;
                        continue;
                    }
                    if (inside(x, y, guest_window.x, guest_window.y,
                               guest_window.w, 27)) {
                        guest_drag = 1;
                        guest_focus = 1; s->app_request = 2;
                        s->press_x = x - guest_window.x;
                        s->press_y = y - guest_window.y;
                        continue;
                    }
                    if (inside(x, y, guest_window.image_x, guest_window.image_y,
                               guest_window.image_w, guest_window.image_h)) {
                        int gx, gy;
                        guest_pixel_point(&guest_window, guest_width, guest_height,
                                          x, y, &gx, &gy);
                        guest_focus = guest_mouse_capture = 1; s->app_request = 2;
                        native_surface_push((RuntimeBridgeInput){RUNTIME_BRIDGE_BUTTON_DOWN,
                            gx, gy, 0, (uint32_t)SDL_GetModState()});
                        continue;
                    }
                    guest_focus = 0;
                    continue;
                }
                if (guest_focus && y >= TITLE_H) {
                    native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST});
                    guest_focus = guest_mouse_capture = 0;
                }
                if (ev.button.button == SDL_BUTTON_RIGHT || (SDL_GetModState() & KMOD_CTRL)) {
                    session_context(s, x, y);
                    continue;
                }
                if (y >= TITLE_H) s->finder_hidden = 0;
                take_clip(s);
                /* Several queued clicks can be drained in one frame. Use each
                 * event's time so the fallback can distinguish those clicks. */
                s->input_now_ms = ev.button.timestamp ? ev.button.timestamp : SDL_GetTicks();
                session_down(s, x, y, ev.button.clicks);
                desktop_trace_input(s, win, ren, ev.button.x, ev.button.y,
                            x, y, ev.button.clicks);
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                logical_point(win, ev.button.x, ev.button.y, &x, &y);
                if (guest_drag) { guest_drag = 0; continue; }
                if (guest_mouse_capture && native_surface_active()) {
                    int gx, gy;
                    guest_pixel_point(&guest_window, guest_width, guest_height,
                                      x, y, &gx, &gy);
                    native_surface_push((RuntimeBridgeInput){RUNTIME_BRIDGE_BUTTON_UP,
                        gx, gy, 0, (uint32_t)SDL_GetModState()});
                    guest_mouse_capture = 0;
                    continue;
                }
                session_up(s, x, y);
            } else if (ev.type == SDL_MOUSEMOTION) {
                logical_point(win, ev.motion.x, ev.motion.y, &x, &y);
                if (finder_apps_modal(s) || finder_apps_pointer_capture(s)) { session_move(s, x, y); continue; }
                if (guest_drag) {
                    guest_window_drag(&guest_window, x, y, s->press_x, s->press_y);
                    continue;
                }
                if (native_surface_active() && guest_placed && finder_apps_guest_hit(s, guest_focus, x, y) &&
                    (guest_mouse_capture || inside(x, y, guest_window.image_x,
                     guest_window.image_y, guest_window.image_w, guest_window.image_h))) {
                    int gx, gy;
                    guest_pixel_point(&guest_window, guest_width, guest_height,
                                      x, y, &gx, &gy);
                    native_surface_push((RuntimeBridgeInput){RUNTIME_BRIDGE_MOTION,
                        gx, gy, 0, (uint32_t)SDL_GetModState()});
                    continue;
                }
                session_move(s, x, y);
            } else if (ev.type == SDL_MOUSEWHEEL) {
                session_wheel(s, ev.wheel.y);
            } else if (ev.type == SDL_TEXTINPUT && (!guest_focus || session_overlay_active(s))) {
                if (s->held_key_active) {
                    if (!s->held_text[0]) snprintf(s->held_text, sizeof s->held_text, "%s", ev.text.text);
                    else if (strcmp(s->held_text, ev.text.text) == 0) continue;
                }
                if (session_editing(s)) session_text(s, ev.text.text);
                else if (s->dialog == 3 || s->dialog == 5 || notes_active(s)) session_text_input(s, ev.text.text);
                else if (!session_overlay_active(s) && !(SDL_GetModState() & (KMOD_GUI | KMOD_CTRL | KMOD_ALT)))
                    terminal_text_input(s, ev.text.text);
            } else if (ev.type == SDL_KEYDOWN) {
                if (guest_focus && native_surface_active() && !session_overlay_active(s)) {
                    native_surface_push((RuntimeBridgeInput){RUNTIME_BRIDGE_KEY_DOWN,
                        0, 0, (uint32_t)ev.key.keysym.sym,
                        (uint32_t)ev.key.keysym.mod});
                    continue;
                }
                if (ev.key.repeat) continue;
                int key = ev.key.keysym.sym == SDLK_KP_ENTER ? '\r' : ev.key.keysym.sym;
                int mods = ((ev.key.keysym.mod & KMOD_GUI) ? MOD_CMD : 0) |
                           ((ev.key.keysym.mod & KMOD_SHIFT) ? TERM_MOD_SHIFT : 0) |
                           ((ev.key.keysym.mod & KMOD_CTRL) ? TERM_MOD_CTRL : 0) |
                           ((ev.key.keysym.mod & KMOD_ALT) ? TERM_MOD_ALT : 0);
                if ((mods & MOD_CMD) && (key == 'v' || key == 'V')) take_clip(s);
                if (ev.key.keysym.mod & (KMOD_CTRL | KMOD_ALT)) mods |= MOD_OTHER;
                session_key_hold(s, key, mods, NULL, SDL_GetTicks());
                if (!session_overlay_active(s) && terminal_front(s) &&
                    !(mods & (MOD_CMD | TERM_MOD_CTRL | TERM_MOD_ALT)) &&
                    key >= 32 && key < 127) continue;
                session_key(s, key, mods);
                if ((mods & MOD_CMD) && (key == 'c' || key == 'C') && s->clip[0])
                    SDL_SetClipboardText(s->clip);
                if (session_take_clip_out(s)) SDL_SetClipboardText(s->clip);
            } else if (ev.type == SDL_KEYUP) {
                session_key_release(s, ev.key.keysym.sym == SDLK_KP_ENTER ? '\r' : ev.key.keysym.sym);
                if (guest_focus && native_surface_active() && !session_overlay_active(s))
                    native_surface_push((RuntimeBridgeInput){RUNTIME_BRIDGE_KEY_UP,
                        0, 0, (uint32_t)ev.key.keysym.sym, (uint32_t)ev.key.keysym.mod});
            } else if (ev.type == SDL_WINDOWEVENT &&
                       ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                session_key_release(s, 0);
                /* The host window lost key focus (a screenshot tool, Cmd-Tab, a notification). Release
                 * the guest's keys and pointer, but keep it the frontmost OS999 app: clearing guest_focus
                 * here sent a running game behind the Finder windows. */
                if (guest_focus) native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST});
                guest_mouse_capture = s->app_switch_tab = 0; s->down = s->drag = s->switcher.gesture = s->switcher.tear_armed = 0;
            }
        }
        if (finder_apps_sync(s, native_surface_active(), native_surface_title(), &guest_focus)) {
            native_surface_push((RuntimeBridgeInput){.kind=RUNTIME_BRIDGE_FOCUS_LOST}); guest_mouse_capture = 0;
        }
        if (session_take_startup_toggle(s)) {
            boot_enabled = !boot_enabled;
            if (os999_startup_pref_write(volume, boot_enabled) != 0)
                fprintf(stderr, "OS999 Stylized: could not save startup preference\n");
        }
        if (session_take_startup_preview(s) && startup_desktop)
            os999_startup_begin(&startup, SDL_GetTicks(), reduce_motion && strcmp(reduce_motion, "1") == 0);
        desktop_host_sync_present(s, ren, tex, volume, &boot_enabled);
        s->host_startup_enabled = boot_enabled;
        desktop_host_tick(s, native_surface_active());
        osa_ipc_poll(s);
        session_tick(s, SDL_GetTicks());
        s->print_available = desktop_print_supported();
        session_paint(s, frame, OSPI_W, OSPI_H);
        desktop_print_service(s, frame);
        if (native_surface_active() && guest_placed && !s->guest_hidden)
            draw_guest_window(frame, &guest_window, guest_pixels, guest_width,
                              guest_height, surface_state, native_surface_title(),
                              surface_message);
        if (native_surface_active() && guest_placed) session_windows_paint_above(s, frame, OSPI_W, OSPI_H, 2);
        if (native_surface_active() && guest_placed)
            session_overlay_paint(s, frame, OSPI_W, OSPI_H);
        s->guest_bounds = guest_placed ? (Rect){guest_window.x, guest_window.y, guest_window.w, guest_window.h} : (Rect){0};
        browser_window_frame(s, win, frame, os999_startup_active(&startup));
        if (startup_desktop) memcpy(startup_desktop, frame,
                                    (size_t)OSPI_W * OSPI_H * sizeof *frame);
        if (os999_startup_active(&startup))
            os999_startup_frame(&startup, SDL_GetTicks(), 1, frame, startup_desktop,
                                OSPI_W, OSPI_H);
        if (dump && !dumped) {
            FILE *f = fopen(dump, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", OSPI_W, OSPI_H);
                for (int i = 0; i < OSPI_W * OSPI_H; i++) {
                    unsigned char rgb[3] = {(unsigned char)(frame[i] >> 16), (unsigned char)(frame[i] >> 8),
                                            (unsigned char)frame[i]};
                    fwrite(rgb, 1, 3, f);
                }
                fclose(f);
            }
            dumped = 1;
        }
        SDL_UpdateTexture(tex, NULL, frame, OSPI_W * (int)sizeof *frame);
        SDL_RenderClear(ren);
        int rw = OSPI_W, rh = OSPI_H;
        SDL_GetRendererOutputSize(ren, &rw, &rh);
        SDL_Rect dst = {0, 0, rw, rh};
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);
    }
    osa_ipc_close();
    native_surface_close();
    browser_window_close();
    desktop_host_end_session(s);
    os999_startup_release();
    free(startup_desktop);
    free(guest_pixels);
    free(frame);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
