#include <ctype.h>
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

enum {
    IPC_CMD_OPEN_URL      = 1,
    IPC_CMD_RESIZE        = 2,
    IPC_CMD_MOUSE         = 3,
    IPC_CMD_KEY           = 4,
    IPC_CMD_CLIPBOARD_SET = 5,
    IPC_CMD_SHUTDOWN      = 6,
    IPC_MAX_PAYLOAD       = 4096
};

typedef struct {
    uint16_t version;
    uint16_t cmd;
    uint32_t payload_len;
    const uint8_t *payload;
} BrowserIpcFrame;

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t ipc_encode_frame(uint8_t *out, size_t cap, uint16_t cmd,
                               const void *payload, uint32_t len) {
    if (!out || len > IPC_MAX_PAYLOAD || 12u + len > cap) return 0;
    memcpy(out, "OS9B", 4);
    put16(out + 4, 1);
    put16(out + 6, cmd);
    put32(out + 8, len);
    if (len > 0 && payload) memcpy(out + 12, payload, len);
    return 12u + len;
}

static int ipc_decode_frame(const uint8_t *buf, size_t len, BrowserIpcFrame *out) {
    if (!buf || !out || len < 12) return -1;
    if (memcmp(buf, "OS9B", 4) != 0) return -1;
    uint16_t ver = get16(buf + 4);
    uint16_t cmd = get16(buf + 6);
    uint32_t plen = get32(buf + 8);
    if (ver != 1 || cmd < IPC_CMD_OPEN_URL || cmd > IPC_CMD_SHUTDOWN) return -1;
    if (plen > IPC_MAX_PAYLOAD || 12u + plen != len) return -1;
    out->version = ver;
    out->cmd = cmd;
    out->payload_len = plen;
    out->payload = buf + 12;
    return 0;
}

static int browser_validate_url(const uint8_t *url, size_t len) {
    if (!url || len < 10 || len > 2048) return -1;
    for (size_t i = 0; i < len; i++) {
        if (url[i] <= 0x20 || url[i] >= 0x7F) return -1;
    }
    size_t host_start = 0;
    if (len > 8 && memcmp(url, "https://", 8) == 0) host_start = 8;
    else if (len > 7 && memcmp(url, "http://", 7) == 0) host_start = 7;
    else return -1;
    if (host_start >= len || url[host_start] == '/') return -1;
    return 0;
}

static void browser_clamp_viewport_and_mouse(int *vw, int *vh, int *mx, int *my) {
    if (*vw < 128) *vw = 128;
    if (*vw > 4096) *vw = 4096;
    if (*vh < 128) *vh = 128;
    if (*vh > 4096) *vh = 4096;
    if (*mx < 0) *mx = 0;
    if (*mx >= *vw) *mx = *vw - 1;
    if (*my < 0) *my = 0;
    if (*my >= *vh) *my = *vh - 1;
}

static int browser_validate_argv(int argc, const char *const *argv) {
    static const char *const forbidden[] = {
        "--no-sandbox",
        "--disable-setuid-sandbox",
        "--disable-web-security",
        "--ignore-certificate-errors",
        "--allow-file-access-from-files",
        "--remote-debugging-port"
    };
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) return -1;
        for (size_t f = 0; f < sizeof(forbidden) / sizeof(forbidden[0]); f++) {
            size_t flen = strlen(forbidden[f]);
            if (strncmp(argv[i], forbidden[f], flen) == 0 &&
                (argv[i][flen] == '\0' || argv[i][flen] == '=')) {
                return -1;
            }
        }
    }
    return 0;
}

