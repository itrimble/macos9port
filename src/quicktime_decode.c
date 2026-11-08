#include "quicktime_decode.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct QtDecoder {
    AVFormatContext *format;
    AVIOContext *io;
    AVCodecContext *codec;
    AVFrame *frame, *software;
    AVPacket *packet;
    struct SwsContext *scale;
    uint32_t *pixels;
    int fd, stream, width, height, drained, hardware;
    enum AVPixelFormat hw_format;
    double duration, origin;
};

static int read_file(void *opaque, uint8_t *data, int n) {
    QtDecoder *d = opaque;
    ssize_t result;
    do { result = read(d->fd, data, (size_t)n); } while (result < 0 && errno == EINTR);
    return result > 0 ? (int)result : result == 0 ? AVERROR_EOF : AVERROR(errno);
}
static int64_t seek_file(void *opaque, int64_t offset, int whence) {
    QtDecoder *d = opaque;
    if (whence == AVSEEK_SIZE) { struct stat st; return fstat(d->fd, &st) ? -1 : st.st_size; }
    return lseek(d->fd, offset, whence & ~AVSEEK_FORCE);
}
static enum AVPixelFormat choose_format(AVCodecContext *ctx, const enum AVPixelFormat *formats) {
    QtDecoder *d = ctx->opaque;
    for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++)
        if (*p == d->hw_format) return *p;
    return formats[0];
}
void qt_decode_close(QtDecoder *d) {
    if (!d) return;
    sws_freeContext(d->scale);
    av_packet_free(&d->packet); av_frame_free(&d->frame); av_frame_free(&d->software);
    avcodec_free_context(&d->codec); avformat_close_input(&d->format);
    if (d->io) { av_freep(&d->io->buffer); avio_context_free(&d->io); }
    if (d->fd >= 0) close(d->fd);
    free(d->pixels); free(d);
}
QtDecoder *qt_decode_open(const char *path, char *error, size_t n) {
    if (error && n) error[0] = 0;
    QtDecoder *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->fd = -1; d->hw_format = AV_PIX_FMT_NONE;
    struct stat st;
    if (!path || (d->fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK)) < 0 ||
        fstat(d->fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > INT64_C(2147483648)) goto fail;
    d->format = avformat_alloc_context();
    unsigned char *buffer = av_malloc(32768);
    if (!d->format || !buffer) { av_free(buffer); goto fail; }
    d->io = avio_alloc_context(buffer, 32768, 0, d, read_file, NULL, seek_file);
    if (!d->io) { av_free(buffer); goto fail; }
    d->format->pb = d->io; d->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVDictionary *options = NULL;
    av_dict_set(&options, "enable_drefs", "0", 0);
    av_dict_set(&options, "use_absolute_path", "0", 0);
    av_dict_set(&options, "max_streams", "32", 0);
    int result = avformat_open_input(&d->format, NULL, av_find_input_format("mov"), &options);
    av_dict_free(&options);
    if (result < 0 || avformat_find_stream_info(d->format, NULL) < 0) goto fail;
    const AVCodec *codec = NULL;
    d->stream = av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (d->stream < 0 || !codec) goto fail;
    AVStream *stream = d->format->streams[d->stream];
    d->width = stream->codecpar->width; d->height = stream->codecpar->height;
    if (d->width < 1 || d->height < 1 || d->width > 1920 || d->height > 1080) goto fail;
    d->duration = d->format->duration > 0 ? (double)d->format->duration / AV_TIME_BASE :
                  stream->duration > 0 ? stream->duration * av_q2d(stream->time_base) : 0;
    if (!isfinite(d->duration) || d->duration <= 0 || d->duration > 86400) goto fail;
    d->origin = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time * av_q2d(stream->time_base);
    d->codec = avcodec_alloc_context3(codec);
    if (!d->codec || avcodec_parameters_to_context(d->codec, stream->codecpar) < 0) goto fail;
    d->codec->thread_count = 2;
#ifdef __APPLE__
    for (int i = 0; ; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        if (config->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            av_hwdevice_ctx_create(&d->codec->hw_device_ctx, config->device_type, NULL, NULL, 0) >= 0) {
            d->hw_format = config->pix_fmt; d->codec->opaque = d;
            d->codec->get_format = choose_format; break;
        }
    }
#endif
    if (avcodec_open2(d->codec, codec, NULL) < 0) goto fail;
    d->frame = av_frame_alloc(); d->software = av_frame_alloc(); d->packet = av_packet_alloc();
    d->pixels = calloc((size_t)d->width * d->height, sizeof *d->pixels);
    if (!d->frame || !d->software || !d->packet || !d->pixels) goto fail;
    return d;
fail:
    if (error && n) snprintf(error, n, "Unsupported or invalid local MOV/MP4 video (maximum 1920x1080, 2 GB, 24 hours).");
    qt_decode_close(d); return NULL;
}

