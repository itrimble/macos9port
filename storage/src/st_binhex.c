/* BinHex 4.0: 6-bit text encoding over an RLE90 stream holding a header,
 * the data fork and the resource fork, each followed by an XMODEM CRC. */
#include "st_int.h"

#include <stdlib.h>

static const char hqx_chars[] =
    "!\"#$%&'()*+,-012345689@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";
static const char hqx_banner[] = "(This file must be converted with BinHex";

static int hqx_value(int c) {
    const char *q = c ? strchr(hqx_chars, c) : NULL;
    return q ? (int)(q - hqx_chars) : -1;
}

static const uint8_t *find_banner(const uint8_t *p, size_t n) {
    size_t bl = sizeof hqx_banner - 1;
    for (size_t i = 0; i + bl <= n && i < 65536; i++)
        if (p[i] == '(' && !memcmp(p + i, hqx_banner, bl)) return p + i;
    return NULL;
}

int st_binhex_detect(const uint8_t *p, size_t n) { return find_banner(p, n) != NULL; }

/* 6-bit decode then RLE90 expansion into out. */
static int hqx_unpack(const uint8_t *p, size_t n, StBytes *out, uint64_t cap, StErr *err) {
    const uint8_t *s = find_banner(p, n);
    if (!s) return st_fail(err, ST_EFORMAT, "no BinHex banner");
    const uint8_t *end = p + n;
    while (s < end && *s != '\n' && *s != '\r') s++;
    while (s < end && *s != ':') s++;
    if (s >= end) return st_fail(err, ST_EFORMAT, "no BinHex start colon");
    s++;
    uint32_t acc = 0;
    int bits = 0, have_last = 0, closed = 0;
    uint8_t last = 0;
    int pending_marker = 0;
    for (; s < end; s++) {
        int c = *s;
        if (c == ':') { closed = 1; break; }
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        int v = hqx_value(c);
        if (v < 0) return st_fail(err, ST_EFORMAT, "invalid BinHex character 0x%02x", c);
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits < 8) continue;
        bits -= 8;
        uint8_t b = (uint8_t)(acc >> bits);
        acc &= (1u << bits) - 1;
        if (pending_marker) {
            pending_marker = 0;
            if (b == 0) {
                last = 0x90;
                have_last = 1;
                if (st_bytes_append(out, &last, 1)) return st_fail(err, ST_ENOMEM, "out of memory");
            } else {
                if (!have_last) return st_fail(err, ST_EFORMAT, "BinHex run with no byte to repeat");
                for (int i = 1; i < b; i++)
                    if (st_bytes_append(out, &last, 1)) return st_fail(err, ST_ENOMEM, "out of memory");
            }
        } else if (b == 0x90) {
            pending_marker = 1;
        } else {
            last = b;
            have_last = 1;
            if (st_bytes_append(out, &b, 1)) return st_fail(err, ST_ENOMEM, "out of memory");
        }
        if (out->n > cap) return st_fail(err, ST_ELIMIT, "BinHex expands past the limit");
    }
    if (!closed) return st_fail(err, ST_EFORMAT, "BinHex text is truncated (no closing colon)");
    return ST_OK;
}

static int check_crc(const uint8_t *p, size_t n, const uint8_t *crc, const char *what, StErr *err) {
    uint16_t want = st_be16(crc), got = st_crc16_xmodem(0, p, n);
    if (want != got) return st_fail(err, ST_ECHECKSUM, "BinHex %s CRC %04x, expected %04x", what, got, want);
    return ST_OK;
}

