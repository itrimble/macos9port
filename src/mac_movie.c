#include "mac_movie_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_SAMPLES = 8192, MAX_RUNS = 64, MAX_W = 640, MAX_H = 480 };
enum { KIND_NONE = 0, KIND_VIDEO = 1, KIND_SOUND = 2 };

typedef struct {
    char err[160];
    uint32_t movie_ts, movie_dur, media_ts, media_dur;
    uint32_t pending_ts, pending_dur;
    uint16_t width, height, depth, pending_w, pending_h;
    uint32_t codec;
    int track_kind;
    int have_video;
    int mdat_set;
    uint32_t mdat_off, mdat_len;
    uint32_t sample_count, uniform;
    uint32_t *sizes;
    uint32_t chunk_count;
    uint32_t *chunks;
    uint32_t stts_count, stts_n[MAX_RUNS], stts_d[MAX_RUNS];
    uint32_t stsc_count, stsc_first[MAX_RUNS], stsc_spc[MAX_RUNS];
} Parse;

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t fourcc(char a, char b, char c, char d) {
    return ((uint32_t)(uint8_t)a << 24) | ((uint32_t)(uint8_t)b << 16) |
           ((uint32_t)(uint8_t)c << 8) | (uint32_t)(uint8_t)d;
}

static int fail(Parse *p, const char *msg) {
    if (!p->err[0]) snprintf(p->err, sizeof p->err, "%s", msg);
    return -1;
}

static void clear_tables(Parse *p) {
    free(p->sizes);
    free(p->chunks);
    p->sizes = NULL;
    p->chunks = NULL;
    p->sample_count = 0;
    p->uniform = 0;
    p->chunk_count = 0;
    p->stts_count = 0;
    p->stsc_count = 0;
    p->codec = 0;
    p->width = 0;
    p->height = 0;
    p->depth = 0;
}

static int skip_media(const Parse *p) {
    return p->have_video || p->track_kind == KIND_SOUND;
}

static int version0(Parse *p, const uint8_t *body, size_t n, size_t need) {
    if (n < need) return fail(p, "That movie is cut off.");
    if (body[0] != 0) return fail(p, "OS999 only reads version 0 movie headers.");
    return 0;
}

static int read_stsd(Parse *p, const uint8_t *body, size_t n) {
    if (skip_media(p)) return 0;
    if (version0(p, body, n, 16)) return -1;
    uint32_t entry = rd32(body + 8);
    /* n >= 16 here; compare in size_t so a huge entry cannot wrap past the check. */
    if (entry < 16 || (size_t)entry > n - 8) return fail(p, "That movie is cut off.");
    p->codec = rd32(body + 12);
    if (entry >= 86 && n >= 92) {
        p->width = rd16(body + 40);
        p->height = rd16(body + 42);
        p->depth = rd16(body + 90);
    }
    p->track_kind = KIND_VIDEO;
    return 0;
}

static int read_runs(Parse *p, const uint8_t *body, size_t n, int pairs) {
    if (skip_media(p)) return 0;
    if (version0(p, body, n, 8)) return -1;
    uint32_t count = rd32(body + 4);
    size_t stride = pairs ? 8u : 12u;
    if (count == 0 || count > MAX_RUNS || n < 8u + count * stride) return fail(p, "That movie's header is damaged.");
    if (pairs) {
        if (p->stts_count) return fail(p, "That movie's header is damaged.");
        for (uint32_t i = 0; i < count; i++) {
            p->stts_n[i] = rd32(body + 8 + i * 8);
            p->stts_d[i] = rd32(body + 12 + i * 8);
            if (p->stts_n[i] == 0) return fail(p, "That movie's header is damaged.");
        }
        p->stts_count = count;
        return 0;
    }
    if (p->stsc_count) return fail(p, "That movie's header is damaged.");
    uint32_t prev = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t first = rd32(body + 8 + i * 12);
        uint32_t spc = rd32(body + 12 + i * 12);
        if (first <= prev || spc == 0) return fail(p, "That movie's header is damaged.");
        p->stsc_first[i] = first;
        p->stsc_spc[i] = spc;
        prev = first;
    }
    p->stsc_count = count;
    return 0;
}

