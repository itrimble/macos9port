#ifndef OS999_AUDIO_MIDI_H
#define OS999_AUDIO_MIDI_H

#include "audio_decoder.h"

typedef struct AudioMidiSynth AudioMidiSynth;

int audio_midi_probe(const char *path, AudioMetadata *out_meta);
int audio_midi_open(AudioMidiSynth **out, const char *path, AudioMetadata *out_meta);
int audio_midi_read_frames(AudioMidiSynth *m, float *pcm_f32, size_t max_frames, int *out_eof);
int audio_midi_seek(AudioMidiSynth *m, double seconds);
void audio_midi_close(AudioMidiSynth *m);

#endif
