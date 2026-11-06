#ifndef MAC_SOUND_DOUBLE_H
#define MAC_SOUND_DOUBLE_H
#include "mac_runtime.h"
typedef struct {
    uint32_t buffers[2], callback, rate, frames;
    uint64_t phase;
    unsigned current, pending;
    int active, loaded, starved;
} MacSoundDouble;
int mac_sound_double_service(MacRuntime *rt, uint16_t opcode);
/* Called only by the serialized CPU-side PCM producer, never audio thread.
 * 1 owns channel (value may be silence while awaiting ready); 0 inactive. */
int mac_sound_double_sample(MacRuntime *rt, unsigned channel, int *value);
size_t mac_sound_double_poll(MacRuntime *rt, size_t budget);
void mac_sound_double_stop(MacRuntime *rt, unsigned channel);
#endif
