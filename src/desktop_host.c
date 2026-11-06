#include "desktop_host.h"
#include "desktop_sound.h"

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

void desktop_host_init(const char *volume) {
    char sounds[4096], preference[OSPI_PATH + 32];
    previous_menu = -1;
    previous_dialog = 0;
    find_sounds(sounds, sizeof sounds);
    int n = snprintf(preference, sizeof preference, "%s/.os999-sound", volume);
    if (n <= 0 || (size_t)n >= sizeof preference) return;
    if (!desktop_sound_init(sounds, preference))
        fprintf(stderr, "OS999 Stylized: desktop sounds unavailable\n");
    (void)desktop_sound_play(DESKTOP_SOUND_STARTUP);
}

void desktop_host_tick(Session *s, int guest_active) {
    if (!s) return;
    desktop_sound_guest_active(guest_active);
    if (s->sound_toggle_request) {
        s->sound_toggle_request = 0;
        if (!desktop_sound_set_enabled(!desktop_sound_enabled()))
            fprintf(stderr, "OS999 Stylized: could not save sound preference\n");
    }
    s->host_sound_enabled = desktop_sound_enabled();
    menu_set_sound_enabled(s->host_sound_enabled);
    if (s->dialog && !previous_dialog)
        (void)desktop_sound_play(DESKTOP_SOUND_ALERT);
    else if (s->menu >= 0 && previous_menu < 0)
        (void)desktop_sound_play(DESKTOP_SOUND_MENU_OPEN);
    else if (s->menu < 0 && previous_menu >= 0)
        (void)desktop_sound_play(DESKTOP_SOUND_MENU_SELECT);
    previous_dialog = s->dialog;
    previous_menu = s->menu;
}

void desktop_host_shutdown(void) {
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