static int browser_validate_download(int user_approved, const char *filename,
                                     char *out_rel, size_t cap) {
    if (!user_approved || !filename || !out_rel || cap < 32) return -1;
    size_t len = strlen(filename);
    if (len == 0 || len > 120 || filename[0] == '.') return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (c < 32 || c == '/' || c == '\\' || c == ':') return -1;
    }
    if (strstr(filename, "..") != NULL) return -1;

    static const char *const blocked_ext[] = { ".app", ".command", ".sh", ".pkg", ".dmg" };
    for (size_t e = 0; e < sizeof(blocked_ext) / sizeof(blocked_ext[0]); e++) {
        size_t elen = strlen(blocked_ext[e]);
        if (len >= elen && strcmp(filename + len - elen, blocked_ext[e]) == 0) {
            return -1;
        }
    }
    snprintf(out_rel, cap, "Downloads/%s", filename);
    return 0;
}

static void test_ipc_framing_and_url_policy(void) {
    uint8_t wire[512];
    const char *good_url = "https://macintoshgarden.org/games";
    size_t n = ipc_encode_frame(wire, sizeof(wire), IPC_CMD_OPEN_URL,
                                good_url, (uint32_t)strlen(good_url));
    CHECK(n == 12 + strlen(good_url));

    BrowserIpcFrame f;
    CHECK(ipc_decode_frame(wire, n, &f) == 0);
    CHECK(f.cmd == IPC_CMD_OPEN_URL);
    CHECK(browser_validate_url(f.payload, f.payload_len) == 0);

    /* Reject dangerous URL schemes and embedded control chars. */
    static const char *const bad_urls[] = {
        "file:///etc/passwd",
        "javascript:alert(1)",
        "data:text/html,<script>alert(1)</script>",
        "chrome://settings",
        "https:///missing-host",
        "https://example.com/bad space"
    };
    for (size_t i = 0; i < sizeof(bad_urls) / sizeof(bad_urls[0]); i++) {
        CHECK(browser_validate_url((const uint8_t *)bad_urls[i], strlen(bad_urls[i])) == -1);
    }

    /* Corrupt magic, bad version, or oversized payload length must fail closed. */
    wire[0] = 'X';
    CHECK(ipc_decode_frame(wire, n, &f) == -1);
    wire[0] = 'O';
    put32(wire + 8, 5000);
    CHECK(ipc_decode_frame(wire, n, &f) == -1);

    /* Viewport and mouse clamping checks. */
    int vw = 99999, vh = 20, mx = -50, my = 80000;
    browser_clamp_viewport_and_mouse(&vw, &vh, &mx, &my);
    CHECK(vw == 4096 && vh == 128 && mx == 0 && my == 127);
}

static void test_sandbox_argv_and_download_gate(void) {
    const char *safe_args[] = { "os999-browser", "--disable-gpu-vsync", "--ozone-platform=x11" };
    CHECK(browser_validate_argv(3, safe_args) == 0);

    const char *bad_args1[] = { "os999-browser", "--no-sandbox" };
    const char *bad_args2[] = { "os999-browser", "--ignore-certificate-errors" };
    const char *bad_args3[] = { "os999-browser", "--remote-debugging-port=9222" };
    CHECK(browser_validate_argv(2, bad_args1) == -1);
    CHECK(browser_validate_argv(2, bad_args2) == -1);
    CHECK(browser_validate_argv(2, bad_args3) == -1);

    char rel[256];
    CHECK(browser_validate_download(0, "Archive.sit", rel, sizeof(rel)) == -1);
    CHECK(browser_validate_download(1, "Archive.sit", rel, sizeof(rel)) == 0);
    CHECK(strcmp(rel, "Downloads/Archive.sit") == 0);
    CHECK(browser_validate_download(1, "../escape.sit", rel, sizeof(rel)) == -1);
    CHECK(browser_validate_download(1, "Trojan.app", rel, sizeof(rel)) == -1);
    CHECK(browser_validate_download(1, "script.command", rel, sizeof(rel)) == -1);
}

int main(void) {
    test_ipc_framing_and_url_policy();
    test_sandbox_argv_and_download_gate();

    if (failures > 0) {
        fprintf(stderr, "test_browser_ipc: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_browser_ipc: all %d checks passed\n", checks);
    return 0;
}
