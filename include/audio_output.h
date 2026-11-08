#ifndef OS999_AUDIO_OUTPUT_H
#define OS999_AUDIO_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct AudioOutput AudioOutput;
typedef void (*AudioOutputPullFn)(void *userdata, float *out_pcm_f32, size_t frames);

/* Open the speaker output backend (interleaved 32-bit float stereo at sample_rate). */
int audio_output_open(AudioOutput **out, int sample_rate, int channels,
                      AudioOutputPullFn pull_cb, void *userdata);

/* Submit interleaved 32-bit float PCM frames into the output ring buffer. */
int audio_output_submit_pcm(AudioOutput *out, const float *pcm_f32, size_t frames);

/* Pause (paused=1) or resume (paused=0) speaker playback. */
void audio_output_pause(AudioOutput *out, int paused);

/* Set or query master output gain (0.0f .. 1.0f). */
void audio_output_set_volume(AudioOutput *out, float volume);
float audio_output_get_volume(const AudioOutput *out);

/* Return number of queued frames in the submitPCM ring buffer. */
size_t audio_output_queued_frames(const AudioOutput *out);
void audio_output_clear(AudioOutput *out);

/* Close the audio output device and release resources. */
void audio_output_close(AudioOutput *out);

#endif
