#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define QT_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

typedef struct {
    uint32_t timescale;
    uint32_t duration;
    uint16_t width;
    uint16_t height;
    uint32_t codec;
    uint32_t sample_count;
    uint32_t sample_delta;
    uint32_t sample_sizes[8];
    uint32_t chunk_offsets[8];
    uint32_t mdat_offset;
    uint32_t mdat_size;
} QtMovieInfo;

static int is_container_atom(uint32_t type) {
    return type == QT_FOURCC('m', 'o', 'o', 'v') ||
           type == QT_FOURCC('t', 'r', 'a', 'k') ||
           type == QT_FOURCC('m', 'd', 'i', 'a') ||
           type == QT_FOURCC('m', 'i', 'n', 'f') ||
           type == QT_FOURCC('s', 't', 'b', 'l');
}

static int qt_parse_atoms(const uint8_t *buf, size_t len, size_t base_off,
                          int depth, QtMovieInfo *info) {
    if (!buf || !info || depth > 8) return -1;
    size_t pos = 0;
    while (pos < len) {
        if (len - pos < 8) return -1;
        uint32_t sz = get32(buf + pos);
        uint32_t type = get32(buf + pos + 4);
        if (sz < 8 || (size_t)sz > len - pos) return -1;

        const uint8_t *body = buf + pos + 8;
        size_t body_len = sz - 8;
        if (is_container_atom(type)) {
            if (qt_parse_atoms(body, body_len, base_off + pos + 8, depth + 1, info) != 0)
                return -1;
        } else if (type == QT_FOURCC('m', 'v', 'h', 'd')) {
            if (body_len < 20) return -1;
            info->timescale = get32(body + 12);
            info->duration = get32(body + 16);
        } else if (type == QT_FOURCC('t', 'k', 'h', 'd')) {
            if (body_len < 84) return -1;
            info->width = (uint16_t)(get32(body + 76) >> 16);
            info->height = (uint16_t)(get32(body + 80) >> 16);
        } else if (type == QT_FOURCC('s', 't', 's', 'd')) {
            if (body_len < 16) return -1;
            info->codec = get32(body + 12);
        } else if (type == QT_FOURCC('s', 't', 't', 's')) {
            if (body_len < 16 || get32(body + 4) != 1) return -1;
            info->sample_count = get32(body + 8);
            info->sample_delta = get32(body + 12);
        } else if (type == QT_FOURCC('s', 't', 's', 'z')) {
            if (body_len < 12) return -1;
            uint32_t count = get32(body + 8);
            if (count > 8 || body_len < 12u + count * 4u) return -1;
            for (uint32_t i = 0; i < count; i++) {
                info->sample_sizes[i] = get32(body + 12 + i * 4);
            }
        } else if (type == QT_FOURCC('s', 't', 'c', 'o')) {
            if (body_len < 8) return -1;
            uint32_t count = get32(body + 4);
            if (count > 8 || body_len < 8u + count * 4u) return -1;
            for (uint32_t i = 0; i < count; i++) {
                info->chunk_offsets[i] = get32(body + 8 + i * 4);
            }
        } else if (type == QT_FOURCC('m', 'd', 'a', 't')) {
            info->mdat_offset = (uint32_t)(base_off + pos + 8);
            info->mdat_size = (uint32_t)body_len;
        }
        pos += sz;
    }
    return 0;
}

static int qt_validate_movie(const uint8_t *buf, size_t len, QtMovieInfo *out) {
    memset(out, 0, sizeof(*out));
    if (qt_parse_atoms(buf, len, 0, 0, out) != 0) return -1;
    if (out->timescale == 0 || out->sample_count == 0 || out->mdat_size == 0) return -1;
    for (uint32_t i = 0; i < out->sample_count; i++) {
        uint32_t off = out->chunk_offsets[i];
        uint32_t sz = out->sample_sizes[i];
        if (off < out->mdat_offset || off + sz > out->mdat_offset + out->mdat_size)
            return -1;
    }
    return 0;
}

