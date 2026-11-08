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
    NET_OK                  = 0,
    NET_ERR_TLS_VERSION     = -1,
    NET_ERR_CERT_EXPIRED    = -2,
    NET_ERR_CERT_UNTRUSTED  = -3,
    NET_ERR_CERT_HOSTNAME   = -4,
    NET_ERR_LEGACY_DISABLED = -5,
    NET_ERR_HOST_DENIED     = -6,
    NET_ERR_WOULD_BLOCK     = -7
};

enum {
    PROTO_HTTPS_TLS = 1,
    PROTO_HTTP_PLAIN,
    PROTO_FTP,
    PROTO_TELNET,
    PROTO_APPLETALK_AFP
};

typedef struct {
    uint16_t tls_version;
    uint64_t not_before;
    uint64_t not_after;
    int trusted_root;
    const char *cert_san;
} TlsPeerInfo;

static int match_hostname(const char *pattern, const char *host) {
    if (!pattern || !host) return 0;
    if (strcmp(pattern, host) == 0) return 1;
    if (pattern[0] == '*' && pattern[1] == '.') {
        const char *dot = strchr(host, '.');
        if (!dot || dot == host) return 0;
        /* Single-label wildcard only: reject additional dots in the matched prefix. */
        return strcmp(dot + 1, pattern + 2) == 0;
    }
    return 0;
}

static int net_validate_tls(const TlsPeerInfo *peer, const char *req_host, uint64_t now) {
    if (!peer || !req_host) return NET_ERR_CERT_UNTRUSTED;
    if (peer->tls_version != 0x0303u && peer->tls_version != 0x0304u)
        return NET_ERR_TLS_VERSION;
    if (!peer->trusted_root) return NET_ERR_CERT_UNTRUSTED;
    if (now < peer->not_before || now > peer->not_after) return NET_ERR_CERT_EXPIRED;
    if (!match_hostname(peer->cert_san, req_host)) return NET_ERR_CERT_HOSTNAME;
    return NET_OK;
}

typedef struct {
    int legacy_opt_in;
    const char *allowlisted_host;
} NetAppProfile;

static int net_authorize_connection(const NetAppProfile *prof, int proto, const char *host) {
    if (!prof || !host || host[0] == '\0') return NET_ERR_HOST_DENIED;
    if (proto == PROTO_HTTPS_TLS) return NET_OK;
    if (!prof->legacy_opt_in) return NET_ERR_LEGACY_DISABLED;
    if (!prof->allowlisted_host || strcmp(prof->allowlisted_host, host) != 0)
        return NET_ERR_HOST_DENIED;
    return NET_OK;
}

static void net_redact_diagnostics(const char *in, char *out, size_t cap) {
    static const char *const keys[] = {
        "Bearer ", "Basic ", "password=", "token="
    };
    size_t w = 0;
    for (size_t i = 0; in[i] != '\0' && w + 1 < cap; ) {
        int matched = 0;
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            size_t klen = strlen(keys[k]);
            if (strncmp(in + i, keys[k], klen) == 0) {
                for (size_t c = 0; c < klen && w + 1 < cap; c++) out[w++] = keys[k][c];
                const char *rep = "[REDACTED]";
                for (size_t c = 0; rep[c] && w + 1 < cap; c++) out[w++] = rep[c];
                i += klen;
                while (in[i] && in[i] != ' ' && in[i] != '&' && in[i] != '\r' && in[i] != '\n')
                    i++;
                matched = 1;
                break;
            }
        }
        if (!matched) out[w++] = in[i++];
    }
    out[w] = '\0';
}

typedef struct {
    uint8_t buf[64];
    size_t head, tail, count;
    int timed_out;
} NetRingSocket;

static int net_ring_send(NetRingSocket *s, const void *data, size_t len) {
    if (!s || len > sizeof(s->buf) - s->count) return NET_ERR_WOULD_BLOCK;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        s->buf[s->head] = p[i];
        s->head = (s->head + 1) % sizeof(s->buf);
        s->count++;
    }
    return (int)len;
}

static int net_ring_recv(NetRingSocket *s, void *out, size_t len) {
    if (!s || s->count == 0) return NET_ERR_WOULD_BLOCK;
    size_t n = len < s->count ? len : s->count;
    uint8_t *p = (uint8_t *)out;
    for (size_t i = 0; i < n; i++) {
        p[i] = s->buf[s->tail];
        s->tail = (s->tail + 1) % sizeof(s->buf);
        s->count--;
    }
    return (int)n;
}

