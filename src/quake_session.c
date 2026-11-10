#include "session_int.h"
#include "quake_launch.h"
#include "quake_data.h"
#include "native_launch.h"
#include "os999_quake_bridge.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

static Os999QuakeBridge *surface;
static pid_t child;
static int child_status, last_x, last_y, pointer_valid;
static char child_log[OSPI_PATH + 64];

static void quake_reap(void) {
    if (child > 0 && waitpid(child, &child_status, WNOHANG) == child) child = 0;
}

static void quake_close(void) {
    if (surface) atomic_store(&surface->closing, 1);
    quake_reap();
    if (child > 0) {
        kill(child, SIGTERM);
        for (int i = 0; i < 20 && child > 0; i++) { usleep(5000); quake_reap(); }
        if (child > 0) { kill(child, SIGKILL); while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {} child = 0; }
    }
    if (surface) munmap(surface, sizeof *surface);
    surface = NULL; pointer_valid = 0;
}

static int quake_state(uint32_t *state, char *message, size_t n) {
    quake_reap();
    if (!surface || !state) return -1;
    *state = !child ? (WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 ?
             RUNTIME_BRIDGE_CLOSED : RUNTIME_BRIDGE_FAILED) :
             atomic_load(&surface->ready) ? RUNTIME_BRIDGE_READY : RUNTIME_BRIDGE_STARTING;
    if (message && n) snprintf(message, n, "%s%s", *state == RUNTIME_BRIDGE_FAILED ?
                               "Quake exited; inspect " : "Quake log: ", child_log);
    return 0;
}

static int quake_read(uint32_t *pixels, size_t capacity, int *width, int *height,
                      uint64_t *sequence) {
    if (!surface || !pixels || capacity < OS999Q_WIDTH * OS999Q_HEIGHT) return 0;
    if (!atomic_load(&surface->ready)) return 0;
    unsigned unlocked = 0;
    if (!atomic_compare_exchange_strong(&surface->frame_lock, &unlocked, 1)) return 0;
    for (unsigned y = 0; y < OS999Q_HEIGHT; y++)
        for (unsigned x = 0; x < OS999Q_WIDTH; x++)
            pixels[y * OS999Q_WIDTH + x] = 0xff000000u |
                surface->pixels[(OS999Q_HEIGHT - 1 - y) * OS999Q_WIDTH + x];
    if (width) *width = OS999Q_WIDTH;
    if (height) *height = OS999Q_HEIGHT;
    if (sequence) *sequence = atomic_load(&surface->frames);
    atomic_store(&surface->frame_lock, 0);
    return 1;
}

/* Tell the engine where the pointer is inside the frame; ignored while it is outside. */
static void quake_pointer_abs(int x, int y) {
    if (!surface || x < 0 || y < 0 || x >= (int)OS999Q_WIDTH || y >= (int)OS999Q_HEIGHT) return;
    Os999QuakeEvent at = {0};
    at.kind = OS999Q_MOUSE_ABS; at.a = x; at.b = y;
    os999q_push(surface, at);
}

static int quake_input(RuntimeBridgeInput input) {
    if (!surface) return 0;
    if (input.kind == RUNTIME_BRIDGE_FOCUS_LOST) {
        atomic_store(&surface->focus, 0); atomic_store(&surface->reset_input, 1);
        pointer_valid = 0; return 1;
    }
    atomic_store(&surface->focus, 1);
    Os999QuakeEvent event = {0};
    if (input.kind == RUNTIME_BRIDGE_KEY_DOWN || input.kind == RUNTIME_BRIDGE_KEY_UP) {
        event.kind = OS999Q_KEY_SYMBOL; event.a = input.message;
        event.b = input.kind == RUNTIME_BRIDGE_KEY_DOWN;
    } else if (input.kind == RUNTIME_BRIDGE_MOTION) {
        quake_pointer_abs(input.x, input.y);
        event.kind = OS999Q_MOUSE;
        event.a = pointer_valid ? input.x - last_x : 0;
        event.b = pointer_valid ? input.y - last_y : 0;
        last_x = input.x; last_y = input.y; pointer_valid = 1;
    } else if (input.kind == RUNTIME_BRIDGE_BUTTON_DOWN || input.kind == RUNTIME_BRIDGE_BUTTON_UP) {
        quake_pointer_abs(input.x, input.y); /* a click lands where the pointer is */
        event.kind = OS999Q_BUTTON; event.a = 1;
        event.b = input.kind == RUNTIME_BRIDGE_BUTTON_DOWN;
        last_x = input.x; last_y = input.y; pointer_valid = 1;
    } else return 0;
    return os999q_push(surface, event);
}