static size_t build_synthetic_mov(uint8_t *buf, size_t cap) {
    if (cap < 512) return 0;
    memset(buf, 0, cap);

    /* Leaf atoms inside stbl: stsd (24B), stts (24B), stsz (32B), stco (28B) -> 108B */
    uint8_t stbl[8 + 108];
    memset(stbl, 0, sizeof(stbl));
    put32(stbl + 0, sizeof(stbl));
    memcpy(stbl + 4, "stbl", 4);
    size_t p = 8;
    put32(stbl + p + 0, 24); memcpy(stbl + p + 4, "stsd", 4);
    put32(stbl + p + 12, 1); memcpy(stbl + p + 20, "rpza", 4);
    p += 24;
    put32(stbl + p + 0, 24); memcpy(stbl + p + 4, "stts", 4);
    put32(stbl + p + 12, 1); put32(stbl + p + 16, 3); put32(stbl + p + 20, 600);
    p += 24;
    put32(stbl + p + 0, 32); memcpy(stbl + p + 4, "stsz", 4);
    put32(stbl + p + 16, 3);
    put32(stbl + p + 20, 16); put32(stbl + p + 24, 20); put32(stbl + p + 28, 24);
    p += 32;

    /* mdat starts after moov: moov size is 8 + mvhd(28) + trak(8 + tkhd(92) + mdia(8 + minf(8 + stbl(116)))) = 268 */
    uint32_t mdat_payload_off = 268 + 8;
    put32(stbl + p + 0, 28); memcpy(stbl + p + 4, "stco", 4);
    put32(stbl + p + 12, 3);
    put32(stbl + p + 16, mdat_payload_off + 0);
    put32(stbl + p + 20, mdat_payload_off + 16);
    put32(stbl + p + 24, mdat_payload_off + 36);

    /* Wrap stbl -> minf -> mdia -> trak -> moov + mdat */
    put32(buf + 0, 268); memcpy(buf + 4, "moov", 4);
    put32(buf + 8, 28);  memcpy(buf + 12, "mvhd", 4);
    put32(buf + 28, 600); put32(buf + 32, 1800);

    put32(buf + 36, 232); memcpy(buf + 40, "trak", 4);
    put32(buf + 44, 92);  memcpy(buf + 48, "tkhd", 4);
    put32(buf + 128, 320u << 16); put32(buf + 132, 240u << 16);

    put32(buf + 136, 132); memcpy(buf + 140, "mdia", 4);
    put32(buf + 144, 124); memcpy(buf + 148, "minf", 4);
    memcpy(buf + 152, stbl, sizeof(stbl));

    put32(buf + 268, 8 + 60); memcpy(buf + 272, "mdat", 4);
    for (int i = 0; i < 60; i++) buf[276 + i] = (uint8_t)(0x40 + i);
    return 268 + 68;
}

typedef enum { QT_STOPPED = 0, QT_PLAYING, QT_PAUSED, QT_ENDED } QtState;

typedef struct {
    QtState state;
    uint32_t current_tick;
    int dropped_frames;
} QtPlayer;

static int qt_lookup_sample(const QtMovieInfo *info, uint32_t tick,
                            uint32_t *out_idx, uint32_t *out_off, uint32_t *out_sz) {
    if (!info || tick >= info->duration || info->sample_delta == 0) return -1;
    uint32_t idx = tick / info->sample_delta;
    if (idx >= info->sample_count) return -1;
    *out_idx = idx;
    *out_off = info->chunk_offsets[idx];
    *out_sz = info->sample_sizes[idx];
    return 0;
}

static void qt_sync_step(QtPlayer *pl, const QtMovieInfo *info,
                         uint32_t video_ms, uint32_t audio_ms) {
    if (pl->state != QT_PLAYING) return;
    int32_t drift_ms = (int32_t)audio_ms - (int32_t)video_ms;
    if (drift_ms > 20) {
        pl->dropped_frames++;
        pl->current_tick = (audio_ms * info->timescale) / 1000u;
    } else {
        pl->current_tick = (video_ms * info->timescale) / 1000u;
    }
    if (pl->current_tick >= info->duration) {
        pl->current_tick = info->duration;
        pl->state = QT_ENDED;
    }
}

static void test_quicktime_parser_and_playback(void) {
    uint8_t mov[512];
    size_t len = build_synthetic_mov(mov, sizeof(mov));
    CHECK(len == 336);

    QtMovieInfo info;
    CHECK(qt_validate_movie(mov, len, &info) == 0);
    CHECK(info.timescale == 600 && info.duration == 1800);
    CHECK(info.width == 320 && info.height == 240);
    CHECK(info.codec == QT_FOURCC('r', 'p', 'z', 'a'));
    CHECK(info.sample_count == 3);

    uint32_t idx = 0, off = 0, sz = 0;
    CHECK(qt_lookup_sample(&info, 0, &idx, &off, &sz) == 0 && idx == 0 && sz == 16);
    CHECK(qt_lookup_sample(&info, 750, &idx, &off, &sz) == 0 && idx == 1 && sz == 20);
    CHECK(qt_lookup_sample(&info, 1500, &idx, &off, &sz) == 0 && idx == 2 && sz == 24);
    CHECK(qt_lookup_sample(&info, 1800, &idx, &off, &sz) == -1);

    /* Transport & A/V sync drift budget checks. */
    QtPlayer pl = { .state = QT_STOPPED, .current_tick = 0, .dropped_frames = 0 };
    pl.state = QT_PLAYING;
    qt_sync_step(&pl, &info, 1000, 1010); /* 10ms drift <= 20ms budget: no drop */
    CHECK(pl.dropped_frames == 0 && pl.current_tick == 600);
    qt_sync_step(&pl, &info, 1050, 1100); /* 50ms lag > 20ms budget: drop frame & resync to audio */
    CHECK(pl.dropped_frames == 1 && pl.current_tick == 660);
    pl.state = QT_PAUSED;
    qt_sync_step(&pl, &info, 2000, 2000);
    CHECK(pl.current_tick == 660);
    pl.state = QT_PLAYING;
    qt_sync_step(&pl, &info, 3000, 3000);
    CHECK(pl.state == QT_ENDED && pl.current_tick == 1800);

    /* Corrupt atom size exceeding buffer must fail closed. */
    put32(mov + 0, 9999);
    CHECK(qt_validate_movie(mov, len, &info) == -1);
}

int main(void) {
    test_quicktime_parser_and_playback();

    if (failures > 0) {
        fprintf(stderr, "test_mac_quicktime: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_quicktime: all %d checks passed\n", checks);
    return 0;
}
