/* Switch sound synthesis.
 *
 * Every keystroke is modelled as the events that make the real noise: the
 * leaf or click jacket snapping (filtered noise burst), the stem hitting
 * bottom a few milliseconds later (keycap and case resonances as damped
 * modes), and on release the stem slamming into the top housing, which is
 * where a heavy desk board gets its signature clack. Wide keys add the
 * stabilizer wire and a lower, longer case boom. Each family has its own
 * travel time, resonances and damping:
 *
 *   DOME    rubber dome boards: muffled thup, soft return
 *   ALPS    heavy desk boards with complicated sliders: tactile tick,
 *           deep bottom-out, sharp top-out clack
 *   MITSUMI the first desk keyboard: bright click, hollow case, spring ping
 *   SCISSOR portables: short travel, thin chassis, crisp and quiet
 *
 * Four seeded variants per sound vary pitch, level and timing; the mixer
 * adds per-press pitch and stereo position from the key's place on screen.
 */
#include "kbd_audio.h"
#include "kbd_skin.h"
#include "kbd.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { EV_END, EV_NOISE_BP, EV_NOISE_LP, EV_MODE };
typedef struct { float t_ms; int type; float f, q, tau_ms, amp; } Ev;

#define BP(t, f, q, tau, a) { t, EV_NOISE_BP, f, q, tau, a }
#define LP(t, f, tau, a) { t, EV_NOISE_LP, f, 0.7f, tau, a }
#define MO(t, f, tau, a) { t, EV_MODE, f, 0, tau, a }
#define END { 0, EV_END, 0, 0, 0, 0 }

static const Ev dome_press[] = { LP(0, 900, 3.0f, 0.42f), MO(4.5f, 340, 11, 0.36f), MO(4.6f, 690, 7, 0.2f),
                                 MO(4.6f, 1380, 3, 0.12f), BP(4.5f, 1500, 0.9f, 1.2f, 0.18f), END };
static const Ev dome_release[] = { LP(0, 700, 4.0f, 0.16f), MO(7, 520, 6, 0.1f), BP(7, 1900, 1.2f, 0.8f, 0.08f), END };
static const Ev dome_wide[] = { LP(0, 800, 4.0f, 0.4f), MO(6, 230, 18, 0.42f), MO(6, 470, 12, 0.3f),
                                MO(9.5f, 1150, 4, 0.12f), BP(9.5f, 2600, 2.0f, 1.0f, 0.14f), BP(6, 1200, 0.8f, 1.6f, 0.2f), END };
static const Ev dome_wide_rel[] = { LP(0, 650, 5.0f, 0.18f), MO(8, 300, 10, 0.14f), BP(11, 2400, 1.5f, 1.0f, 0.1f), END };

static const Ev alps_press[] = { BP(0, 3500, 2.0f, 0.6f, 0.24f), MO(0.3f, 3100, 1.5f, 0.06f),
                                 BP(7, 1200, 0.8f, 2.0f, 0.5f), MO(7, 1900, 5, 0.34f), MO(7, 420, 22, 0.5f),
                                 MO(7, 760, 14, 0.34f), MO(7.2f, 1250, 9, 0.24f), MO(7, 140, 18, 0.28f), END };
static const Ev alps_release[] = { BP(0, 2200, 1.5f, 0.5f, 0.08f), BP(9, 2600, 1.6f, 1.2f, 0.45f),
                                   MO(9, 2200, 4, 0.3f), MO(9, 900, 8, 0.2f), MO(9, 450, 14, 0.18f), END };
static const Ev alps_wide[] = { BP(0, 3300, 2.0f, 0.6f, 0.2f), BP(10, 1000, 0.8f, 2.4f, 0.5f), MO(10, 300, 30, 0.55f),
                                MO(10, 610, 18, 0.36f), MO(10, 1700, 6, 0.26f), MO(10, 120, 24, 0.3f),
                                BP(14, 4500, 3.0f, 2.0f, 0.12f), MO(14, 4400, 6, 0.05f), END };
static const Ev alps_wide_rel[] = { BP(11, 2300, 1.5f, 1.4f, 0.4f), MO(11, 380, 18, 0.24f), MO(11, 1900, 4, 0.2f),
                                    BP(15, 4600, 3.0f, 1.6f, 0.1f), END };

static const Ev mit_press[] = { BP(1.5f, 4500, 3.0f, 0.5f, 0.85f), MO(1.5f, 4100, 3, 0.4f),
                                BP(6, 1500, 0.9f, 1.5f, 0.35f), MO(6, 600, 26, 0.45f), MO(6, 1100, 12, 0.3f),
                                MO(6, 2300, 5, 0.16f), MO(1.5f, 5300, 25, 0.035f), END };
