#include "audio_dsp.h"
#include <math.h>
#include <pthread.h>
#include <string.h>

static const char *k_band_labels[AUDIO_EQ_BANDS] = {
    "60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"
};

static const float k_band_freqs[AUDIO_EQ_BANDS] = {
    60.0f, 170.0f, 310.0f, 600.0f, 1000.0f, 3000.0f, 6000.0f, 12000.0f, 14000.0f, 16000.0f
};

static const struct {
    const char *name;
    float preamp;
    float bands[AUDIO_EQ_BANDS];
} k_presets[AUDIO_EQ_PRESETS] = {
    {"Flat",        0.0f, { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f}},
    {"Rock",        1.5f, { 5.0f,  3.5f, -3.0f, -4.5f, -2.0f,  2.5f,  5.5f,  7.0f,  7.0f,  7.0f}},
    {"Club / Bass", 2.0f, { 7.5f,  6.5f,  4.0f,  2.0f,  0.0f,  0.0f,  2.0f,  3.5f,  4.0f,  3.0f}},
    {"Full B&T",    1.0f, { 6.5f,  5.5f,  0.0f, -4.5f, -3.0f,  1.0f,  5.0f,  7.5f,  8.5f,  8.5f}},
    {"Pop",         1.0f, {-1.5f,  3.0f,  5.0f,  5.5f,  3.5f, -1.0f, -1.5f, -1.5f, -1.0f, -1.0f}},
    {"Classic Mac", 0.5f, {-2.0f,  1.5f,  3.5f,  4.0f,  3.0f,  2.0f,  0.5f, -1.5f, -3.0f, -4.5f}},
};

static struct {
    pthread_mutex_t lock;
    int eq_enabled;
    int eq_auto;
    int preset_idx;
    float preamp_db;
    float band_db[AUDIO_EQ_BANDS];
    float balance;
    float lp_state[AUDIO_EQ_BANDS][2];
    float vis_smooth[AUDIO_VIS_BANDS];
    float vis_peak[AUDIO_VIS_BANDS];
    int8_t vis_wave[AUDIO_VIS_WAVE];
} dsp = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .eq_enabled = 1,
    .eq_auto = 0,
    .preset_idx = 0
};

static float clamp_db(float db) {
    return db < -12.0f ? -12.0f : (db > 12.0f ? 12.0f : db);
}

static float db_to_lin(float db) {
    return powf(10.0f, db / 20.0f);
}

void audio_dsp_reset(void) {
    pthread_mutex_lock(&dsp.lock);
    dsp.eq_enabled = 1;
    dsp.eq_auto = 0;
    dsp.preset_idx = 0;
    dsp.preamp_db = 0.0f;
    dsp.balance = 0.0f;
    memset(dsp.band_db, 0, sizeof dsp.band_db);
    memset(dsp.lp_state, 0, sizeof dsp.lp_state);
    memset(dsp.vis_smooth, 0, sizeof dsp.vis_smooth);
    memset(dsp.vis_peak, 0, sizeof dsp.vis_peak);
    memset(dsp.vis_wave, 0, sizeof dsp.vis_wave);
    pthread_mutex_unlock(&dsp.lock);
}

void audio_eq_set_enabled(int enabled) {
    pthread_mutex_lock(&dsp.lock);
    dsp.eq_enabled = !!enabled;
    pthread_mutex_unlock(&dsp.lock);
}

int audio_eq_get_enabled(void) {
    pthread_mutex_lock(&dsp.lock);
    int v = dsp.eq_enabled;
    pthread_mutex_unlock(&dsp.lock);
    return v;
}

void audio_eq_set_auto(int auto_eq) {
    pthread_mutex_lock(&dsp.lock);
    dsp.eq_auto = !!auto_eq;
    pthread_mutex_unlock(&dsp.lock);
}

int audio_eq_get_auto(void) {
    pthread_mutex_lock(&dsp.lock);
    int v = dsp.eq_auto;
    pthread_mutex_unlock(&dsp.lock);
    return v;
}

