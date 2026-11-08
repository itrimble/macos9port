#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    char decoder_name[48];
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t total_frames;
    uint32_t dropped_frames;
    uint32_t p99_decode_us;
} BrowserVideoTelemetry;

typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t build;
    uint32_t patch;
    uint32_t patch_age_days;
    int manifest_signed;
} BrowserVersionBaseline;

typedef struct {
    char guest_bind_ip[32];
    int user_plaintext_warning_acknowledged;
    uint16_t upstream_tls_version;
    char upstream_cipher[48];
    int upstream_cert_valid;
} LegacyGatewaySession;

enum {
    GATE_OK = 0,
    GATE_ERR_SOFTWARE_DECODER = -1,
    GATE_ERR_RESOLUTION_FPS = -2,
    GATE_ERR_DROPPED_FRAMES = -3,
    GATE_ERR_DECODE_LATENCY = -4,
    GATE_ERR_VERSION_ROLLBACK = -5,
    GATE_ERR_PATCH_STALE = -6,
    GATE_ERR_UNSIGNED_MANIFEST = -7,
    GATE_ERR_NON_LOOPBACK_BIND = -8,
    GATE_ERR_UNACKED_PLAINTEXT = -9,
    GATE_ERR_TLS_DOWNGRADE = -10
};

static int is_hardware_video_decoder(const char *name) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "V4L2StatefulVideoDecoder") == 0 ||
        strcmp(name, "V4L2VideoDecoder") == 0 ||
        strcmp(name, "VaapiVideoDecoder") == 0 ||
        strcmp(name, "VideoToolboxVideoDecoder") == 0) {
        return 1;
    }
    return 0;
}

static int browser_verify_youtube_hw_decode(const BrowserVideoTelemetry *t) {
    if (!t || !is_hardware_video_decoder(t->decoder_name)) {
        return GATE_ERR_SOFTWARE_DECODER;
    }
    if (t->width < 1280u || t->height < 720u || t->fps < 30u || t->total_frames < 1800u) {
        return GATE_ERR_RESOLUTION_FPS;
    }
    if (t->dropped_frames * 100u > t->total_frames) {
        return GATE_ERR_DROPPED_FRAMES;
    }
    if (t->p99_decode_us > 16667u) {
        return GATE_ERR_DECODE_LATENCY;
    }
    return GATE_OK;
}

static int compare_version(const BrowserVersionBaseline *a, const BrowserVersionBaseline *b) {
    if (a->major != b->major) return (a->major > b->major) ? 1 : -1;
    if (a->minor != b->minor) return (a->minor > b->minor) ? 1 : -1;
    if (a->build != b->build) return (a->build > b->build) ? 1 : -1;
    if (a->patch != b->patch) return (a->patch > b->patch) ? 1 : -1;
    return 0;
}

static int browser_verify_security_baseline(const BrowserVersionBaseline *candidate,
    const BrowserVersionBaseline *min_baseline) {
    if (!candidate || !min_baseline) {
        return GATE_ERR_VERSION_ROLLBACK;
    }
    if (!candidate->manifest_signed) {
        return GATE_ERR_UNSIGNED_MANIFEST;
    }
    if (compare_version(candidate, min_baseline) < 0) {
        return GATE_ERR_VERSION_ROLLBACK;
    }
    if (candidate->patch_age_days > 14u) {
        return GATE_ERR_PATCH_STALE;
    }
    return GATE_OK;
}

static int gateway_verify_session(const LegacyGatewaySession *s) {
    if (!s || strcmp(s->guest_bind_ip, "127.0.0.1") != 0) {
        return GATE_ERR_NON_LOOPBACK_BIND;
    }
    if (!s->user_plaintext_warning_acknowledged) {
        return GATE_ERR_UNACKED_PLAINTEXT;
    }
    if (s->upstream_tls_version < 0x0303u || !s->upstream_cert_valid) {
        return GATE_ERR_TLS_DOWNGRADE;
    }
    if (strstr(s->upstream_cipher, "RC4") != NULL ||
        strstr(s->upstream_cipher, "3DES") != NULL ||
        strstr(s->upstream_cipher, "NULL") != NULL ||
        strncmp(s->upstream_cipher, "TLS_", 4) != 0) {
        return GATE_ERR_TLS_DOWNGRADE;
    }
    return GATE_OK;
}

