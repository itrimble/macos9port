#ifndef ST_INT_H
#define ST_INT_H

#include "os999_storage.h"

#include <stdint.h>
#include <string.h>

static inline uint16_t st_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t st_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t st_be64(const uint8_t *p) {
    return ((uint64_t)st_be32(p) << 32) | st_be32(p + 4);
}
static inline uint16_t st_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t st_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t st_le64(const uint8_t *p) {
    return st_le32(p) | ((uint64_t)st_le32(p + 4) << 32);
}
static inline void st_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void st_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline void st_put64(uint8_t *p, uint64_t v) { st_put32(p, (uint32_t)(v >> 32)); st_put32(p + 4, (uint32_t)v); }
static inline void st_putle16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void st_putle32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* True when [off, off+len) lies inside n bytes, without overflow. */
static inline int st_span(size_t n, uint64_t off, uint64_t len) {
    return off <= n && len <= n - off;
}

/* Copy a fork into b, respecting limits. */
int st_take(StBytes *b, const uint8_t *src, size_t n, const StLimits *lim, StErr *err);
/* Fill a StMacFile name from a MacRoman Pascal-style name. */
int st_name_from_macroman(StMacFile *f, const uint8_t *name, size_t n, StErr *err);
/* Unix (1970) <-> Mac (1904) seconds; Mac epoch difference. */
#define ST_MAC_EPOCH_DELTA 2082844800u

/* MSB-first bit reader and LSB-first bit reader over a byte span. */
typedef struct {
    const uint8_t *p;
    size_t n, pos; /* pos in bits */
} StBits;
static inline void st_bits_init(StBits *b, const uint8_t *p, size_t n) { b->p = p; b->n = n; b->pos = 0; }
static inline int st_bits_left(const StBits *b) { return b->pos < (uint64_t)b->n * 8; }
/* Reading past the end yields zero bits; callers check st_bits_over. */
static inline uint32_t st_bits_msb(StBits *b, int count) {
    uint32_t v = 0;
    for (int i = 0; i < count; i++) {
        size_t byte = b->pos >> 3;
        uint32_t bit = byte < b->n ? (b->p[byte] >> (7 - (b->pos & 7))) & 1u : 0;
        v = (v << 1) | bit;
        b->pos++;
    }
    return v;
}
static inline uint32_t st_bits_lsb(StBits *b, int count) {
    uint32_t v = 0;
    for (int i = 0; i < count; i++) {
        size_t byte = b->pos >> 3;
        uint32_t bit = byte < b->n ? (b->p[byte] >> (b->pos & 7)) & 1u : 0;
        v |= bit << i;
        b->pos++;
    }
    return v;
}
static inline int st_bits_over(const StBits *b) { return b->pos > (uint64_t)b->n * 8; }

/* Canonical-ish Huffman decoder built from code lengths (MSB-first codes,
 * shorter codes first, ties by symbol). max 16 bits, up to 1024 symbols. */
typedef struct {
    uint16_t count[17];
    uint16_t symbol[1024];
    int nsym;
} StHuff;
int st_huff_build(StHuff *h, const uint8_t *lengths, int nsym);
/* MSB-first read; returns symbol or -1. lsb=1 reads codes LSB-first. */
int st_huff_read(const StHuff *h, StBits *b, int lsb);

#endif