static void test_tls_baseline_and_legacy_isolation(void) {
    TlsPeerInfo peer = {
        .tls_version = 0x0304u, /* TLS 1.3 */
        .not_before = 1700000000ull,
        .not_after = 1800000000ull,
        .trusted_root = 1,
        .cert_san = "*.macintoshgarden.org"
    };
    CHECK(net_validate_tls(&peer, "mirror.macintoshgarden.org", 1750000000ull) == NET_OK);
    CHECK(net_validate_tls(&peer, "deep.sub.macintoshgarden.org", 1750000000ull) == NET_ERR_CERT_HOSTNAME);
    CHECK(net_validate_tls(&peer, "evil.org", 1750000000ull) == NET_ERR_CERT_HOSTNAME);
    CHECK(net_validate_tls(&peer, "mirror.macintoshgarden.org", 1900000000ull) == NET_ERR_CERT_EXPIRED);

    /* Downgrade to TLS 1.0 (0x0301) or TLS 1.1 (0x0302) must fail. */
    peer.tls_version = 0x0301u;
    CHECK(net_validate_tls(&peer, "mirror.macintoshgarden.org", 1750000000ull) == NET_ERR_TLS_VERSION);
    peer.tls_version = 0x0303u; /* TLS 1.2 */
    peer.trusted_root = 0;
    CHECK(net_validate_tls(&peer, "mirror.macintoshgarden.org", 1750000000ull) == NET_ERR_CERT_UNTRUSTED);

    /* Default profile blocks HTTP/FTP/Telnet/AppleTalk; opt-in allows only allowlisted host. */
    NetAppProfile def = { .legacy_opt_in = 0, .allowlisted_host = NULL };
    CHECK(net_authorize_connection(&def, PROTO_HTTPS_TLS, "macintoshgarden.org") == NET_OK);
    CHECK(net_authorize_connection(&def, PROTO_HTTP_PLAIN, "macintoshgarden.org") == NET_ERR_LEGACY_DISABLED);
    CHECK(net_authorize_connection(&def, PROTO_FTP, "ftp.apple.com") == NET_ERR_LEGACY_DISABLED);
    CHECK(net_authorize_connection(&def, PROTO_TELNET, "bbs.example.org") == NET_ERR_LEGACY_DISABLED);
    CHECK(net_authorize_connection(&def, PROTO_APPLETALK_AFP, "ChooserZone") == NET_ERR_LEGACY_DISABLED);

    NetAppProfile compat = { .legacy_opt_in = 1, .allowlisted_host = "192.168.1.10" };
    CHECK(net_authorize_connection(&compat, PROTO_APPLETALK_AFP, "192.168.1.10") == NET_OK);
    CHECK(net_authorize_connection(&compat, PROTO_APPLETALK_AFP, "192.168.1.99") == NET_ERR_HOST_DENIED);
}

static void test_credential_redaction_and_socket_ring(void) {
    const char *raw = "GET /api?token=sec123&password=pw999 Authorization: Bearer jwt_abc_xyz";
    char scrubbed[256];
    net_redact_diagnostics(raw, scrubbed, sizeof(scrubbed));
    CHECK(strstr(scrubbed, "sec123") == NULL);
    CHECK(strstr(scrubbed, "pw999") == NULL);
    CHECK(strstr(scrubbed, "jwt_abc_xyz") == NULL);
    CHECK(strstr(scrubbed, "token=[REDACTED]") != NULL);
    CHECK(strstr(scrubbed, "Bearer [REDACTED]") != NULL);

    NetRingSocket sock = {0};
    CHECK(net_ring_recv(&sock, scrubbed, 8) == NET_ERR_WOULD_BLOCK);
    CHECK(net_ring_send(&sock, "OS999NET", 8) == 8);
    uint8_t big[60];
    memset(big, 'A', sizeof(big));
    CHECK(net_ring_send(&sock, big, sizeof(big)) == NET_ERR_WOULD_BLOCK);

    char out[16] = {0};
    CHECK(net_ring_recv(&sock, out, 8) == 8);
    CHECK(memcmp(out, "OS999NET", 8) == 0);
}

int main(void) {
    test_tls_baseline_and_legacy_isolation();
    test_credential_redaction_and_socket_ring();

    if (failures > 0) {
        fprintf(stderr, "test_mac_network_policy: %d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("test_mac_network_policy: all %d checks passed\n", checks);
    return 0;
}
