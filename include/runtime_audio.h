#ifndef RUNTIME_AUDIO_H
#define RUNTIME_AUDIO_H
#include "mac_sound.h"
typedef struct {
    uint32_t device;
    int owns_audio;
    uint64_t submitted_frames;
    uint64_t last_trace_ticks;
    uint64_t last_pump_us;
    uint32_t last_pump_frames;
} RuntimeAudio;
int runtime_audio_open(RuntimeAudio *audio);
int runtime_audio_pump(RuntimeAudio *audio, MacRuntime *rt,
                       size_t callback_budget, size_t *callback_instructions);
void runtime_audio_close(RuntimeAudio *audio);
#endif