static int read_stsz(Parse *p, const uint8_t *body, size_t n) {
    if (skip_media(p)) return 0;
    if (version0(p, body, n, 12)) return -1;
    if (p->sample_count) return fail(p, "That movie's header is damaged.");
    uint32_t uniform = rd32(body + 4);
    uint32_t count = rd32(body + 8);
    if (count == 0 || count > MAX_SAMPLES) return fail(p, "That movie has too many frames.");
    p->sample_count = count;
    p->uniform = uniform;
    if (uniform) return 0;
    if (n < 12u + count * 4u) return fail(p, "That movie is cut off.");
    p->sizes = calloc(count, sizeof(uint32_t));
    if (!p->sizes) return fail(p, "OS999 could not keep that movie in memory.");
    for (uint32_t i = 0; i < count; i++) {
        p->sizes[i] = rd32(body + 12 + i * 4);
        if (p->sizes[i] == 0) return fail(p, "That movie's header is damaged.");
    }
    return 0;
}

static int read_stco(Parse *p, const uint8_t *body, size_t n) {
    if (skip_media(p)) return 0;
    if (version0(p, body, n, 8)) return -1;
    if (p->chunk_count) return fail(p, "That movie's header is damaged.");
    uint32_t count = rd32(body + 4);
    if (count == 0 || count > MAX_SAMPLES || n < 8u + count * 4u) return fail(p, "That movie's header is damaged.");
    p->chunks = calloc(count, sizeof(uint32_t));
    if (!p->chunks) return fail(p, "OS999 could not keep that movie in memory.");
    for (uint32_t i = 0; i < count; i++) p->chunks[i] = rd32(body + 8 + i * 4);
    p->chunk_count = count;
    return 0;
}

