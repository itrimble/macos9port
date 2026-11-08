#include "mac_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mac_copybits_service(MacRuntime *rt, uint16_t opcode);

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define SP 0x8000u

static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) {
    CHECK(mac_cpu_write(r->cpu, p, n, v) == 0);
}

static void write_rect(MacRuntime *r, uint32_t p, int t, int l, int b, int right) {
    wr(r, p + 0, 2, (uint16_t)t);
    wr(r, p + 2, 2, (uint16_t)l);
    wr(r, p + 4, 2, (uint16_t)b);
    wr(r, p + 6, 2, (uint16_t)right);
}

static MacRuntime setup_copybits_rt(int depth, int dst_h, int dst_w) {
    MacRuntime r = {0};
    r.cpu = mac_cpu_create(65536);
    CHECK(r.cpu != NULL);
    mac_cpu_set(r.cpu, MAC_A7, SP);
    for (unsigned p = 0x1000; p <= 0x1100; p += 0x100) {
        wr(&r, p + 0, 4, p == 0x1000 ? 0x4000 : 0x5000);
        wr(&r, p + 4, 2, depth == 8 ? 0x8008u : 2u);
        write_rect(&r, p + 6, 0, 0, 8, 8);
        if (depth == 8) {
            wr(&r, p + 32, 2, 8);
            wr(&r, p + 34, 2, 1);
            wr(&r, p + 36, 2, 8);
            wr(&r, p + 42, 4, 0x2000);
        }
    }
    wr(&r, 0x2000, 4, 0x2100);
    wr(&r, 0x2100, 4, 1024); /* equal ctSeed for direct indexed transfer */
    wr(&r, 0x2106, 2, 255);
    write_rect(&r, 0x1200, 0, 0, 4, 8);
    write_rect(&r, 0x1300, 0, 0, dst_h, dst_w);
    wr(&r, SP + 0, 4, 0);       /* maskRgn */
    wr(&r, SP + 4, 2, 0);       /* mode */
    wr(&r, SP + 6, 4, 0x1300);  /* dstRect */
    wr(&r, SP + 10, 4, 0x1200); /* srcRect */
    wr(&r, SP + 14, 4, 0x1100); /* dstBits */
    wr(&r, SP + 18, 4, 0x1000); /* srcBits */
    return r;
}

typedef enum {
    ACCEL_BACKEND_LUT_TILE = 1,
    ACCEL_BACKEND_SW_FALLBACK = 2
} AccelBackendKind;

typedef struct {
    AccelBackendKind backend;
    int require_accelerated;
    uint32_t clut[256];
    uint32_t clut_seed;
    uint32_t tile_seed[4];
    uint32_t argb_cache[64];
    char last_error[128];
} AccelBlitter;

static uint8_t transfer_byte(uint8_t src, uint8_t dst, unsigned mode) {
    uint8_t s = (mode & 4u) ? (uint8_t)~src : src;
    switch (mode & 3u) {
    case 0: return s;
    case 1: return (uint8_t)(s | dst);
    case 2: return (uint8_t)(s ^ dst);
    default: return (uint8_t)((~s) & dst);
    }
}

static int accel_blitter_check_gate(AccelBlitter *ab) {
    if (!ab) return -1;
    if (ab->require_accelerated && ab->backend == ACCEL_BACKEND_SW_FALLBACK) {
        snprintf(ab->last_error, sizeof(ab->last_error),
                 "Required accelerated blitter unavailable: fell back to software");
        return -1;
    }
    ab->last_error[0] = '\0';
    return 0;
}

static int accel_blitter_blit_8bit(AccelBlitter *ab, const uint8_t *src, uint8_t *dst,
                                   int src_w, int src_h, int dst_w, int dst_h,
                                   int mask_l, int mask_t, int mask_r, int mask_b,
                                   unsigned mode) {
    if (accel_blitter_check_gate(ab) != 0) return -1;
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        for (int x = 0; x < dst_w; x++) {
            if (x < mask_l || x >= mask_r || y < mask_t || y >= mask_b) continue;
            int sx = (x * src_w) / dst_w;
            uint8_t s = src[sy * 8 + sx];
            uint8_t d = dst[y * 8 + x];
            dst[y * 8 + x] = transfer_byte(s, d, mode);
        }
    }
    for (int t = 0; t < 4; t++) ab->tile_seed[t] = 0;
    return 0;
}

static void accel_blitter_present_argb(AccelBlitter *ab, const uint8_t *indexed_64) {
    for (int t = 0; t < 4; t++) {
        if (ab->tile_seed[t] == ab->clut_seed) continue;
        for (int i = t * 16; i < (t + 1) * 16; i++) {
            ab->argb_cache[i] = ab->clut[indexed_64[i]];
        }
        ab->tile_seed[t] = ab->clut_seed;
    }
}

