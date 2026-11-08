#include "audio_decoder.h"
#include "audio_midi.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { STAGE_CAP_FRAMES = 16384 };

struct AudioDecoder {
    int is_midi;
    AudioMidiSynth *midi;
    AVFormatContext *fmt_ctx;
    AVCodecContext *codec_ctx;
    SwrContext *swr_ctx;
    AVPacket *pkt;
    AVFrame *frame;
    int stream_index;
    int input_eof;
    int codec_eof;
    int swr_eof;
    AudioMetadata meta;
    float *stage_pcm;
    size_t stage_len;
    size_t stage_pos;
    size_t stage_cap;
};

static void copy_tag(char *dst, size_t cap, AVDictionary *dict, const char *key) {
    if (!dict || !dst || !cap) return;
    AVDictionaryEntry *e = av_dict_get(dict, key, NULL, 0);
    if (e && e->value && e->value[0]) snprintf(dst, cap, "%s", e->value);
}

static void fill_metadata(AVFormatContext *fmt, AVStream *st, const AVCodec *codec,
                          const AVCodecContext *cctx, const char *path, AudioMetadata *m) {
    memset(m, 0, sizeof *m);
    if (fmt && fmt->iformat && fmt->iformat->name)
        snprintf(m->container_name, sizeof m->container_name, "%s", fmt->iformat->name);
    if (codec && codec->name)
        snprintf(m->codec_name, sizeof m->codec_name, "%s", codec->name);
    if (codec && codec->long_name)
        snprintf(m->codec_long_name, sizeof m->codec_long_name, "%s", codec->long_name);
    int rate = cctx ? cctx->sample_rate : (st && st->codecpar ? st->codecpar->sample_rate : 0);
    int ch = cctx ? cctx->ch_layout.nb_channels :
             (st && st->codecpar ? st->codecpar->ch_layout.nb_channels : 0);
    m->sample_rate = rate;
    m->channels = ch;
    const AVChannelLayout *layout = cctx ? &cctx->ch_layout :
                                    (st && st->codecpar ? &st->codecpar->ch_layout : NULL);
    if (layout && layout->nb_channels > 0)
        av_channel_layout_describe(layout, m->channel_layout_desc, sizeof m->channel_layout_desc);
    else
        snprintf(m->channel_layout_desc, sizeof m->channel_layout_desc, "%dch", ch);

    if (st && st->duration > 0 && st->time_base.den > 0)
        m->duration_seconds = (double)st->duration * av_q2d(st->time_base);
    else if (fmt && fmt->duration > 0)
        m->duration_seconds = (double)fmt->duration / (double)AV_TIME_BASE;
    m->seekable = fmt && fmt->pb ? (fmt->pb->seekable != 0) : 1;

    if (fmt) {
        copy_tag(m->title, sizeof m->title, fmt->metadata, "title");
        copy_tag(m->artist, sizeof m->artist, fmt->metadata, "artist");
        copy_tag(m->album, sizeof m->album, fmt->metadata, "album");
    }
    if (st) {
        if (!m->title[0]) copy_tag(m->title, sizeof m->title, st->metadata, "title");
        if (!m->artist[0]) copy_tag(m->artist, sizeof m->artist, st->metadata, "artist");
        if (!m->album[0]) copy_tag(m->album, sizeof m->album, st->metadata, "album");
    }
    if (!m->title[0] && path) {
        const char *slash = strrchr(path, '/');
        snprintf(m->title, sizeof m->title, "%s", slash ? slash + 1 : path);
    }
}

static int valid_file(const char *path) {
    if (!path || !*path) return 0;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 8;
}

int audio_decoder_probe(const char *path, AudioMetadata *out_meta) {
    if (!valid_file(path)) return -1;
    if (audio_midi_probe(path, out_meta) == 0) return 0;

    av_log_set_level(AV_LOG_QUIET);
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, path, NULL, NULL) < 0 || !fmt) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }
    const char *fmt_name = fmt->iformat && fmt->iformat->name ? fmt->iformat->name : "";
    if (!fmt_name[0] || strcmp(fmt_name, "tty") == 0 || strcmp(fmt_name, "bin") == 0 ||
        strcmp(fmt_name, "data") == 0 || strstr(fmt_name, "image") != NULL ||
        strstr(fmt_name, "pipe") != NULL) {
        avformat_close_input(&fmt);
        return -1;
    }
    const AVCodec *codec = NULL;
    int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (idx < 0 || !codec) {
        avformat_close_input(&fmt);
        return -1;
    }
    AVStream *st = fmt->streams[idx];
    if (!st || !st->codecpar || st->codecpar->sample_rate <= 0 ||
        st->codecpar->ch_layout.nb_channels <= 0) {
        avformat_close_input(&fmt);
        return -1;
    }
    /* verify at least one packet or valid stream duration so random binary/text files are rejected */
    AVCodecContext *cctx = avcodec_alloc_context3(codec);
    int ok = cctx && avcodec_parameters_to_context(cctx, st->codecpar) >= 0 &&
             avcodec_open2(cctx, codec, NULL) >= 0;
    if (ok && out_meta)
        fill_metadata(fmt, st, codec, cctx, path, out_meta);
    if (cctx) avcodec_free_context(&cctx);
    avformat_close_input(&fmt);
    return ok ? 0 : -1;
}

