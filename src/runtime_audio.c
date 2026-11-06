#include "runtime_audio.h"
#include "mac_sound_int.h"
#include "mac_sound_double.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The guest worker produces PCM; SDL's real-time callback only copies it from
 * this bounded single-producer/single-consumer ring. It never runs guest code. */
#define AUDIO_TARGET_FRAMES 4096u
#define AUDIO_CHUNK_FRAMES 64u
#define AUDIO_CAPTURE_SECONDS 300u

static void store_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_capture(RuntimeAudio *audio) {
    if (!audio->capture_pcm || !audio->capture_path[0]) return;
    uint64_t data_size64 = audio->capture_frames * 4;
    if (data_size64 > UINT32_MAX - 36) {
        fprintf(stderr, "PCM capture is too large for a WAV file.\n");
        return;
    }
    FILE *file = fopen(audio->capture_path, "wb");
    if (!file) {
        fprintf(stderr, "Cannot write PCM capture %s.\n", audio->capture_path);
        return;
    }
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    store_le32(header + 4, 36 + (uint32_t)data_size64);
    memcpy(header + 8, "WAVEfmt ", 8);
    store_le32(header + 16, 16);
    store_le16(header + 20, 1);
    store_le16(header + 22, 2);
    store_le32(header + 24, MAC_SOUND_RATE);
    store_le32(header + 28, MAC_SOUND_RATE * 4);
    store_le16(header + 32, 4);
    store_le16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    store_le32(header + 40, (uint32_t)data_size64);
    int ok = fwrite(header, sizeof header, 1, file) == 1;
    uint8_t samples[8192];
    uint64_t offset = 0;
    while (ok && offset < audio->capture_frames * 2) {
        size_t count = (size_t)(audio->capture_frames * 2 - offset);
        if (count > sizeof samples / 2) count = sizeof samples / 2;
        for (size_t i = 0; i < count; i++)
            store_le16(samples + i * 2, (uint16_t)audio->capture_pcm[offset + i]);
        ok = fwrite(samples, 2, count, file) == count;
        offset += count;
    }
    if (fclose(file)) ok = 0;
    if (!ok) fprintf(stderr, "PCM capture write failed: %s\n", audio->capture_path);
    else fprintf(stderr, "PCM producer capture: %s (%llu frames, %llu seconds max)\n",
                 audio->capture_path, (unsigned long long)audio->capture_frames,
                 (unsigned long long)AUDIO_CAPTURE_SECONDS);
}

static void audio_callback(void *context, Uint8 *stream, int length) {
    RuntimeAudio *audio = context;
    if (!audio || length <= 0) return;
    memset(stream, 0, (size_t)length);
    size_t requested = (size_t)length / (sizeof(int16_t) * 2);
    uint64_t read_at = atomic_load_explicit(&audio->ring_read_frames, memory_order_relaxed);
    uint64_t write_at = atomic_load_explicit(&audio->ring_write_frames, memory_order_acquire);
    uint64_t available = write_at - read_at;
    size_t copied = available < requested ? (size_t)available : requested;
    int16_t *output = (int16_t *)stream;
    for (size_t i = 0; i < copied; i++) {
        size_t frame = (size_t)((read_at + i) & (RUNTIME_AUDIO_RING_FRAMES - 1));
        output[i * 2] = audio->ring[frame * 2];
        output[i * 2 + 1] = audio->ring[frame * 2 + 1];
    }
    atomic_store_explicit(&audio->ring_read_frames, read_at + copied, memory_order_release);
    atomic_fetch_add_explicit(&audio->callback_frames, requested, memory_order_relaxed);
    if (copied < requested)
        atomic_fetch_add_explicit(&audio->callback_underrun_frames,
                                  requested - copied, memory_order_relaxed);
}

