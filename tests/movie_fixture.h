#ifndef MOVIE_FIXTURE_H
#define MOVIE_FIXTURE_H

#include "mac_movie.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Builds a version-0 classic movie container in memory. The sample entry's
 * first field is its own size (86), which is where the parser reads it. */
typedef struct {
    uint8_t data[1536];
    size_t len;
    size_t timescale_at;
    size_t stco_at;
    size_t stco2_at;
} MovieFixture;

typedef struct {
    uint8_t *p;
    size_t n;
    size_t cap;
    int err;
} MovieBuf;

static void mb_need(MovieBuf *b, size_t k) {
    if (b->err || k > b->cap - b->n) b->err = 1;
}

static void mb_u8(MovieBuf *b, uint8_t v) {
    mb_need(b, 1);
    if (!b->err) b->p[b->n++] = v;
}

static void mb_u16(MovieBuf *b, uint16_t v) {
    mb_u8(b, (uint8_t)(v >> 8));
    mb_u8(b, (uint8_t)v);
}

static void mb_u32(MovieBuf *b, uint32_t v) {
    mb_u8(b, (uint8_t)(v >> 24));
    mb_u8(b, (uint8_t)(v >> 16));
    mb_u8(b, (uint8_t)(v >> 8));
    mb_u8(b, (uint8_t)v);
}

static void mb_gap(MovieBuf *b, size_t n) {
    mb_need(b, n);
    if (b->err) return;
    memset(b->p + b->n, 0, n);
    b->n += n;
}

static int mb_begin(MovieBuf *b, const char *type) {
    size_t at = b->n;
    mb_need(b, 8);
    if (b->err) return -1;
    b->n += 8;
    memcpy(b->p + at + 4, type, 4);
    return (int)at;
}

static void mb_end(MovieBuf *b, int at) {
    if (b->err || at < 0) return;
    uint32_t sz = (uint32_t)(b->n - (size_t)at);
    b->p[at] = (uint8_t)(sz >> 24);
    b->p[at + 1] = (uint8_t)(sz >> 16);
    b->p[at + 2] = (uint8_t)(sz >> 8);
    b->p[at + 3] = (uint8_t)sz;
}

static void mb_frame(MovieBuf *b, uint8_t r, uint8_t g, uint8_t bl) {
    for (int i = 0; i < 4; i++) {
        mb_u8(b, 0);
        mb_u8(b, r);
        mb_u8(b, g);
        mb_u8(b, bl);
    }
}

static void mb_stsd(MovieBuf *b, uint32_t codec, uint16_t depth) {
    int at = mb_begin(b, "stsd");
    mb_u32(b, 0);
    mb_u32(b, 1);
    mb_u32(b, 86);
    mb_u32(b, codec);
    mb_gap(b, 6);
    mb_u16(b, 1);
    mb_gap(b, 16);
    mb_u16(b, 2);
    mb_u16(b, 2);
    mb_u32(b, 0x00480000u);
    mb_u32(b, 0x00480000u);
    mb_u32(b, 0);
    mb_u16(b, 1);
    mb_gap(b, 32);
    mb_u16(b, depth);
    mb_u16(b, 0xffffu);
    mb_end(b, at);
}

