/* Headless, deterministic runtime probe. Runs a guest application on a fixed
 * 60 Hz clock with scripted input, pulls PCM through the production Sound
 * Manager poll/render chain, and writes a WAV plus periodic frame dumps.
 * No SDL, no real time: the same script and fork produce the same output. */
#include "mac_runtime.h"
#include "mac_sound_int.h"
#include "mac_sound_double.h"
#include "mac_time.h"
#include "runtime_frame.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_TICK_US 16667u
#define PROBE_FRAMES_PER_TICK (MAC_SOUND_RATE / 60u)
#define PROBE_CYCLES_PER_SECOND UINT64_C(16000000)
#define PROBE_CHUNK_FRAMES 64u
#define PROBE_CALLBACK_BUDGET 200000u
#define PROBE_MAX_ACTIONS 512

typedef struct { uint64_t at_us; char kind[8]; int x, y, code, ascii; } ProbeAction;

static uint8_t *read_bytes(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long length = ftell(f);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    uint8_t *bytes = malloc(length ? (size_t)length : 1);
    if (!bytes || fread(bytes, 1, (size_t)length, f) != (size_t)length) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)length;
    return bytes;
}

static int parse_script(const char *path, ProbeAction *actions, int max) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open script %s\n", path); return -1; }
    char line[256];
    int count = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (count == max) { fprintf(stderr, "Script exceeds %d actions.\n", max); fclose(f); return -1; }
        ProbeAction a = {0};
        double seconds;
        int fields = sscanf(line, "%lf %7s %d %d", &seconds, a.kind, &a.x, &a.y);
        if (fields < 2 || seconds < 0 || seconds > 3600) { fprintf(stderr, "Bad script line: %s", line); fclose(f); return -1; }
        a.at_us = (uint64_t)(seconds * 1000000.0);
        if (!strcmp(a.kind, "key")) { a.code = a.x; a.ascii = a.y; }
        else if (strcmp(a.kind, "down") && strcmp(a.kind, "up") && strcmp(a.kind, "move") && strcmp(a.kind, "click")) {
            fprintf(stderr, "Unknown action %s\n", a.kind); fclose(f); return -1;
        }
        actions[count++] = a;
    }
    fclose(f);
    return count;
}

static void store_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void store_le32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

static int write_wav(const char *path, const int16_t *pcm, uint64_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint8_t header[44] = {0};
    uint32_t data = (uint32_t)(frames * 4);
    memcpy(header, "RIFF", 4); store_le32(header + 4, 36 + data);
    memcpy(header + 8, "WAVEfmt ", 8); store_le32(header + 16, 16);
    store_le16(header + 20, 1); store_le16(header + 22, 2);
    store_le32(header + 24, MAC_SOUND_RATE); store_le32(header + 28, MAC_SOUND_RATE * 4);
    store_le16(header + 32, 4); store_le16(header + 34, 16);
    memcpy(header + 36, "data", 4); store_le32(header + 40, data);
    int ok = fwrite(header, sizeof header, 1, f) == 1;
    for (uint64_t i = 0; ok && i < frames * 2; i++) {
        uint8_t s[2]; store_le16(s, (uint16_t)pcm[i]);
        ok = fwrite(s, 2, 1, f) == 1;
    }
    return fclose(f) || !ok ? -1 : 0;
}