static const NativeSurfaceBackend backend = {
    quake_read, quake_input, quake_state, quake_close, "Quake III Arena"
};

/* OS999 > Quake III Arena: start the separately built, GPL-2 engine as its own
 * process. Nothing from Quake links into OS999. */
/* mode 0: full game. 1: Arena demo. 2: Applications icon: the full game when its data is installed,
 * otherwise the demo. */
static int open_quake(Session *s, int mode) {
    int demo = mode == 1;
    if (native_surface_active()) {
        dialog_info(s, "Quake III Arena", "Close the current guest application first.");
        return -1;
    }
    char exe[1024] = {0};
#ifdef __APPLE__
    uint32_t size = sizeof exe;
    if (_NSGetExecutablePath(exe, &size)) exe[0] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) exe[n] = 0;
#endif
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    QuakeLaunch q;
    /* Quake's data and settings live in OS999's own folder, not the Mac's Library. */
    char own[OSPI_PATH + 32]; const char *home = os999_private_dir(NULL, own, sizeof own) ? NULL : own;
    int ready = quake_launch_resolve(&q, exe[0] ? exe : NULL, home);
    if (mode == 2 && ready != -1 && ready != 0) demo = 1; /* no retail data: use the demo */
    if (demo) {
        ready = quake_launch_resolve_demo(&q, exe[0] ? exe : NULL, home);
        if (ready == -3 && quake_data_import_demo(home, s->root) >= 0) /* found a verified copy */
            ready = quake_launch_resolve_demo(&q, exe[0] ? exe : NULL, home);
    }
    if (ready == -1) {
        dialog_info(s, "Quake III Arena", "The Quake III engine is not installed. Build quake/ and package it with make app.");
        return -1;
    }
    if (ready == -3) {
        dialog_info(s, "Quake III Arena Demo", "Put the original Arena 1.11 demo pak0.pk3 in this volume's Downloads or Documents folder and open Quake again. OS999 checks and installs it. Local play only; modified data is rejected.");
        return -1;
    }
    if (ready == -2) {
#ifdef __APPLE__
        dialog_info(s, "Quake III Arena", "Put pak0.pk3 in this disk's Downloads or Documents folder and open Quake again.");
#else
        dialog_info(s, "Quake III Arena", "Copy pak0.pk3 into ~/.local/share/os999/quake3/baseq3 (or demota for the demo).");
#endif
        return -1;
    }
    char *argv[24];
    if (!quake_launch_argv(&q, argv, 24)) return -1;
    char path[OSPI_PATH + 64];
    int fd = os999_private_temp(path, sizeof path, "quake-surface");
    if (fd < 0) return -1;
    unlink(path);
    if (ftruncate(fd, sizeof *surface)) { close(fd); return -1; }
    surface = mmap(NULL, sizeof *surface, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (surface == MAP_FAILED) { surface = NULL; close(fd); return -1; }
    memset(surface, 0, sizeof *surface);
    surface->magic = OS999Q_MAGIC; surface->version = OS999Q_VERSION;
    surface->bytes = sizeof *surface; surface->width = OS999Q_WIDTH; surface->height = OS999Q_HEIGHT;
    int log = os999_private_temp(child_log, sizeof child_log, "quake-log");
    if (log < 0) { close(fd); quake_close(); return -1; }
    int count = 0;
    while (environ[count]) count++;
    char **env = calloc((size_t)count + 2, sizeof *env);
    if (!env) { close(fd); close(log); quake_close(); return -1; }
    int used = 0;
    for (int i = 0; i < count; i++)
        if (strncmp(environ[i], "OS999_QUAKE_BRIDGE_FD=", sizeof "OS999_QUAKE_BRIDGE_FD=" - 1)) env[used++] = environ[i];
    char fd_env[64]; snprintf(fd_env, sizeof fd_env, "OS999_QUAKE_BRIDGE_FD=%d", fd);
    env[used] = fd_env;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, log, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, log, STDERR_FILENO);
    int result = posix_spawn(&child, q.binary, &actions, NULL, argv, env);
    posix_spawn_file_actions_destroy(&actions);
    free(env); close(log); close(fd);
    if (result || native_surface_attach(&backend)) {
        if (result) child = 0;
        quake_close(); dialog_info(s, "Quake III Arena", "Quake III could not be embedded."); return -1;
    }
    s->app_request = 2;
    return 0;
}

int session_open_quake(Session *s) { return open_quake(s, 0); }
int session_open_quake_app(Session *s) { return open_quake(s, 2); }
int session_open_quake_demo(Session *s) { return open_quake(s, 1); }
