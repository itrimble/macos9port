/* Real-time bridge probe. Drives the production `ospi-runtime --bridge`
 * process exactly as the OS999 desktop does: a private shared-memory surface,
 * scripted mouse and key input pushed through the bridge, frames read back
 * and dumped, then a CLOSED state to end the guest cleanly. Unlike the
 * headless probe this measures the live SDL audio path; the runtime's own
 * OSPI_AUDIO_TRACE / OSPI_PERF_TRACE lines land in the given log file. */
#include "runtime_bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROBE_MAX_ACTIONS 512
#define FRAME_CAPACITY ((size_t)RUNTIME_BRIDGE_MAX_WIDTH * RUNTIME_BRIDGE_MAX_HEIGHT)

typedef struct { uint64_t at_us; char kind[8]; int x, y, code; } ProbeAction;

static uint64_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static int parse_script(const char *path, ProbeAction *actions, int max) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open script %s\n", path); return -1; }
    char line[256];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (count == max) { fclose(f); return -1; }
        ProbeAction a = {0};
        double seconds;
        int fields = sscanf(line, "%lf %7s %d %d", &seconds, a.kind, &a.x, &a.y);
        if (fields < 2 || seconds < 0 || seconds > 3600) { fprintf(stderr, "Bad script line: %s", line); fclose(f); return -1; }
        a.at_us = (uint64_t)(seconds * 1000000.0);
        if (!strcmp(a.kind, "key")) a.code = a.x;
        else if (strcmp(a.kind, "down") && strcmp(a.kind, "up") && strcmp(a.kind, "move") && strcmp(a.kind, "click")) {
            fprintf(stderr, "Unknown action %s\n", a.kind); fclose(f); return -1;
        }
        actions[count++] = a;
    }
    fclose(f);
    for (int i = 0; i < count; i++) if (!strcmp(actions[i].kind, "click")) {
        if (count == max) return -1;
        strcpy(actions[i].kind, "down");
        ProbeAction up = actions[i]; strcpy(up.kind, "up"); up.at_us += 100000;
        int j = count++;
        while (j > 0 && actions[j - 1].at_us > up.at_us) { actions[j] = actions[j - 1]; j--; }
        actions[j] = up;
    }
    return count;
}

static void push(RuntimeBridge *bridge, uint32_t kind, int x, int y, uint32_t message) {
    RuntimeBridgeInput input = {.kind = kind, .x = x, .y = y, .message = message, .modifiers = 0};
    if (runtime_bridge_push(bridge, input) < 0) fprintf(stderr, "bridge input queue full\n");
}

static void apply(RuntimeBridge *bridge, const ProbeAction *a, uint64_t elapsed_us) {
    if (!strcmp(a->kind, "move")) push(bridge, RUNTIME_BRIDGE_MOTION, a->x, a->y, 0);
    else if (!strcmp(a->kind, "down")) { push(bridge, RUNTIME_BRIDGE_MOTION, a->x, a->y, 0); push(bridge, RUNTIME_BRIDGE_BUTTON_DOWN, a->x, a->y, 0); }
    else if (!strcmp(a->kind, "up")) push(bridge, RUNTIME_BRIDGE_BUTTON_UP, a->x, a->y, 0);
    else if (!strcmp(a->kind, "key")) { push(bridge, RUNTIME_BRIDGE_KEY_DOWN, 0, 0, (uint32_t)a->code); push(bridge, RUNTIME_BRIDGE_KEY_UP, 0, 0, (uint32_t)a->code); }
    fprintf(stderr, "bridge-action wall-ms=%llu %s %d %d\n", (unsigned long long)(elapsed_us / 1000), a->kind, a->x, a->y);
}

static int write_frame(const char *dir, uint64_t seconds, const uint32_t *pixels, int width, int height) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/frame-%04llus.ppm", dir, (unsigned long long)seconds);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (size_t i = 0; i < (size_t)width * height; i++) {
        uint8_t rgb[3] = {(uint8_t)(pixels[i] >> 16), (uint8_t)(pixels[i] >> 8), (uint8_t)pixels[i]};
        fwrite(rgb, 1, 3, f);
    }
    return fclose(f);
}

