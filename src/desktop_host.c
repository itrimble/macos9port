#include "quicktime_movie.h"
#include <SDL.h>
#include "desktop_bt_host.h"
#include "session_int.h"
#include "audio_service.h"
#include "control_panel_store.h"
#include "control_panels.h"
#include "desktop_host.h"
#include "desktop_sound.h"
#include "media_player_panel.h"
#include "os999_startup.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static int previous_menu = -1, previous_dialog;
static int prev_scale_mode = -1, prev_vsync_mode = -1;

static int sound_directory(const char *directory) {
    char path[4096];
    struct stat st;
    int n = snprintf(path, sizeof path, "%s/01-startup_chord.wav", directory);
    return n > 0 && (size_t)n < sizeof path &&
           stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void find_sounds(char *out, size_t capacity) {
    char executable[4096];
#if defined(__APPLE__)
    uint32_t size = sizeof executable;
    int found = _NSGetExecutablePath(executable, &size) == 0;
#else
    ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
    int found = size > 0 && (size_t)size < sizeof executable - 1;
    if (found) executable[size] = 0;
#endif
    if (found) {
        char *slash = strrchr(executable, '/');
        if (slash) {
            *slash = 0;
            const char *suffixes[] = {"/../Resources/sounds/classic9",
                                       "/../assets/sounds/classic9"};
            for (unsigned i = 0; i < sizeof suffixes / sizeof *suffixes; i++) {
                int n = snprintf(out, capacity, "%s%s", executable, suffixes[i]);
                if (n > 0 && (size_t)n < capacity && sound_directory(out)) return;
            }
        }
    }
    snprintf(out, capacity, "assets/sounds/classic9");
}

static void cp_audio_set_volume(int percent) {
    int pct = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    audio_set_bus_volume(AUDIO_BUS_SYSTEM, (float)pct / 100.0f);
}

static void cp_audio_set_muted(int muted) {
    (void)desktop_sound_set_enabled(!muted);
}

static const ControlPanelsAudioHooks k_cp_audio_hooks = {
    .set_volume = cp_audio_set_volume,
    .set_muted = cp_audio_set_muted,
    .play_sound = NULL
};

void desktop_host_init(const char *volume) {
    char sounds[4096], preference[OSPI_PATH + 32];
    previous_menu = -1;
    previous_dialog = 0;
    prev_scale_mode = prev_vsync_mode = -1;
    find_sounds(sounds, sizeof sounds);
    media_panel_init_hooks();
    desktop_bt_host_init();
    quicktime_install_backend();
    control_panels_set_audio_hooks(&k_cp_audio_hooks);
    int n = snprintf(preference, sizeof preference, "%s/.os999-sound", volume);
    if (n <= 0 || (size_t)n >= sizeof preference) return;
    if (!desktop_sound_init(sounds, preference))
        fprintf(stderr, "OS999 Stylized: desktop sounds unavailable\n");
    int play_chime = 1;
    os999_cp_store_t store;
    if (os999_cp_store_init(&store, volume) == 0) {
        if (os999_cp_store_load(&store) == 0) {
            int vol = os999_cp_store_get_int(&store, "sound", "volume", 80);
            cp_audio_set_volume(vol);
            if (os999_cp_store_has(&store, "sound", "muted"))
                (void)desktop_sound_set_enabled(!os999_cp_store_get_bool(&store, "sound", "muted", 0));
            play_chime = os999_cp_store_get_bool(&store, "sound", "startup_chime", 1);
        }
        os999_cp_store_destroy(&store);
    }
    if (play_chime)
        (void)desktop_sound_play(DESKTOP_SOUND_STARTUP);
}

void desktop_host_tick(Session *s, int guest_active) {
    if (!s) return;
    s->guest_running = !!guest_active;
    media_panel_tick();
    desktop_bt_host_tick();
    if (s->clipboard_write_request) {
        s->clipboard_write_request = 0;
        if (SDL_SetClipboardText(session_clipboard_output(s)) != 0)
            dialog_info(s, "Copy", "Could not copy to the clipboard.");
    }
    desktop_sound_guest_active(guest_active);
    if (s->sound_toggle_request) {
        s->sound_toggle_request = 0;
        if (!desktop_sound_set_enabled(!desktop_sound_enabled()))
            fprintf(stderr, "OS999 Stylized: could not save sound preference\n");
        s->sound_muted = !desktop_sound_enabled();
    }
    if (s->volume_sync_request) {
        s->volume_sync_request = 0;
        cp_audio_set_volume(s->system_volume);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, s->display_integer_scale ? "0" : "1");
    if (s->power_profile == 1 && !guest_active) SDL_Delay(14);
    if (s->sound_preview_request >= 0) {
        int ev = s->sound_preview_request;
        s->sound_preview_request = -1;
        (void)desktop_sound_play(ev);
    }
    s->host_sound_enabled = desktop_sound_enabled();
    menu_set_sound_enabled(s->host_sound_enabled);
    if (s->dialog && !previous_dialog)
        (void)desktop_sound_play_named(s->alert_sound[0] ? s->alert_sound : "alert.beep");
    else if (s->menu >= 0 && previous_menu < 0)
        (void)desktop_sound_play(DESKTOP_SOUND_MENU_OPEN);
    else if (s->menu < 0 && previous_menu >= 0)
        (void)desktop_sound_play(DESKTOP_SOUND_MENU_SELECT);
    else if (s->sound_event[0])
        (void)desktop_sound_play_named(s->sound_event);
    s->sound_event[0] = 0;
    previous_dialog = s->dialog;
    previous_menu = s->menu;
}

void desktop_host_sync_present(Session *s, SDL_Renderer *ren, SDL_Texture *tex,
                               const char *volume, int *boot_enabled) {
    if (!s) return;
    if (s->startup_sync_request && volume && boot_enabled) {
        s->startup_sync_request = 0;
        *boot_enabled = os999_startup_pref_read(volume);
        s->host_startup_enabled = *boot_enabled;
    }
    int nearest = s->display_integer_scale ? 1 : 0;
    if (tex && nearest != prev_scale_mode) {
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(tex, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
        prev_scale_mode = nearest;
    }
    int vsync = s->power_profile == 2 ? 0 : 1;
    if (ren && vsync != prev_vsync_mode) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        (void)SDL_RenderSetVSync(ren, vsync);
#endif
        prev_vsync_mode = vsync;
    }
}

void desktop_host_shutdown(void) {
    desktop_bt_host_shutdown();
    media_panel_shutdown();
    desktop_sound_shutdown();
    previous_menu = -1;
    previous_dialog = 0;
}

int desktop_host_resolve_volume(int argc, char **argv, char *out, size_t n) {
    const char *chosen = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--volume") == 0 && i + 1 < argc) {
            chosen = argv[++i];
        }
    }
    char path[OSPI_PATH];
    if (chosen) {
        snprintf(path, sizeof path, "%s", chosen);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            fprintf(stderr, "OS999 Stylized: HOME is not set\n");
            return -1;
        }
