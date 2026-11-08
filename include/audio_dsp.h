#ifndef OS999_AUDIO_DSP_H
#define OS999_AUDIO_DSP_H

#include <stddef.h>
#include <stdint.h>

enum {
    AUDIO_EQ_BANDS = 10,
    AUDIO_EQ_PRESETS = 6,
    AUDIO_VIS_BANDS = 16,
    AUDIO_VIS_WAVE = 76
};

void audio_dsp_reset(void);

/* 10-band graphic equalizer (-12.0 dB .. +12.0 dB) + preamp + presets */
void audio_eq_set_enabled(int enabled);
int audio_eq_get_enabled(void);
void audio_eq_set_auto(int auto_eq);
int audio_eq_get_auto(void);
void audio_eq_set_preamp(float db);
float audio_eq_get_preamp(void);
void audio_eq_set_band(int band, float db);
float audio_eq_get_band(int band);
int audio_eq_get_preset(void);
void audio_eq_apply_preset(int preset_index);
const char *audio_eq_preset_name(int preset_index);
const char *audio_eq_band_label(int band);

/* Stereo balance (-1.0f = full left, 0.0f = center, +1.0f = full right) */
void audio_set_media_balance(float pan);
float audio_get_media_balance(void);

/* Process media stereo float32 PCM in-place (EQ + Preamp + Balance + Visualizer tap) */
void audio_dsp_process_media(float *stereo_pcm, size_t frames, int sample_rate);

/* Read current 16-band spectrum (0..31), peak-hold dots (0..31), and 76-sample scope (-15..+15) */
void audio_get_visualizer(uint8_t out_spectrum[AUDIO_VIS_BANDS],
                          uint8_t out_peaks[AUDIO_VIS_BANDS],
                          int8_t out_wave[AUDIO_VIS_WAVE]);

#endif
