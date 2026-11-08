/* UDIF (.dmg): a 512-byte "koly" trailer points at an XML property list
 * whose "blkx" entries are base64 "mish" tables of compressed chunks.
 * Also: a tiny resource-fork lookup used by NDIF. */
#include "st_image.h"

#include <stdlib.h>
#include <zlib.h>
#ifdef OS999_STORAGE_BZIP2
#include <bzlib.h>
#endif

int st_find_resource(const uint8_t *f, size_t n, uint32_t type, int16_t id, const uint8_t **data,
                     size_t *size) {
    if (!f || n < 16) return -1;
    uint32_t doff = st_be32(f), moff = st_be32(f + 4), dlen = st_be32(f + 8), mlen = st_be32(f + 12);
    if (!st_span(n, doff, dlen) || !st_span(n, moff, mlen) || mlen < 30) return -1;
    const uint8_t *m = f + moff;
    uint32_t tl = st_be16(m + 24);
    if (!st_span(mlen, tl, 2)) return -1;
    int types = st_be16(m + tl) + 1;
    for (int t = 0; t < types && types < 0x10000; t++) {
        uint64_t te = (uint64_t)tl + 2 + (uint64_t)t * 8;
        if (!st_span(mlen, te, 8)) return -1;
        if (st_be32(m + te) != type) continue;
        int refs = st_be16(m + te + 4) + 1;
        uint32_t rl = tl + st_be16(m + te + 6);
        for (int r = 0; r < refs; r++) {
            uint64_t re = (uint64_t)rl + (uint64_t)r * 12;
            if (!st_span(mlen, re, 12)) return -1;
            if ((int16_t)st_be16(m + re) != id) continue;
            uint32_t ro = st_be32(m + re + 4) & 0x00FFFFFF;
            if (!st_span(dlen, ro, 4)) return -1;
            uint32_t len = st_be32(f + doff + ro);
            if (!st_span(dlen, (uint64_t)ro + 4, len)) return -1;
            *data = f + doff + ro + 4;
            *size = len;
            return 0;
        }
    }
    return -1;
}

static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64(const char *s, size_t n, StBytes *out) {
    uint32_t acc = 0;
    int bits = 0;
    out->n = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '=') break;
        int v = b64v((unsigned char)s[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            uint8_t b = (uint8_t)(acc >> bits);
            if (st_bytes_append(out, &b, 1)) return -1;
        }
    }
    return 0;
}

static const char *find(const char *s, const char *end, const char *needle) {
    size_t k = strlen(needle);
    for (; s + k <= end; s++)
        if (*s == *needle && !memcmp(s, needle, k)) return s;
    return NULL;
}

static int chunk(uint32_t type, const uint8_t *src, size_t sn, uint8_t *dst, size_t want, StErr *err) {
    if (type == 0 || type == 2) { memset(dst, 0, want); return ST_OK; }
    if (type == 1) {
        if (sn < want) return st_fail(err, ST_EFORMAT, "UDIF raw chunk is short");
        memcpy(dst, src, want);
        return ST_OK;
    }
    if (type == 0x80000004u) {
        StBytes t = {0};
        int rc = st_adc_decode(src, sn, &t, want, err);
        if (rc == ST_OK) memcpy(dst, t.p, want);
        st_bytes_free(&t);
        return rc;
    }
    if (type == 0x80000005u) {
        uLongf got = (uLongf)want;
        if (uncompress(dst, &got, src, (uLong)sn) != Z_OK || got != want)
            return st_fail(err, ST_EFORMAT, "UDIF zlib chunk is corrupt");
        return ST_OK;
    }
#ifdef OS999_STORAGE_BZIP2
    if (type == 0x80000006u) {
        unsigned int got = (unsigned int)want;
        if (BZ2_bzBuffToBuffDecompress((char *)dst, &got, (char *)src, (unsigned int)sn, 0, 0) != BZ_OK ||
            got != want)
            return st_fail(err, ST_EFORMAT, "UDIF bzip2 chunk is corrupt");
        return ST_OK;
    }
#endif
    static const char *names[] = {"bzip2", "LZFSE", "LZMA"};
    if (type >= 0x80000006u && type <= 0x80000008u)
        return st_fail(err, ST_EUNSUPPORTED, "UDIF %s chunks", names[type - 0x80000006u]);
    return st_fail(err, ST_EUNSUPPORTED, "UDIF chunk type 0x%08x", type);
}

int st_udif_decode(const uint8_t *p, size_t n, StBytes *raw, const StLimits *lim, StErr *err) {
    const uint8_t *k = p + n - 512;
    uint64_t dfo = st_be64(k + 24), xo = st_be64(k + 216), xl = st_be64(k + 224);
    uint64_t sectors = st_be64(k + 492);
    uint64_t maxb = lim ? lim->max_entry : (1ull << 30);
    if (sectors > maxb / 512) return st_fail(err, ST_ELIMIT, "UDIF image of %llu sectors is too large", (unsigned long long)sectors);
    if (!xl || !st_span(n, xo, xl)) return st_fail(err, ST_EUNSUPPORTED, "UDIF image without an XML block map");
    const char *x = (const char *)p + xo, *xe = x + xl;
    const char *blkx = find(x, xe, "<key>blkx</key>");
    if (!blkx) return st_fail(err, ST_EFORMAT, "UDIF property list has no blkx");
    const char *aend = find(blkx, xe, "</array>");
    if (!aend) aend = xe;
    raw->n = 0;
    if (st_bytes_reserve(raw, (size_t)sectors * 512 + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    memset(raw->p, 0, (size_t)sectors * 512);
    raw->n = (size_t)sectors * 512;
    StBytes m = {0};
    int rc = ST_OK;
    for (const char *s = blkx; rc == ST_OK && (s = find(s, aend, "<data>")) != NULL;) {
        s += 6;
        const char *e = find(s, aend, "</data>");
        if (!e) { rc = st_fail(err, ST_EFORMAT, "UDIF data element is unterminated"); break; }
        if (base64(s, (size_t)(e - s), &m)) { rc = st_fail(err, ST_ENOMEM, "out of memory"); break; }
        s = e + 7;
        if (m.n < 204 || memcmp(m.p, "mish", 4)) continue;
        uint64_t first = st_be64(m.p + 8), base = st_be64(m.p + 24);
        uint32_t count = st_be32(m.p + 200);
        if (!st_span(m.n, 204, (uint64_t)count * 40)) { rc = st_fail(err, ST_EFORMAT, "UDIF mish table is truncated"); break; }
        for (uint32_t i = 0; i < count && rc == ST_OK; i++) {
            const uint8_t *c = m.p + 204 + (size_t)i * 40;
            uint32_t type = st_be32(c);
            if (type == 0xFFFFFFFFu) break;
            if (type == 0x7FFFFFFEu) continue;
            uint64_t sec = first + st_be64(c + 8), cnt = st_be64(c + 16);
            uint64_t co = dfo + base + st_be64(c + 24), cl = st_be64(c + 32);
            if (sec > sectors || cnt > sectors - sec) { rc = st_fail(err, ST_EFORMAT, "UDIF chunk lies outside the disk"); break; }
            if (type != 0 && type != 2 && !st_span(n, co, cl)) { rc = st_fail(err, ST_EFORMAT, "UDIF chunk data lies outside the file"); break; }
            rc = chunk(type, p + co, (size_t)cl, raw->p + sec * 512, (size_t)cnt * 512, err);
        }
    }
    st_bytes_free(&m);
    return rc;
}
