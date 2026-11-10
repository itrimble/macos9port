#include "mac_runtime.h"
#include "runtime_audio.h"
#include "runtime_keys.h"
#include "runtime_perf.h"
#include "mac_time.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_CPU_CLOCK_HZ UINT64_C(16000000)
#define GUEST_CPU_MAX_CATCHUP_US UINT64_C(33333)
#define GUEST_CLOCK_GAP_THRESHOLD_US UINT64_C(250000)
#define GUEST_CPU_INSTRUCTION_QUANTUM 10000u

#ifndef OSPI_VIDEO_CONVERSION_TEST
typedef struct {
    MacRuntime *rt;
    RuntimeAudio *audio;
    SDL_mutex *lock;
    size_t budget;
    uint64_t began, frequency, base_clock, guest_elapsed_us;
    uint32_t base_ticks;
    int audio_setup_failed, result;
    SDL_atomic_t stop, done;
} GuestWorker;
#endif

static uint64_t guest_cycle_slice(uint64_t wall_elapsed_us,
                                  uint64_t *guest_elapsed_us) {
    if (!guest_elapsed_us || wall_elapsed_us <= *guest_elapsed_us) return 0;
    uint64_t slice_us = wall_elapsed_us - *guest_elapsed_us;
    if (slice_us > GUEST_CPU_MAX_CATCHUP_US) slice_us = GUEST_CPU_MAX_CATCHUP_US;
    *guest_elapsed_us += slice_us;
    return slice_us * GUEST_CPU_CLOCK_HZ / 1000000;
}

static int guest_clock_rebase_after_gap(uint64_t gap_us, uint64_t last_elapsed_us,
                                        uint64_t *guest_elapsed_us) {
    if (!guest_elapsed_us || gap_us <= GUEST_CLOCK_GAP_THRESHOLD_US) return 0;
    *guest_elapsed_us = last_elapsed_us;
    return 1;
}

/* This backend accelerates presentation only. Guest QuickDraw and framebuffer
 * conversion still run on the CPU; it is not full guest graphics acceleration. */
static int elapsed_microseconds(uint64_t counter, uint64_t frequency, uint64_t base, uint64_t *out) {
    if (!frequency || frequency > UINT64_MAX / 1000000 ||
        counter / frequency > (UINT64_MAX - base) / 1000000) return -1;
    uint64_t whole = counter / frequency * 1000000;
    uint64_t fraction = counter % frequency * 1000000 / frequency;
    if (fraction > UINT64_MAX - base - whole) return -1;
    *out = base + whole + fraction;
    return 0;
}

