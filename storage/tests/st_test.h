/* Minimal headless test harness shared by the storage tests. */
#ifndef ST_TEST_H
#define ST_TEST_H

#include "os999_storage.h"
#include "os999_unpack.h"
#include "os999_volume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int st_checks, st_failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        st_checks++;                                                                 \
        if (!(cond)) {                                                               \
            st_failures++;                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                            \
    } while (0)

#define CHECK_RC(expr, want)                                                         \
    do {                                                                             \
        StErr e_ = {0};                                                              \
        int rc_ = (expr);                                                            \
        (void)e_;                                                                    \
        st_checks++;                                                                 \
        if (rc_ != (want)) {                                                         \
            st_failures++;                                                           \
            fprintf(stderr, "FAIL %s:%d: %s returned %d (%s), wanted %d\n", __FILE__, \
                    __LINE__, #expr, rc_, st_status_text(rc_), (want));              \
        }                                                                            \
    } while (0)

static inline int st_test_done(const char *name) {
    printf("%s: %d checks, %d failures\n", name, st_checks, st_failures);
    return st_failures ? 1 : 0;
}

/* Deterministic pseudo-random bytes (xorshift32). */
static inline uint32_t st_rand(uint32_t *s) {
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static inline void st_fill(StBytes *b, size_t n, uint32_t seed, int text) {
    b->n = 0;
    st_bytes_reserve(b, n + 1);
    for (size_t i = 0; i < n; i++) {
        uint32_t r = st_rand(&seed);
        b->p[i] = text ? (uint8_t)("etaoin shrdlu\r\x90"[r % 15]) : (uint8_t)r;
    }
    b->n = n;
}

static inline void st_make_file(StMacFile *f, const char *name, size_t dn, size_t rn, uint32_t seed) {
    st_macfile_init(f);
    snprintf(f->name, sizeof f->name, "%s", name);
    st_mac_set_type_creator(f, 0x54455854u /* TEXT */, 0x74747874u /* ttxt */);
    f->finfo[8] = 0x01;
    f->finfo[9] = 0x00;
    f->fxinfo[0] = 0x12;
    f->ctime = 0xA0000000u + seed;
    f->mtime = 0xB0000000u + seed;
    st_fill(&f->data, dn, seed, 1);
    st_fill(&f->rsrc, rn, seed * 7 + 1, 0);
}

static inline int st_same_file(const StMacFile *a, const StMacFile *b) {
    return a->data.n == b->data.n && a->rsrc.n == b->rsrc.n &&
           (!a->data.n || !memcmp(a->data.p, b->data.p, a->data.n)) &&
           (!a->rsrc.n || !memcmp(a->rsrc.p, b->rsrc.p, a->rsrc.n)) && !memcmp(a->finfo, b->finfo, 10);
}

#endif