int st_binhex_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    StBytes raw = {0};
    int rc = hqx_unpack(p, n, &raw, lim->max_total, err);
    if (rc) { st_bytes_free(&raw); return rc; }
    const uint8_t *r = raw.p;
    size_t rn = raw.n;
    st_macfile_init(out);
    rc = ST_EFORMAT;
    if (rn < 1 || r[0] < 1 || r[0] > 63 || rn < (size_t)r[0] + 22) {
        st_fail(err, ST_EFORMAT, "BinHex header is truncated");
        goto done;
    }
    size_t nl = r[0], h = 1 + nl;
    if (r[h] != 0) { st_fail(err, ST_EFORMAT, "BinHex version byte is not zero"); goto done; }
    if ((rc = check_crc(r, h + 19, r + h + 19, "header", err))) goto done;
    if ((rc = st_name_from_macroman(out, r + 1, nl, err))) goto done;
    memcpy(out->finfo, r + h + 1, 10); /* type, creator, flags */
    uint32_t dlen = st_be32(r + h + 11), rlen = st_be32(r + h + 15);
    uint64_t off = h + 21;
    rc = ST_EFORMAT;
    if (!st_span(rn, off, (uint64_t)dlen + 2)) { st_fail(err, rc, "BinHex data fork is truncated"); goto done; }
    if ((rc = check_crc(r + off, dlen, r + off + dlen, "data fork", err))) goto done;
    if ((rc = st_take(&out->data, r + off, dlen, lim, err))) goto done;
    off += (uint64_t)dlen + 2;
    rc = ST_EFORMAT;
    if (!st_span(rn, off, (uint64_t)rlen + 2)) { st_fail(err, rc, "BinHex resource fork is truncated"); goto done; }
    if ((rc = check_crc(r + off, rlen, r + off + rlen, "resource fork", err))) goto done;
    rc = st_take(&out->rsrc, r + off, rlen, lim, err);
done:
    st_bytes_free(&raw);
    if (rc) st_macfile_free(out);
    return rc;
}

/* RLE90 compress then 6-bit encode, 64 characters per line. */
int st_binhex_encode(const StMacFile *f, StBytes *out, StErr *err) {
    uint8_t name[64];
    int nl = st_utf8_to_macroman(f->name, name, 63);
    if (nl <= 0) return st_fail(err, ST_EINVAL, "name must be 1 to 63 MacRoman characters");
    if (f->data.n > 0x7FFFFFFF || f->rsrc.n > 0x7FFFFFFF)
        return st_fail(err, ST_ELIMIT, "fork too large for BinHex");
    StBytes raw = {0}, rle = {0};
    uint8_t h[96], c[2];
    h[0] = (uint8_t)nl;
    memcpy(h + 1, name, (size_t)nl);
    size_t k = 1 + (size_t)nl;
    h[k] = 0;
    memcpy(h + k + 1, f->finfo, 10);
    st_put32(h + k + 11, (uint32_t)f->data.n);
    st_put32(h + k + 15, (uint32_t)f->rsrc.n);
    st_put16(h + k + 19, st_crc16_xmodem(0, h, k + 19));
    int bad = st_bytes_append(&raw, h, k + 21);
    st_put16(c, st_crc16_xmodem(0, f->data.p, f->data.n));
    bad |= st_bytes_append(&raw, f->data.p, f->data.n) | st_bytes_append(&raw, c, 2);
    st_put16(c, st_crc16_xmodem(0, f->rsrc.p, f->rsrc.n));
    bad |= st_bytes_append(&raw, f->rsrc.p, f->rsrc.n) | st_bytes_append(&raw, c, 2);
    for (size_t i = 0; !bad && i < raw.n;) {
        uint8_t b = raw.p[i];
        size_t run = 1;
        while (i + run < raw.n && raw.p[i + run] == b && run < 255) run++;
        uint8_t lit[2] = {b, 0};
        bad |= st_bytes_append(&rle, lit, b == 0x90 ? 2 : 1);
        if (run >= 3) {
            uint8_t mk[2] = {0x90, (uint8_t)run};
            bad |= st_bytes_append(&rle, mk, 2);
            i += run;
        } else {
            i++;
        }
    }
    static const char head[] = "(This file must be converted with BinHex 4.0)\r\r:";
    out->n = 0;
    bad |= st_bytes_append(out, head, sizeof head - 1);
    uint32_t acc = 0;
    int bits = 0, col = 1;
    for (size_t i = 0; !bad && i <= rle.n; i++) {
        if (i < rle.n) { acc = (acc << 8) | rle.p[i]; bits += 8; }
        else if (bits) { acc <<= 6 - bits % 6; bits += 6 - bits % 6; }
        while (bits >= 6) {
            bits -= 6;
            char ch = hqx_chars[(acc >> bits) & 63];
            bad |= st_bytes_append(out, &ch, 1);
            if (++col == 64) { bad |= st_bytes_append(out, "\r", 1); col = 0; }
        }
    }
    bad |= st_bytes_append(out, ":\r", 2);
    st_bytes_free(&raw);
    st_bytes_free(&rle);
    return bad ? st_fail(err, ST_ENOMEM, "out of memory") : ST_OK;
}