/* two_chunk uses one chunk of two frames and a second chunk after an 8-byte gap. */
static int movie_fixture_build(MovieFixture *out, int two_chunk, uint32_t codec, uint16_t depth) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    MovieBuf b = {out->data, 0, sizeof out->data, 0};
    int moov = mb_begin(&b, "moov");
    int mvhd = mb_begin(&b, "mvhd");
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    out->timescale_at = b.n;
    mb_u32(&b, 600);
    mb_u32(&b, 1800);
    mb_u32(&b, 0x00010000u);
    mb_u16(&b, 0x0100u);
    mb_gap(&b, 10);
    mb_gap(&b, 36);
    mb_gap(&b, 24);
    mb_u32(&b, 2);
    mb_end(&b, mvhd);
    int trak = mb_begin(&b, "trak");
    int tkhd = mb_begin(&b, "tkhd");
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    mb_u32(&b, 1);
    mb_u32(&b, 0);
    mb_u32(&b, 1800);
    mb_gap(&b, 8);
    mb_u16(&b, 0);
    mb_u16(&b, 0);
    mb_u16(&b, 0);
    mb_u16(&b, 0);
    mb_gap(&b, 36);
    mb_u32(&b, 2u << 16);
    mb_u32(&b, 2u << 16);
    mb_end(&b, tkhd);
    int mdia = mb_begin(&b, "mdia");
    int mdhd = mb_begin(&b, "mdhd");
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    mb_u32(&b, 0);
    mb_u32(&b, 600);
    mb_u32(&b, 1800);
    mb_u16(&b, 0);
    mb_u16(&b, 0);
    mb_end(&b, mdhd);
    int hdlr = mb_begin(&b, "hdlr");
    mb_u32(&b, 0);
    mb_u32(&b, 0x6d686c72u);
    mb_u32(&b, 0x76696465u);
    mb_end(&b, hdlr);
    int minf = mb_begin(&b, "minf");
    int stbl = mb_begin(&b, "stbl");
    mb_stsd(&b, codec, depth);
    int stts = mb_begin(&b, "stts");
    mb_u32(&b, 0);
    mb_u32(&b, 1);
    mb_u32(&b, 3);
    mb_u32(&b, 600);
    mb_end(&b, stts);
    if (two_chunk) {
        int stsc = mb_begin(&b, "stsc");
        mb_u32(&b, 0);
        mb_u32(&b, 2);
        mb_u32(&b, 1);
        mb_u32(&b, 2);
        mb_u32(&b, 1);
        mb_u32(&b, 2);
        mb_u32(&b, 1);
        mb_u32(&b, 1);
        mb_end(&b, stsc);
    }
    int stsz = mb_begin(&b, "stsz");
    mb_u32(&b, 0);
    mb_u32(&b, 16);
    mb_u32(&b, 3);
    mb_end(&b, stsz);
    int stco = mb_begin(&b, "stco");
    mb_u32(&b, 0);
    mb_u32(&b, two_chunk ? 2u : 3u);
    out->stco_at = b.n;
    mb_u32(&b, 0);
    if (two_chunk) {
        out->stco2_at = b.n;
        mb_u32(&b, 0);
    }
    else {
        mb_u32(&b, 0);
        mb_u32(&b, 0);
    }
    mb_end(&b, stco);
    mb_end(&b, stbl);
    mb_end(&b, minf);
    mb_end(&b, mdia);
    mb_end(&b, trak);
    mb_end(&b, moov);
    int mdat = mb_begin(&b, "mdat");
    size_t payload = b.n;
    mb_frame(&b, 255, 0, 0);
    mb_frame(&b, 0, 255, 0);
    if (two_chunk) mb_gap(&b, 8);
    size_t third = b.n;
    mb_frame(&b, 0, 0, 255);
    mb_end(&b, mdat);
    if (b.err || out->timescale_at + 4 > b.n || out->stco_at + 4 > b.n) return -1;
    mb_u32(&(MovieBuf){out->data, out->stco_at, sizeof out->data, 0}, (uint32_t)payload);
    if (!two_chunk) {
        mb_u32(&(MovieBuf){out->data, out->stco_at + 4, sizeof out->data, 0}, (uint32_t)(payload + 16));
        mb_u32(&(MovieBuf){out->data, out->stco_at + 8, sizeof out->data, 0}, (uint32_t)(payload + 32));
    }
    else {
        mb_u32(&(MovieBuf){out->data, out->stco2_at, sizeof out->data, 0}, (uint32_t)third);
    }
    out->len = b.n;
    return b.err ? -1 : 0;
}

#endif
