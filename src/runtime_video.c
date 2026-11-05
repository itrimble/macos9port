#include "mac_runtime.h"
#include "runtime_audio.h"
#include "mac_time.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUEST_CPU_CLOCK_HZ UINT64_C(16000000)
#define GUEST_CPU_MAX_CATCHUP_US UINT64_C(33333)
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
    uint64_t trace_ticks = SDL_GetTicks(), trace_instructions = rt->instructions;
    uint64_t trace_cycles = mac_cpu_cycles(rt->cpu);
    worker->result = 4;
    while (!SDL_AtomicGet(&worker->stop)) {
        SDL_LockMutex(worker->lock);
        if (rt->instructions >= worker->budget || mac_cpu_halted(rt->cpu)) {
            worker->result = mac_cpu_halted(rt->cpu) ? (rt->exited ? 0 : 3) : 4;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        uint64_t now_us;
        if (elapsed_microseconds(SDL_GetPerformanceCounter() - worker->began,
                worker->frequency, worker->base_clock, &now_us) || mac_time_set(rt, now_us)) {
            mac_cpu_halt(rt->cpu, "Host monotonic clock is unavailable or overflowed.");
            worker->result = 3;
            SDL_UnlockMutex(worker->lock);
            break;
        }
        uint64_t elapsed_us = now_us - worker->base_clock;
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
        if (getenv("OSPI_AUDIO_TRACE")) {
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

static int frame_dimensions(const MacRuntime *rt, size_t *pixels) {
    size_t w = rt->screen_width, h = rt->screen_height;
    if (!w || !h || w > 4096 || h > 4096 ||
        (rt->screen_depth != 1 && rt->screen_depth != 8)) return -1;
    *pixels = w * h;
    return 0;
}

static int copy_frame(MacRuntime *rt, uint32_t *out, size_t capacity) {
    size_t count;
    if (!rt || !rt->cpu || !out || frame_dimensions(rt, &count) || count > capacity) return -1;
    size_t width = rt->screen_width, height = rt->screen_height;
    size_t required_row = rt->screen_depth == 1 ? (width + 7) / 8 : width;
    size_t stride = rt->screen_row_bytes;
    size_t ram_size = mac_cpu_ram_size(rt->cpu);
    size_t base = rt->screen_base;
    if (stride < required_row || base > ram_size ||
        (height - 1) * stride + required_row > ram_size - base) return -1;
    const uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (size_t y = 0; y < height; y++) {
        const uint8_t *row = ram + base + y * stride;
        for (size_t x = 0; x < width; x++) {
            uint32_t rgb;
            if (rt->screen_depth == 1) rgb = row[x / 8] & (0x80u >> (x % 8)) ? 0 : 0xffffff;
            else rgb = rt->palette[row[x]] & 0xffffffu;
            out[y * width + x] = 0xff000000u | rgb;
        }
    }
    return 0;
}

static SDL_Rect viewport(int width, int height, int guest_width, int guest_height) {
    SDL_Rect r = {0, 0, 1, 1};
    if (width < 1 || height < 1 || guest_width < 1 || guest_height < 1) return r;
    if ((int64_t)width * guest_height < (int64_t)height * guest_width) {
        r.w = width;
        r.h = (int)((int64_t)width * guest_height / guest_width);
    } else {
        r.h = height;
        r.w = (int)((int64_t)height * guest_width / guest_height);
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}

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

/* Interface key numbers cross-checked against SDL's scancodes_darwin table.
 * This deliberately handles US ASCII and navigation, not Unicode text/IME. */
static int key_message(const SDL_Keysym *key, uint32_t *message) {
    static const uint8_t letters[] = {
        0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6
    };
    static const uint8_t digits[] = {29,18,19,20,21,23,22,26,28,25};
    SDL_Keycode k = key->sym;
    unsigned code, character;
    if (k >= SDLK_a && k <= SDLK_z) {
        code = letters[k - SDLK_a];
        character = (unsigned)k;
        if (!!(key->mod & KMOD_SHIFT) != !!(key->mod & KMOD_CAPS)) character -= 32;
    } else if (k >= SDLK_0 && k <= SDLK_9) {
        code = digits[k - SDLK_0];
        character = key->mod & KMOD_SHIFT ? (unsigned char)")!@#$%^&*("[k - SDLK_0] : (unsigned)k;
    } else if (k >= SDLK_F1 && k <= SDLK_F12) {
        static const uint8_t function_codes[] = {122,120,99,118,96,97,98,100,101,109,103,111};
        code = function_codes[k - SDLK_F1];
        character = 16; /* Event Manager's function-key character, independent of modifiers. */
    } else if (k >= SDLK_F13 && k <= SDLK_F15) {
        static const uint8_t function_codes[] = {105,107,113};
        code = function_codes[k - SDLK_F13];
        character = 16;
    } else {
        switch (k) {
        case SDLK_LEFT: code = 123; character = 28; break;
        case SDLK_RIGHT: code = 124; character = 29; break;
        case SDLK_DOWN: code = 125; character = 31; break;
        case SDLK_UP: code = 126; character = 30; break;
        case SDLK_RETURN: code = 36; character = 13; break;
        case SDLK_KP_ENTER: code = 76; character = 3; break;
        case SDLK_SPACE: code = 49; character = 32; break;
        case SDLK_TAB: code = 48; character = 9; break;
        case SDLK_BACKSPACE: code = 51; character = 8; break;
        case SDLK_DELETE: code = 117; character = 127; break;
        case SDLK_ESCAPE: code = 53; character = 27; break;
        case SDLK_MINUS: code = 27; character = key->mod & KMOD_SHIFT ? '_' : '-'; break;
        case SDLK_EQUALS: code = 24; character = key->mod & KMOD_SHIFT ? '+' : '='; break;
        case SDLK_LEFTBRACKET: code = 33; character = key->mod & KMOD_SHIFT ? '{' : '['; break;
        case SDLK_RIGHTBRACKET: code = 30; character = key->mod & KMOD_SHIFT ? '}' : ']'; break;
        case SDLK_BACKSLASH: code = 42; character = key->mod & KMOD_SHIFT ? '|' : '\\'; break;
        case SDLK_SEMICOLON: code = 41; character = key->mod & KMOD_SHIFT ? ':' : ';'; break;
        case SDLK_QUOTE: code = 39; character = key->mod & KMOD_SHIFT ? '"' : '\''; break;
        case SDLK_COMMA: code = 43; character = key->mod & KMOD_SHIFT ? '<' : ','; break;
        case SDLK_PERIOD: code = 47; character = key->mod & KMOD_SHIFT ? '>' : '.'; break;
        case SDLK_SLASH: code = 44; character = key->mod & KMOD_SHIFT ? '?' : '/'; break;
        case SDLK_BACKQUOTE: code = 50; character = key->mod & KMOD_SHIFT ? '~' : '`'; break;
        default: return 0;
        }
    }
    *message = (code << 8) | character;
    return 1;
}

#ifndef OSPI_VIDEO_CONVERSION_TEST
int mac_runtime_window(MacRuntime *rt, const char *title, size_t budget) {
    size_t capacity;
    if (!rt || !rt->cpu || frame_dimensions(rt, &capacity)) {
        fprintf(stderr, "Unsupported guest framebuffer geometry/depth.\n"); return 5;
    }
    Uint32 required = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    Uint32 owned = required & ~SDL_WasInit(required);
    if (owned && SDL_InitSubSystem(owned)) {
        fprintf(stderr, "SDL video initialization failed: %s\n", SDL_GetError()); return 5;
    }
    HostCursor cursor = {.previous = SDL_GetCursor(),
        .previous_visibility = SDL_ShowCursor(SDL_QUERY), .visibility = -1};
    SDL_Window *window = SDL_CreateWindow(title ? title : "OS Pi Classic Runtime",
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
    if (!window || !renderer) {
        fprintf(stderr, "Accelerated guest presentation unavailable: %s\n", SDL_GetError());
        goto done;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) ||
        !(info.flags & SDL_RENDERER_ACCELERATED) || !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
        fprintf(stderr, "Guest presentation requires accelerated renderer and vsync.\n"); goto done;
    }
    fprintf(stderr, "GPU presentation backend: %s; guest drawing and conversion remain CPU work.\n", info.name);
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
    rt->modifiers = key_modifiers(SDL_GetModState(), mouse_down);
    uint64_t present_trace_ticks = SDL_GetTicks(), presented_frames = 0;
    worker = (GuestWorker){.rt = rt, .audio = &audio, .lock = runtime_lock, .budget = budget,
        .began = SDL_GetPerformanceCounter(), .frequency = SDL_GetPerformanceFrequency(),
        .base_clock = rt->clock_us, .base_ticks = rt->ticks,
        .audio_setup_failed = audio_setup_failed};
    guest_thread = SDL_CreateThread(run_guest_worker, "OS Pi guest CPU", &worker);
    if (!guest_thread) {
        fprintf(stderr, "Cannot start guest CPU worker: %s\n", SDL_GetError()); goto done;
    }
    worker_started = 1;
    while (!SDL_AtomicGet(&worker.done)) {
        SDL_Event event;
        for (unsigned pending = 0; pending < 256 && SDL_PollEvent(&event); pending++) {
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
                if (key_message(&event.key.keysym, &message))
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
        if (sync_cursor(rt, &cursor)) {
            SDL_UnlockMutex(runtime_lock); goto done;
        }
        if (frame_dimensions(rt, &capacity)) {
            SDL_UnlockMutex(runtime_lock);
            fprintf(stderr, "Guest changed to unsupported framebuffer geometry/depth.\n"); goto done;
        }
        int frame_width = rt->screen_width, frame_height = rt->screen_height;
        if (texture_width != frame_width || texture_height != frame_height) {
            uint32_t *replacement = malloc(capacity * sizeof *replacement);
            SDL_Texture *replacement_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);
            if (!replacement || !replacement_texture) {
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
        if (getenv("OSPI_AUDIO_TRACE")) {
            presented_frames++;
            uint64_t now = SDL_GetTicks();
            if (now - present_trace_ticks >= 1000) {
                uint64_t interval_ms = now - present_trace_ticks;
                fprintf(stderr, "present-trace ms=%llu interval-ms=%llu frames=%llu fps=%.1f\n",
                        (unsigned long long)now, (unsigned long long)interval_ms,
                        (unsigned long long)presented_frames,
                        (double)presented_frames * 1000.0 / (double)interval_ms);
                present_trace_ticks = now;
                presented_frames = 0;
            }
        }
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
    restore_cursor(&cursor);
    if (window && result != 0 && result != 6) {
        const char *reason = mac_cpu_halted(rt->cpu) ? mac_cpu_error(rt->cpu) :
            result == 4 ? "The development instruction limit was reached." :
            "The graphics backend could not present this application.";
        char message[384];
        snprintf(message, sizeof message,
            "This application stopped in OS Pi's development runtime.\n\n%s\n\n"
            "Compatibility is incomplete; no game progress was saved.", reason);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OS Pi — application stopped", message, window);
    }
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (owned) SDL_QuitSubSystem(owned);
    return result;
}
#endif
