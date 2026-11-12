#include "native_settings.h"
#include "native_launch.h"
#include "ospi.h"
#include "mac_resource.h"
#include "application_probe.h"
#include "runtime_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static const NativeSurfaceBackend *external_surface;
static RuntimeBridge *active_surface;
static char active_surface_name[64];
static char active_surface_title[256];

int native_surface_active(void) { return active_surface != NULL || external_surface != NULL; }

int native_surface_attach(const NativeSurfaceBackend *backend) {
    if (native_surface_active() || !backend || !backend->read || !backend->push ||
        !backend->state || !backend->close || !backend->title) return -1;
    external_surface = backend;
    return 0;
}

int native_surface_command(int command) {
    if (command != CMD_CLOSE && command != CMD_QUIT) return -1;
    if (external_surface) { native_surface_close(); return 0; }
    uint32_t state;
    if (!active_surface || runtime_bridge_get_state(active_surface, &state, NULL, 0) ||
        state != RUNTIME_BRIDGE_READY) return 0;
    return runtime_bridge_shortcut(active_surface, command == CMD_QUIT ? 'q' : 'w');
}

int native_surface_read(uint32_t *pixels, size_t capacity, int *width, int *height,
                        uint64_t *sequence) {
    if (external_surface) return external_surface->read(pixels, capacity, width, height, sequence);
    return active_surface ? runtime_bridge_read(active_surface, pixels, capacity,
                                                 width, height, sequence) : 0;
}

int native_surface_push(RuntimeBridgeInput input) {
    if (external_surface) return external_surface->push(input);
    return active_surface ? runtime_bridge_push(active_surface, input) : 0;
}

int native_surface_state(uint32_t *state, char *message, size_t message_size) {
    if (external_surface) return external_surface->state(state, message, message_size);
    return active_surface ? runtime_bridge_get_state(active_surface, state, message,
                                                       message_size) : -1;
}

const char *native_surface_title(void) {
    if (external_surface) return external_surface->title;
    return active_surface_title[0] ? active_surface_title : "Classic Macintosh Application";
}

void native_surface_close(void) {
    if (external_surface) {
        const NativeSurfaceBackend *old = external_surface;
        external_surface = NULL;
        old->close();
    }
    if (!active_surface) return;
    runtime_bridge_set_state(active_surface, RUNTIME_BRIDGE_CLOSED, "");
    runtime_bridge_close(active_surface);
    runtime_bridge_unlink(active_surface_name);
    active_surface = NULL;
    active_surface_name[0] = 0;
    active_surface_title[0] = 0;
}

static int problem(char *err, size_t n, const char *message) {
    if (err && n) snprintf(err, n, "%s", message);
    return -1;
}

static int contained(const char *root, const char *path) {
    size_t n = strlen(root);
    return !strncmp(root, path, n) &&
           ((n == 1 && root[0] == '/') || path[n] == '/' || path[n] == 0);
}

static int application_fork(const char *path, char *err, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) return problem(err, n, "Could not read this application's resource fork.");
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        st.st_size > 64 * 1024 * 1024) {
        fclose(f);
        return problem(err, n, "Resource fork is not a regular file below 64 MiB.");
    }
    size_t size = (size_t)st.st_size;
    uint8_t *bytes = malloc(size ? size : 1);
    if (!bytes || fread(bytes, 1, size, f) != size) {
        free(bytes); fclose(f);
        return problem(err, n, "Could not read this application's resource fork.");
    }
    fclose(f);
    StMacFile app;
    st_macfile_init(&app);
    StErr why = {0};
    int result;
    if (st_applesingle_detect(bytes, size)) {
        result = st_applesingle_decode(bytes, size, &app, NULL, &why);
    } else {
        result = st_bytes_append(&app.rsrc, bytes, size) ? ST_ENOMEM : ST_OK;
    }
    MacApplicationKind kind;
    if (!result) result = mac_application_probe(&app, &kind, &why);
    st_macfile_free(&app);
    free(bytes);
    if (result == ST_ENOENT) return 1;
    if (result) return problem(err, n, why.msg[0] ? why.msg : "Invalid application resource fork.");
    return 0;
}

