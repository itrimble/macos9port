/* Bridge-mode frame pacing. There is no renderer or vsync in bridge mode, so
 * the presenter paces itself to a fixed 60 Hz schedule outside the runtime
 * mutex: each deadline is the previous one plus a frame period, so frame copy
 * time is not added to the period. After a stall longer than a frame the
 * schedule restarts from now instead of racing to catch up. Shared with the
 * headless presenter test through runtime_video.c. */
#ifndef RUNTIME_PACING_H
#define RUNTIME_PACING_H
#include <SDL.h>
#include <stdint.h>
#define BRIDGE_FRAME_US UINT64_C(16667)
static inline uint64_t bridge_next_deadline(uint64_t now_us, uint64_t deadline_us) {
    if (now_us > deadline_us + BRIDGE_FRAME_US) return now_us + BRIDGE_FRAME_US;
    return deadline_us + BRIDGE_FRAME_US;
}
static inline uint32_t bridge_frame_delay(uint64_t now_us, uint64_t deadline_us) {
    return deadline_us > now_us ? (uint32_t)((deadline_us - now_us) / 1000) : 0;
}
/* Host monotonic microseconds from SDL's performance counter; overflow-safe
 * for any realistic uptime because the whole seconds are scaled first. */
static inline uint64_t host_microseconds(void) {
    uint64_t counter = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();
    if (!frequency) return 0;
    return counter / frequency * 1000000u + counter % frequency * 1000000u / frequency;
}
#endif
