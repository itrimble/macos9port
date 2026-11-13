/* Disk images: Disk Copy 4.2, NDIF (Disk Copy 6), UDIF and raw device
 * images, plus ADC decompression and HFS volume location. */
#include "st_image.h"

#include <stdio.h>

int st_adc_decode(const uint8_t *p, size_t n, StBytes *out, size_t want, StErr *err) {
    size_t i = 0, start = out->n;
    if (st_bytes_reserve(out, out->n + want)) return st_fail(err, ST_ENOMEM, "out of memory");
    while (i < n && out->n - start < want) {
        uint8_t b = p[i++];
        size_t len, dist;
        if (b & 0x80) {
            len = (size_t)(b & 0x7F) + 1;
            if (len > n - i || len > want - (out->n - start))
                return st_fail(err, ST_EFORMAT, "ADC literal runs past the end");
            memcpy(out->p + out->n, p + i, len);
            out->n += len;
            i += len;
            continue;
        }
        if (b & 0x40) {
            if (n - i < 2) return st_fail(err, ST_EFORMAT, "ADC copy is truncated");
            len = (size_t)(b & 0x3F) + 4;
            dist = ((size_t)p[i] << 8 | p[i + 1]) + 1;
            i += 2;
        } else {
            if (n - i < 1) return st_fail(err, ST_EFORMAT, "ADC copy is truncated");
            len = (size_t)((b & 0x3F) >> 2) + 3;
            dist = ((size_t)(b & 3) << 8 | p[i]) + 1;
            i += 1;
        }
        if (dist > out->n - start) return st_fail(err, ST_EFORMAT, "ADC copy reaches before the start");
        if (len > want - (out->n - start)) len = want - (out->n - start);
        for (size_t k = 0; k < len; k++, out->n++) out->p[out->n] = out->p[out->n - dist];
    }
    if (out->n - start != want)
        return st_fail(err, ST_EFORMAT, "ADC chunk produced %zu of %zu bytes", out->n - start, want);
    return ST_OK;
}

/* ---- Disk Copy 4.2 ---- */
static uint32_t dc42_sum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        sum += st_be16(p + i);
        sum = (sum >> 1) | (sum << 31);
    }
    return sum;
}

static int dc42_detect(const uint8_t *p, size_t n) {
    if (n < 84 || st_be16(p + 82) != 0x0100 || p[0] > 63) return 0;
    uint64_t data = st_be32(p + 64), tags = st_be32(p + 68);
    return data > 0 && data % 512 == 0 && 84 + data + tags <= n;
}

static int dc42_decode(const uint8_t *p, size_t n, StBytes *raw, const StLimits *lim, StErr *err) {
    (void)n;
    uint32_t data = st_be32(p + 64), tags = st_be32(p + 68);
    if (dc42_sum(p + 84, data) != st_be32(p + 72))
        return st_fail(err, ST_ECHECKSUM, "Disk Copy 4.2 data checksum mismatch");
    if (tags > 12 && dc42_sum(p + 84 + data + 12, tags - 12) != st_be32(p + 76))
        return st_fail(err, ST_ECHECKSUM, "Disk Copy 4.2 tag checksum mismatch");
    return st_take(raw, p + 84, data, lim, err);
}

int st_dc42_encode(const uint8_t *raw, size_t n, const char *name, StBytes *out, StErr *err) {
    if (n == 0 || n % 512 || n > 0x7FFFFFFF) return st_fail(err, ST_EINVAL, "disk size must be whole 512-byte blocks");
    uint8_t h[84];
    memset(h, 0, sizeof h);
    int nl = st_utf8_to_macroman(name && *name ? name : "Untitled", h + 1, 63);
    h[0] = (uint8_t)(nl < 0 ? 0 : nl);
    st_put32(h + 64, (uint32_t)n);
    st_put32(h + 72, dc42_sum(raw, n));
    h[80] = n == 409600 ? 0 : n == 819200 ? 1 : n == 737280 ? 2 : n == 1474560 ? 3 : 0xFF;
    h[81] = n == 409600 ? 0x12 : n == 819200 ? 0x22 : 0x24;
    st_put16(h + 82, 0x0100);
    out->n = 0;
    if (st_bytes_append(out, h, sizeof h) || st_bytes_append(out, raw, n))
        return st_fail(err, ST_ENOMEM, "out of memory");
    return ST_OK;
}

/* ---- NDIF (Disk Copy 6): data fork + 'bcem' resource 128 ----
 * bcem: version, fsid, Str63 name, block count @68, max chunk @72,
 * backing offset @76, CRC @80, chunk count @124, then 12-byte chunks:
 * 24-bit start block, type (0 zero, 2 raw, 0x83 ADC, 0x80 KenCode,
 * 0xFF end), offset, length. Layout cross-checked with the BSD-licensed
 * ndif2raw tool, which is used on real images. */