void audio_eq_set_preamp(float db) {
    pthread_mutex_lock(&dsp.lock);
    dsp.preamp_db = clamp_db(db);
    pthread_mutex_unlock(&dsp.lock);
}

float audio_eq_get_preamp(void) {
    pthread_mutex_lock(&dsp.lock);
    float v = dsp.preamp_db;
    pthread_mutex_unlock(&dsp.lock);
    return v;
}

void audio_eq_set_band(int band, float db) {
    if (band < 0 || band >= AUDIO_EQ_BANDS) return;
    pthread_mutex_lock(&dsp.lock);
    dsp.band_db[band] = clamp_db(db);
    pthread_mutex_unlock(&dsp.lock);
}

float audio_eq_get_band(int band) {
    if (band < 0 || band >= AUDIO_EQ_BANDS) return 0.0f;
    pthread_mutex_lock(&dsp.lock);
    float v = dsp.band_db[band];
    pthread_mutex_unlock(&dsp.lock);
    return v;
}

int audio_eq_get_preset(void) {
    pthread_mutex_lock(&dsp.lock);
    int p = dsp.preset_idx;
    pthread_mutex_unlock(&dsp.lock);
    return p;
}

void audio_eq_apply_preset(int preset_index) {
    int idx = preset_index % AUDIO_EQ_PRESETS;
    if (idx < 0) idx += AUDIO_EQ_PRESETS;
    pthread_mutex_lock(&dsp.lock);
    dsp.preset_idx = idx;
    dsp.preamp_db = k_presets[idx].preamp;
    for (int i = 0; i < AUDIO_EQ_BANDS; i++)
        dsp.band_db[i] = k_presets[idx].bands[i];
    pthread_mutex_unlock(&dsp.lock);
}

const char *audio_eq_preset_name(int preset_index) {
    int idx = preset_index % AUDIO_EQ_PRESETS;
    if (idx < 0) idx += AUDIO_EQ_PRESETS;
    return k_presets[idx].name;
}

const char *audio_eq_band_label(int band) {
    if (band < 0 || band >= AUDIO_EQ_BANDS) return "";
    return k_band_labels[band];
}

void audio_set_media_balance(float pan) {
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    pthread_mutex_lock(&dsp.lock);
    dsp.balance = pan;
    pthread_mutex_unlock(&dsp.lock);
}

float audio_get_media_balance(void) {
    pthread_mutex_lock(&dsp.lock);
    float p = dsp.balance;
    pthread_mutex_unlock(&dsp.lock);
    return p;
}