int main(int argc, char **argv) {
    const char *runtime = "build/ospi-runtime", *fork_path = NULL, *script = NULL;
    const char *frames_dir = NULL, *log_path = NULL, *wav = NULL;
    double seconds = 60, frame_every = 5;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--runtime") && i + 1 < argc) runtime = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_dir = argv[++i];
        else if (!strcmp(argv[i], "--frame-every") && i + 1 < argc) frame_every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) log_path = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (argv[i][0] != '-' && !fork_path) fork_path = argv[i];
        else { fprintf(stderr, "Unexpected argument: %s\n", argv[i]); return 2; }
    }
    if (!fork_path || !log_path || seconds <= 0 || seconds > 1200 || frame_every <= 0) {
        fprintf(stderr, "Usage: ospi-bridge-probe --log runtime.log [--runtime PATH] [--seconds S] [--script FILE] [--frames DIR] [--frame-every S] [--wav out.wav] resource-fork\n");
        return 2;
    }
    ProbeAction actions[PROBE_MAX_ACTIONS];
    int action_count = 0, next_action = 0;
    if (script && (action_count = parse_script(script, actions, PROBE_MAX_ACTIONS)) < 0) return 2;
    char runtime_abs[PATH_MAX], fork_abs[PATH_MAX];
    if (!realpath(runtime, runtime_abs) || !realpath(fork_path, fork_abs)) { fprintf(stderr, "Cannot resolve runtime or fork path.\n"); return 2; }
    char bridge_name[64];
    RuntimeBridge *bridge = runtime_bridge_create(bridge_name, sizeof bridge_name);
    if (!bridge) { fprintf(stderr, "Cannot create bridge.\n"); return 2; }
    int log = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log < 0) { fprintf(stderr, "Cannot open log %s\n", log_path); return 2; }
    uint64_t started = now_us();
    pid_t child = fork();
    if (child == 0) {
        int input = open("/dev/null", O_RDONLY);
        if (input < 0 || dup2(input, STDIN_FILENO) < 0 || dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) _exit(125);
        setenv("OSPI_PERF_TRACE", "1", 1);
        setenv("OSPI_AUDIO_TRACE", "1", 1);
        if (wav) setenv("OSPI_AUDIO_WAV", wav, 1);
        char *args[] = {runtime_abs, "--window", "--indexed", "--interactive", "--bridge", bridge_name, fork_abs, NULL};
        execv(runtime_abs, args);
        _exit(127);
    }
    close(log);
    if (child < 0) { fprintf(stderr, "fork failed\n"); return 2; }
    uint32_t *pixels = malloc(FRAME_CAPACITY * sizeof *pixels);
    uint64_t next_frame_us = 0, last_sequence = 0, frames_seen = 0;
    uint32_t state = RUNTIME_BRIDGE_STARTING;
    char message[256] = "";
    int ready_reported = 0;
    for (;;) {
        uint64_t elapsed = now_us() - started;
        if (elapsed >= (uint64_t)(seconds * 1000000.0)) break;
        runtime_bridge_get_state(bridge, &state, message, sizeof message);
        if (state == RUNTIME_BRIDGE_FAILED || state == RUNTIME_BRIDGE_CLOSED) {
            fprintf(stderr, "runtime state %u at wall-ms=%llu: %s\n", state, (unsigned long long)(elapsed / 1000), message);
            break;
        }
        if (state == RUNTIME_BRIDGE_READY && !ready_reported) { ready_reported = 1; fprintf(stderr, "runtime READY at wall-ms=%llu\n", (unsigned long long)(elapsed / 1000)); }
        while (next_action < action_count && actions[next_action].at_us <= elapsed) apply(bridge, &actions[next_action++], elapsed);
        int width, height;
        uint64_t sequence;
        if (pixels && runtime_bridge_read(bridge, pixels, FRAME_CAPACITY, &width, &height, &sequence) == 0 && sequence != last_sequence) {
            last_sequence = sequence; frames_seen++;
            if (frames_dir && elapsed >= next_frame_us) {
                write_frame(frames_dir, (elapsed + 500000) / 1000000, pixels, width, height);
                next_frame_us += (uint64_t)(frame_every * 1000000.0);
            }
        }
        int status;
        if (waitpid(child, &status, WNOHANG) == child) { fprintf(stderr, "runtime exited early (status %d)\n", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)); child = -1; break; }
        struct timespec rest = {0, 4000000};
        nanosleep(&rest, NULL);
    }
    runtime_bridge_set_state(bridge, RUNTIME_BRIDGE_CLOSED, "");
    int status = -1, code = -1;
    if (child > 0) {
        for (int wait = 0; wait < 100; wait++) {
            if (waitpid(child, &status, WNOHANG) == child) { code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status); break; }
            struct timespec rest = {0, 50000000}; nanosleep(&rest, NULL);
        }
        if (code < 0) { kill(child, SIGTERM); waitpid(child, &status, 0); fprintf(stderr, "runtime did not exit after CLOSED; terminated\n"); }
    }
    printf("bridge-frames=%llu last-sequence=%llu runtime-exit=%d state=%u\n", (unsigned long long)frames_seen, (unsigned long long)last_sequence, code, state);
    free(pixels);
    runtime_bridge_close(bridge);
    runtime_bridge_unlink(bridge_name);
    return 0;
}
