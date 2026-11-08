#include "session_int.h"
#include "term.h"
#include "native_apps.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

int terminal_tailnet_open(Session *s) {
    char path[OSPI_PATH];
#ifdef __APPLE__
    uint32_t size = sizeof path;
    if (_NSGetExecutablePath(path, &size)) path[0] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n > 0) path[n] = 0; else path[0] = 0;
#endif
    char *slash = strrchr(path, '/');
    if (!slash || (size_t)(slash - path) + sizeof "/os999-tailnet" > sizeof path) return -1;
    strcpy(slash + 1, "os999-tailnet");
    if (access(path, X_OK)) {
        dialog_info(s, "Tailscale", "The Tailscale utility is missing. Rebuild or reinstall OS999.");
        return -1;
    }
    char *args[] = {path, NULL};
    int result = terminal_open_custom(s, TERM_MODE_SHELL, args);
    Window *w = terminal_front(s);
    if (!result && w) snprintf(w->title, sizeof w->title, "Tailscale");
    if (!result) native_app_note(s, NATIVE_TAILSCALE);
    return result;
}
