#ifndef RUNTIME_AUDIO_H
#define RUNTIME_AUDIO_H
#include "mac_sound.h"
#include <stdatomic.h>

#define RUNTIME_AUDIO_RING_FRAMES 8192u

typedef struct {
    uint32_t device;
    int owns_audio;
    int started;
    int16_t ring[RUNTIME_AUDIO_RING_FRAMES * 2];
    _Atomic uint64_t ring_read_frames;
    _Atomic uint64_t ring_write_frames;
    _Atomic uint64_t callback_frames;
    _Atomic uint64_t callback_underrun_frames;
    uint64_t submitted_frames;
    uint64_t last_trace_ticks;
    uint64_t last_pump_us;
    uint64_t last_pump_ticks;
    uint32_t last_pump_frames;
    uint64_t trace_frames, trace_pump_ticks_total, trace_pump_ticks_max;
    uint64_t trace_callback_frames, trace_underrun_frames;
    uint64_t trace_rendered_frames, trace_clipped_frames;
    uint32_t trace_pumps, trace_ring_min;
} RuntimeAudio;
int runtime_audio_open(RuntimeAudio *audio);
int runtime_audio_pump(RuntimeAudio *audio, MacRuntime *rt,
                       size_t callback_budget, size_t *callback_instructions);
void runtime_audio_close(RuntimeAudio *audio);
#endif