static const Ev mit_release[] = { BP(0.5f, 4200, 3.0f, 0.4f, 0.3f), MO(0.5f, 3900, 2.5f, 0.14f),
                                  MO(5, 640, 16, 0.2f), MO(0.5f, 5300, 28, 0.03f), END };
static const Ev mit_wide[] = { BP(1.5f, 4300, 3.0f, 0.6f, 0.5f), BP(8, 1300, 0.8f, 2.0f, 0.4f), MO(8, 420, 34, 0.5f),
                               MO(8, 900, 18, 0.3f), BP(11, 5000, 3.0f, 1.8f, 0.14f), MO(1.5f, 5200, 30, 0.04f), END };
static const Ev mit_wide_rel[] = { BP(0.5f, 4000, 3.0f, 0.5f, 0.28f), MO(6, 460, 20, 0.22f),
                                   BP(9, 5000, 3.0f, 1.4f, 0.1f), END };

static const Ev sci_press[] = { BP(0, 2800, 1.8f, 0.8f, 0.34f), MO(0.2f, 1600, 3, 0.24f),
                                MO(2.5f, 520, 6, 0.24f), BP(2.5f, 1100, 1.0f, 1.0f, 0.16f), END };
static const Ev sci_release[] = { BP(0, 3200, 2.0f, 0.6f, 0.14f), MO(2, 1800, 2.5f, 0.08f), END };
static const Ev sci_wide[] = { BP(0, 2500, 1.6f, 1.0f, 0.34f), MO(3, 380, 9, 0.3f), MO(3, 1300, 4, 0.2f),
                               BP(5, 3800, 2.5f, 1.2f, 0.1f), END };
static const Ev sci_wide_rel[] = { BP(0, 3000, 2.0f, 0.8f, 0.16f), MO(3, 420, 6, 0.1f), END };

static const Ev pad_click[] = { BP(0, 1800, 1.0f, 1.5f, 0.5f), MO(0, 380, 15, 0.42f), MO(0, 900, 9, 0.3f),
                                MO(0.2f, 2400, 3, 0.16f), MO(0, 170, 12, 0.18f), END };
static const Ev pad_release[] = { BP(0, 2100, 1.2f, 1.0f, 0.26f), MO(0, 450, 9, 0.18f), MO(0, 1100, 5, 0.12f), END };

static const Ev *recipe(int family, int sound) {
    static const Ev *const tab[KBD_SW_COUNT][4] = {
        { dome_press, dome_release, dome_wide, dome_wide_rel },
        { alps_press, alps_release, alps_wide, alps_wide_rel },
        { mit_press, mit_release, mit_wide, mit_wide_rel },
        { sci_press, sci_release, sci_wide, sci_wide_rel },
    };
    if (sound == KBD_SND_PAD_CLICK) return pad_click;
    if (sound == KBD_SND_PAD_RELEASE) return pad_release;
    if (family < 0 || family >= KBD_SW_COUNT || sound < 0 || sound > 3) return NULL;
    return tab[family][sound];
}

static float peak_target(int sound) {
    static const float t[] = { 0.5f, 0.3f, 0.62f, 0.36f, 0.55f, 0.32f };
    return sound >= 0 && sound < KBD_SND_COUNT ? t[sound] : 0.4f;
}

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } Biquad;

static void bq_bandpass(Biquad *b, float f, float q) {
    float w = 2 * 3.14159265f * f / KBD_SYNTH_RATE, al = sinf(w) / (2 * q), a0 = 1 + al;
    *b = (Biquad){ al / a0, 0, -al / a0, -2 * cosf(w) / a0, (1 - al) / a0, 0, 0 };
}

static void bq_lowpass(Biquad *b, float f, float q) {
    float w = 2 * 3.14159265f * f / KBD_SYNTH_RATE, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
    *b = (Biquad){ (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - al) / a0, 0, 0 };
}