#ifndef OSPI_VIDEO_CONVERSION_TEST
static int run_guest_worker(void *context) {
    GuestWorker *worker = context;
    MacRuntime *rt = worker->rt;
    /* This thread is the sole PCM producer. A host stall of one ring depth
     * (85 ms) empties the ring, so ask for higher scheduling priority; a
     * refusal is not fatal and is reported once. */
    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH))
        fprintf(stderr, "Guest worker keeps normal priority: %s\n", SDL_GetError());
    uint64_t last_wall_counter = worker->began, last_elapsed_us = 0;
    uint64_t trace_ticks = SDL_GetTicks(), trace_instructions = rt->instructions;
    uint64_t trace_cycles = mac_cpu_cycles(rt->cpu);
    worker->result = 4;
    while (!SDL_AtomicGet(&worker->stop)) {
        uint64_t wall_counter = SDL_GetPerformanceCounter();
        uint64_t gap_us = 0;
        if (wall_counter >= last_wall_counter)
            elapsed_microseconds(wall_counter - last_wall_counter,
                worker->frequency, 0, &gap_us);
        if (guest_clock_rebase_after_gap(gap_us, last_elapsed_us,
                                         &worker->guest_elapsed_us)) {
            worker->began += wall_counter - last_wall_counter;
            fprintf(stderr, "Guest clock paused across host scheduling gap (%llu ms); dropped catch-up debt.\n",
                    (unsigned long long)(gap_us / 1000));
        }
        last_wall_counter = wall_counter;
        SDL_LockMutex(worker->lock);
        if (rt->instructions >= worker->budget || mac_cpu_halted(rt->cpu)) {
            worker->result = mac_cpu_halted(rt->cpu) ? (rt->exited ? 0 : 3) : 4;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        uint64_t now_us;
        if (elapsed_microseconds(wall_counter - worker->began,
                worker->frequency, worker->base_clock, &now_us) || mac_time_set(rt, now_us)) {
            mac_cpu_halt(rt->cpu, "Host monotonic clock is unavailable or overflowed.");
            worker->result = 3;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        uint64_t elapsed_us = now_us - worker->base_clock;
        last_elapsed_us = elapsed_us;
        uint64_t cycle_budget = guest_cycle_slice(elapsed_us, &worker->guest_elapsed_us);
        rt->ticks = worker->base_ticks + (uint32_t)((elapsed_us / 1000000) * 60 +
                                                    (elapsed_us % 1000000) * 60 / 1000000);
        size_t ran = 0;
        if (worker->budget == SIZE_MAX) {
            if (cycle_budget) ran = mac_runtime_run_cycles(rt, cycle_budget);
        } else {
            size_t remain = (size_t)(worker->budget - rt->instructions);
            size_t quantum = remain < GUEST_CPU_INSTRUCTION_QUANTUM ? remain :
                             GUEST_CPU_INSTRUCTION_QUANTUM;
            if (quantum) ran = mac_runtime_run(rt, quantum);
        }
        if (!ran && (cycle_budget || worker->budget != SIZE_MAX) && !mac_cpu_halted(rt->cpu)) {
            fprintf(stderr, "Guest CPU made no progress.\n");
            worker->result = 3;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        size_t audio_instructions = 0;
        size_t audio_budget = rt->instructions < worker->budget ?
            (size_t)(worker->budget - rt->instructions) : 0;
        int audio_error = rt->sound && worker->audio_setup_failed ? -1 :
            runtime_audio_pump(worker->audio, rt, audio_budget, &audio_instructions);
        rt->instructions += audio_instructions;
        if (audio_error) {
            if (!mac_cpu_halted(rt->cpu)) mac_cpu_halt(rt->cpu, "Audio output could not process or submit PCM.");
            fprintf(stderr, "%s\n", mac_cpu_error(rt->cpu));
            worker->result = 3;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        if (getenv("OSPI_PERF_TRACE") || getenv("OSPI_AUDIO_TRACE")) {
            uint64_t now = SDL_GetTicks();
            if (now - trace_ticks >= 1000) {
                uint64_t interval_ms = now - trace_ticks;
                uint64_t cycle_delta = mac_cpu_cycles(rt->cpu) - trace_cycles;
                fprintf(stderr, "guest-trace ms=%llu interval-ms=%llu instructions=%llu guest-cycles=%llu guest-MHz=%.2f guest-ticks=%u\n",
                        (unsigned long long)now, (unsigned long long)interval_ms,
                        (unsigned long long)(rt->instructions - trace_instructions),
                        (unsigned long long)cycle_delta,
                        (double)cycle_delta * 1000.0 / ((double)interval_ms * 1000000.0),
                        rt->ticks);
                trace_ticks = now;
                trace_instructions = rt->instructions;
                trace_cycles = mac_cpu_cycles(rt->cpu);
            }
        }
        SDL_UnlockMutex(worker->lock);
        if (!cycle_budget) SDL_Delay(1);
    }
    SDL_AtomicSet(&worker->done, 1);
    return 0;
}
#endif

#include "runtime_pacing.h"

#include "runtime_video_pixels.h"

/* SDL uses the same MSB-first data/mask encoding as monochrome QuickDraw.
 * Inverted pixels (data=1,mask=0) may become black on host backends without
 * inversion support. Cursor size follows the host, not framebuffer scaling. */
#if !defined(OSPI_VIDEO_CONVERSION_TEST) || defined(OSPI_CURSOR_TEST)
typedef struct {
    SDL_Cursor *previous, *guest;
    int previous_visibility, visibility;
    uint8_t bytes[68];
} HostCursor;

static int cursor_hotspot(const uint8_t bytes[68], int *x, int *y) {
    *y = (int16_t)((unsigned)bytes[64] << 8 | bytes[65]);
    *x = (int16_t)((unsigned)bytes[66] << 8 | bytes[67]);
    return *x < 0 || *x >= 16 || *y < 0 || *y >= 16 ? -1 : 0;
}

static int sync_cursor(MacRuntime *rt, HostCursor *state) {
    if (rt->cursor_valid && (!state->guest || memcmp(state->bytes, rt->cursor, 68))) {
        int x, y;
        if (cursor_hotspot(rt->cursor, &x, &y)) {
            mac_cpu_halt(rt->cpu, "Unsupported cursor hotspot outside 16-by-16 image.");
            return -1;
        }
        SDL_Cursor *replacement = SDL_CreateCursor(rt->cursor, rt->cursor + 32, 16, 16, x, y);
        if (!replacement) {
            fprintf(stderr, "Cannot create guest cursor: %s\n", SDL_GetError());
            return -1;
        }
        SDL_SetCursor(replacement);
        SDL_FreeCursor(state->guest);
        state->guest = replacement;
        memcpy(state->bytes, rt->cursor, 68);
    }
    /* Read raw low memory even after a guest halt, so final-frame state is
     * available without changing CPU diagnostics. */
    if (mac_cpu_ram_size(rt->cpu) <= 0x8d1) return -1;
    const uint8_t *ram = mac_cpu_ram(rt->cpu);
    int level = (int16_t)((unsigned)ram[0x8d0] << 8 | ram[0x8d1]);
    int visible = level == 0 && !rt->cursor_obscured ? SDL_ENABLE : SDL_DISABLE;
    if (visible != state->visibility) {
        if (SDL_ShowCursor(visible) < 0) {
            fprintf(stderr, "Cannot update guest cursor visibility: %s\n", SDL_GetError());
            return -1;
        }
        state->visibility = visible;
    }
    return 0;
}

static void restore_cursor(HostCursor *state) {
    /* Never free the borrowed incumbent cursor. Restore before freeing ours. */
    if (state->previous) SDL_SetCursor(state->previous);
    else if (state->guest) SDL_SetCursor(SDL_GetDefaultCursor());
    if (state->previous_visibility >= 0) SDL_ShowCursor(state->previous_visibility);
    SDL_FreeCursor(state->guest);
    state->guest = NULL;
}
#endif

#ifndef OSPI_VIDEO_CONVERSION_TEST
static void mouse_point(MacRuntime *rt, SDL_Window *window, int x, int y) {
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    SDL_Rect r = viewport(width, height, rt->screen_width, rt->screen_height);
    int64_t gx = ((int64_t)x - r.x) * rt->screen_width / r.w;
    int64_t gy = ((int64_t)y - r.y) * rt->screen_height / r.h;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    if (gx >= rt->screen_width) gx = rt->screen_width - 1;
    if (gy >= rt->screen_height) gy = rt->screen_height - 1;
    rt->mouse_x = (int16_t)gx;
    rt->mouse_y = (int16_t)gy;
}
#endif

static uint16_t key_modifiers(SDL_Keymod mod, int mouse_down) {
    uint16_t result = mouse_down ? 0 : 0x80; /* Classic btnState is set when up. */
    if (mod & KMOD_GUI) result |= 0x100;
    if (mod & KMOD_SHIFT) result |= 0x200;
    if (mod & KMOD_CAPS) result |= 0x400;
    if (mod & KMOD_ALT) result |= 0x800;
    if (mod & KMOD_CTRL) result |= 0x1000;
    return result;
}

#ifndef OSPI_VIDEO_CONVERSION_TEST
static int runtime_video_run(MacRuntime *rt, const char *title, size_t budget,
                             RuntimeBridge *bridge) {
    size_t capacity;
    if (!rt || !rt->cpu || frame_dimensions(rt, &capacity)) {
        fprintf(stderr, "Unsupported guest framebuffer geometry/depth.\n"); return 5;
    }
    Uint32 required = (bridge ? 0 : SDL_INIT_VIDEO) | SDL_INIT_EVENTS;
    Uint32 owned = required & ~SDL_WasInit(required);
    if (owned && SDL_InitSubSystem(owned)) {
        fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError()); return 5;
    }
    HostCursor cursor = {0};
    if (!bridge) {
        cursor.previous = SDL_GetCursor();
        cursor.previous_visibility = SDL_ShowCursor(SDL_QUERY);
        cursor.visibility = -1;
    }
    SDL_Window *window = bridge ? NULL : SDL_CreateWindow(title ? title : "OS999 Classic Runtime",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, rt->screen_width, rt->screen_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    SDL_Texture *texture = NULL;
    uint32_t *pixels = NULL;
    int result = 5;
    RuntimeAudio audio = {0};
    SDL_mutex *runtime_lock = NULL;
    SDL_Thread *guest_thread = NULL;
    GuestWorker worker = {0};
    int worker_started = 0;
    if (!bridge && (!window || !renderer)) {
        fprintf(stderr, "Accelerated guest presentation unavailable: %s\n", SDL_GetError());
        goto done;
    }
    if (!bridge) {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) ||
            !(info.flags & SDL_RENDERER_ACCELERATED) || !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
            fprintf(stderr, "Guest presentation requires accelerated renderer and vsync.\n"); goto done;
        }
        fprintf(stderr, "GPU presentation backend: %s; guest drawing and conversion remain CPU work.\n", info.name);
    } else {
        fprintf(stderr, "Guest presentation: OS999 shared desktop surface; QuickDraw and conversion remain CPU work.\n");
    }
    int texture_width = 0, texture_height = 0, mouse_down = 0;
    /* CoreAudio initialization can take seconds. Complete host setup before
     * starting the guest clock so it cannot create a fictitious VBL backlog.
     * Apps without sound may still run if the host has no usable device. */
    int audio_setup_failed = runtime_audio_open(&audio) != 0;
    if (audio_setup_failed) fprintf(stderr, "Audio setup unavailable: %s\n", SDL_GetError());
    runtime_lock = SDL_CreateMutex();
    if (!runtime_lock) {
        fprintf(stderr, "Cannot create runtime lock: %s\n", SDL_GetError()); goto done;
    }
    if (bridge) runtime_bridge_set_state(bridge, RUNTIME_BRIDGE_READY, "");
    rt->modifiers = key_modifiers(SDL_GetModState(), mouse_down);
    int perf_enabled = getenv("OSPI_PERF_TRACE") || getenv("OSPI_AUDIO_TRACE");
    RuntimePerf perf;
    runtime_perf_init(&perf, SDL_GetPerformanceCounter(), SDL_GetPerformanceFrequency());
    worker = (GuestWorker){.rt = rt, .audio = &audio, .lock = runtime_lock, .budget = budget,
        .began = SDL_GetPerformanceCounter(), .frequency = SDL_GetPerformanceFrequency(),
        .base_clock = rt->clock_us, .base_ticks = rt->ticks,
        .audio_setup_failed = audio_setup_failed};
    guest_thread = SDL_CreateThread(run_guest_worker, "OS999 guest CPU", &worker);
    if (!guest_thread) {
        fprintf(stderr, "Cannot start guest CPU worker: %s\n", SDL_GetError()); goto done;
    }
    worker_started = 1;
    uint64_t bridge_deadline_us = host_microseconds();
    while (!SDL_AtomicGet(&worker.done)) {
        if (bridge) {
            uint32_t delay = bridge_frame_delay(host_microseconds(), bridge_deadline_us);
            if (delay) SDL_Delay(delay);
            bridge_deadline_us = bridge_next_deadline(host_microseconds(), bridge_deadline_us);
        }
        SDL_Event event;
        if (bridge) {
            RuntimeBridgeInput input;
            for (unsigned pending = 0; pending < RUNTIME_BRIDGE_INPUTS &&
                 runtime_bridge_pop(bridge, &input) > 0; pending++) {
                SDL_LockMutex(runtime_lock);
                rt->mouse_x = (int16_t)input.x;
                rt->mouse_y = (int16_t)input.y;
                if (input.kind == RUNTIME_BRIDGE_MOTION) {
                    rt->cursor_obscured = 0;
                    rt->modifiers = key_modifiers((SDL_Keymod)input.modifiers, mouse_down);
                } else if (input.kind == RUNTIME_BRIDGE_BUTTON_DOWN ||
                           input.kind == RUNTIME_BRIDGE_BUTTON_UP) {
                    mouse_down = input.kind == RUNTIME_BRIDGE_BUTTON_DOWN;
                    rt->modifiers = key_modifiers((SDL_Keymod)input.modifiers, mouse_down);
                    mac_runtime_post_event(rt, mouse_down ? 1 : 2, 0, rt->modifiers,
                                           rt->mouse_x, rt->mouse_y);
                } else if (input.kind == RUNTIME_BRIDGE_KEY_DOWN ||
                           input.kind == RUNTIME_BRIDGE_KEY_UP) {
                    SDL_Keysym key = {.sym = (SDL_Keycode)input.message,
                                      .mod = (Uint16)input.modifiers};
                    uint32_t message;
                    rt->modifiers = key_modifiers((SDL_Keymod)input.modifiers, mouse_down);
                    if (runtime_key_message(&key, &message))
                        mac_runtime_post_event(rt,
                            input.kind == RUNTIME_BRIDGE_KEY_UP ? 4 : 3,
                            message, rt->modifiers, rt->mouse_x, rt->mouse_y);
                } else if (input.kind == RUNTIME_BRIDGE_FOCUS_LOST) {
                    rt->modifiers = 0x80;
                    memset(rt->keys, 0, sizeof rt->keys);
                    if (mouse_down) mac_runtime_post_event(rt, 2, 0, rt->modifiers,
                                                           rt->mouse_x, rt->mouse_y);
                    mouse_down = 0;
                }
                SDL_UnlockMutex(runtime_lock);
            }
            uint32_t surface_state = RUNTIME_BRIDGE_STARTING;
            runtime_bridge_get_state(bridge, &surface_state, NULL, 0);
            if (surface_state == RUNTIME_BRIDGE_CLOSED) { result = 6; goto done; }
        } else for (unsigned pending = 0; pending < 256 && SDL_PollEvent(&event); pending++) {
            if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE)) {
                fprintf(stderr, "Guest window closed by user; gameplay acceptance not established.\n");
                result = 6; goto done;
            }
            SDL_LockMutex(runtime_lock);
            if (event.type == SDL_MOUSEMOTION) {
                if (event.motion.xrel || event.motion.yrel) rt->cursor_obscured = 0;
                mouse_point(rt, window, event.motion.x, event.motion.y);
            } else if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
                       event.button.button == SDL_BUTTON_LEFT) {
                mouse_point(rt, window, event.button.x, event.button.y);
                mouse_down = event.type == SDL_MOUSEBUTTONDOWN;
                rt->modifiers = key_modifiers(SDL_GetModState(), mouse_down);
                mac_runtime_post_event(rt, mouse_down ? 1 : 2, 0, rt->modifiers, rt->mouse_x, rt->mouse_y);
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                uint32_t message;
                rt->modifiers = key_modifiers((SDL_Keymod)event.key.keysym.mod, mouse_down);
                if (runtime_key_message(&event.key.keysym, &message))
                    mac_runtime_post_event(rt, event.type == SDL_KEYUP ? 4 : event.key.repeat ? 5 : 3,
                                           message, rt->modifiers, rt->mouse_x, rt->mouse_y);
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                rt->modifiers = 0x80;
                memset(rt->keys,0,sizeof rt->keys);
                if (mouse_down) mac_runtime_post_event(rt, 2, 0, rt->modifiers, rt->mouse_x, rt->mouse_y);
                mouse_down = 0;
            }
            SDL_UnlockMutex(runtime_lock);
        }
        SDL_LockMutex(runtime_lock);
        if (!bridge && sync_cursor(rt, &cursor)) {
            SDL_UnlockMutex(runtime_lock); goto done;
        }
        if (frame_dimensions(rt, &capacity)) {
            SDL_UnlockMutex(runtime_lock);
            fprintf(stderr, "Guest changed to unsupported framebuffer geometry/depth.\n"); goto done;
        }
        int frame_width = rt->screen_width, frame_height = rt->screen_height;
        if (texture_width != frame_width || texture_height != frame_height) {
            uint32_t *replacement = malloc(capacity * sizeof *replacement);
            SDL_Texture *replacement_texture = bridge ? NULL : SDL_CreateTexture(renderer,
                SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);
            if (!replacement || (!bridge && !replacement_texture)) {
                free(replacement); SDL_DestroyTexture(replacement_texture); SDL_UnlockMutex(runtime_lock);
                fprintf(stderr, "Guest framebuffer allocation failed: %s\n", SDL_GetError()); goto done;
            }
            free(pixels); SDL_DestroyTexture(texture);
            pixels = replacement; texture = replacement_texture;
            texture_width = frame_width; texture_height = frame_height;
        }
        if (copy_frame(rt, pixels, capacity)) {
            SDL_UnlockMutex(runtime_lock);
            fprintf(stderr, "Guest framebuffer is outside emulated memory.\n"); goto done;
        }
        SDL_UnlockMutex(runtime_lock);
        if (bridge) {
            if (runtime_bridge_publish(bridge, pixels, frame_width, frame_height)) {
                fprintf(stderr, "Could not publish the guest framebuffer to OS999.\n"); goto done;
            }
        } else {
            int width, height;
            if (SDL_GetRendererOutputSize(renderer, &width, &height)) {
                fprintf(stderr, "Cannot measure guest drawable: %s\n", SDL_GetError()); goto done;
            }
            SDL_Rect dst = viewport(width, height, frame_width, frame_height);
            if (SDL_UpdateTexture(texture, NULL, pixels, frame_width * (int)sizeof *pixels) ||
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) || SDL_RenderClear(renderer) ||
                SDL_RenderCopy(renderer, texture, NULL, &dst)) {
                fprintf(stderr, "Guest presentation failed: %s\n", SDL_GetError()); goto done;
            }
            SDL_RenderPresent(renderer);
        }
        if (perf_enabled)
            runtime_perf_present(&perf, SDL_GetPerformanceCounter());
    }
    SDL_AtomicSet(&worker.stop, 1);
    SDL_WaitThread(guest_thread, NULL);
    worker_started = 0;
    result = worker.result;
    if (mac_cpu_halted(rt->cpu)) {
        fprintf(stderr, "%s\n", mac_cpu_error(rt->cpu));
        result = rt->exited ? 0 : 3;
    } else {
        fprintf(stderr, "Instruction budget exhausted; gameplay unverified.\n"); result = 4;
    }
