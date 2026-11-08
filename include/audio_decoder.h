#ifndef OS999_AUDIO_DECODER_H
#define OS999_AUDIO_DECODER_H

#include <stddef.h>
#include <stdint.h>

enum {
    AUDIO_INTERNAL_RATE = 48000,
    AUDIO_INTERNAL_CHANNELS = 2
};

typedef struct {
    char container_name[64];
    char codec_name[64];
    char codec_long_name[128];
    char channel_layout_desc[64];
    char title[128];
    char artist[128];
    char album[128];
    int sample_rate;
    int channels;
    double duration_seconds;
    int seekable;
} AudioMetadata;

typedef struct {
    char group[40];
    char label[48];
    char codec_name[32];
    int available;
} AudioFormatSupport;

typedef struct AudioDecoder AudioDecoder;

/* Probe file content (never file extension) to locate and verify a decodable audio stream. */
int audio_decoder_probe(const char *path, AudioMetadata *out_meta);

/* Open a media file for decoding into interleaved 32-bit float stereo PCM at 48000 Hz. */
int audio_decoder_open(AudioDecoder **out, const char *path);

/* Decode up to max_frames stereo float32 frames. Sets *out_eof=1 when drained. Returns frames >= 0 or -1. */
int audio_decoder_read_frames(AudioDecoder *dec, float *pcm_f32, size_t max_frames, int *out_eof);

/* Seek to position in seconds. Returns 0 on success, -1 on error. */
int audio_decoder_seek(AudioDecoder *dec, double seconds);

/* Copy metadata and source channel layout info for an open decoder. */
int audio_decoder_metadata(const AudioDecoder *dec, AudioMetadata *out_meta);

/* Close decoder and free all FFmpeg/backend state. */
void audio_decoder_close(AudioDecoder *dec);

/* Enumerate runtime decoder availability across OS999's target compatibility matrix. */
size_t audio_decoder_enumerate_formats(AudioFormatSupport *out_list, size_t max_items);

/* Format a human-readable multi-line summary of runtime-available decoders. */
int audio_decoder_format_summary(char *out_text, size_t capacity);

/* Return the linked FFmpeg version description string. */
const char *audio_decoder_ffmpeg_version(void);

#endif