static int walk(Parse *p, const uint8_t *buf, size_t len, uint64_t base, int depth) {
    if (depth > 8) return fail(p, "That movie's header is damaged.");
    size_t pos = 0;
    while (pos < len) {
        if (len - pos < 8) return fail(p, "That movie is cut off.");
        uint64_t sz = rd32(buf + pos);
        uint32_t type = rd32(buf + pos + 4);
        size_t header = 8;
        if (sz == 1) {
            if (len - pos < 16) return fail(p, "That movie is cut off.");
            sz = ((uint64_t)rd32(buf + pos + 8) << 32) | rd32(buf + pos + 12);
            header = 16;
        } else if (sz == 0) {
            sz = len - pos;
        }
        if (sz < header || sz > len - pos) return fail(p, "That movie's header is damaged.");
        const uint8_t *body = buf + pos + header;
        size_t body_len = (size_t)(sz - header);
        uint64_t child = base + pos + header;
        if (type == fourcc('t', 'r', 'a', 'k')) {
            int saved = p->track_kind;
            uint32_t ts = p->pending_ts, dur = p->pending_dur;
            uint16_t pw = p->pending_w, ph = p->pending_h;
            p->track_kind = KIND_NONE;
            p->pending_ts = p->pending_dur = 0;
            p->pending_w = p->pending_h = 0;
            if (walk(p, body, body_len, child, depth + 1)) return -1;
            if (p->track_kind == KIND_SOUND && !p->have_video) clear_tables(p);
            else if (!p->have_video && (p->track_kind == KIND_VIDEO || p->codec)) {
                p->have_video = 1;
                if (p->pending_ts) {
                    p->media_ts = p->pending_ts;
                    p->media_dur = p->pending_dur;
                }
                if (!p->width) {
                    p->width = p->pending_w;
                    p->height = p->pending_h;
                }
            }
            p->track_kind = saved;
            p->pending_ts = ts;
            p->pending_dur = dur;
            p->pending_w = pw;
            p->pending_h = ph;
        } else if (type == fourcc('m', 'o', 'o', 'v') || type == fourcc('m', 'd', 'i', 'a') ||
                   type == fourcc('m', 'i', 'n', 'f') || type == fourcc('s', 't', 'b', 'l')) {
            if (walk(p, body, body_len, child, depth + 1)) return -1;
        } else if (type == fourcc('m', 'v', 'h', 'd')) {
            if (version0(p, body, body_len, 20)) return -1;
            p->movie_ts = rd32(body + 12);
            p->movie_dur = rd32(body + 16);
        } else if (type == fourcc('t', 'k', 'h', 'd')) {
            if (version0(p, body, body_len, 84)) return -1;
            p->pending_w = (uint16_t)(rd32(body + 76) >> 16);
            p->pending_h = (uint16_t)(rd32(body + 80) >> 16);
        } else if (type == fourcc('m', 'd', 'h', 'd')) {
            if (version0(p, body, body_len, 20)) return -1;
            p->pending_ts = rd32(body + 12);
            p->pending_dur = rd32(body + 16);
        } else if (type == fourcc('h', 'd', 'l', 'r')) {
            if (version0(p, body, body_len, 12)) return -1;
            uint32_t sub = rd32(body + 8);
            if (!p->have_video && sub == fourcc('v', 'i', 'd', 'e')) p->track_kind = KIND_VIDEO;
            else if (sub == fourcc('s', 'o', 'u', 'n')) p->track_kind = KIND_SOUND;
        } else if (type == fourcc('s', 't', 's', 'd')) {
            if (read_stsd(p, body, body_len)) return -1;
        } else if (type == fourcc('s', 't', 't', 's')) {
            if (read_runs(p, body, body_len, 1)) return -1;
        } else if (type == fourcc('s', 't', 's', 'c')) {
            if (read_runs(p, body, body_len, 0)) return -1;
        } else if (type == fourcc('s', 't', 's', 'z')) {
            if (read_stsz(p, body, body_len)) return -1;
        } else if (type == fourcc('s', 't', 'c', 'o')) {
            if (read_stco(p, body, body_len)) return -1;
        } else if (type == fourcc('c', 'o', '6', '4')) {
            return fail(p, "OS999 does not read 64-bit chunk offsets.");
        } else if (type == fourcc('m', 'd', 'a', 't')) {
            if (p->mdat_set) return fail(p, "OS999 plays movies with one media block.");
            if (child > 0xffffffffu || body_len > 0xffffffffu) return fail(p, "That movie is larger than 8 MB.");
            p->mdat_off = (uint32_t)child;
            p->mdat_len = (uint32_t)body_len;
            p->mdat_set = 1;
        }
        pos += (size_t)sz;
    }
    return 0;
}

