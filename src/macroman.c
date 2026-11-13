#include "macroman.h"

#include <string.h>

#include "macroman_table.h"

uint32_t mr_unicode(uint8_t byte) { return byte < 128 ? byte : HIGH[byte - 128]; }

int mr_from_unicode(uint32_t cp) {
    if (cp < 128) return (int)cp;
    for (int i = 0; i < 128; i++) if (HIGH[i] == cp) return 128 + i;
    return -1;
}

uint8_t mr_fold(uint8_t byte) {
    if (byte >= 'A' && byte <= 'Z') return (uint8_t)(byte + 32);
    if (byte < 128) return byte;
    for (size_t i = 0; i < sizeof FOLD_PAIRS / sizeof FOLD_PAIRS[0]; i++)
        if (FOLD_PAIRS[i][0] == byte) return FOLD_PAIRS[i][1];
    return byte;
}

int mr_name_byte_ok(uint8_t byte) {
    if (byte == 13) return 1;   /* CR: the Finder's custom-icon file is named "Icon" followed by CR; HFS allows it, so a guest can look it up and make it */
    return byte >= 32 && byte != 127 && byte != ':' && byte != '/' && byte != '\\';
}

int mr_equal_fold(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    if (an != bn) return 0;
    for (size_t i = 0; i < an; i++) if (mr_fold(a[i]) != mr_fold(b[i])) return 0;
    return 1;
}

static size_t utf8_put(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | cp >> 6); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    out[0] = (char)(0xE0 | cp >> 12); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;   /* every Mac Roman codepoint is in the basic plane */
}

int mr_component_to_host(const uint8_t *mr, size_t n, char *out, size_t capacity) {
    if (!n || n > MR_NAME_MAX || mr[0] == '.' || capacity < n * 3 + 1) return -1;
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        if (!mr_name_byte_ok(mr[i])) return -1;
        at += utf8_put(mr_unicode(mr[i]), out + at);
    }
    out[at] = 0;
    return (int)at;
}

/* One strict UTF-8 sequence: the codepoint and the bytes used, or 0 for malformed, overlong, surrogate or out-of-range input. */
static int utf8_get(const unsigned char *p, uint32_t *cp) {
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if (p[0] >= 0xC2 && p[0] <= 0xDF && (p[1] & 0xC0) == 0x80) { *cp = (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F); return 2; }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        uint32_t v = (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
        if (v < 0x800 || (v >= 0xD800 && v <= 0xDFFF)) return 0;
        *cp = v; return 3;
    }
    if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        uint32_t v = (uint32_t)(p[0] & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 | (p[3] & 0x3F);
        if (v < 0x10000 || v > 0x10FFFF) return 0;
        *cp = v; return 4;
    }
    return 0;
}

static int compose(uint32_t base, uint32_t mark) {
    for (size_t i = 0; i < sizeof COMPOSE / sizeof COMPOSE[0]; i++)
        if (COMPOSE[i].base == base && COMPOSE[i].mark == mark) return COMPOSE[i].byte;
    return -1;
}

static int decode(const char *utf8, uint8_t *out, size_t capacity, size_t limit, int leading_dot_ok) {
    const unsigned char *p = (const unsigned char *)utf8;
    size_t n = 0;
    if (!*p || (*p == '.' && !leading_dot_ok)) return -1;
    while (*p) {
        uint32_t cp;
        int used = utf8_get(p, &cp);
        if (!used) return -1;
        p += used;
        int byte;
        if (*p == 0xCC || *p == 0xCD) {   /* a combining mark (U+0300 to U+036F) follows: NFD input */
            uint32_t mark;
            int more = utf8_get(p, &mark);
            if (more && mark >= 0x300 && mark <= 0x36F) {
                if ((byte = compose(cp, mark)) < 0) return -1;
                p += more;
            } else byte = mr_from_unicode(cp);
        } else byte = mr_from_unicode(cp);
        if (byte < 0 || !mr_name_byte_ok((uint8_t)byte) || n >= capacity || n >= limit) return -1;
        out[n++] = (uint8_t)byte;
    }
    return (int)n;
}

int mr_component_from_host(const char *utf8, uint8_t *out, size_t capacity) { return decode(utf8, out, capacity, MR_NAME_MAX, 0); }

int mr_host_equal_fold(const char *a, const char *b) {
    uint8_t x[255], y[255];
    int xn = decode(a, x, sizeof x, sizeof x, 1), yn = decode(b, y, sizeof y, sizeof y, 1);
    if (xn < 0 || yn < 0) return !strcmp(a, b);
    return mr_equal_fold(x, (size_t)xn, y, (size_t)yn);
}

int mr_host_representable(const char *utf8) {
    uint8_t scratch[255];
    return decode(utf8, scratch, sizeof scratch, sizeof scratch, 1) >= 0;
}

int mr_sidecar_ok(const char *leaf) {
    return mr_host_length(leaf) >= 0;   /* the sidecar's own name may pass 31: it is never shown to the guest (mr_is_sidecar) */
}

int mr_is_sidecar(const char *host) {
    size_t n = strlen(host);
    char base[MR_HOST_MAX + 8];
    if (n <= 5 || n >= sizeof base || strcmp(host + n - 5, ".rsrc")) return 0;
    memcpy(base, host, n - 5); base[n - 5] = 0;
    return mr_host_length(base) >= 0;
}

int mr_host_length(const char *utf8) {
    uint8_t scratch[MR_NAME_MAX];
    return mr_component_from_host(utf8, scratch, sizeof scratch);
}

int mr_put_pstring(uint8_t *dst, size_t max, const char *utf8) {
    uint8_t name[MR_NAME_MAX];
    int n = mr_component_from_host(utf8, name, sizeof name);
    if (n < 0 || (size_t)n > max) { dst[0] = 0; return -1; }
    dst[0] = (uint8_t)n;
    memcpy(dst + 1, name, (size_t)n);
    return 0;
}
