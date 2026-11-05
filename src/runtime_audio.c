#include "runtime_audio.h"
#include "mac_sound_int.h"
#include "mac_sound_double.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* SDL owns a copy of submitted PCM. Guest execution and mixing stay on the
 * caller's thread; there is no guest CPU access from an audio callback. */
#define AUDIO_TARGET_FRAMES 2048u
#define AUDIO_CHUNK_FRAMES 64u
int runtime_audio_open(RuntimeAudio *audio) {
    if (audio->device) return 0;
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
        audio->owns_audio = 1;
    }
    SDL_AudioSpec wanted, obtained;
    SDL_zero(wanted);
    wanted.freq = MAC_SOUND_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (!audio->device) { runtime_audio_close(audio); return -1; }
    if (obtained.freq != wanted.freq || obtained.format != wanted.format ||
        obtained.channels != wanted.channels) {
        runtime_audio_close(audio);
        SDL_SetError("Audio device did not provide the requested PCM format");
        return -1;
    }
    SDL_PauseAudioDevice(audio->device, 0);
    return 0;
}
int runtime_audio_pump(RuntimeAudio *audio, MacRuntime *rt,
                       size_t callback_budget, size_t *callback_instructions) {
    uint64_t pump_started = SDL_GetPerformanceCounter();
    uint64_t submitted_before = audio->submitted_frames;
    *callback_instructions = 0;
    if (!rt->sound || mac_cpu_halted(rt->cpu)) return 0;
    if (!audio->device && runtime_audio_open(audio)) {
        fprintf(stderr, "Cannot open audio output: %s\n", SDL_GetError());
        return -1;
    }
    uint32_t bytes = SDL_GetQueuedAudioSize(audio->device);
    if (bytes > AUDIO_TARGET_FRAMES * 4 || bytes % 4) {
        SDL_SetError("Audio queue violated the bounded stereo-frame contract");
        return -1;
    }
    unsigned available = AUDIO_TARGET_FRAMES - bytes / 4;
    if (getenv("OSPI_AUDIO_TRACE")) {
        uint64_t now = SDL_GetTicks();
        if (!audio->last_trace_ticks || now - audio->last_trace_ticks >= 1000) {
            audio->last_trace_ticks = now;
            fprintf(stderr, "audio-trace ms=%llu host-queued-frames=%u submitted=%llu callback-budget=%zu last-pump-us=%llu last-pump-frames=%u\n",
                    (unsigned long long)now, bytes / 4,
                    (unsigned long long)audio->submitted_frames, callback_budget,
                    (unsigned long long)audio->last_pump_us, audio->last_pump_frames);
            for (unsigned i = 0; i < MAC_SOUND_CHANNELS; i++) {
                const MacSoundChannel *c = &rt->sound->channels[i];
                if (!c->address) continue;
                fprintf(stderr, "  ch%u playing=%d paused=%d count=%u callback=%d wait=%u buffer=%d double=%d/%u voice=%d phase=%llu rate=%llu\n",
                        i, c->playing, c->paused, c->count, c->callback_pending,
                        c->wait_frames, c->buffer_block, c->double_buffer.active,
                        c->double_buffer.pending, c->voice,
                        (unsigned long long)c->phase, (unsigned long long)c->rate);
            }
        }
    }
    int16_t pcm[AUDIO_CHUNK_FRAMES * 2];
    for (unsigned work = 0; available && work < AUDIO_TARGET_FRAMES / AUDIO_CHUNK_FRAMES; work++) {
        size_t ran = mac_sound_poll(rt, callback_budget);
        callback_budget -= ran;
        *callback_instructions += ran;
        if (mac_cpu_halted(rt->cpu)) return -1;
        ran = mac_sound_double_poll(rt, callback_budget);
        callback_budget -= ran;
        *callback_instructions += ran;
        if (mac_cpu_halted(rt->cpu)) return -1;
        unsigned frames = available < AUDIO_CHUNK_FRAMES ? available : AUDIO_CHUNK_FRAMES;
        size_t rendered = mac_sound_render(rt, pcm, frames);
        if (rendered != frames || mac_cpu_halted(rt->cpu)) return -1;
        if (SDL_QueueAudio(audio->device, pcm, frames * sizeof(int16_t) * 2)) {
            fprintf(stderr, "Cannot submit audio output: %s\n", SDL_GetError());
            return -1;
        }
        audio->submitted_frames += frames;
        available -= frames;
    }
    uint64_t elapsed = SDL_GetPerformanceCounter() - pump_started;
    uint64_t frequency = SDL_GetPerformanceFrequency();
    audio->last_pump_us = frequency ? elapsed / frequency * UINT64_C(1000000) +
        elapsed % frequency * UINT64_C(1000000) / frequency : 0;
    uint64_t submitted = audio->submitted_frames - submitted_before;
    audio->last_pump_frames = submitted > UINT32_MAX ? UINT32_MAX : (uint32_t)submitted;
    return 0;
}
void runtime_audio_close(RuntimeAudio *audio) {
    if (audio->device) {
        SDL_PauseAudioDevice(audio->device, 1);
        SDL_ClearQueuedAudio(audio->device);
        SDL_CloseAudioDevice(audio->device);
    }
    if (audio->owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    memset(audio, 0, sizeof *audio);
}