static int build_tables(Parse *p, MacMovie *m, size_t len) {
    if (!p->have_video || !p->sample_count || !p->stts_count || !p->chunk_count || !p->mdat_set)
        return fail(p, "That movie has no video track.");
    if (!p->movie_ts) return fail(p, "That movie has no timescale.");
    uint32_t media_ts = p->media_ts ? p->media_ts : p->movie_ts;
    if (p->uniform) {
        p->sizes = calloc(p->sample_count, sizeof(uint32_t));
        if (!p->sizes) return fail(p, "OS999 could not keep that movie in memory.");
        for (uint32_t i = 0; i < p->sample_count; i++) p->sizes[i] = p->uniform;
    }
    m->sizes = p->sizes;
    p->sizes = NULL;
    m->offs = calloc(p->sample_count, sizeof(uint32_t));
    m->dts = calloc(p->sample_count, sizeof(uint32_t));
    if (!m->offs || !m->dts) return fail(p, "OS999 could not keep that movie in memory.");
    uint32_t n = 0;
    uint64_t media_t = 0;
    for (uint32_t e = 0; e < p->stts_count; e++) {
        if (p->stts_n[e] > p->sample_count - n) return fail(p, "That movie's header is damaged.");
        for (uint32_t i = 0; i < p->stts_n[e]; i++) {
            uint64_t movie_t = media_t * p->movie_ts / media_ts;
            if (movie_t > 0xffffffffu) return fail(p, "That movie's header is damaged.");
            m->dts[n++] = (uint32_t)movie_t;
            if (p->stts_d[e] > 0xffffffffu - media_t) return fail(p, "That movie's header is damaged.");
            media_t += p->stts_d[e];
        }
    }
    if (n != p->sample_count) return fail(p, "That movie's header is damaged.");
    uint64_t end = media_t * p->movie_ts / media_ts;
    if (p->movie_dur) m->duration = p->movie_dur;
    else if (end > 0 && end <= 0xffffffffu) m->duration = (uint32_t)end;
    else return fail(p, "That movie has no timescale.");
    if (p->stsc_count == 0) {
        if (p->chunk_count != p->sample_count) return fail(p, "That movie's sample table is incomplete.");
        p->stsc_count = 1;
        p->stsc_first[0] = 1;
        p->stsc_spc[0] = 1;
    }
    uint32_t produced = 0;
    for (uint32_t e = 0; e < p->stsc_count; e++) {
        uint32_t next = e + 1 < p->stsc_count ? p->stsc_first[e + 1] : p->chunk_count + 1;
        if (next < p->stsc_first[e]) return fail(p, "That movie's header is damaged.");
        for (uint32_t chunk = p->stsc_first[e]; chunk < next; chunk++) {
            if (chunk == 0 || chunk > p->chunk_count) return fail(p, "That movie's header is damaged.");
            uint64_t off = p->chunks[chunk - 1];
            for (uint32_t s = 0; s < p->stsc_spc[e]; s++) {
                if (produced >= p->sample_count) return fail(p, "That movie's header is damaged.");
                uint32_t sz = m->sizes[produced];
                uint64_t mdat_end = (uint64_t)p->mdat_off + p->mdat_len;
                if (sz == 0 || off < p->mdat_off || off > mdat_end || sz > mdat_end - off || off > len ||
                    sz > len - off)
                    return fail(p, "That movie's samples sit outside its media block.");
                m->offs[produced++] = (uint32_t)off;
                off += sz;
            }
        }
    }
    if (produced != p->sample_count) return fail(p, "That movie's sample table is incomplete.");
    m->sample_count = p->sample_count;
    m->timescale = p->movie_ts;
    m->width = p->width;
    m->height = p->height;
    m->depth = p->depth;
    m->codec = p->codec;
    int raw = m->codec == MAC_MOVIE_CODEC_RAW && m->depth == 32 && m->width > 0 && m->height > 0 &&
              m->width <= MAX_W && m->height <= MAX_H;
    uint32_t need = raw ? (uint32_t)m->width * m->height * 4u : 0;
    if (raw) {
        for (uint32_t i = 0; i < m->sample_count; i++) {
            if (m->sizes[i] != need) raw = 0;
        }
    }
    m->can_draw = raw;
    if (!raw) return 0;
    m->pixels = calloc((size_t)m->width * m->height, sizeof(uint32_t));
    if (!m->pixels) return fail(p, "OS999 could not keep that movie in memory.");
    return mac_movie_show(m) < 0 ? fail(p, "That movie's frames do not match its picture.") : 0;
}

static void parse_free(Parse *p) {
    free(p->sizes);
    free(p->chunks);
    p->sizes = NULL;
    p->chunks = NULL;
}

static void set_err(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg ? msg : "");
}

MacMovie *mac_movie_open(const uint8_t *bytes, size_t len, char *err, size_t err_n) {
    if (len > MAC_MOVIE_MAX_BYTES) {
        set_err(err, err_n, "That movie is larger than 8 MB.");
        return NULL;
    }
    if (!bytes || len < 8) {
        set_err(err, err_n, "That movie is cut off.");
        return NULL;
    }
    Parse p;
    memset(&p, 0, sizeof p);
    MacMovie *m = calloc(1, sizeof *m);
    if (!m) {
        set_err(err, err_n, "OS999 could not keep that movie in memory.");
        return NULL;
    }
    m->bytes = malloc(len);
    if (!m->bytes) {
        mac_movie_close(m);
        set_err(err, err_n, "OS999 could not keep that movie in memory.");
        return NULL;
    }
    memcpy(m->bytes, bytes, len);
    m->len = len;
    m->state = MAC_MOVIE_STOPPED;
    if (walk(&p, m->bytes, len, 0, 0) || build_tables(&p, m, len)) {
        set_err(err, err_n, p.err[0] ? p.err : "Could not read that movie.");
        parse_free(&p);
        mac_movie_close(m);
        return NULL;
    }
    parse_free(&p);
    set_err(err, err_n, "");
    return m;
}

