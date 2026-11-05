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
    fprintf(stderr, "Audio output: driver=%s rate=%dHz channels=%u buffer=%u frames\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown",
            obtained.freq, obtained.channels, obtained.samples);
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
    int trace = getenv("OSPI_AUDIO_TRACE") != NULL;
    if (trace) {
        uint64_t now = SDL_GetTicks();
        if (!audio->last_trace_ticks || now - audio->last_trace_ticks >= 1000) {
            uint64_t interval_ms = audio->last_trace_ticks ? now - audio->last_trace_ticks : 0;
            audio->last_trace_ticks = now;
            uint64_t frequency = SDL_GetPerformanceFrequency();
            double avg_us = audio->trace_pumps && frequency ?
                (double)audio->trace_pump_ticks_total * 1000000.0 /
                    ((double)frequency * audio->trace_pumps) : 0.0;
            double max_us = frequency ?
                (double)audio->trace_pump_ticks_max * 1000000.0 / frequency : 0.0;
            double mixed_fps = interval_ms ?
                (double)audio->trace_frames * 1000.0 / interval_ms : 0.0;
            double queue_avg = audio->trace_queue_samples ?
                (double)audio->trace_queue_frames_total / audio->trace_queue_samples : 0.0;
            uint64_t queue_frames = bytes / 4;
            uint64_t consumed_frames = 0, expected_frames = 0, estimated_deficit_frames = 0;
            int queue_balance_valid = audio->trace_have_queue_baseline &&
                audio->trace_start_queue_frames + audio->trace_frames >= queue_frames;
            if (interval_ms) {
                expected_frames = (uint64_t)((long double)interval_ms * MAC_SOUND_RATE / 1000.0L);
            }
            if (queue_balance_valid) {
                consumed_frames = audio->trace_start_queue_frames + audio->trace_frames - queue_frames;
                if (expected_frames > consumed_frames)
                    estimated_deficit_frames = expected_frames - consumed_frames;
            }
            audio->trace_start_queue_frames = queue_frames;
            audio->trace_have_queue_baseline = 1;
            uint64_t rendered = rt->sound->rendered_frames;
            uint64_t clipped = rt->sound->clipped_frames;
            uint64_t interval_rendered = rendered - audio->trace_rendered_frames;
            uint64_t interval_clipped = clipped - audio->trace_clipped_frames;
            double clip_rate = interval_rendered ?
                (double)interval_clipped * 100.0 / interval_rendered : 0.0;
            audio->trace_rendered_frames = rendered;
            audio->trace_clipped_frames = clipped;
            fprintf(stderr, "audio-trace ms=%llu host-queued-frames=%u submitted=%llu queue-balance=%s consumed-est=%llu consumed-fps=%.1f nominal-rate=%u deficit-est=%llu callback-budget=%zu last-pump-ticks=%llu last-pump-frames=%u pump-count=%u pump-avg-us=%.3f pump-max-us=%.3f mixed-frames=%llu mixed-fps=%.1f clip-frames=%llu clip-rate-pct=%.3f peak-abs=%u queue-samples=%llu queue-avg-frames=%.1f queue-min-frames=%u queue-max-frames=%u empty-queue-checks=%u\n",
                    (unsigned long long)now, bytes / 4,
                    (unsigned long long)audio->submitted_frames,
                    queue_balance_valid ? "valid" : "invalid",
                    (unsigned long long)consumed_frames,
                    interval_ms ? (double)consumed_frames * 1000.0 / interval_ms : 0.0,
                    (unsigned)MAC_SOUND_RATE,
                    (unsigned long long)estimated_deficit_frames,
                    callback_budget,
                    (unsigned long long)audio->last_pump_ticks, audio->last_pump_frames,
                    audio->trace_pumps, avg_us, max_us,
                    (unsigned long long)audio->trace_frames,
                    mixed_fps,
                    (unsigned long long)interval_clipped, clip_rate,
                    rt->sound->peak_absolute,
                    (unsigned long long)audio->trace_queue_samples,
                    queue_avg,
                    audio->trace_queue_min,
                    audio->trace_queue_max,
                    audio->trace_empty_queue_checks);
            audio->trace_frames = audio->trace_pump_ticks_total = audio->trace_pump_ticks_max = 0;
            audio->trace_queue_frames_total = audio->trace_queue_samples = 0;
            audio->trace_queue_min = audio->trace_queue_max = 0;
            audio->trace_pumps = audio->trace_empty_queue_checks = 0;
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
        audio->trace_pumps++;
        uint32_t queued_frames = bytes / 4;
        audio->trace_queue_frames_total += queued_frames;
        audio->trace_queue_samples++;
        if (audio->trace_queue_samples == 1 || queued_frames < audio->trace_queue_min)
            audio->trace_queue_min = queued_frames;
        if (queued_frames > audio->trace_queue_max) audio->trace_queue_max = queued_frames;
        if (!queued_frames) audio->trace_empty_queue_checks++;
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
    audio->last_pump_ticks = elapsed;
    audio->last_pump_us = frequency ? elapsed / frequency * UINT64_C(1000000) +
        elapsed % frequency * UINT64_C(1000000) / frequency : 0;
    if (trace) {
        audio->trace_pump_ticks_total += elapsed;
        if (elapsed > audio->trace_pump_ticks_max)
            audio->trace_pump_ticks_max = elapsed;
    }
    uint64_t submitted = audio->submitted_frames - submitted_before;
    audio->last_pump_frames = submitted > UINT32_MAX ? UINT32_MAX : (uint32_t)submitted;
    if (trace) audio->trace_frames += submitted;
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
