#ifndef RUNTIME_PERF_H
#define RUNTIME_PERF_H

#include <stdint.h>

typedef struct {
    uint64_t frequency, interval_start, previous;
    uint32_t frame_us[256];
    unsigned samples;
    uint64_t frames;
} RuntimePerf;

void runtime_perf_init(RuntimePerf *perf, uint64_t now, uint64_t frequency);
void runtime_perf_present(RuntimePerf *perf, uint64_t now);

#endif
