#include "runtime_perf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static int cmp_u32(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    return (va > vb) - (va < vb);
}

typedef struct {
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t max_us;
} FramePacingStats;

static int evaluate_frame_pacing_gate(const uint32_t *samples, size_t n,
                                      FramePacingStats *out) {
    if (!samples || n == 0 || n > 256 || !out) return -1;
    uint32_t sorted[256];
    memcpy(sorted, samples, n * sizeof(uint32_t));
    qsort(sorted, n, sizeof(uint32_t), cmp_u32);

    out->p50_us = sorted[(n * 50) / 100];
    out->p95_us = sorted[(n * 95) / 100];
    out->p99_us = sorted[(n * 99) / 100];
    out->max_us = sorted[n - 1];

    /* G05 Budget: p50 <= 16.7ms, p95 <= 25.0ms, p99 <= 33.3ms, max <= 50.0ms */
    if (out->p50_us > 16700u || out->p95_us > 25000u ||
        out->p99_us > 33333u || out->max_us > 50000u) {
        return -1;
    }
    return 0;
}

static void test_frame_pacing_budget_gate(void) {
    RuntimePerf perf;
    runtime_perf_init(&perf, 0, 1000000ull); /* 1 tick = 1 us */

    uint64_t t = 0;
    for (int i = 0; i < 50; i++) {
        t += 16600ull;
        runtime_perf_present(&perf, t);
    }
    CHECK(perf.samples == 50 && perf.frames == 50);

    FramePacingStats st = {0};
    CHECK(evaluate_frame_pacing_gate(perf.frame_us, perf.samples, &st) == 0);
    CHECK(st.p50_us == 16600u && st.max_us == 16600u);

    /* Inject hitch spikes exceeding p99 (33.3ms) and max (50ms) budgets. */
    t += 42000ull;
    runtime_perf_present(&perf, t);
    t += 65000ull;
    runtime_perf_present(&perf, t);
    CHECK(evaluate_frame_pacing_gate(perf.frame_us, perf.samples, &st) == -1);
    CHECK(st.max_us == 65000u);
}

enum {
    AUDIO_GATE_OK                 = 0,
    AUDIO_ERR_UNDERRUN_OR_GAP     = -1,
    AUDIO_ERR_DOUBLED_NOTE        = -2,
    AUDIO_ERR_PERSISTENT_TICKING  = -3
};

typedef struct {
    uint32_t time_ms;
    uint8_t voice;
    uint8_t pitch;
    uint8_t note_on;
} NoteTraceEvent;

typedef struct {
    int16_t peak_abs;
    int16_t mean_abs;
} PcmWindowMetric;

static int evaluate_r05_audio_gate(const NoteTraceEvent *notes, size_t n_notes,
                                   const PcmWindowMetric *wins, size_t n_wins,
                                   uint32_t underrun_frames, uint32_t startup_gap_ms) {
    if (underrun_frames > 0 || startup_gap_ms > 250u) {
        return AUDIO_ERR_UNDERRUN_OR_GAP;
    }

    /* Check for doubled notes: same voice & pitch re-triggered within < 50ms while active. */
    int active_pitch[8];
    uint32_t last_on_ms[8];
    for (int v = 0; v < 8; v++) { active_pitch[v] = -1; last_on_ms[v] = 0; }

    for (size_t i = 0; i < n_notes; i++) {
        uint8_t v = notes[i].voice & 7u;
        if (notes[i].note_on) {
            if (active_pitch[v] == (int)notes[i].pitch &&
                notes[i].time_ms >= last_on_ms[v] &&
                notes[i].time_ms - last_on_ms[v] < 50u) {
                return AUDIO_ERR_DOUBLED_NOTE;
            }
            active_pitch[v] = notes[i].pitch;
            last_on_ms[v] = notes[i].time_ms;
        } else if (active_pitch[v] == (int)notes[i].pitch) {
            active_pitch[v] = -1;
        }
    }

    /* Check for persistent ticking: >= 4 consecutive windows with impulse peak >= 3000
       but near-zero sustained tonal body (mean_abs < 40). */
    int tick_streak = 0;
    for (size_t w = 0; w < n_wins; w++) {
        if (wins[w].peak_abs >= 3000 && wins[w].mean_abs < 40) {
            tick_streak++;
            if (tick_streak >= 4) return AUDIO_ERR_PERSISTENT_TICKING;
        } else {
            tick_streak = 0;
        }
    }
    return AUDIO_GATE_OK;
}

static void test_r05_audio_correlation_gates(void) {
    const NoteTraceEvent clean_notes[] = {
        {   0, 0, 60, 1 }, { 200, 0, 60, 0 },
        { 220, 0, 64, 1 }, { 420, 0, 64, 0 },
        { 440, 0, 67, 1 }, { 640, 0, 67, 0 }
    };
    const PcmWindowMetric clean_wins[] = {
        { 8000, 2400 }, { 8200, 2500 }, { 7900, 2300 }, { 8100, 2450 }
    };
    CHECK(evaluate_r05_audio_gate(clean_notes, 6, clean_wins, 4, 0, 80) == AUDIO_GATE_OK);

    /* 1. Startup gap > 250ms or nonzero underrun frames must fail. */
    CHECK(evaluate_r05_audio_gate(clean_notes, 6, clean_wins, 4, 128, 80) == AUDIO_ERR_UNDERRUN_OR_GAP);
    CHECK(evaluate_r05_audio_gate(clean_notes, 6, clean_wins, 4, 0, 450) == AUDIO_ERR_UNDERRUN_OR_GAP);

    /* 2. Doubled-note anomaly (same pitch reissued 18ms later without note-off) must fail. */
    const NoteTraceEvent doubled_notes[] = {
        { 100, 0, 60, 1 },
        { 118, 0, 60, 1 }, /* duplicate trigger 18ms later */
        { 300, 0, 60, 0 }
    };
    CHECK(evaluate_r05_audio_gate(doubled_notes, 3, clean_wins, 4, 0, 40) == AUDIO_ERR_DOUBLED_NOTE);

    /* 3. Persistent ticking (impulse clicks with no sustained music body) must fail. */
    const PcmWindowMetric ticking_wins[] = {
        { 4500, 12 }, { 4800, 10 }, { 4600, 11 }, { 5100, 9 }
    };
    CHECK(evaluate_r05_audio_gate(clean_notes, 6, ticking_wins, 4, 0, 40) == AUDIO_ERR_PERSISTENT_TICKING);
}

int main(void) {
    test_frame_pacing_budget_gate();
    test_r05_audio_correlation_gates();

    if (failures > 0) {
        fprintf(stderr, "test_mac_perf_audio_gates: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_perf_audio_gates: all %d checks passed\n", checks);
    return 0;
}