static void test_youtube_hw_decode_gate(void) {
    BrowserVideoTelemetry rpi5 = {
        "V4L2StatefulVideoDecoder", 1280u, 720u, 60u, 3600u, 12u, 11400u
    };
    BrowserVideoTelemetry mac = {
        "VideoToolboxVideoDecoder", 1920u, 1080u, 60u, 3600u, 4u, 8200u
    };
    CHECK(browser_verify_youtube_hw_decode(&rpi5) == GATE_OK);
    CHECK(browser_verify_youtube_hw_decode(&mac) == GATE_OK);

    BrowserVideoTelemetry sw_ffmpeg = rpi5;
    strcpy(sw_ffmpeg.decoder_name, "FFmpegVideoDecoder");
    CHECK(browser_verify_youtube_hw_decode(&sw_ffmpeg) == GATE_ERR_SOFTWARE_DECODER);

    BrowserVideoTelemetry sw_dav1d = rpi5;
    strcpy(sw_dav1d.decoder_name, "dav1d");
    CHECK(browser_verify_youtube_hw_decode(&sw_dav1d) == GATE_ERR_SOFTWARE_DECODER);

    BrowserVideoTelemetry sw_vpx = rpi5;
    strcpy(sw_vpx.decoder_name, "libvpx");
    CHECK(browser_verify_youtube_hw_decode(&sw_vpx) == GATE_ERR_SOFTWARE_DECODER);

    BrowserVideoTelemetry drop_spike = rpi5;
    drop_spike.dropped_frames = 50u;
    CHECK(browser_verify_youtube_hw_decode(&drop_spike) == GATE_ERR_DROPPED_FRAMES);

    BrowserVideoTelemetry slow_p99 = rpi5;
    slow_p99.p99_decode_us = 21000u;
    CHECK(browser_verify_youtube_hw_decode(&slow_p99) == GATE_ERR_DECODE_LATENCY);
}

static void test_browser_security_baseline_gate(void) {
    BrowserVersionBaseline baseline = {124u, 0u, 6367u, 207u, 0u, 1};
    BrowserVersionBaseline current = {126u, 0u, 6478u, 126u, 5u, 1};
    CHECK(browser_verify_security_baseline(&current, &baseline) == GATE_OK);

    BrowserVersionBaseline rollback = {124u, 0u, 6367u, 118u, 2u, 1};
    CHECK(browser_verify_security_baseline(&rollback, &baseline) == GATE_ERR_VERSION_ROLLBACK);

    BrowserVersionBaseline stale = {126u, 0u, 6478u, 126u, 19u, 1};
    CHECK(browser_verify_security_baseline(&stale, &baseline) == GATE_ERR_PATCH_STALE);

    BrowserVersionBaseline unsigned_pkg = current;
    unsigned_pkg.manifest_signed = 0;
    CHECK(browser_verify_security_baseline(&unsigned_pkg, &baseline) == GATE_ERR_UNSIGNED_MANIFEST);
}

static void test_legacy_tls_gateway_gate(void) {
    LegacyGatewaySession valid = {
        "127.0.0.1", 1, 0x0304u, "TLS_AES_256_GCM_SHA384", 1
    };
    CHECK(gateway_verify_session(&valid) == GATE_OK);

    LegacyGatewaySession lan_bind = valid;
    strcpy(lan_bind.guest_bind_ip, "0.0.0.0");
    CHECK(gateway_verify_session(&lan_bind) == GATE_ERR_NON_LOOPBACK_BIND);

    LegacyGatewaySession unacked = valid;
    unacked.user_plaintext_warning_acknowledged = 0;
    CHECK(gateway_verify_session(&unacked) == GATE_ERR_UNACKED_PLAINTEXT);

    LegacyGatewaySession tls10 = valid;
    tls10.upstream_tls_version = 0x0301u;
    CHECK(gateway_verify_session(&tls10) == GATE_ERR_TLS_DOWNGRADE);

    LegacyGatewaySession weak_cipher = valid;
    strcpy(weak_cipher.upstream_cipher, "TLS_RSA_WITH_RC4_128_SHA");
    CHECK(gateway_verify_session(&weak_cipher) == GATE_ERR_TLS_DOWNGRADE);
}

int main(void) {
    test_youtube_hw_decode_gate();
    test_browser_security_baseline_gate();
    test_legacy_tls_gateway_gate();
    if (failures > 0) {
        fprintf(stderr, "test_browser_media_gateway: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_browser_media_gateway: all %d checks passed\n", checks);
    return 0;
}