#if defined(__APPLE__)
        snprintf(path, sizeof path, "%s/Library/Application Support/OS Pi/Volume", home);
#else
        snprintf(path, sizeof path, "%s/.local/share/ospi/Volume", home);
#endif
    }
    if (volume_mkdir_p(path) != 0 || volume_seed(path) != 0) {
        fprintf(stderr, "OS999 Stylized: could not create the volume at %s\n", path);
        return -1;
    }
    char resolved[PATH_MAX];
    if (!realpath(path, resolved) || strlen(resolved) >= n) {
        fprintf(stderr, "OS999 Stylized: could not resolve %s\n", path);
        return -1;
    }
    memcpy(out, resolved, strlen(resolved) + 1);
    return 0;
}

void desktop_trace_input(Session *s, SDL_Window *win, SDL_Renderer *ren,
                        int raw_x, int raw_y, int x, int y, int clicks) {
    if (!getenv("OSPI_INPUT_TRACE")) return;
    int ww = 0, wh = 0, rw = 0, rh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &rw, &rh);
    const Window *active = s->nwin > 0 ? &s->win[s->nwin - 1] : NULL;
    const char *selected = "-";
    if (active) {
        for (int i = 0; i < active->nitems; i++) {
            if (window_items(active)[i].selected) {
                selected = window_items(active)[i].name;
                break;
            }
        }
    }
    fprintf(stderr, "input-trace raw=%d,%d mapped=%d,%d clicks=%d window=%dx%d drawable=%dx%d front=%s selected=%s\n",
            raw_x, raw_y, x, y, clicks, ww, wh, rw, rh,
            active ? active->rel : "-", selected);
}
