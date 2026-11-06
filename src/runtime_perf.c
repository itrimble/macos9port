#include "runtime_perf.h"

#include <stdio.h>
#include <stdlib.h>

void runtime_perf_init(RuntimePerf *perf, uint64_t now, uint64_t frequency) {
    *perf = (RuntimePerf){.frequency = frequency,
                          .interval_start = now,
                          .previous = now};
}

static int compare_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static uint32_t percentile(const uint32_t *values, unsigned count,
                           unsigned percentage) {
    unsigned rank = (count * percentage + 99) / 100;
    return values[rank ? rank - 1 : 0];
}

void runtime_perf_present(RuntimePerf *perf, uint64_t now) {
    if (!perf || !perf->frequency || now < perf->previous) return;
    uint64_t ticks = now - perf->previous;
    uint64_t whole = ticks / perf->frequency;
    uint64_t fraction = ticks % perf->frequency;
    uint64_t us = whole * UINT64_C(1000000) +
                  fraction * UINT64_C(1000000) / perf->frequency;
    if (us > UINT32_MAX) us = UINT32_MAX;
    if (perf->samples < sizeof perf->frame_us / sizeof perf->frame_us[0])
        perf->frame_us[perf->samples++] = (uint32_t)us;
    perf->frames++;
    perf->previous = now;
    if (now - perf->interval_start < perf->frequency) return;

    uint64_t elapsed = now - perf->interval_start;
    qsort(perf->frame_us, perf->samples, sizeof perf->frame_us[0], compare_u32);
    if (perf->samples) {
        unsigned n = perf->samples;
        fprintf(stderr,
                "present-trace fps=%.1f frames=%llu frame-ms-p50=%.3f "
                "p95=%.3f p99=%.3f max=%.3f samples=%u/%llu\n",
                elapsed ? (double)perf->frames * (double)perf->frequency /
                         (double)elapsed : 0.0,
                (unsigned long long)perf->frames,
                percentile(perf->frame_us, n, 50) / 1000.0,
                percentile(perf->frame_us, n, 95) / 1000.0,
                percentile(perf->frame_us, n, 99) / 1000.0,
                perf->frame_us[n - 1] / 1000.0, n,
                (unsigned long long)perf->frames);
    }
    perf->interval_start = now;
    perf->samples = 0;
    perf->frames = 0;
}