done:
    if (worker_started) {
        SDL_AtomicSet(&worker.stop, 1);
        SDL_WaitThread(guest_thread, NULL);
    }
    SDL_DestroyMutex(runtime_lock);
    if (audio.device) fprintf(stderr, "Audio submitted %llu stereo frames; audible playback unverified.\n",
                              (unsigned long long)audio.submitted_frames);
    runtime_audio_close(&audio);
    if (!bridge) restore_cursor(&cursor);
    if (bridge) {
        if (result == 0 || result == 6)
            runtime_bridge_set_state(bridge, RUNTIME_BRIDGE_CLOSED, "");
        else {
            const char *reason = mac_cpu_halted(rt->cpu) ? mac_cpu_error(rt->cpu) :
                result == 4 ? "The development instruction limit was reached." :
                "The guest display surface could not be updated.";
            runtime_bridge_set_state(bridge, RUNTIME_BRIDGE_FAILED, reason);
        }
    }
    if (!bridge && window && result != 0 && result != 6) {
        const char *reason = mac_cpu_halted(rt->cpu) ? mac_cpu_error(rt->cpu) :
            result == 4 ? "The development instruction limit was reached." :
            "The graphics backend could not present this application.";
        char message[384];
        snprintf(message, sizeof message,
            "This application stopped in OS999's development runtime.\n\n%s\n\n"
            "Compatibility is incomplete; no game progress was saved.", reason);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OS999 — application stopped", message, window);
    }
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (owned) SDL_QuitSubSystem(owned);
    return result;
}

int mac_runtime_window(MacRuntime *rt, const char *title, size_t budget) {
    return runtime_video_run(rt, title, budget, NULL);
}

int mac_runtime_bridge_window(MacRuntime *rt, size_t budget, RuntimeBridge *bridge) {
    if (!bridge) return 5;
    return runtime_video_run(rt, "", budget, bridge);
}
#endif