static uint32_t ndif_crc(uint32_t crc, const uint8_t *p, size_t n) {
    static uint32_t t[256];
    if (!t[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0x04C11DB7u : c >> 1;
            t[i] = c;
        }
    for (size_t i = 0; i < n; i++) crc = (crc >> 8) ^ t[(crc ^ p[i]) & 0xFF];
    return crc;
}

static int ndif_decode(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rn, StBytes *raw,
                       const StLimits *lim, StErr *err) {
    const uint8_t *b;
    size_t bn;
    if (st_find_resource(rsrc, rn, 0x6263656Du /* bcem */, 128, &b, &bn))
        return st_fail(err, ST_EFORMAT, "NDIF image has no bcem resource");
    if (bn < 128) return st_fail(err, ST_EFORMAT, "NDIF bcem resource is truncated");
    uint64_t sectors = st_be32(b + 68), base = st_be32(b + 76);
    uint32_t want_crc = st_be32(b + 80), count = st_be32(b + 124);
    if (sectors * 512 > (lim ? lim->max_entry : (1ull << 30)))
        return st_fail(err, ST_ELIMIT, "NDIF image is too large");
    if ((uint64_t)count * 12 > bn - 128) return st_fail(err, ST_EFORMAT, "NDIF chunk table is truncated");
    raw->n = 0;
    if (st_bytes_reserve(raw, (size_t)sectors * 512 + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    for (uint32_t i = 0; i < count && raw->n < sectors * 512; i++) {
        const uint8_t *c = b + 128 + (size_t)i * 12;
        uint32_t start = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
        uint8_t type = c[3];
        uint64_t off = base + st_be32(c + 4), len = st_be32(c + 8);
        if (type == 0xFF) break;
        uint32_t next = i + 1 < count ? ((uint32_t)c[12] << 16 | (uint32_t)c[13] << 8 | c[14]) : (uint32_t)sectors;
        if (start != raw->n / 512 || next < start || next > sectors)
            return st_fail(err, ST_EFORMAT, "NDIF chunk %u is out of order", i);
        size_t want = (size_t)(next - start) * 512;
        if (type == 0) {
            memset(raw->p + raw->n, 0, want);
            raw->n += want;
            continue;
        }
        if (!st_span(n, off, len)) return st_fail(err, ST_EFORMAT, "NDIF chunk %u lies outside the image", i);
        if (type == 2) {
            if (len < want) return st_fail(err, ST_EFORMAT, "NDIF raw chunk %u is short", i);
            memcpy(raw->p + raw->n, p + off, want);
            raw->n += want;
        } else if (type == 0x83) {
            int rc = st_adc_decode(p + off, (size_t)len, raw, want, err);
            if (rc) return rc;
        } else if (type == 0x80) {
            return st_fail(err, ST_EUNSUPPORTED,
                           "NDIF (Disk Copy 6) chunk %u uses KenCode compression, which OS999 does not decode", i);
        } else {
            return st_fail(err, ST_EUNSUPPORTED,
                           "NDIF (Disk Copy 6) chunk %u has compression type 0x%02x, which OS999 does not decode",
                           i, type);
        }
    }
    if (raw->n != sectors * 512) return st_fail(err, ST_EFORMAT, "NDIF image is incomplete");
    if (want_crc && ndif_crc(0xFFFFFFFFu, raw->p, raw->n) != want_crc)
        return st_fail(err, ST_ECHECKSUM, "NDIF image checksum mismatch");
    return ST_OK;
}

StImageKind st_image_detect(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n) {
    const uint8_t *b;
    size_t bn;
    if (n >= 512 && !memcmp(p + n - 512, "koly", 4)) return ST_IMG_UDIF;
    if (dc42_detect(p, n)) return ST_IMG_DC42;
    if (rsrc && !st_find_resource(rsrc, rsrc_n, 0x6263656Du, 128, &b, &bn)) return ST_IMG_NDIF;
    size_t off, len;
    if (st_find_hfs(p, n, &off, &len, NULL) == ST_OK) return ST_IMG_RAW;
    return ST_IMG_NONE;
}

int st_image_decode(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n, StBytes *raw,
                    const StLimits *lim, StErr *err) {
    switch (st_image_detect(p, n, rsrc, rsrc_n)) {
    case ST_IMG_UDIF: return st_udif_decode(p, n, raw, lim, err);
    case ST_IMG_DC42: return dc42_decode(p, n, raw, lim, err);
    case ST_IMG_NDIF: return ndif_decode(p, n, rsrc, rsrc_n, raw, lim, err);
    case ST_IMG_RAW: return st_take(raw, p, n, lim, err);
    default: return st_fail(err, ST_EFORMAT, "not a recognized disk image");
    }
}

/* ---- locating HFS / HFS+ ---- */
static int hfs_sig_at(const uint8_t *raw, size_t n, uint64_t off) {
    if (!st_span(n, off, 1024 + 512)) return 0;
    uint16_t s = st_be16(raw + off + 1024);
    return s == 0x4244 || s == 0x482B || s == 0x4858; /* BD, H+, HX */
}

int st_find_hfs(const uint8_t *raw, size_t n, size_t *off, size_t *len, StErr *err) {
    if (hfs_sig_at(raw, n, 0)) { *off = 0; *len = n; return ST_OK; }
    /* A bare volume behind a one-block (512-byte) header. */
    if (!(n >= 1024 && st_be16(raw) == 0x4552) && hfs_sig_at(raw, n, 512) && !hfs_sig_at(raw, n, 0)) {
        *off = 512;
        *len = n - 512;
        return ST_OK;
    }
    /* Apple partition map: DDM 'ER' at block 0, entries 'PM'. */
    if (n >= 1024 && st_be16(raw) == 0x4552) {
        uint32_t bs = st_be16(raw + 2);
        static const uint32_t strides[] = {512, 2048};
        for (int s = 0; s < 2; s++) {
            uint32_t stride = strides[s];
            if (!st_span(n, stride, 512) || st_be16(raw + stride) != 0x504D) continue;
            uint32_t entries = st_be32(raw + stride + 4);
            for (uint32_t i = 0; i < entries && i < 256; i++) {
                uint64_t e = (uint64_t)stride * (i + 1);
                if (!st_span(n, e, 512) || st_be16(raw + e) != 0x504D) break;
                const uint8_t *pm = raw + e;
                if (strncmp((const char *)pm + 48, "Apple_HFS", 9)) continue;
                uint64_t start = st_be32(pm + 8), count = st_be32(pm + 12);
                static const uint32_t units[] = {0, 512, 2048};
                for (int u = 0; u < 3; u++) {
                    uint64_t unit = units[u] ? units[u] : (bs ? bs : 512);
                    uint64_t o = start * unit, l = count * unit;
                    if (hfs_sig_at(raw, n, o)) {
                        if (o + l > n) l = n - o;
                        *off = (size_t)o;
                        *len = (size_t)l;
                        return ST_OK;
                    }
                }
            }
        }
    }
    /* GUID partition table: any partition holding an HFS signature. */
    if (n >= 1024 && !memcmp(raw + 512, "EFI PART", 8)) {
        uint64_t lba = st_le64(raw + 512 + 72);
        uint32_t count = st_le32(raw + 512 + 80), size = st_le32(raw + 512 + 84);
        if (size >= 128 && size <= 4096)
            for (uint32_t i = 0; i < count && i < 256; i++) {
                uint64_t e = lba * 512 + (uint64_t)i * size;
                if (!st_span(n, e, 128)) break;
                uint64_t first = st_le64(raw + e + 32), last = st_le64(raw + e + 40);
                if (!first || last < first) continue;
                uint64_t o = first * 512, l = (last - first + 1) * 512;
                if (hfs_sig_at(raw, n, o)) {
                    *off = (size_t)o;
                    *len = (size_t)(o + l > n ? n - o : l);
                    return ST_OK;
                }
                if (st_span(n, o + 32, 4) && !memcmp(raw + o + 32, "NXSB", 4))
                    return st_fail(err, ST_EUNSUPPORTED, "APFS volumes (OS999 targets HFS and HFS+)");
            }
    }
    return st_fail(err, ST_EFORMAT, "no HFS or HFS+ volume found");
}

/* A volume whose Master Directory Block claims more bytes than the image
 * holds is usually a compressed NDIF data fork read without its 'bcem'
 * resource. Replace the generic parse error with a precise one. */
int st_explain_short_hfs(const uint8_t *vol, size_t len, StErr *err) {
    if (len < 1024 + 512 || st_be16(vol + 1024) != 0x4244) return 0;
    uint64_t need = (uint64_t)st_be16(vol + 1024 + 28) * 512 +
                    (uint64_t)st_be16(vol + 1024 + 18) * st_be32(vol + 1024 + 20);
    if (need <= len) return 0;
    st_fail(err, ST_EFORMAT,
            "the image holds %zu bytes but its HFS volume needs %llu; it looks like a compressed Disk Copy 6 "
            "(NDIF) image whose resource fork ('bcem' map, the .rsrc file beside it) is missing",
            len, (unsigned long long)need);
    return 1;
}
