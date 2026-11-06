#include "desktop_host.h"
#include "desktop_sound.h"

#include <stdio.h>
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
