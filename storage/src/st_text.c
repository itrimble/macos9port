#include "st_int.h"

#include <stdlib.h>

#include "st_macroman_table.h"

static int put_utf8(uint32_t cp, char *out, size_t cap, size_t *o) {
    char tmp[4];
    size_t k;
    if (cp < 0x80) { tmp[0] = (char)cp; k = 1; }
    else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
    else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F)); k = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); k = 4;
    }
    if (*o + k >= cap) return -1;
    memcpy(out + *o, tmp, k);
    *o += k;
    return 0;
}

int st_macroman_to_utf8(const uint8_t *src, size_t n, char *out, size_t cap) {
    size_t o = 0;
    if (!cap) return -1;
    for (size_t i = 0; i < n; i++) {
        uint32_t cp = src[i] < 0x80 ? src[i] : st_macroman_hi[src[i] - 0x80];
        if (cp == 0) cp = 0xFFFD; /* never embed NUL */
        if (put_utf8(cp, out, cap, &o) != 0) { out[0] = 0; return -1; }
    }
    out[o] = 0;
    return (int)o;
}

/* Decode one UTF-8 scalar; returns bytes consumed or -1. */
static int get_utf8(const char *s, uint32_t *cp) {
    const uint8_t *u = (const uint8_t *)s;
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    int k = (u[0] & 0xE0) == 0xC0 ? 2 : (u[0] & 0xF0) == 0xE0 ? 3 : (u[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (!k) return -1;
    uint32_t v = u[0] & (0x7F >> k);
    for (int i = 1; i < k; i++) {
        if ((u[i] & 0xC0) != 0x80) return -1;
        v = (v << 6) | (u[i] & 0x3F);
    }
    *cp = v;
    return k;
}

int st_utf8_to_macroman(const char *src, uint8_t *out, size_t cap) {
    size_t o = 0;
    while (*src) {
        uint32_t cp;
        int k = get_utf8(src, &cp);
        if (k < 0) return -1;
        src += k;
        uint8_t m = '?';
        if (cp < 0x80) m = (uint8_t)cp;
        else
            for (int i = 0; i < 128; i++)
                if (st_macroman_hi[i] == cp) { m = (uint8_t)(0x80 + i); break; }
        if (o >= cap) return -1;
        out[o++] = m;
    }
    return (int)o;
}

int st_host_component(const char *mac_name, char *out, size_t cap) {
    size_t o = 0, limit = cap - 1 < 255 ? cap - 1 : 255;
    if (cap < 2) return -1;
    for (const char *s = mac_name; *s;) {
        uint32_t cp;
        int k = get_utf8(s, &cp);
        if (k < 0) { cp = '_'; k = 1; }
        char tmp[4];
        int tk = k;
        if (cp == '/') { tmp[0] = ':'; tk = 1; }
        else if (cp < 0x20 || cp == 0x7F || (k == 1 && cp >= 0x80)) { tmp[0] = '_'; tk = 1; }
        else memcpy(tmp, s, (size_t)k);
        if (o + (size_t)tk > limit) break; /* never split a character */
        memcpy(out + o, tmp, (size_t)tk);
        o += (size_t)tk;
        s += k;
    }
    out[o] = 0;
    if (o == 0 || !strcmp(out, ".") || !strcmp(out, "..")) return -1;
    return (int)o;
}