void audio_dsp_process_media(float *stereo_pcm, size_t frames, int sample_rate) {
    if (!stereo_pcm || frames == 0 || sample_rate <= 0) return;
    pthread_mutex_lock(&dsp.lock);
    int eq_on = dsp.eq_enabled;
    float preamp = db_to_lin(dsp.preamp_db);
    float gains[AUDIO_EQ_BANDS];
    float alphas[AUDIO_EQ_BANDS];
    for (int b = 0; b < AUDIO_EQ_BANDS; b++) {
        gains[b] = db_to_lin(dsp.band_db[b]) - 1.0f;
        float rc = 1.0f / (2.0f * 3.14159265f * k_band_freqs[b]);
        float dt = 1.0f / (float)sample_rate;
        alphas[b] = dt / (rc + dt);
    }
    float pan = dsp.balance;
    float l_pan = pan <= 0.0f ? 1.0f : (1.0f - pan);
    float r_pan = pan >= 0.0f ? 1.0f : (1.0f + pan);

    float band_energy[AUDIO_EQ_BANDS] = {0};

    for (size_t i = 0; i < frames; i++) {
        float l = stereo_pcm[i * 2];
        float r = stereo_pcm[i * 2 + 1];
        float prev_lp_l = 0.0f, prev_lp_r = 0.0f;
        float eq_l = l, eq_r = r;

        for (int b = 0; b < AUDIO_EQ_BANDS; b++) {
            float a = alphas[b];
            dsp.lp_state[b][0] += a * (l - dsp.lp_state[b][0]);
            dsp.lp_state[b][1] += a * (r - dsp.lp_state[b][1]);
            float bp_l = dsp.lp_state[b][0] - prev_lp_l;
            float bp_r = dsp.lp_state[b][1] - prev_lp_r;
            prev_lp_l = dsp.lp_state[b][0];
            prev_lp_r = dsp.lp_state[b][1];
            if (eq_on) {
                eq_l += bp_l * gains[b];
                eq_r += bp_r * gains[b];
            }
            float m = 0.5f * (fabsf(bp_l) + fabsf(bp_r));
            band_energy[b] += m;
        }
        if (eq_on) {
            eq_l *= preamp;
            eq_r *= preamp;
        }
        stereo_pcm[i * 2] = eq_l * l_pan;
        stereo_pcm[i * 2 + 1] = eq_r * r_pan;
    }

    /* Update 76-sample oscilloscope trace */
    for (int w = 0; w < AUDIO_VIS_WAVE; w++) {
        size_t idx = (size_t)w * frames / AUDIO_VIS_WAVE;
        if (idx >= frames) idx = frames - 1;
        float mono = 0.5f * (stereo_pcm[idx * 2] + stereo_pcm[idx * 2 + 1]);
        int val = (int)(mono * 18.0f);
        if (val < -15) val = -15;
        if (val > 15) val = 15;
        dsp.vis_wave[w] = (int8_t)val;
    }

    /* Interpolate 10 filter-bank energies into 16 Winamp spectrum columns */
    static const float scale_boost[AUDIO_EQ_BANDS] = {
        3.2f, 4.0f, 4.8f, 5.5f, 6.5f, 8.0f, 10.0f, 13.0f, 15.0f, 18.0f
    };
    float norm_b[AUDIO_EQ_BANDS];
    for (int b = 0; b < AUDIO_EQ_BANDS; b++) {
        float avg = (band_energy[b] / (float)frames) * scale_boost[b];
        norm_b[b] = avg > 1.0f ? 1.0f : avg;
    }
    for (int col = 0; col < AUDIO_VIS_BANDS; col++) {
        float pos = (float)col * (float)(AUDIO_EQ_BANDS - 1) / (float)(AUDIO_VIS_BANDS - 1);
        int i0 = (int)pos;
        int i1 = i0 + 1 < AUDIO_EQ_BANDS ? i0 + 1 : i0;
        float frac = pos - (float)i0;
        float level = ((1.0f - frac) * norm_b[i0] + frac * norm_b[i1]) * 31.0f;
        if (level > dsp.vis_smooth[col])
            dsp.vis_smooth[col] = level;
        else
            dsp.vis_smooth[col] = dsp.vis_smooth[col] * 0.78f + level * 0.22f;
        if (dsp.vis_smooth[col] >= dsp.vis_peak[col])
            dsp.vis_peak[col] = dsp.vis_smooth[col];
        else if (dsp.vis_peak[col] > 0.35f)
            dsp.vis_peak[col] -= 0.35f;
        else
            dsp.vis_peak[col] = 0.0f;
    }
    pthread_mutex_unlock(&dsp.lock);
}

void audio_get_visualizer(uint8_t out_spectrum[AUDIO_VIS_BANDS],
                          uint8_t out_peaks[AUDIO_VIS_BANDS],
                          int8_t out_wave[AUDIO_VIS_WAVE]) {
    pthread_mutex_lock(&dsp.lock);
    if (out_spectrum) {
        for (int i = 0; i < AUDIO_VIS_BANDS; i++) {
            int v = (int)(dsp.vis_smooth[i] + 0.5f);
            out_spectrum[i] = (uint8_t)(v < 0 ? 0 : (v > 31 ? 31 : v));
        }
    }
    if (out_peaks) {
        for (int i = 0; i < AUDIO_VIS_BANDS; i++) {
            int v = (int)(dsp.vis_peak[i] + 0.5f);
            out_peaks[i] = (uint8_t)(v < 0 ? 0 : (v > 31 ? 31 : v));
        }
    }
    if (out_wave) {
        memcpy(out_wave, dsp.vis_wave, AUDIO_VIS_WAVE);
    }
    pthread_mutex_unlock(&dsp.lock);
}
