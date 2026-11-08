/* Finder metadata for the unar fallback. `unar -k visible` writes a
 * "<name>.rsrc" AppleDouble only when a file has a resource fork, and on
 * Linux keeps no type/creator elsewhere, so `lsar -j` (JSON) supplies
 * XADFileType, XADFileCreator and XADFinderFlags for every entry. This
 * is a small reader for that one document shape, not a general JSON
 * parser: it tracks strings and object depth and pulls named fields. */
#include "st_int.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char name[1024];
    uint32_t type, creator, flags;
    int have_type, have_creator, have_flags;
} LsarEntry;

static size_t put_cp(char *o, size_t k, size_t cap, uint32_t c) {
    uint8_t t[4];
    size_t n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (n == 1) t[0] = (uint8_t)c;
    else if (n == 2) { t[0] = (uint8_t)(0xC0 | c >> 6); t[1] = (uint8_t)(0x80 | (c & 63)); }
    else if (n == 3) { t[0] = (uint8_t)(0xE0 | c >> 12); t[1] = (uint8_t)(0x80 | ((c >> 6) & 63)); t[2] = (uint8_t)(0x80 | (c & 63)); }
    else { t[0] = (uint8_t)(0xF0 | c >> 18); t[1] = (uint8_t)(0x80 | ((c >> 12) & 63)); t[2] = (uint8_t)(0x80 | ((c >> 6) & 63)); t[3] = (uint8_t)(0x80 | (c & 63)); }
    if (k + n >= cap) return k;
    memcpy(o + k, t, n);
    return k + n;
}

static int hex4(const char *s, uint32_t *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        *v = *v << 4 | (uint32_t)d;
    }
    return 0;
}

/* Parse the JSON string starting after its opening quote. */
static const char *jstring(const char *s, const char *end, char *out, size_t cap) {
    size_t k = 0;
    while (s < end && *s != '"') {
        if (*s != '\\') { if (k + 1 < cap) out[k++] = *s; s++; continue; }
        if (++s >= end) break;
        char e = *s++;
        uint32_t c = (uint32_t)e;
        if (e == 'n') c = '\n'; else if (e == 't') c = '\t'; else if (e == 'r') c = '\r';
        else if (e == 'b') c = '\b'; else if (e == 'f') c = '\f';
        else if (e == 'u') {
            if (end - s < 4 || hex4(s, &c)) break;
            s += 4;
            uint32_t lo;
            if (c >= 0xD800 && c < 0xDC00 && end - s >= 6 && s[0] == '\\' && s[1] == 'u' && !hex4(s + 2, &lo) &&
                lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                s += 6;
            }
        }
        k = put_cp(out, k, cap, c);
    }
    out[k] = 0;
    return s < end ? s + 1 : end;
}

static void apply(StArchive *a, const LsarEntry *e) {
    if (!e->name[0] || !(e->have_type || e->have_creator || e->have_flags)) return;
    for (size_t i = 0; i < a->n; i++) {
        if (strcmp(a->v[i].path, e->name)) continue;
        StMacFile *f = &a->v[i].file;
        if (e->have_type && !st_be32(f->finfo)) st_put32(f->finfo, e->type);
        if (e->have_creator && !st_be32(f->finfo + 4)) st_put32(f->finfo + 4, e->creator);
        if (e->have_flags && !st_be16(f->finfo + 8)) st_put16(f->finfo + 8, (uint16_t)e->flags);
        return;
    }
}

int st_lsar_merge(const char *json, size_t n, StArchive *a) {
    const char *s = json, *end = json + n;
    int depth = 0, merged = 0;
    LsarEntry e;
    memset(&e, 0, sizeof e);
    char key[64];
    key[0] = 0;
    while (s < end) {
        char c = *s++;
        if (c == '"') {
            char tmp[1024];
            s = jstring(s, end, tmp, sizeof tmp);
            while (s < end && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t')) s++;
            if (s < end && *s == ':') { snprintf(key, sizeof key, "%.63s", tmp); s++; continue; }
            if (depth == 2 && !strcmp(key, "XADFileName")) snprintf(e.name, sizeof e.name, "%s", tmp);
            continue;
        }
        if (c == '{') {
            if (++depth == 2) memset(&e, 0, sizeof e);
        } else if (c == '}') {
            if (depth-- == 2) { apply(a, &e); merged++; }
        } else if ((c == '-' || (c >= '0' && c <= '9')) && depth == 2) {
            /* Bounded parse: never reads past `end`, always advances. */
            int neg = c == '-';
            long long v = neg ? 0 : c - '0';
            while (s < end && *s >= '0' && *s <= '9' && v < 0x7FFFFFFFFFLL) v = v * 10 + (*s++ - '0');
            if (neg) v = -v;
            if (!strcmp(key, "XADFileType")) { e.type = (uint32_t)v; e.have_type = 1; }
            else if (!strcmp(key, "XADFileCreator")) { e.creator = (uint32_t)v; e.have_creator = 1; }
            else if (!strcmp(key, "XADFinderFlags")) { e.flags = (uint32_t)v; e.have_flags = 1; }
        }
    }
    return merged;
}