static float bq(Biquad *b, float x) {       /* transposed direct form II */
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

static uint32_t rnd(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static float frnd(uint32_t *s) { return (float)(rnd(s) >> 8) / 16777216.0f; }

int kbd_synth_render(int family, int sound, int variant, float *out, int cap) {
    const Ev *ev = recipe(family, sound);
    if (!ev || !out || cap <= 0) return 0;
    memset(out, 0, sizeof(float) * (size_t)cap);
    uint32_t seed = 0x51ED270Bu ^ (uint32_t)(family * 7919 + sound * 104729 + variant * 1299709);
    if (!seed) seed = 1;
    float fscale = 1 + (frnd(&seed) - 0.5f) * 0.08f;
    for (const Ev *e = ev; e->type != EV_END; e++) {
        float jitter = e->t_ms > 0 ? (frnd(&seed) - 0.5f) * 1.4f : 0;
        float t0 = fmaxf(0, e->t_ms + jitter) * KBD_SYNTH_RATE / 1000.0f;
        float amp = e->amp * (1 + (frnd(&seed) - 0.5f) * 0.3f);
        float f = e->f * fscale * (1 + (frnd(&seed) - 0.5f) * 0.05f);
        float tau = e->tau_ms * KBD_SYNTH_RATE / 1000.0f;
        int len = (int)(tau * 7);
        Biquad b;
        if (e->type == EV_NOISE_BP) bq_bandpass(&b, f, e->q);
        else bq_lowpass(&b, f, e->q);
        float phase = frnd(&seed) * 6.2831853f, w = 2 * 3.14159265f * f / KBD_SYNTH_RATE;
        float gain = e->type == EV_MODE ? 1 : (e->type == EV_NOISE_BP ? 3.0f : 1.6f);
        for (int i = 0; i < len + 64; i++) {
            int at = (int)t0 + i;
            if (at >= cap) break;
            float env = expf(-(float)i / tau);
            float attack = i < 12 ? (float)i / 12.0f : 1;   /* no clicks from a hard edge */
            float x;
            if (e->type == EV_MODE) x = sinf(phase + w * (float)i) * env * attack;
            else x = bq(&b, (frnd(&seed) * 2 - 1) * env * attack);
            out[at] += x * amp * gain;
        }
    }
    /* A touch of room: two early reflections off a desk and a wall. */
    int d1 = KBD_SYNTH_RATE * 43 / 10000, d2 = KBD_SYNTH_RATE * 79 / 10000;
    for (int i = cap - 1; i >= d1; i--) out[i] += out[i - d1] * 0.1f + (i >= d2 ? out[i - d2] * 0.06f : 0);
    float peak = 0;
    for (int i = 0; i < cap; i++) peak = fmaxf(peak, fabsf(out[i]));
    if (peak > 0) {
        float g = peak_target(sound) / peak;
        for (int i = 0; i < cap; i++) out[i] *= g;
    }
    int n = cap;
    while (n > 1 && fabsf(out[n - 1]) < 1.5e-4f) n--;
    for (int i = 0; i < 96 && n - 1 - i >= 0; i++) out[n - 1 - i] *= (float)i / 96.0f;  /* fade tail */
    return n;
}

struct KbdSoundBank { KbdSample s[KBD_SW_COUNT][KBD_SND_COUNT][KBD_SYNTH_VARIANTS]; };

KbdSoundBank *kbd_bank_create(void) {
    KbdSoundBank *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    int cap = KBD_SYNTH_RATE / 4;
    float *tmp = malloc(sizeof(float) * (size_t)cap);
    if (!tmp) { free(b); return NULL; }
    for (int f = 0; f < KBD_SW_COUNT; f++)
        for (int s = 0; s < KBD_SND_COUNT; s++)
            for (int v = 0; v < KBD_SYNTH_VARIANTS; v++) {
                int n = kbd_synth_render(f, s, v, tmp, cap);
                KbdSample *o = &b->s[f][s][v];
                o->data = malloc(sizeof(float) * (size_t)(n > 0 ? n : 1));
                if (o->data && n > 0) { memcpy(o->data, tmp, sizeof(float) * (size_t)n); o->frames = n; }
            }
    free(tmp);
    return b;
}

void kbd_bank_destroy(KbdSoundBank *b) {
    if (!b) return;
    for (int f = 0; f < KBD_SW_COUNT; f++)
        for (int s = 0; s < KBD_SND_COUNT; s++)
            for (int v = 0; v < KBD_SYNTH_VARIANTS; v++) free(b->s[f][s][v].data);
    free(b);
}

const KbdSample *kbd_bank_get(const KbdSoundBank *b, int family, int sound, int variant) {
    if (!b || family < 0 || family >= KBD_SW_COUNT || sound < 0 || sound >= KBD_SND_COUNT) return NULL;
    return &b->s[family][sound][((variant % KBD_SYNTH_VARIANTS) + KBD_SYNTH_VARIANTS) % KBD_SYNTH_VARIANTS];
}