static void test_differential_copybits_parity(void) {
    AccelBlitter ab = { .backend = ACCEL_BACKEND_LUT_TILE, .require_accelerated = 1 };

    /* 1. Verify all 8 transfer modes on 8-bit indexed and 1-bit monochrome surfaces. */
    for (unsigned mode = 0; mode < 8; mode++) {
        MacRuntime r = setup_copybits_rt(8, 4, 8);
        uint8_t *ram = mac_cpu_ram(r.cpu);
        uint8_t accel_dst[64];
        for (int i = 0; i < 64; i++) {
            ram[0x4000 + i] = (uint8_t)((i * 19 + 7) & 0xFF);
            ram[0x5000 + i] = (uint8_t)((i * 37 + 0x55) & 0xFF);
            accel_dst[i] = ram[0x5000 + i];
        }
        wr(&r, SP + 4, 2, mode);
        CHECK(mac_copybits_service(&r, 0xA8EC) == 1);
        CHECK(accel_blitter_blit_8bit(&ab, ram + 0x4000, accel_dst,
                                      8, 4, 8, 4, 0, 0, 8, 4, mode) == 0);
        CHECK(memcmp(ram + 0x5000, accel_dst, 32) == 0);
        mac_cpu_destroy(r.cpu);

        /* 1-bit monochrome parity check. */
        r = setup_copybits_rt(1, 4, 8);
        ram = mac_cpu_ram(r.cpu);
        memset(ram + 0x4000, 0xA5, 16);
        memset(ram + 0x5000, 0x3C, 16);
        wr(&r, SP + 4, 2, mode);
        CHECK(mac_copybits_service(&r, 0xA8EC) == 1);
        uint8_t expected = transfer_byte(0xA5, 0x3C, mode);
        CHECK(ram[0x5000] == expected && ram[0x5002] == expected);
        mac_cpu_destroy(r.cpu);
    }

    /* 2. Verify 2x vertical + horizontal scaling with a clipping maskRgn. */
    MacRuntime r = setup_copybits_rt(8, 4, 8);
    uint8_t *ram = mac_cpu_ram(r.cpu);
    uint8_t accel_dst[64];
    memset(ram + 0x5000, 0x11, 64);
    memset(accel_dst, 0x11, 64);
    for (int i = 0; i < 64; i++) ram[0x4000 + i] = (uint8_t)(10 + i);
    write_rect(&r, 0x1200, 0, 0, 2, 4); /* 4x2 source scaled to 8x4 dest */
    wr(&r, SP + 0, 4, 0x3000);          /* maskRgn handle */
    wr(&r, 0x3000, 4, 0x3010);
    wr(&r, 0x3010, 2, 10);
    write_rect(&r, 0x3012, 1, 2, 3, 6); /* clip to x:[2..6), y:[1..3) */
    CHECK(mac_copybits_service(&r, 0xA8EC) == 1);
    CHECK(accel_blitter_blit_8bit(&ab, ram + 0x4000, accel_dst,
                                  4, 2, 8, 4, 2, 1, 6, 3, 0) == 0);
    CHECK(memcmp(ram + 0x5000, accel_dst, 32) == 0);
    mac_cpu_destroy(r.cpu);
}

static void test_palette_animation_and_fallback_gate(void) {
    AccelBlitter ab = { .backend = ACCEL_BACKEND_LUT_TILE, .require_accelerated = 1, .clut_seed = 1 };
    uint8_t indexed[64];
    for (int i = 0; i < 64; i++) indexed[i] = (uint8_t)(i & 3);
    ab.clut[0] = 0xFFFFFFFFu;
    ab.clut[1] = 0xFFFF0000u;
    ab.clut[2] = 0xFF00FF00u;
    ab.clut[3] = 0xFF0000FFu;

    accel_blitter_present_argb(&ab, indexed);
    CHECK(ab.argb_cache[1] == 0xFFFF0000u && ab.argb_cache[3] == 0xFF0000FFu);

    /* Animate palette entry 1 -> yellow and bump CLUT seed; verify dirty tiles update. */
    ab.clut[1] = 0xFFFFFF00u;
    ab.clut_seed = 2;
    accel_blitter_present_argb(&ab, indexed);
    for (int i = 0; i < 64; i++) {
        CHECK(ab.argb_cache[i] == ab.clut[indexed[i]]);
    }

    /* G04 Gate: when accelerated path is required and backend falls back to software, fail closed. */
    ab.backend = ACCEL_BACKEND_SW_FALLBACK;
    ab.require_accelerated = 1;
    CHECK(accel_blitter_check_gate(&ab) == -1);
    CHECK(strstr(ab.last_error, "fell back to software") != NULL);

    /* When software fallback is explicitly allowed (require_accelerated = 0), gate succeeds. */
    ab.require_accelerated = 0;
    CHECK(accel_blitter_check_gate(&ab) == 0);
}

int main(void) {
    test_differential_copybits_parity();
    test_palette_animation_and_fallback_gate();

    if (failures > 0) {
        fprintf(stderr, "test_mac_quickdraw_accel: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_quickdraw_accel: all %d checks passed\n", checks);
    return 0;
}