static int write_frame(MacRuntime *rt, const char *dir, uint64_t clock_us) {
    size_t pixels;
    if (frame_dimensions(rt, &pixels)) return -1;
    uint32_t *out = malloc(pixels * sizeof *out);
    if (!out || copy_frame(rt, out, pixels)) { free(out); return -1; }
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/frame-%04llus.ppm", dir, (unsigned long long)((clock_us + 500000) / 1000000));
    FILE *f = fopen(path, "wb");
    if (!f) { free(out); return -1; }
    fprintf(f, "P6\n%u %u\n255\n", rt->screen_width, rt->screen_height);
    for (size_t i = 0; i < pixels; i++) {
        uint8_t rgb[3] = {(uint8_t)(out[i] >> 16), (uint8_t)(out[i] >> 8), (uint8_t)out[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    free(out);
    return 0;
}

/* Mirrors runtime_audio_pump's production order: callbacks, then a bounded
 * chunk of mixed PCM, repeated until the tick's frames are rendered. */
static int render_tick(MacRuntime *rt, int16_t *out, unsigned frames, uint64_t *callback_instructions) {
    size_t budget = PROBE_CALLBACK_BUDGET;
    if (!rt->sound) { memset(out, 0, (size_t)frames * 2 * sizeof *out); return 0; }
    for (unsigned done = 0; done < frames;) {
        size_t ran = mac_sound_poll(rt, budget);
        budget -= ran; *callback_instructions += ran;
        if (mac_cpu_halted(rt->cpu)) return -1;
        ran = mac_sound_double_poll(rt, budget);
        budget -= ran; *callback_instructions += ran;
        if (mac_cpu_halted(rt->cpu)) return -1;
        unsigned chunk = frames - done < PROBE_CHUNK_FRAMES ? frames - done : PROBE_CHUNK_FRAMES;
        size_t rendered = mac_sound_render(rt, out + (size_t)done * 2, chunk);
        if (rendered != chunk || mac_cpu_halted(rt->cpu)) return -1;
        done += chunk;
    }
    return 0;
}

static void apply_action(MacRuntime *rt, const ProbeAction *a, int *mouse_down, uint64_t clock_us) {
    if (!strcmp(a->kind, "move")) {
        rt->mouse_x = (int16_t)a->x; rt->mouse_y = (int16_t)a->y; rt->cursor_obscured = 0;
    } else if (!strcmp(a->kind, "down") || !strcmp(a->kind, "up")) {
        *mouse_down = a->kind[0] == 'd';
        mac_runtime_post_event(rt, *mouse_down ? 1 : 2, 0, *mouse_down ? 0 : 0x80, (int16_t)a->x, (int16_t)a->y);
    } else if (!strcmp(a->kind, "key")) {
        uint32_t message = ((uint32_t)(a->code & 255) << 8) | (uint32_t)(a->ascii & 255);
        mac_runtime_post_event(rt, 3, message, (uint16_t)(*mouse_down ? 0 : 0x80), rt->mouse_x, rt->mouse_y);
    }
    fprintf(stderr, "probe-action guest-ms=%llu %s %d %d\n", (unsigned long long)(clock_us / 1000), a->kind, a->x, a->y);
}

int main(int argc, char **argv) {
    const char *path = NULL, *wav = NULL, *frames_dir = NULL, *script = NULL, *dump_ram = NULL;
    double seconds = 30, frame_every = 5;
    int slices = 4, trace = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames_dir = argv[++i];
        else if (!strcmp(argv[i], "--frame-every") && i + 1 < argc) frame_every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--slices") && i + 1 < argc) slices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) trace = 1;
        else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) dump_ram = argv[++i];
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else { fprintf(stderr, "Unexpected argument: %s\n", argv[i]); return 2; }
    }
    if (!path || seconds <= 0 || seconds > 1200 || slices < 1 || slices > 16 || frame_every <= 0) {
        fprintf(stderr, "Usage: ospi-runtime-probe [--seconds S] [--wav out.wav] [--frames DIR] [--frame-every S] [--script FILE] [--slices N] [--trace] [--dump-ram FILE] resource-fork\n");
        return 2;
    }
    ProbeAction actions[PROBE_MAX_ACTIONS];
    int action_count = 0, next_action = 0;
    if (script && (action_count = parse_script(script, actions, PROBE_MAX_ACTIONS)) < 0) return 2;
    /* Expand click into down + up 100 ms later, keeping time order. */
    for (int i = 0; i < action_count; i++) if (!strcmp(actions[i].kind, "click")) {
        if (action_count == PROBE_MAX_ACTIONS) { fprintf(stderr, "Too many actions after click expansion.\n"); return 2; }
        strcpy(actions[i].kind, "down");
        ProbeAction up = actions[i]; strcpy(up.kind, "up"); up.at_us += 100000;
        int j = action_count++;
        while (j > 0 && actions[j - 1].at_us > up.at_us) { actions[j] = actions[j - 1]; j--; }
        actions[j] = up;
    }
    size_t size;
    uint8_t *bytes = read_bytes(path, &size);
    if (!bytes) { fprintf(stderr, "Cannot read resource fork.\n"); return 2; }
    MacResourceFork fork;
    char error[240];
    if (mac_resource_open_container(&fork, bytes, size, error, sizeof error)) { fprintf(stderr, "Resource fork: %s\n", error); return 2; }
    MacRuntime rt;
    if (mac_runtime_init(&rt, &fork, error, sizeof error)) { fprintf(stderr, "Loader: %s\n", error); return 2; }
    rt.indexed_requested = 1;
    rt.trace = trace;
    char data_root[PATH_MAX];
    if (!realpath(path, data_root)) { fprintf(stderr, "Cannot resolve %s\n", path); return 2; }
    char *slash = strrchr(data_root, '/');
    if (!slash) return 2;
    *slash = 0;
    if (mac_files_root(&rt, data_root)) { fprintf(stderr, "Cannot establish data directory.\n"); return 2; }

    uint64_t total_frames = (uint64_t)(seconds * MAC_SOUND_RATE) + PROBE_FRAMES_PER_TICK;
    int16_t *pcm = wav ? calloc((size_t)total_frames * 2, sizeof *pcm) : NULL;
    uint64_t captured = 0, clock_us = 0, callback_instructions = 0, next_frame_us = 0;
    uint64_t cycles_per_slice = PROBE_CYCLES_PER_SECOND / 60 / (uint64_t)slices;
    unsigned frames_per_slice = PROBE_FRAMES_PER_TICK / (unsigned)slices;
    int mouse_down = 0, result = 4;
    uint64_t trace_second = 0, trace_instructions = 0;
    int16_t slice_pcm[PROBE_FRAMES_PER_TICK * 2];
    while (clock_us < (uint64_t)(seconds * 1000000.0)) {
        if (mac_cpu_halted(rt.cpu)) { result = rt.exited ? 0 : 3; break; }
        while (next_action < action_count && actions[next_action].at_us <= clock_us)
            apply_action(&rt, &actions[next_action++], &mouse_down, clock_us);
        rt.ticks++;
        clock_us += PROBE_TICK_US;
        if (mac_time_set(&rt, clock_us)) { fprintf(stderr, "Time manager rejected clock.\n"); result = 3; break; }
        int failed = 0;
        for (int s = 0; s < slices && !failed; s++) {
            if (!mac_runtime_run_cycles(&rt, cycles_per_slice) && !mac_cpu_halted(rt.cpu)) {
                fprintf(stderr, "Guest CPU made no progress.\n"); failed = 1; break;
            }
            if (mac_cpu_halted(rt.cpu)) break;
            unsigned frames = s == slices - 1 ? PROBE_FRAMES_PER_TICK - frames_per_slice * (unsigned)(slices - 1) : frames_per_slice;
            if (render_tick(&rt, slice_pcm, frames, &callback_instructions)) { failed = 1; break; }
            if (pcm && captured + frames <= total_frames) {
                memcpy(pcm + captured * 2, slice_pcm, (size_t)frames * 4);
                captured += frames;
            }
        }
        if (failed && !mac_cpu_halted(rt.cpu)) { result = 3; break; }
        if (frames_dir && clock_us >= next_frame_us) {
            if (write_frame(&rt, frames_dir, clock_us)) fprintf(stderr, "Frame dump failed at %llu ms (no screen yet?).\n", (unsigned long long)(clock_us / 1000));
            next_frame_us += (uint64_t)(frame_every * 1000000.0);
        }
        if (clock_us / 1000000 > trace_second) {
            trace_second = clock_us / 1000000;
            fprintf(stderr, "probe-second=%llu instructions=%llu callback-instructions=%llu", (unsigned long long)trace_second,
                    (unsigned long long)(rt.instructions - trace_instructions), (unsigned long long)callback_instructions);
            trace_instructions = rt.instructions; callback_instructions = 0;
            if (rt.sound) for (unsigned i = 0; i < MAC_SOUND_CHANNELS; i++) {
                const MacSoundChannel *c = &rt.sound->channels[i];
                if (c->address) fprintf(stderr, " ch%u[play=%d q=%u wait=%u dbl=%d/%u voice=%d]", i, c->playing, c->count, c->wait_frames, c->double_buffer.active, c->double_buffer.pending, c->voice);
            }
            fputc('\n', stderr);
        }
    }
    if (mac_cpu_halted(rt.cpu)) fprintf(stderr, "halt: %s\n", mac_cpu_error(rt.cpu));
    if (!mac_cpu_halted(rt.cpu) && result == 4) fprintf(stderr, "Probe budget exhausted at %llu ms; gameplay unverified.\n", (unsigned long long)(clock_us / 1000));
    printf("instructions=%llu guest-ms=%llu captured-frames=%llu result=%d\n", (unsigned long long)rt.instructions,
           (unsigned long long)(clock_us / 1000), (unsigned long long)captured, result);
    if (pcm && write_wav(wav, pcm, captured)) fprintf(stderr, "Cannot write %s\n", wav);
    if (dump_ram) {
        FILE *f = fopen(dump_ram, "wb");
        if (!f || fwrite(mac_cpu_ram(rt.cpu), 1, mac_cpu_ram_size(rt.cpu), f) != mac_cpu_ram_size(rt.cpu))
            fprintf(stderr, "Cannot write RAM dump %s\n", dump_ram);
        if (f) fclose(f);
        fprintf(stderr, "ram-dump %s pc=%08x a7=%08x\n", dump_ram, mac_cpu_get(rt.cpu, MAC_PC), mac_cpu_get(rt.cpu, MAC_A7));
    }
    free(pcm);
    mac_runtime_destroy(&rt);
    free(bytes);
    return result;
}