static int resolve_fork(const char *root, const char *full, char *out, size_t n,
                        char *err, size_t err_n) {
    char canonical_root[PATH_MAX], candidate[PATH_MAX], resolved[PATH_MAX];
    if (!root || !full || !realpath(root, canonical_root))
        return problem(err, err_n, "Could not resolve the OS999 volume.");
    size_t length = strlen(full);
    int explicit_fork = length > 5 && !strcasecmp(full + length - 5, ".rsrc");
    const char *suffixes[] = {".rsrc", "/..namedfork/rsrc"};
    int count = explicit_fork ? 1 : 2;
    for (int i = 0; i < count; i++) {
        int k = snprintf(candidate, sizeof candidate, "%s%s", full, explicit_fork ? "" : suffixes[i]);
        if (k < 0 || (size_t)k >= sizeof candidate)
            return problem(err, err_n, "Application path is too long.");
        struct stat st;
        if (lstat(candidate, &st)) {
            if (errno == ENOENT || errno == ENOTDIR) continue;
            return problem(err, err_n, "Could not inspect this application's resource fork.");
        }
        if (!explicit_fork && i == 1) {
            /* macOS treats ..namedfork as a virtual suffix: realpath rejects
             * it even when open succeeds. Resolve/check the base file first. */
            if (!realpath(full, resolved) || !contained(canonical_root, resolved))
                return problem(err, err_n, "Application resource fork is outside the OS999 volume.");
            k = snprintf(candidate, sizeof candidate, "%s/..namedfork/rsrc", resolved);
            if (k < 0 || (size_t)k >= sizeof candidate)
                return problem(err, err_n, "Application path is too long.");
            memcpy(resolved, candidate, (size_t)k + 1);
        } else if (!realpath(candidate, resolved) || !contained(canonical_root, resolved)) {
            return problem(err, err_n, "Application resource fork is outside the OS999 volume.");
        }
        int result = application_fork(resolved, err, err_n);
        if (result < 0) return -1;
        if (result == 1) continue;
        if (strlen(resolved) >= n) return problem(err, err_n, "Application path is too long.");
        memcpy(out, resolved, strlen(resolved) + 1);
        return 0;
    }
    /* A data-fork PEF must not fall through to an unrelated sibling archive. */
    int data = -1;
    if (realpath(full, resolved) && contained(canonical_root, resolved))
        data = open(resolved, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (data >= 0) {
        unsigned char magic[8];
        struct stat st;
        ssize_t got = fstat(data, &st) == 0 && S_ISREG(st.st_mode) ? read(data, magic, sizeof magic) : -1;
        close(data);
        if (got == sizeof magic && !memcmp(magic, "Joy!peff", 8))
            return problem(err, err_n, "PEF/CFM applications are not supported by the 68k runtime.");
    }
    return 1;
}

static int runtime_path(char *out, size_t n) {
    const char *override = getenv("OSPI_RUNTIME_PATH");
    char path[PATH_MAX], resolved[PATH_MAX];
    if (override && override[0]) {
        if (override[0] != '/' || strlen(override) >= sizeof path) return -1;
        memcpy(path, override, strlen(override) + 1);
    } else {
#if defined(__APPLE__)
        uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size)) return -1;
#else
        ssize_t size = readlink("/proc/self/exe", path, sizeof path - 1);
        if (size < 0) return -1;
        path[size] = 0;
#endif
        if (!realpath(path, resolved)) return -1;
        char *slash = strrchr(resolved, '/');
        if (!slash) return -1;
        *slash = 0;
        int k = snprintf(path, sizeof path, "%s/ospi-runtime", resolved);
        if (k < 0 || (size_t)k >= sizeof path) return -1;
    }
    struct stat st;
    if (!realpath(path, resolved) || stat(resolved, &st) || !S_ISREG(st.st_mode) ||
        access(resolved, X_OK) || strlen(resolved) >= n) return -1;
    memcpy(out, resolved, strlen(resolved) + 1);
    return 0;
}

static int wait_startup(pid_t child) {
    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started)) return 125;
    uint64_t start = (uint64_t)started.tv_sec * 1000000000u + (uint64_t)started.tv_nsec;
    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return 125;
        uint64_t elapsed = (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec - start;
        if (elapsed >= 800000000u) return 0;
        int status;
        pid_t got;
        do { got = waitpid(child, &status, WNOHANG); } while (got < 0 && errno == EINTR);
        if (got == child) return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (got < 0) return 125;
        uint64_t remaining = 800000000u - elapsed;
        struct timespec rest = {0, (long)(remaining < 50000000u ? remaining : 50000000u)};
        while (nanosleep(&rest, &rest) && errno == EINTR) {}
    }
}

static void summarize_log(const char *path, char *out, size_t n) {
    if (!n) return;
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return; }
    long end = ftell(f);
    if (end < 0 || fseek(f, end > 4096 ? end - 4096 : 0, SEEK_SET)) { fclose(f); return; }
    char text[4097];
    size_t got = fread(text, 1, sizeof text - 1, f);
    text[got] = 0;
    fclose(f);
    char *line = text;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (*line && strncmp(line, "resources=", 10) && strncmp(line, "instructions=", 13)) {
            for (char *p = line; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
            snprintf(out, n, "%s", line);
        }
        if (!next) break;
        line = next;
    }
}

/* Saves never land in the game folder or the volume. They go beside the
 * volume under "Saves/<application folder name>", one directory per app, so
 * reinstalling or removing a game leaves its saves in place. The parent is
 * created here; the runtime creates the leaf and enforces containment. */
