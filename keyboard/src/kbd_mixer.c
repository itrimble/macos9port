/* Polyphonic one-shot mixer. Fast typing overlaps a dozen clicks, so voices
 * never cut each other off; when all are busy the oldest is stolen. Pitch is
 * linear-interpolated resampling; panning is equal power; the bus ends in a
 * soft knee so a burst of keys can never hard-clip. */
#include "kbd_audio.h"
#include <math.h>
#include <string.h>

void kbd_mixer_init(KbdMixer *m) {
    memset(m, 0, sizeof *m);
    m->master = 0.8f;
}

void kbd_mixer_trigger(KbdMixer *m, const KbdSample *s, float pitch, float gain, float pan) {
    if (!s || !s->data || s->frames <= 0) return;
    KbdVoice *v = NULL;
    for (int i = 0; i < KBD_MIX_VOICES && !v; i++)
        if (!m->v[i].active) v = &m->v[i];
    if (!v) {
        v = &m->v[0];
        for (int i = 1; i < KBD_MIX_VOICES; i++)
            if (m->v[i].age < v->age) v = &m->v[i];
    }
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    float a = (pan + 1) * 0.785398163f;
    *v = (KbdVoice){ s->data, s->frames, 0.0, pitch > 0.25f ? pitch : 0.25f,
                     cosf(a) * gain, sinf(a) * gain, 1, ++m->clock };
}

static float knee(float x) {
    float ax = fabsf(x);
    if (ax <= 0.8f) return x;
    float over = ax - 0.8f, y = 0.8f + over / (1 + over * 5);   /* asymptote < 1 */
    return x < 0 ? -y : y;
}

void kbd_mixer_render(KbdMixer *m, int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        float l = 0, r = 0;
        for (int j = 0; j < KBD_MIX_VOICES; j++) {
            KbdVoice *v = &m->v[j];
            if (!v->active) continue;
            int p = (int)v->pos;
            if (p + 1 >= v->frames) { v->active = 0; continue; }
            float fr = (float)(v->pos - p);
            float x = v->data[p] + (v->data[p + 1] - v->data[p]) * fr;
            l += x * v->gl;
            r += x * v->gr;
            v->pos += v->rate;
        }
        l = knee(l * m->master);
        r = knee(r * m->master);
        out[2 * i] = (int16_t)lrintf(l * 32767.0f);
        out[2 * i + 1] = (int16_t)lrintf(r * 32767.0f);
    }
}

int kbd_mixer_active(const KbdMixer *m) {
    int n = 0;
    for (int i = 0; i < KBD_MIX_VOICES; i++) n += m->v[i].active;
    return n;
}
