/* MacBinary I, II and III: a 128-byte header, an optional secondary
 * header, then the data and resource forks, each padded to 128 bytes. */
#include "st_int.h"

static size_t pad128(size_t n) { return (n + 127) & ~(size_t)127; }

int st_macbinary_detect(const uint8_t *p, size_t n) {
    if (n < 128) return 0;
    if (p[0] != 0 || p[74] != 0 || p[82] != 0) return 0;
    if (p[1] < 1 || p[1] > 63) return 0;
    uint32_t dlen = st_be32(p + 83), rlen = st_be32(p + 87);
    if (dlen > 0x7FFFFFFF || rlen > 0x7FFFFFFF) return 0;
    uint16_t crc = st_crc16_xmodem(0, p, 124);
    if (crc == st_be16(p + 124) && (p[122] >= 129 || p[123] >= 129 || !memcmp(p + 102, "mBIN", 4)))
        return memcmp(p + 102, "mBIN", 4) == 0 ? 3 : 2;
    /* MacBinary I: no CRC; insist on zeroed tail bytes and sane sizes. */
    for (int i = 99; i < 128; i++)
        if (p[i] != 0) return 0;
    uint64_t need = 128 + pad128(dlen) + (uint64_t)rlen;
    if (need > n + 127) return 0;
    if (dlen == 0 && rlen == 0) return 0;
    return 1;
}

int st_macbinary_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err) {
    int ver = st_macbinary_detect(p, n);
    if (!ver) return st_fail(err, ST_EFORMAT, "not a MacBinary file");
    st_macfile_init(out);
    int rc = st_name_from_macroman(out, p + 2, p[1], err);
    if (rc) return rc;
    memcpy(out->finfo, p + 65, 8);        /* type, creator */
    out->finfo[8] = p[73];                /* flags high byte */
    out->finfo[9] = ver >= 2 ? p[101] : 0; /* flags low byte */
    memcpy(out->finfo + 10, p + 75, 6);   /* location, folder */
    out->ctime = st_be32(p + 91);
    out->mtime = st_be32(p + 95);
    if (ver == 3) {
        out->fxinfo[8] = p[106];          /* fdScript */
        out->fxinfo[9] = p[107];          /* fdXFlags */
    }
    size_t sec = ver >= 2 ? st_be16(p + 120) : 0;
    uint64_t off = 128 + pad128(sec);
    uint32_t dlen = st_be32(p + 83), rlen = st_be32(p + 87);
    if (!st_span(n, off, dlen)) return st_fail(err, ST_EFORMAT, "MacBinary data fork is truncated");
    rc = st_take(&out->data, p + off, dlen, lim, err);
    if (rc) return rc;
    off += pad128(dlen);
    if (rlen && !st_span(n, off, rlen)) {
        st_macfile_free(out);
        return st_fail(err, ST_EFORMAT, "MacBinary resource fork is truncated");
    }
    rc = st_take(&out->rsrc, p + off, rlen, lim, err);
    if (rc) st_macfile_free(out);
    return rc;
}

int st_macbinary_encode(const StMacFile *f, StBytes *out, StErr *err) {
    uint8_t h[128];
    memset(h, 0, sizeof h);
    uint8_t name[64];
    int nl = st_utf8_to_macroman(f->name, name, 63);
    if (nl <= 0) return st_fail(err, ST_EINVAL, "name must be 1 to 63 MacRoman characters");
    if (f->data.n > 0x7FFFFFFF || f->rsrc.n > 0x7FFFFFFF)
        return st_fail(err, ST_ELIMIT, "fork too large for MacBinary");
    h[1] = (uint8_t)nl;
    memcpy(h + 2, name, (size_t)nl);
    memcpy(h + 65, f->finfo, 8);
    h[73] = f->finfo[8];
    memcpy(h + 75, f->finfo + 10, 6);
    st_put32(h + 83, (uint32_t)f->data.n);
    st_put32(h + 87, (uint32_t)f->rsrc.n);
    st_put32(h + 91, f->ctime);
    st_put32(h + 95, f->mtime);
    h[101] = f->finfo[9];
    memcpy(h + 102, "mBIN", 4);
    h[106] = f->fxinfo[8];
    h[107] = f->fxinfo[9];
    h[122] = 130;
    h[123] = 129;
    st_put16(h + 124, st_crc16_xmodem(0, h, 124));
    static const uint8_t zero[128];
    out->n = 0;
    if (st_bytes_append(out, h, 128) || st_bytes_append(out, f->data.p, f->data.n) ||
        st_bytes_append(out, zero, pad128(f->data.n) - f->data.n) ||
        st_bytes_append(out, f->rsrc.p, f->rsrc.n) ||
        st_bytes_append(out, zero, pad128(f->rsrc.n) - f->rsrc.n))
        return st_fail(err, ST_ENOMEM, "out of memory");
    return ST_OK;
}