static int saves_directory(const char *root, const char *fork_path, char *out, size_t n) {
    char app[PATH_MAX], parent[PATH_MAX];
    size_t length = strlen(fork_path), suffix = strlen("/..namedfork/rsrc");
    if (length >= sizeof app) return -1;
    memcpy(app, fork_path, length + 1);
    if (length > suffix && !strcmp(app + length - suffix, "/..namedfork/rsrc")) app[length - suffix] = 0;
    char *slash = strrchr(app, '/');
    if (!slash || slash == app) return -1;
    *slash = 0;
    const char *name = strrchr(app, '/');
    name = name ? name + 1 : app;
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) return -1;
    if (!realpath(root, parent)) return -1;
    slash = strrchr(parent, '/');
    if (!slash || slash == parent) return -1;
    *slash = 0;
    int k = snprintf(out, n, "%s/Saves", parent);
    if (k < 0 || (size_t)k >= n) return -1;
    if (mkdir(out, 0700) && errno != EEXIST) return -1;
    k = snprintf(out, n, "%s/Saves/%s", parent, name);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

int native_open(const char *root, const char *full, char *err, size_t n) {
    if (err && n) err[0] = 0;
    char fork_path[PATH_MAX], runtime[PATH_MAX], saves[PATH_MAX];
    int applicable = resolve_fork(root, full, fork_path, sizeof fork_path, err, n);
    if (applicable != 0) return applicable;
    if (saves_directory(root, fork_path, saves, sizeof saves))
        return problem(err, n, "Could not prepare this application's save folder.");
    if (native_surface_active()) {
        uint32_t state = RUNTIME_BRIDGE_STARTING;
        if (native_surface_state(&state, NULL, 0) == 0 && state == RUNTIME_BRIDGE_CLOSED)
            native_surface_close();
        else return problem(err, n, "Close the currently open classic application first.");
    }
    if (runtime_path(runtime, sizeof runtime))
        return problem(err, n, "OS999's independent runtime is missing. Build and install ospi-runtime beside OS999.");
    char bridge_name[64];
    RuntimeBridge *bridge = runtime_bridge_create(bridge_name, sizeof bridge_name);
    if (!bridge) return problem(err, n, "Could not create a private guest display surface.");
    char log_path[PATH_MAX];
    int log = os999_private_temp(log_path, sizeof log_path, "runtime");
    if (log < 0) {
        runtime_bridge_close(bridge); runtime_bridge_unlink(bridge_name);
        return problem(err, n, "Could not create the private runtime log.");
    }
    char disk_root[PATH_MAX];   /* the guest measures this internal disk (volume_capacity.h) */
    if (!realpath(root, disk_root)) snprintf(disk_root, sizeof disk_root, "%s", root);
    NativeSettings settings = native_settings_load(root);
    pid_t mid = fork();
    if (mid == 0) {
        if (setsid() < 0) _exit(125);
        pid_t child = fork();
        if (child == 0) {
            int input = open("/dev/null", O_RDONLY);
            if (input < 0 || dup2(input, STDIN_FILENO) < 0 ||
                dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) _exit(125);
            if (input > STDERR_FILENO) close(input);
            if (log > STDERR_FILENO) close(log);
            if (!getenv("OSPI_PERF_TRACE")) setenv("OSPI_PERF_TRACE", "1", 1);
            native_settings_apply(&settings);
            char *argv[] = {runtime, "--window", "--indexed", "--interactive",
                            "--bridge", bridge_name, "--saves", saves, "--disk", disk_root, fork_path, NULL};
            execv(runtime, argv);
            dprintf(STDERR_FILENO, "Could not execute OS999 runtime: %s\n", strerror(errno));
            _exit(127);
        }
        close(log);
        _exit(child < 0 ? 125 : wait_startup(child));
    }
    close(log);
    int status = 0;
    pid_t got = -1;
    if (mid > 0) do { got = waitpid(mid, &status, 0); } while (got < 0 && errno == EINTR);
    if (mid < 0 || got != mid || !WIFEXITED(status) || WEXITSTATUS(status)) {
        char detail[112];
        summarize_log(log_path, detail, sizeof detail);
        int code = got == mid && WIFEXITED(status) ? WEXITSTATUS(status) : 125;
        if (err && n) snprintf(err, n, "OS999 runtime stopped (%d).\n%s\nLog: %s",
                               code, detail[0] ? detail : "Startup failed.", log_path);
        runtime_bridge_close(bridge); runtime_bridge_unlink(bridge_name);
        return -1;
    }
    active_surface = bridge;
    snprintf(active_surface_name, sizeof active_surface_name, "%s", bridge_name);
    const char *title = strrchr(full, '/');
    title = title ? title + 1 : full;
    size_t title_length = strlen(title);
    if (title_length > 5 && !strcasecmp(title + title_length - 5, ".rsrc"))
        title_length -= 5;
    if (title_length >= sizeof active_surface_title) title_length = sizeof active_surface_title - 1;
    memcpy(active_surface_title, title, title_length);
    active_surface_title[title_length] = 0;
    if (err && n) snprintf(err, n, "Runtime started. Log: %s", log_path);
    return 0;
}