static uint64_t audio_ring_queued(const RuntimeAudio *audio) {
    uint64_t write_at = atomic_load_explicit(&audio->ring_write_frames, memory_order_acquire);
    uint64_t read_at = atomic_load_explicit(&audio->ring_read_frames, memory_order_acquire);
    uint64_t queued = write_at - read_at;
    return queued > RUNTIME_AUDIO_RING_FRAMES ? RUNTIME_AUDIO_RING_FRAMES : queued;
}

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
    wanted.callback = audio_callback;
    wanted.userdata = audio;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (!audio->device) { runtime_audio_close(audio); return -1; }
    if (obtained.freq != wanted.freq || obtained.format != wanted.format ||
        obtained.channels != wanted.channels) {
        runtime_audio_close(audio);
        SDL_SetError("Audio device did not provide the requested PCM format");
        return -1;
    }
    atomic_init(&audio->ring_read_frames, 0);
    atomic_init(&audio->ring_write_frames, 0);
    atomic_init(&audio->callback_frames, 0);
    atomic_init(&audio->callback_underrun_frames, 0);
    const char *capture_path = getenv("OSPI_AUDIO_WAV");
    if (capture_path && *capture_path) {
        size_t path_size = strlen(capture_path);
        audio->capture_capacity_frames = (uint64_t)MAC_SOUND_RATE * AUDIO_CAPTURE_SECONDS;
        if (path_size >= sizeof audio->capture_path) {
            fprintf(stderr, "PCM capture path is too long; capture disabled.\n");
            audio->capture_capacity_frames = 0;
        } else {
            audio->capture_pcm = malloc((size_t)audio->capture_capacity_frames * 2 * sizeof(int16_t));
            if (!audio->capture_pcm) {
                fprintf(stderr, "PCM capture allocation failed; capture disabled.\n");
                audio->capture_capacity_frames = 0;
            } else {
                memcpy(audio->capture_path, capture_path, path_size + 1);
                fprintf(stderr, "PCM producer capture enabled (up to %u seconds): %s\n",
                        AUDIO_CAPTURE_SECONDS, audio->capture_path);
            }
        }
    }
    fprintf(stderr, "Audio output: driver=%s rate=%dHz channels=%u buffer=%u frames callback=ring\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "unknown",
            obtained.freq, obtained.channels, obtained.samples);
    /* Start only after the guest worker has primed the ring. */
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
    uint64_t queued_frames = audio_ring_queued(audio);
    unsigned available = queued_frames < AUDIO_TARGET_FRAMES ?
        AUDIO_TARGET_FRAMES - (unsigned)queued_frames : 0;
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
            uint64_t callback_frames = atomic_load_explicit(
                &audio->callback_frames, memory_order_relaxed);
            uint64_t underrun_frames = atomic_load_explicit(
                &audio->callback_underrun_frames, memory_order_relaxed);
            uint64_t consumed = callback_frames - audio->trace_callback_frames;
            uint64_t underruns = underrun_frames - audio->trace_underrun_frames;
            uint64_t rendered = rt->sound->rendered_frames;
            uint64_t clipped = rt->sound->clipped_frames;
            uint64_t interval_rendered = rendered - audio->trace_rendered_frames;
            uint64_t interval_clipped = clipped - audio->trace_clipped_frames;
            double clip_rate = interval_rendered ?
                (double)interval_clipped * 100.0 / interval_rendered : 0.0;
            audio->trace_callback_frames = callback_frames;
            audio->trace_underrun_frames = underrun_frames;
            audio->trace_rendered_frames = rendered;
            audio->trace_clipped_frames = clipped;
            queued_frames = audio_ring_queued(audio);
            uint64_t produced = audio->trace_frames;
            audio->trace_frames = 0;
            fprintf(stderr, "audio-trace ms=%llu ring-queued-frames=%llu submitted=%llu producer-fps=%.1f callback-frames=%llu callback-fps=%.1f underrun-frames=%llu ring-min-frames=%u clip-frames=%llu clip-rate-pct=%.3f peak-abs=%u callback-budget=%zu last-pump-ticks=%llu last-pump-frames=%u pump-count=%u pump-avg-us=%.3f pump-max-us=%.3f\n",
                    (unsigned long long)now,
                    (unsigned long long)queued_frames,
                    (unsigned long long)audio->submitted_frames,
                    interval_ms ? (double)produced * 1000.0 / interval_ms : 0.0,
                    (unsigned long long)consumed,
                    interval_ms ? (double)consumed * 1000.0 / interval_ms : 0.0,
                    (unsigned long long)underruns,
                    audio->trace_ring_min,
                    (unsigned long long)interval_clipped, clip_rate,
                    rt->sound->peak_absolute,
                    callback_budget,
                    (unsigned long long)audio->last_pump_ticks,
                    audio->last_pump_frames, audio->trace_pumps,
                    avg_us, max_us);
            for (unsigned i = 0; i < MAC_SOUND_CHANNELS; i++) {
                const MacSoundChannel *c = &rt->sound->channels[i];
                if (!c->address) continue;
                fprintf(stderr, "  ch%u playing=%d paused=%d count=%u callback=%d wait=%u buffer=%d double=%d/%u voice=%d phase=%llu rate=%llu\n",
                        i, c->playing, c->paused, c->count, c->callback_pending,
                        c->wait_frames, c->buffer_block, c->double_buffer.active,
                        c->double_buffer.pending, c->voice,
                        (unsigned long long)c->phase, (unsigned long long)c->rate);
            }
            audio->trace_pumps = audio->trace_pump_ticks_total =
                audio->trace_pump_ticks_max = 0;
            audio->trace_ring_min = (uint32_t)RUNTIME_AUDIO_RING_FRAMES;
        }
        audio->trace_pumps++;
        if (queued_frames < audio->trace_ring_min)
            audio->trace_ring_min = (uint32_t)queued_frames;
    }

    int16_t pcm[AUDIO_CHUNK_FRAMES * 2];
    for (unsigned work = 0; available &&
         work < AUDIO_TARGET_FRAMES / AUDIO_CHUNK_FRAMES; work++) {
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
        uint64_t write_at = atomic_load_explicit(&audio->ring_write_frames,
                                                 memory_order_relaxed);
        for (unsigned i = 0; i < frames; i++) {
            size_t frame = (size_t)((write_at + i) & (RUNTIME_AUDIO_RING_FRAMES - 1));
            audio->ring[frame * 2] = pcm[i * 2];
            audio->ring[frame * 2 + 1] = pcm[i * 2 + 1];
        }
        atomic_store_explicit(&audio->ring_write_frames, write_at + frames,
                              memory_order_release);
        if (audio->capture_pcm && audio->capture_frames < audio->capture_capacity_frames) {
            uint64_t captured = audio->capture_capacity_frames - audio->capture_frames;
            if (captured > frames) captured = frames;
            memcpy(audio->capture_pcm + audio->capture_frames * 2, pcm,
                   (size_t)captured * 2 * sizeof(int16_t));
            audio->capture_frames += captured;
        }
        audio->submitted_frames += frames;
        available -= frames;
    }
    if (!audio->started && audio_ring_queued(audio) >= AUDIO_TARGET_FRAMES) {
        SDL_PauseAudioDevice(audio->device, 0);
        audio->started = 1;
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
        SDL_CloseAudioDevice(audio->device);
    }
    if (audio->owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    write_capture(audio);
    free(audio->capture_pcm);
    memset(audio, 0, sizeof *audio);
}
