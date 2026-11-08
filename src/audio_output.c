#include "audio_output.h"
#include <SDL.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { OUTPUT_RING_FRAMES = 65536 }; /* power of two */

struct AudioOutput {
    SDL_AudioDeviceID device;
    SDL_AudioFormat obtained_format;
    int sample_rate;
    int channels;
    int owns_sdl_audio;
    _Atomic int paused;
    _Atomic int volume_milli;
    AudioOutputPullFn pull_cb;
    void *userdata;
    float ring[OUTPUT_RING_FRAMES * 2];
    _Atomic uint64_t read_pos;
    _Atomic uint64_t write_pos;
};

static float clamp_f32(float x) {
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

static void sdl_output_callback(void *ctx, Uint8 *stream, int len) {
    AudioOutput *out = ctx;
    if (!out || len <= 0) return;
    memset(stream, 0, (size_t)len);
    if (atomic_load_explicit(&out->paused, memory_order_relaxed)) return;

    int is_f32 = (out->obtained_format == AUDIO_F32LSB || out->obtained_format == AUDIO_F32MSB ||
                  out->obtained_format == AUDIO_F32SYS);
    size_t bytes_per_frame = (size_t)out->channels * (is_f32 ? sizeof(float) : sizeof(int16_t));
    if (bytes_per_frame == 0) return;
    size_t frames = (size_t)len / bytes_per_frame;
    float vol = (float)atomic_load_explicit(&out->volume_milli, memory_order_relaxed) / 1000.0f;

    float temp[2048 * 2];
    size_t offset = 0;
    while (offset < frames) {
        size_t chunk = (frames - offset) < 2048 ? (frames - offset) : 2048;
        memset(temp, 0, chunk * 2 * sizeof(float));
        if (out->pull_cb) {
            out->pull_cb(out->userdata, temp, chunk);
        }
        uint64_t r = atomic_load_explicit(&out->read_pos, memory_order_relaxed);
        uint64_t w = atomic_load_explicit(&out->write_pos, memory_order_acquire);
        uint64_t avail = w - r;
        size_t take = avail < chunk ? (size_t)avail : chunk;
        for (size_t i = 0; i < take; i++) {
            size_t idx = (size_t)((r + i) & (OUTPUT_RING_FRAMES - 1));
            temp[i * 2] += out->ring[idx * 2];
            temp[i * 2 + 1] += out->ring[idx * 2 + 1];
        }
        if (take > 0)
            atomic_store_explicit(&out->read_pos, r + take, memory_order_release);

        if (is_f32) {
            float *dst = (float *)stream + offset * 2;
            for (size_t i = 0; i < chunk * 2; i++)
                dst[i] = clamp_f32(temp[i] * vol);
        } else {
            int16_t *dst = (int16_t *)stream + offset * 2;
            for (size_t i = 0; i < chunk * 2; i++) {
                float s = clamp_f32(temp[i] * vol);
                dst[i] = (int16_t)(s * 32767.0f);
            }
        }
        offset += chunk;
    }
}

int audio_output_open(AudioOutput **out_ptr, int sample_rate, int channels,
                      AudioOutputPullFn pull_cb, void *userdata) {
    if (!out_ptr || sample_rate <= 0 || channels != 2) return -1;
    *out_ptr = NULL;
    AudioOutput *out = calloc(1, sizeof *out);
    if (!out) return -1;
    out->sample_rate = sample_rate;
    out->channels = channels;
    out->pull_cb = pull_cb;
    out->userdata = userdata;
    atomic_init(&out->paused, 0);
    atomic_init(&out->volume_milli, 1000);
    atomic_init(&out->read_pos, 0);
    atomic_init(&out->write_pos, 0);

    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            free(out);
            return -1;
        }
        out->owns_sdl_audio = 1;
    }
    SDL_AudioSpec want = {0}, got = {0};
    want.freq = sample_rate;
    want.format = AUDIO_F32SYS;
    want.channels = (Uint8)channels;
    want.samples = 512;
    want.callback = sdl_output_callback;
    want.userdata = out;
    out->device = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!out->device) {
        want.format = AUDIO_S16SYS;
        out->device = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    }
    if (!out->device) {
        if (out->owns_sdl_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        free(out);
        return -1;
    }
    out->obtained_format = got.format;
    SDL_PauseAudioDevice(out->device, 0);
    *out_ptr = out;
    return 0;
}

int audio_output_submit_pcm(AudioOutput *out, const float *pcm_f32, size_t frames) {
    if (!out || !pcm_f32) return -1;
    uint64_t r = atomic_load_explicit(&out->read_pos, memory_order_acquire);
    uint64_t w = atomic_load_explicit(&out->write_pos, memory_order_relaxed);
    uint64_t used = w - r;
    if (used >= OUTPUT_RING_FRAMES) return 0;
    size_t space = (size_t)(OUTPUT_RING_FRAMES - used);
    size_t write_n = frames < space ? frames : space;
    for (size_t i = 0; i < write_n; i++) {
        size_t idx = (size_t)((w + i) & (OUTPUT_RING_FRAMES - 1));
        out->ring[idx * 2] = pcm_f32[i * 2];
        out->ring[idx * 2 + 1] = pcm_f32[i * 2 + 1];
    }
    atomic_store_explicit(&out->write_pos, w + write_n, memory_order_release);
    return (int)write_n;
}

void audio_output_pause(AudioOutput *out, int paused) {
    if (!out) return;
    atomic_store_explicit(&out->paused, paused ? 1 : 0, memory_order_relaxed);
    if (out->device) SDL_PauseAudioDevice(out->device, paused ? 1 : 0);
}

void audio_output_set_volume(AudioOutput *out, float volume) {
    if (!out) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    atomic_store_explicit(&out->volume_milli, (int)(volume * 1000.0f + 0.5f), memory_order_relaxed);
}

float audio_output_get_volume(const AudioOutput *out) {
    if (!out) return 0.0f;
    return (float)atomic_load_explicit(&out->volume_milli, memory_order_relaxed) / 1000.0f;
}

size_t audio_output_queued_frames(const AudioOutput *out) {
    if (!out) return 0;
    uint64_t r = atomic_load_explicit(&out->read_pos, memory_order_acquire);
    uint64_t w = atomic_load_explicit(&out->write_pos, memory_order_acquire);
    return w > r ? (size_t)(w - r) : 0;
}

void audio_output_clear(AudioOutput *out) {
    if (!out) return;
    uint64_t w = atomic_load_explicit(&out->write_pos, memory_order_relaxed);
    atomic_store_explicit(&out->read_pos, w, memory_order_release);
}

void audio_output_close(AudioOutput *out) {
    if (!out) return;
    if (out->device) {
        SDL_PauseAudioDevice(out->device, 1);
        SDL_CloseAudioDevice(out->device);
    }
    if (out->owns_sdl_audio)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(out);
}