void mac_movie_close(MacMovie *movie) {
    if (!movie) return;
    free(movie->bytes);
    free(movie->sizes);
    free(movie->offs);
    free(movie->dts);
    free(movie->pixels);
    free(movie);
}

int mac_movie_show(MacMovie *movie) {
    if (!movie || !movie->sample_count) return -1;
    uint32_t idx = movie->sample_count - 1;
    if (movie->time < movie->duration) {
        uint32_t off = 0, sz = 0;
        if (mac_movie_sample_at(movie, movie->time, &idx, &off, &sz) != 0) return -1;
    }
    if (movie->have_frame && movie->shown == idx) return 0;
    movie->shown = idx;
    if (!movie->can_draw) {
        movie->have_frame = 0;
        return 0;
    }
    uint32_t need = (uint32_t)movie->width * movie->height * 4u;
    if (movie->sizes[idx] != need || movie->offs[idx] > movie->len || need > movie->len - movie->offs[idx])
        return -1;
    const uint8_t *s = movie->bytes + movie->offs[idx];
    uint32_t count = (uint32_t)movie->width * movie->height;
    for (uint32_t i = 0; i < count; i++) {
        movie->pixels[i] = 0xff000000u | ((uint32_t)s[i * 4 + 1] << 16) | ((uint32_t)s[i * 4 + 2] << 8) |
                           s[i * 4 + 3];
    }
    movie->have_frame = 1;
    return 1;
}

int mac_movie_can_draw(const MacMovie *movie) { return movie && movie->can_draw; }
uint32_t mac_movie_codec(const MacMovie *movie) { return movie ? movie->codec : 0; }
int mac_movie_width(const MacMovie *movie) { return movie ? movie->width : 0; }
int mac_movie_height(const MacMovie *movie) { return movie ? movie->height : 0; }
uint16_t mac_movie_depth(const MacMovie *movie) { return movie ? movie->depth : 0; }
uint32_t mac_movie_timescale(const MacMovie *movie) { return movie ? movie->timescale : 0; }
uint32_t mac_movie_duration(const MacMovie *movie) { return movie ? movie->duration : 0; }
uint32_t mac_movie_sample_count(const MacMovie *movie) { return movie ? movie->sample_count : 0; }
uint32_t mac_movie_time(const MacMovie *movie) { return movie ? movie->time : 0; }
int mac_movie_state(const MacMovie *movie) { return movie ? movie->state : MAC_MOVIE_STOPPED; }
int mac_movie_dropped(const MacMovie *movie) { return movie ? movie->dropped : 0; }

void mac_movie_codec_chars(const MacMovie *movie, char out[5]) {
    uint32_t c = mac_movie_codec(movie);
    for (int i = 0; i < 4; i++) {
        unsigned char ch = (unsigned char)(c >> (24 - 8 * i));
        out[i] = (ch >= 32 && ch < 127) ? (char)ch : '?';
    }
    out[4] = 0;
}

const uint32_t *mac_movie_pixels(const MacMovie *movie, int *width, int *height) {
    if (width) *width = movie ? movie->width : 0;
    if (height) *height = movie ? movie->height : 0;
    if (!movie || !movie->have_frame || !movie->pixels) return NULL;
    return movie->pixels;
}

int mac_movie_sample_at(const MacMovie *movie, uint32_t tick, uint32_t *index, uint32_t *offset,
                        uint32_t *size) {
    if (!movie || !index || !offset || !size || !movie->sample_count || !movie->dts || tick >= movie->duration)
        return -1;
    uint32_t lo = 0, hi = movie->sample_count;
    while (lo + 1 < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (movie->dts[mid] <= tick) lo = mid;
        else hi = mid;
    }
    if (movie->dts[lo] > tick) return -1;
    *index = lo;
    *offset = movie->offs[lo];
    *size = movie->sizes[lo];
    return 0;
}