int qt_decode_next(QtDecoder *d, double *pts) {
    if (!d) return -1;
    for (int budget = 0; budget < 256; budget++) {
        int result = avcodec_receive_frame(d->codec, d->frame);
        if (!result) {
            AVFrame *frame = d->frame;
            if (frame->format == d->hw_format) {
                av_frame_unref(d->software);
                if (av_hwframe_transfer_data(d->software, frame, 0) < 0) return -1;
                frame = d->software; d->hardware = 1;
            }
            if (frame->width != d->width || frame->height != d->height) return -1;
            d->scale = sws_getCachedContext(d->scale, d->width, d->height, frame->format,
                d->width, d->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
            if (!d->scale) return -1;
            uint8_t *planes[] = {(uint8_t *)d->pixels, NULL, NULL, NULL};
            int strides[] = {d->width * 4, 0, 0, 0};
            if (sws_scale(d->scale, (const uint8_t *const *)frame->data, frame->linesize,
                          0, d->height, planes, strides) != d->height) return -1;
            int64_t stamp = d->frame->best_effort_timestamp;
            double seconds = stamp == AV_NOPTS_VALUE ? 0 : stamp * av_q2d(d->format->streams[d->stream]->time_base) - d->origin;
            if (pts) *pts = seconds < 0 ? 0 : seconds;
            av_frame_unref(d->frame); return 1;
        }
        if (result == AVERROR_EOF) return 0;
        if (result != AVERROR(EAGAIN) || d->drained) return -1;
        result = av_read_frame(d->format, d->packet);
        if (result < 0) {
            if (result != AVERROR_EOF) return -1;
            d->drained = 1;
            if (avcodec_send_packet(d->codec, NULL) < 0) return -1;
        } else {
            if (d->packet->stream_index == d->stream)
                result = avcodec_send_packet(d->codec, d->packet);
            else result = 0;
            av_packet_unref(d->packet);
            if (result < 0) return -1;
        }
    }
    return 2;
}
int qt_decode_seek(QtDecoder *d, double seconds) {
    if (!d || !isfinite(seconds) || seconds < 0 || seconds > d->duration) return -1;
    int64_t stamp = (int64_t)((seconds + d->origin) / av_q2d(d->format->streams[d->stream]->time_base));
    if (av_seek_frame(d->format, d->stream, stamp, AVSEEK_FLAG_BACKWARD) < 0) return -1;
    avcodec_flush_buffers(d->codec); av_frame_unref(d->frame); av_packet_unref(d->packet);
    d->drained = 0; return 0;
}
const uint32_t *qt_decode_pixels(const QtDecoder *d, int *w, int *h) {
    if (!d) return NULL;
    if (w) *w = d->width;
    if (h) *h = d->height;
    return d->pixels;
}
double qt_decode_duration(const QtDecoder *d) { return d ? d->duration : 0; }
int qt_decode_hardware(const QtDecoder *d) { return d && d->hardware; }
const char *qt_decode_codec(const QtDecoder *d) { return d ? avcodec_get_name(d->codec->codec_id) : "none"; }
