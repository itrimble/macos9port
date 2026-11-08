/* Pointer motion and the haptic vocabulary.
 *
 * Touch panels report every contact with a pixel or so of noise, and the
 * report rate varies (60, 120, 240 Hz, sometimes two samples in one
 * millisecond). A fixed smoothing either wobbles at rest or lags in a flick,
 * so the pad uses a 1-euro filter: heavy smoothing when the finger is nearly
 * still, almost none when it moves fast. Velocity comes from real
 * timestamps, never an assumed frame rate, so the same gesture gives the
 * same pointer travel at any report rate.
 *
 * The acceleration curve is the shape a Mac trackpad has: slow motion is
 * precise (close to one-to-one in screen terms), fast motion throws the
 * pointer across the screen, with a smooth ramp between. It scales with the
 * pad-to-screen ratio so a thumb pad and the Flex pad both cover the screen
 * in about one and a half strokes at a brisk pace.
 */
#include "kbd_int.h"
#include <math.h>

enum { MIN_DT_MS = 4, MAX_DT_MS = 50 };

/* Cutoffs in Hz; beta per point/second of finger speed. */
#define EURO_MIN_CUTOFF 3.0f
#define EURO_BETA 0.05f
#define EURO_D_CUTOFF 1.0f

float kbd_motion_dt(uint32_t now, uint32_t last) {
    uint32_t d = now - last;
    if (last == 0 || d < MIN_DT_MS) d = MIN_DT_MS;
    if (d > MAX_DT_MS) d = MAX_DT_MS;
    return (float)d / 1000.0f;
}

static float alpha(float cutoff, float dt) {
    float tau = 1.0f / (2.0f * 3.14159265f * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

void kbd_euro_reset(KEuro *e, float x) {
    e->x = x;
    e->dx = 0;
    e->init = 1;
}

float kbd_euro(KEuro *e, float x, float dt, float dp) {
    if (!e->init) { kbd_euro_reset(e, x); return x; }
    float raw_dx = (x - e->x) / dt;
    e->dx += (raw_dx - e->dx) * alpha(EURO_D_CUTOFF, dt);
    float cutoff = EURO_MIN_CUTOFF + EURO_BETA * fabsf(e->dx) / (dp > 0 ? dp : 1);
    e->x += (x - e->x) * alpha(cutoff, dt);
    return e->x;
}

static float smoothstep(float a, float b, float v) {
    float t = (v - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

float kbd_pointer_gain(const Kbd *k, float speed_px, float pad_w) {
    float v = speed_px / (k->dp > 0 ? k->dp : 1);           /* points per second */
    float ratio = pad_w > 1 ? (float)k->sw / pad_w : 1;
    ratio = ratio < 1 ? 1 : ratio > 4.5f ? 4.5f : ratio;
    float track = k->set.tracking > 0 ? k->set.tracking : 1;
    float slow = (0.55f + 0.32f * ratio) * (0.8f + 0.2f * track);   /* precision end */
    float fast = (0.55f + 0.32f * ratio) * (1.0f + 1.6f * track);   /* throw end */
    return slow + (fast - slow) * smoothstep(30, 1100, v);
}

/* intensity, sharpness, duration (ms) */
static const KbdHapticSpec specs[KBD_HAPTIC_COUNT] = {
    [KBD_HAPTIC_KEY] = { 0.45f, 0.85f, 8 },
    [KBD_HAPTIC_KEY_WIDE] = { 0.6f, 0.6f, 12 },
    [KBD_HAPTIC_MODIFIER] = { 0.5f, 0.7f, 10 },
    [KBD_HAPTIC_SELECTION] = { 0.35f, 0.9f, 6 },
    [KBD_HAPTIC_TAP] = { 0.5f, 0.8f, 9 },
    [KBD_HAPTIC_CLICK] = { 0.85f, 0.75f, 14 },
    [KBD_HAPTIC_RELEASE] = { 0.55f, 0.9f, 9 },
    [KBD_HAPTIC_FORCE] = { 1.0f, 0.45f, 22 },
    [KBD_HAPTIC_MODE] = { 0.6f, 0.4f, 16 },
};

void kbd_haptic_spec(int type, KbdHapticSpec *out) {
    if (type < 0 || type >= KBD_HAPTIC_COUNT) *out = (KbdHapticSpec){ 0, 0, 0 };
    else *out = specs[type];
}

void kbd_emit_haptic(Kbd *k, int type) {
    if (!k->set.haptics || type < 0 || type >= KBD_HAPTIC_COUNT) return;
    float scale = k->set.haptic_strength;
    if (scale <= 0) return;
    KbdHapticSpec s = specs[type];
    float st = s.intensity * scale;
    KbdEvent e = { .kind = KBD_EV_HAPTIC, .haptic = type, .strength = st > 1 ? 1 : st,
                   .sharpness = s.sharpness, .duration_ms = s.duration_ms };
    kbd_emit(k, &e);
}
