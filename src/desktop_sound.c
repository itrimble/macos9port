#include "desktop_sound.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_WAV_BYTES = 2 * 1024 * 1024, MAX_PCM_BYTES = 48000 * 4 * 8 };
typedef struct { uint8_t *data; uint32_t bytes; } Sound;
static Sound sounds[DESKTOP_SOUND_COUNT];
static SDL_AudioDeviceID device;
static int enabled = 1, guest_active, audio_ref;
static uint32_t last_effect;
static int played_effect;
static char preference[4096];

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }

/* Only the shipped PCM16 stereo/48kHz format; never allocate from a WAV length. */
static int load_sound(Sound *out, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct stat st;
    if (fstat(fileno(f), &st) || !S_ISREG(st.st_mode) || st.st_size < 44 || st.st_size > MAX_WAV_BYTES) {
        fclose(f); return 0;
    }
    size_t n = (size_t)st.st_size;
    uint8_t *b = malloc(n);
    int read_ok = b && fread(b, 1, n, f) == n;
    fclose(f);
    if (!read_ok) { free(b); return 0; }
    int valid = !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4) && le32(b + 4) == n - 8;
    size_t pcm = 0, bytes = 0;
    int format = 0;
    for (size_t at = 12; valid && at < n;) {
        if (n - at < 8) { valid = 0; break; }
        size_t len = le32(b + at + 4), data = at + 8;
        if (len > n - data) { valid = 0; break; }
        if (!memcmp(b + at, "fmt ", 4)) {
            if (format || len < 16 || le16(b + data) != 1 || le16(b + data + 2) != 2 ||
                le32(b + data + 4) != 48000 || le32(b + data + 8) != 192000 ||
                le16(b + data + 12) != 4 || le16(b + data + 14) != 16) valid = 0;
            else format = 1;
        } else if (!memcmp(b + at, "data", 4)) {
            if (pcm || !len || len > MAX_PCM_BYTES || len % 4) valid = 0;
            else { pcm = data; bytes = len; }
        }
        size_t padded = len + (len & 1u);
        if (padded > n - data) { valid = 0; break; }
        at = data + padded;
    }
    if (!valid || !format || !pcm) { free(b); return 0; }
    memmove(b, b + pcm, bytes);
    out->data = b;
    out->bytes = (uint32_t)bytes;
    return 1;
}

void desktop_sound_shutdown(void) {
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    for (int i = 0; i < DESKTOP_SOUND_COUNT; i++) {
        free(sounds[i].data); sounds[i] = (Sound){0};
    }
    if (audio_ref) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_ref = 0;
    played_effect = guest_active = 0;
}

int desktop_sound_init(const char *dir, const char *preference_file) {
    desktop_sound_shutdown();
    preference[0] = 0;
    enabled = 1;
    if (!dir || !*dir) return 0;
    if (preference_file && *preference_file) {
        if (strlen(preference_file) >= sizeof preference) return 0;
        strcpy(preference, preference_file);
        FILE *f = fopen(preference, "r");
        if (f) { int c = fgetc(f); if (c == '0') enabled = 0; fclose(f); }
    }
    static const char *files[] = {"01-startup_chord.wav", "03-plain_beep.wav",
                                  "16-menu_open.wav", "17-menu_select.wav"};
    int loaded = 0;
    for (int i = 0; i < DESKTOP_SOUND_COUNT; i++) {
        char path[4096];
        int n = snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        if (n > 0 && (size_t)n < sizeof path) loaded += load_sound(&sounds[i], path);
    }
    if (!loaded || SDL_InitSubSystem(SDL_INIT_AUDIO)) return 0;
    audio_ref = 1;
    SDL_AudioSpec want = {0};
    want.freq = 48000; want.format = AUDIO_S16LSB; want.channels = 2; want.samples = 512;
    device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!device) return 0;
    SDL_PauseAudioDevice(device, 0);
    return loaded;
}

int desktop_sound_enabled(void) { return enabled; }

int desktop_sound_set_enabled(int value) {
    value = !!value;
    if (preference[0]) {
        char temp[4096];
        int n = snprintf(temp, sizeof temp, "%s.XXXXXX", preference);
        if (n <= 0 || (size_t)n >= sizeof temp) return 0;
        int fd = mkstemp(temp);
        if (fd < 0) return 0;
        char data[2] = {value ? '1' : '0', '\n'};
        int ok = write(fd, data, sizeof data) == sizeof data && fsync(fd) == 0;
        if (close(fd)) ok = 0;
        if (ok && rename(temp, preference)) ok = 0;
        if (!ok) { unlink(temp); return 0; }
    }
    enabled = value;
    if (!enabled && device) SDL_ClearQueuedAudio(device);
    return 1;
}

void desktop_sound_guest_active(int active) {
    guest_active = !!active;
    if (guest_active && device) SDL_ClearQueuedAudio(device);
}

uint32_t desktop_sound_queued_bytes(void) {
    return device ? SDL_GetQueuedAudioSize(device) : 0;
}

int desktop_sound_play(int event) {
    if (!device || !enabled || guest_active || event < 0 || event >= DESKTOP_SOUND_COUNT ||
        !sounds[event].data || SDL_GetQueuedAudioSize(device)) return 0;
    uint32_t now = SDL_GetTicks();
    if (event != DESKTOP_SOUND_STARTUP && played_effect && now - last_effect < 120) return 0;
    if (SDL_QueueAudio(device, sounds[event].data, sounds[event].bytes)) return 0;
    last_effect = now; played_effect = 1;
    return 1;
}
