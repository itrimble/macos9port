#ifndef RUNTIME_AUDIO_H
#define RUNTIME_AUDIO_H
#include "mac_sound.h"
typedef struct {
    uint32_t device;
    int owns_audio;
    uint64_t submitted_frames;
    uint64_t last_trace_ticks;
    uint64_t last_pump_us;
    uint64_t last_pump_ticks;
    uint32_t last_pump_frames;
    uint64_t trace_frames, trace_pump_ticks_total, trace_pump_ticks_max;
    uint64_t trace_queue_frames_total, trace_queue_samples;
    uint64_t trace_rendered_frames, trace_clipped_frames;
    uint32_t trace_pumps, trace_empty_queue_checks;
    uint32_t trace_queue_min, trace_queue_max;
} RuntimeAudio;
int runtime_audio_open(RuntimeAudio *audio);
int runtime_audio_pump(RuntimeAudio *audio, MacRuntime *rt,
                       size_t callback_budget, size_t *callback_instructions);
void runtime_audio_close(RuntimeAudio *audio);
#endif