static int init_resampler(AudioDecoder *d) {
    if (d->swr_ctx) swr_free(&d->swr_ctx);
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_layout = d->codec_ctx->ch_layout;
    if (in_layout.nb_channels <= 0) return -1;
    if (in_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&in_layout, in_layout.nb_channels);
    int rc = swr_alloc_set_opts2(&d->swr_ctx,
                                 &out_layout, AV_SAMPLE_FMT_FLT, AUDIO_INTERNAL_RATE,
                                 &in_layout, d->codec_ctx->sample_fmt, d->codec_ctx->sample_rate,
                                 0, NULL);
    av_channel_layout_uninit(&in_layout);
    if (rc < 0 || !d->swr_ctx || swr_init(d->swr_ctx) < 0) return -1;
    return 0;
}

int audio_decoder_open(AudioDecoder **out, const char *path) {
    if (!out || !valid_file(path)) return -1;
    *out = NULL;
    AudioDecoder *d = calloc(1, sizeof *d);
    if (!d) return -1;

    if (audio_midi_open(&d->midi, path, &d->meta) == 0) {
        d->is_midi = 1;
        *out = d;
        return 0;
    }

    av_log_set_level(AV_LOG_QUIET);
    if (avformat_open_input(&d->fmt_ctx, path, NULL, NULL) < 0 || !d->fmt_ctx) {
        audio_decoder_close(d);
        return -1;
    }
    if (avformat_find_stream_info(d->fmt_ctx, NULL) < 0) {
        audio_decoder_close(d);
        return -1;
    }
    const AVCodec *codec = NULL;
    d->stream_index = av_find_best_stream(d->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (d->stream_index < 0 || !codec) {
        audio_decoder_close(d);
        return -1;
    }
    AVStream *st = d->fmt_ctx->streams[d->stream_index];
    d->codec_ctx = avcodec_alloc_context3(codec);
    if (!d->codec_ctx ||
        avcodec_parameters_to_context(d->codec_ctx, st->codecpar) < 0 ||
        avcodec_open2(d->codec_ctx, codec, NULL) < 0 ||
        d->codec_ctx->sample_rate <= 0 ||
        d->codec_ctx->ch_layout.nb_channels <= 0) {
        audio_decoder_close(d);
        return -1;
    }
    if (init_resampler(d) != 0) {
        audio_decoder_close(d);
        return -1;
    }
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->stage_cap = STAGE_CAP_FRAMES;
    d->stage_pcm = calloc(d->stage_cap * AUDIO_INTERNAL_CHANNELS, sizeof(float));
    if (!d->pkt || !d->frame || !d->stage_pcm) {
        audio_decoder_close(d);
        return -1;
    }
    fill_metadata(d->fmt_ctx, st, codec, d->codec_ctx, path, &d->meta);
    *out = d;
    return 0;
}

static int stage_ensure(AudioDecoder *d, size_t extra_frames) {
    if (d->stage_pos > 0) {
        size_t rem = d->stage_len - d->stage_pos;
        if (rem > 0)
            memmove(d->stage_pcm, d->stage_pcm + d->stage_pos * 2, rem * 2 * sizeof(float));
        d->stage_len = rem;
        d->stage_pos = 0;
    }
    if (d->stage_len + extra_frames <= d->stage_cap) return 0;
    size_t ncap = d->stage_cap * 2;
    while (ncap < d->stage_len + extra_frames) ncap *= 2;
    float *nb = realloc(d->stage_pcm, ncap * 2 * sizeof(float));
    if (!nb) return -1;
    d->stage_pcm = nb;
    d->stage_cap = ncap;
    return 0;
}

static int decode_step(AudioDecoder *d) {
    while (!d->swr_eof) {
        int rc = avcodec_receive_frame(d->codec_ctx, d->frame);
        if (rc == 0) {
            int64_t delay = swr_get_delay(d->swr_ctx, d->codec_ctx->sample_rate);
            int out_max = (int)av_rescale_rnd(delay + d->frame->nb_samples,
                                              AUDIO_INTERNAL_RATE, d->codec_ctx->sample_rate,
                                              AV_ROUND_UP) + 32;
            if (out_max < 64) out_max = 64;
            if (stage_ensure(d, (size_t)out_max) != 0) {
                av_frame_unref(d->frame);
                return -1;
            }
            uint8_t *out_ptr = (uint8_t *)(d->stage_pcm + d->stage_len * 2);
            int converted = swr_convert(d->swr_ctx, &out_ptr, out_max,
                                        (const uint8_t **)d->frame->extended_data,
                                        d->frame->nb_samples);
            av_frame_unref(d->frame);
            if (converted < 0) return -1;
            d->stage_len += (size_t)converted;
            if (converted > 0) return 0;
            continue;
        }
        if (rc == AVERROR_EOF) {
            d->codec_eof = 1;
            if (stage_ensure(d, 4096) != 0) return -1;
            uint8_t *out_ptr = (uint8_t *)(d->stage_pcm + d->stage_len * 2);
            int flushed = swr_convert(d->swr_ctx, &out_ptr, 4096, NULL, 0);
            if (flushed > 0) d->stage_len += (size_t)flushed;
            d->swr_eof = 1;
            return 0;
        }
        if (rc != AVERROR(EAGAIN)) return -1;

        if (d->input_eof) {
            avcodec_send_packet(d->codec_ctx, NULL);
            continue;
        }
        int prc = av_read_frame(d->fmt_ctx, d->pkt);
        if (prc < 0) {
            d->input_eof = 1;
            avcodec_send_packet(d->codec_ctx, NULL);
            continue;
        }
        if (d->pkt->stream_index == d->stream_index) {
            int src = avcodec_send_packet(d->codec_ctx, d->pkt);
            av_packet_unref(d->pkt);
            if (src < 0 && src != AVERROR(EAGAIN) && src != AVERROR_EOF) {
                /* Skip corrupt packet and continue decoding if stream remains readable */
                continue;
            }
        } else {
            av_packet_unref(d->pkt);
        }
    }
    return 0;
}

int audio_decoder_read_frames(AudioDecoder *dec, float *pcm_f32, size_t max_frames, int *out_eof) {
    if (!dec || !pcm_f32) return -1;
    if (out_eof) *out_eof = 0;
    if (dec->is_midi)
        return audio_midi_read_frames(dec->midi, pcm_f32, max_frames, out_eof);

    size_t copied = 0;
    while (copied < max_frames) {
        size_t avail = dec->stage_len - dec->stage_pos;
        if (avail > 0) {
            size_t take = (max_frames - copied) < avail ? (max_frames - copied) : avail;
            memcpy(pcm_f32 + copied * 2, dec->stage_pcm + dec->stage_pos * 2,
                   take * 2 * sizeof(float));
            dec->stage_pos += take;
            copied += take;
            continue;
        }
        if (dec->swr_eof) break;
        if (decode_step(dec) != 0) return copied > 0 ? (int)copied : -1;
    }
    if (out_eof && dec->swr_eof && dec->stage_pos >= dec->stage_len)
        *out_eof = 1;
    return (int)copied;
}

int audio_decoder_seek(AudioDecoder *dec, double seconds) {
    if (!dec) return -1;
    if (seconds < 0.0) seconds = 0.0;
    if (dec->is_midi) return audio_midi_seek(dec->midi, seconds);
    if (!dec->fmt_ctx || !dec->codec_ctx) return -1;

    AVStream *st = dec->fmt_ctx->streams[dec->stream_index];
    int64_t ts = (int64_t)(seconds * (double)AV_TIME_BASE);
    int stream_idx = -1;
    if (st && st->time_base.den > 0 && st->time_base.num > 0) {
        ts = av_rescale_q(ts, AV_TIME_BASE_Q, st->time_base);
        if (st->start_time != AV_NOPTS_VALUE) ts += st->start_time;
        stream_idx = dec->stream_index;
    }
    if (av_seek_frame(dec->fmt_ctx, stream_idx, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        if (av_seek_frame(dec->fmt_ctx, -1, (int64_t)(seconds * (double)AV_TIME_BASE),
                          AVSEEK_FLAG_BACKWARD) < 0)
            return -1;
    }
    avcodec_flush_buffers(dec->codec_ctx);
    if (dec->swr_ctx) swr_init(dec->swr_ctx);
    dec->stage_pos = dec->stage_len = 0;
    dec->input_eof = dec->codec_eof = dec->swr_eof = 0;
    return 0;
}

int audio_decoder_metadata(const AudioDecoder *dec, AudioMetadata *out_meta) {
    if (!dec || !out_meta) return -1;
    *out_meta = dec->meta;
    return 0;
}

void audio_decoder_close(AudioDecoder *dec) {
    if (!dec) return;
    if (dec->midi) audio_midi_close(dec->midi);
    if (dec->frame) av_frame_free(&dec->frame);
    if (dec->pkt) av_packet_free(&dec->pkt);
    if (dec->swr_ctx) swr_free(&dec->swr_ctx);
    if (dec->codec_ctx) avcodec_free_context(&dec->codec_ctx);
    if (dec->fmt_ctx) avformat_close_input(&dec->fmt_ctx);
    free(dec->stage_pcm);
    free(dec);
}

static const struct { const char *group, *label, *codec; } k_matrix[] = {
    {"Everyday",     "MP3",                     "mp3"},
    {"Everyday",     "AAC (M4A/MP4)",           "aac"},
    {"Everyday",     "FLAC",                    "flac"},
    {"Everyday",     "ALAC (Apple Lossless)",   "alac"},
    {"Everyday",     "Vorbis (OGG)",            "vorbis"},
    {"Everyday",     "Opus",                    "opus"},
    {"Uncompressed", "WAV / CAF Signed PCM16",  "pcm_s16le"},
    {"Uncompressed", "AIFF / AU Signed PCM16",  "pcm_s16be"},
    {"Uncompressed", "Unsigned 8-bit PCM",      "pcm_u8"},
    {"Uncompressed", "Floating-Point PCM32",    "pcm_f32le"},
    {"Older Media",  "Windows Media Audio",     "wmav2"},
    {"Older Media",  "RealAudio 14.4/28.8",     "ra_144"},
    {"Older Media",  "Microsoft ADPCM",         "adpcm_ms"},
    {"Older Media",  "Yamaha ADPCM",            "adpcm_yamaha"},
    {"Surround",     "Dolby AC-3",              "ac3"},
    {"Surround",     "Dolby E-AC-3",            "eac3"},
    {"Surround",     "DTS Coherent Acoustics",  "dca"},
    {"Surround",     "Dolby TrueHD",            "truehd"},
    {"Lossless",     "WavPack",                 "wavpack"},
    {"Lossless",     "Monkey's Audio (APE)",    "ape"},
    {"Lossless",     "TAK Lossless",            "tak"},
    {"Classic Mac",  "MACE 3:1",                "mace3"},
    {"Classic Mac",  "MACE 6:1",                "mace6"},
    {"Classic Mac",  "QuickTime IMA ADPCM",     "adpcm_ima_qt"},
    {"Classic Mac",  "QDesign Music 2 (QT)",    "qdm2"},
    {"Retro/Synth",  "Standard MIDI (SMF)",     "midi_synth"},
};

size_t audio_decoder_enumerate_formats(AudioFormatSupport *out_list, size_t max_items) {
    size_t count = sizeof k_matrix / sizeof k_matrix[0];
    if (!out_list) return count;
    size_t n = count < max_items ? count : max_items;
    for (size_t i = 0; i < n; i++) {
        memset(&out_list[i], 0, sizeof out_list[i]);
        snprintf(out_list[i].group, sizeof out_list[i].group, "%s", k_matrix[i].group);
        snprintf(out_list[i].label, sizeof out_list[i].label, "%s", k_matrix[i].label);
        snprintf(out_list[i].codec_name, sizeof out_list[i].codec_name, "%s", k_matrix[i].codec);
        if (strcmp(k_matrix[i].codec, "midi_synth") == 0)
            out_list[i].available = 1;
        else
            out_list[i].available = avcodec_find_decoder_by_name(k_matrix[i].codec) != NULL;
    }
    return n;
}

const char *audio_decoder_ffmpeg_version(void) {
    return av_version_info();
}

int audio_decoder_format_summary(char *out_text, size_t capacity) {
    if (!out_text || capacity == 0) return -1;
    AudioFormatSupport list[32];
    size_t n = audio_decoder_enumerate_formats(list, 32);
    size_t used = 0;
    int w = snprintf(out_text, capacity, "FFmpeg %s | Internal: 48 kHz 32-bit Float Stereo\n",
                     av_version_info());
    if (w > 0) used = (size_t)w < capacity ? (size_t)w : capacity - 1;
    for (size_t i = 0; i < n && used + 64 < capacity; i++) {
        w = snprintf(out_text + used, capacity - used, "[%s] %-12s %-24s (%s)\n",
                     list[i].available ? "OK" : "--",
                     list[i].group, list[i].label, list[i].codec_name);
        if (w <= 0) break;
        used += (size_t)w;
    }
    return 0;
}
