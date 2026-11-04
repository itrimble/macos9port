#include "mac_sound_int.h"
/* Independent bounded sample-rate conversion. initNoDrop is defined by
 * Inside Macintosh Sound-65: avoid dropping samples to fake conversion.
 * Area integration includes every source sample crossed by an output frame.
 * initNoInterp integrates held samples; otherwise it integrates linear ramps.
 * This is a reference filter, not a claim of Apple's exact reconstruction. */
#define FRAME_UNIT ((uint64_t)MAC_SOUND_RATE << 16)
static uint32_t boundary(const MacSoundChannel *c) {
    return c->looping ? c->loop_end : c->length;
}
static int loops(const MacSoundChannel *c) {
    return c->note_playback && (c->loop_start || c->loop_end);
}
static void advance(MacSoundChannel *c, uint64_t amount) {
    uint64_t end = (uint64_t)boundary(c) * FRAME_UNIT;
    c->phase += amount;
    if (c->phase < end) return;
    if (loops(c)) {
        uint64_t span = (uint64_t)(c->loop_end - c->loop_start) * FRAME_UNIT;
        c->phase = (uint64_t)c->loop_start * FRAME_UNIT + (c->phase - end) % span;
        c->looping = 1;
    } else {
        c->playing = 0;
        c->buffer_block = 0;
    }
}
static int value(const uint8_t *ram, const MacSoundChannel *c, uint32_t at) {
    return ((int)ram[c->data + at] - 128) * 256;
}
static int next_value(const uint8_t *ram, const MacSoundChannel *c, uint32_t at) {
    at++;
    if (at >= boundary(c)) at = loops(c) ? c->loop_start : at - 1;
    return value(ram, c, at);
}
int mac_sound_sample(MacRuntime *r, MacSoundChannel *c) {
    const uint8_t *ram = mac_cpu_ram(r->cpu);
    if (c->rate <= FRAME_UNIT || !(c->init & 8)) {
        uint32_t at = (uint32_t)(c->phase / FRAME_UNIT);
        int sample = value(ram, c, at);
        if (!(c->init & 4)) {
            int next = next_value(ram, c, at);
            sample += (int)((int64_t)(next - sample) * (int64_t)(c->phase % FRAME_UNIT) / (int64_t)FRAME_UNIT);
        }
        advance(c, c->rate);
        return sample;
    }
    uint64_t remaining = c->rate;
    int64_t sum = 0;
    for (unsigned work = 0; remaining && c->playing && work < 17; work++) {
        uint32_t at = (uint32_t)(c->phase / FRAME_UNIT);
        uint64_t fraction = c->phase % FRAME_UNIT;
        uint64_t take = FRAME_UNIT - fraction;
        if (take > remaining) take = remaining;
        int first = value(ram, c, at), last = first;
        if (!(c->init & 4)) {
            int delta = next_value(ram, c, at) - first;
            last = first + (int)((int64_t)delta * (int64_t)(fraction + take) / (int64_t)FRAME_UNIT);
            first += (int)((int64_t)delta * (int64_t)fraction / (int64_t)FRAME_UNIT);
        }
        sum += (int64_t)(first + last) * (int64_t)take;
        advance(c, take);
        remaining -= take;
    }
    if (remaining && c->playing) {
        mac_sound_fail(r, "Sample-rate integration exceeded its 17-cell work bound.");
        return 0;
    }
    return (int)(sum / (int64_t)(2 * c->rate));
}
