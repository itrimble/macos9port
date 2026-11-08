#ifndef OS999_AUDIO_SERVICE_H
#define OS999_AUDIO_SERVICE_H

#include "audio_decoder.h"
#include "audio_dsp.h"
#include "audio_output.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_BUS_SYSTEM = 0,
    AUDIO_BUS_MEDIA = 1,
    AUDIO_BUS_APP = 2,
    AUDIO_BUS_COUNT = 3
} AudioBus;

typedef struct AudioMediaPlayer AudioMediaPlayer;

enum {
    AUDIO_MEDIA_RING_FRAMES = 48000 * 4, /* Bounded 4-second streaming buffer */
    AUDIO_APP_RING_FRAMES = 16384
};

/* Initialize shared AudioService, output backend, and pre-decoded system sound cache. */
int audio_service_init(const char *sound_pack_dir);
void audio_service_shutdown(void);
int audio_service_is_initialized(void);

/* Short-sound cache (AudioBus::System): decoded once in advance, triggered immediately. */
int audio_cache_system_sound(const char *name, const char *path);
int audio_play_system_sound(const char *name);
void audio_clear_system_voices(void);
size_t audio_active_system_frames(void);
size_t audio_cached_sound_count(void);

/* Streaming media player (AudioBus::Media): FFmpeg worker thread + bounded buffer. */
AudioMediaPlayer *audio_open_media(const char *path);
void audio_player_play(AudioMediaPlayer *player);
void audio_player_pause(AudioMediaPlayer *player);
int audio_player_seek(AudioMediaPlayer *player, double seconds);
void audio_player_set_volume(AudioMediaPlayer *player, float volume);
float audio_player_get_volume(const AudioMediaPlayer *player);
void audio_player_set_looping(AudioMediaPlayer *player, int looping);
int audio_player_get_looping(const AudioMediaPlayer *player);
int audio_player_is_playing(const AudioMediaPlayer *player);
int audio_player_is_eof(const AudioMediaPlayer *player);
double audio_player_position(const AudioMediaPlayer *player);
double audio_player_duration(const AudioMediaPlayer *player);
size_t audio_player_buffered_frames(const AudioMediaPlayer *player);
int audio_player_metadata(const AudioMediaPlayer *player, AudioMetadata *out_meta);
const char *audio_player_path(const AudioMediaPlayer *player);
void audio_player_close(AudioMediaPlayer *player);

/* Application / Game audio feed (AudioBus::App). */
int audio_submit_app_pcm_f32(const float *pcm_f32, size_t frames);
int audio_submit_app_pcm_s16(const int16_t *pcm_s16, size_t frames);

/* Independent bus volumes (System / Media / App) and Master Mute. */
void audio_set_bus_volume(AudioBus bus, float volume);
float audio_get_bus_volume(AudioBus bus);
void audio_set_master_muted(int muted);
int audio_get_master_muted(void);

/* Pause/resume underlying hardware output (useful for deterministic headless mixer tests). */
void audio_service_pause_output(int paused);

/* Pull mixed stereo float32 PCM frames across all buses (System + Media + App). */
void audio_service_mix_frames(float *out_pcm_f32, size_t frames);

#endif
