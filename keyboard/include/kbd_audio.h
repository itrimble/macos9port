/* Switch sounds: physical-model synthesis plus a small polyphonic mixer.
 * Both are SDL-free; kbd_sdl.c drives the mixer from an audio callback. */
#ifndef OS999_KBD_AUDIO_H
#define OS999_KBD_AUDIO_H

#include <stdint.h>

enum { KBD_SYNTH_RATE = 48000, KBD_SYNTH_VARIANTS = 4, KBD_MIX_VOICES = 24 };

typedef struct {
    float *data;      /* mono, KBD_SYNTH_RATE */
    int frames;
} KbdSample;

/* All switch families x sounds x variants. Deterministic: same bytes every run. */
typedef struct KbdSoundBank KbdSoundBank;
KbdSoundBank *kbd_bank_create(void);
void kbd_bank_destroy(KbdSoundBank *b);
const KbdSample *kbd_bank_get(const KbdSoundBank *b, int family, int sound, int variant);
/* Render one sound into a caller buffer (used by the bank and by tests). */
int kbd_synth_render(int family, int sound, int variant, float *out, int cap);

typedef struct {
    const float *data;
    int frames;
    double pos, rate;
    float gl, gr;
    int active;
    uint32_t age;
} KbdVoice;

typedef struct {
    KbdVoice v[KBD_MIX_VOICES];
    float master;
    uint32_t clock;
} KbdMixer;

void kbd_mixer_init(KbdMixer *m);
/* pitch 1 = original, pan -1..1 (equal power), gain linear. Steals the
 * oldest voice when all are busy. */
void kbd_mixer_trigger(KbdMixer *m, const KbdSample *s, float pitch, float gain, float pan);
/* Write frames of interleaved stereo S16 (overwrites out). */
void kbd_mixer_render(KbdMixer *m, int16_t *out, int frames);
int kbd_mixer_active(const KbdMixer *m);

#endif
