#include "quicktime_movie.h"
#include "quicktime_decode.h"
#include "audio_service.h"
#include "mac_movie.h"
#include "movie_host.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct QuickTimeMovie {
    QtDecoder *decoder;
    AudioMediaPlayer *audio;
    uint32_t *pixels;
    int width, height, state, anchor, pending, eof, have_frame;
    uint32_t origin_ms;
    double time, origin_time, pending_time;
    float volume;
};

static int draw_until(QuickTimeMovie *m, double target) {
    for (int i = 0; i < 32; i++) {
        if (!m->pending && !m->eof) {
            int result = qt_decode_next(m->decoder, &m->pending_time);
            if (result < 0) return -1;
            if (result == 2) return 0;
            if (!result) { m->eof = 1; break; }
            m->pending = 1;
        }
        if (!m->pending || (m->have_frame && m->pending_time > target)) break;
        memcpy(m->pixels, qt_decode_pixels(m->decoder, NULL, NULL),
               (size_t)m->width * m->height * sizeof *m->pixels);
        m->have_frame = 1; m->pending = 0;
    }
    return 0;
}
int NewMovieFromFile(QuickTimeMovie **out, const char *path, char *error, size_t n) {
    if (!out) return -1;
    *out = NULL;
    QuickTimeMovie *m = calloc(1, sizeof *m);
    if (!m) return -1;
    m->decoder = qt_decode_open(path, error, n);
    if (!m->decoder) { free(m); return -1; }
    qt_decode_pixels(m->decoder, &m->width, &m->height);
    m->pixels = calloc((size_t)m->width * m->height, sizeof *m->pixels);
    m->volume = 1.0f;
    if (!m->pixels || draw_until(m, 0) < 0 || !m->have_frame) { DisposeMovie(m); return -1; }
    if (audio_service_is_initialized()) m->audio = audio_open_media(path);
    *out = m; return 0;
}
void DisposeMovie(QuickTimeMovie *m) {
    if (!m) return;
    if (m->audio) audio_player_close(m->audio);
    qt_decode_close(m->decoder); free(m->pixels); free(m);
}
void StartMovie(QuickTimeMovie *m) {
    if (!m) return;
    if (m->state == MAC_MOVIE_ENDED && SetMovieTime(m, 0)) return;
    m->state = MAC_MOVIE_PLAYING; m->anchor = 1;
    if (m->audio) audio_player_play(m->audio);
}
void StopMovie(QuickTimeMovie *m) {
    if (!m) return;
    m->state = MAC_MOVIE_PAUSED;
    if (m->audio) audio_player_pause(m->audio);
}
uint32_t GetMovieTime(const QuickTimeMovie *m) { return m ? (uint32_t)(m->time * 1000) : 0; }
uint32_t GetMovieDuration(const QuickTimeMovie *m) { return m ? (uint32_t)(qt_decode_duration(m->decoder) * 1000) : 0; }
int SetMovieTime(QuickTimeMovie *m, uint32_t milliseconds) {
    if (!m || milliseconds > GetMovieDuration(m)) return -1;
    double seconds = milliseconds / 1000.0;
    if (qt_decode_seek(m->decoder, seconds)) return -1;
    if (m->audio && audio_player_seek(m->audio, seconds)) return -1;
    m->pending = m->eof = 0; m->time = seconds; m->anchor = 1;
    if (m->state == MAC_MOVIE_ENDED) m->state = MAC_MOVIE_PAUSED;
    /* Decode towards the target in bounded batches; later task calls continue. */
    return draw_until(m, seconds);
}
int MoviesTask(QuickTimeMovie *m, uint32_t host_ms) {
    if (!m) return -1;
    if (m->state == MAC_MOVIE_PLAYING) {
        if (m->anchor) { m->origin_ms = host_ms; m->origin_time = m->time; m->anchor = 0; }
        double target = m->origin_time + (uint32_t)(host_ms - m->origin_ms) / 1000.0;
        if (m->audio && !audio_player_is_eof(m->audio)) {
            target = audio_player_position(m->audio);
            /* Continue from the audio clock when a shorter soundtrack ends. */
            m->origin_ms = host_ms; m->origin_time = target;
        }
        m->time = fmin(qt_decode_duration(m->decoder), fmax(0, target));
        if (m->time >= qt_decode_duration(m->decoder)) {
            m->state = MAC_MOVIE_ENDED;
            if (m->audio) audio_player_pause(m->audio);
        }
    }
    if (draw_until(m, m->time) < 0) { StopMovie(m); return -1; }
    return 0;
}
int GetMovieState(const QuickTimeMovie *m) { return m ? m->state : MAC_MOVIE_STOPPED; }
const uint32_t *GetMoviePixels(const QuickTimeMovie *m, int *w, int *h) {
    if (!m) return NULL;
    if (w) *w = m->width;
    if (h) *h = m->height;
    return m->have_frame ? m->pixels : NULL;
}
void SetMovieVolume(QuickTimeMovie *m, float volume) {
    if (!m || !isfinite(volume)) return;
    m->volume = fminf(1, fmaxf(0, volume));
    if (m->audio) audio_player_set_volume(m->audio, m->volume);
}
float GetMovieVolume(const QuickTimeMovie *m) { return m ? m->volume : 0; }
int MovieHardwareDecoded(const QuickTimeMovie *m) { return m && qt_decode_hardware(m->decoder); }
const char *GetMovieCodec(const QuickTimeMovie *m) { return m ? qt_decode_codec(m->decoder) : "none"; }
