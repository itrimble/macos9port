/* ZIP reader (stored, deflate; ZIP64) that restores Mac metadata from
 * "__MACOSX/<dir>/._<name>" and "._<name>" AppleDouble companions. */
#include "st_int.h"

#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>

int st_zip_detect(const uint8_t *p, size_t n) {
    return n >= 22 && (st_le32(p) == 0x04034b50u || st_le32(p) == 0x06054b50u);
}

static int find_eocd(const uint8_t *p, size_t n, size_t *at) {
    if (n < 22) return -1;
    size_t lo = n > 22 + 65535 ? n - 22 - 65535 : 0;
    for (size_t i = n - 22 + 1; i-- > lo;)
        if (st_le32(p + i) == 0x06054b50u) { *at = i; return 0; }
    return -1;
}

static int inflate_raw(const uint8_t *src, size_t sn, StBytes *out, uint64_t want, StErr *err) {
    if (st_bytes_reserve(out, (size_t)want + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    z_stream z;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return st_fail(err, ST_ENOMEM, "zlib init failed");
    z.next_in = (Bytef *)src;
    z.avail_in = (uInt)(sn > UINT32_MAX ? UINT32_MAX : sn);
    z.next_out = out->p;
    z.avail_out = (uInt)want;
    int zr = inflate(&z, Z_FINISH);
    size_t got = (size_t)z.total_out;
    inflateEnd(&z);
    if (zr != Z_STREAM_END || got != want)
        return st_fail(err, ST_EFORMAT, "deflate stream is corrupt (%d, %zu of %llu bytes)", zr, got,
                       (unsigned long long)want);
    out->n = got;
    return ST_OK;
}

/* Normalize a ZIP name into a '/' path of Mac names; returns 0 or -1. */
static int zip_name(const uint8_t *s, size_t len, int utf8, char *out, size_t cap) {
    char tmp[1024];
    if (len == 0 || len >= sizeof tmp) return -1;
    (void)utf8;
    {
        /* Validate UTF-8; otherwise fall back to MacRoman (Mac archivers). */
        int ok = 1;
        for (size_t i = 0; i < len && ok;) {
            uint8_t c = s[i];
            int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            if (!k || i + (size_t)k > len) ok = 0;
            for (int j = 1; ok && j < k; j++)
                if ((s[i + (size_t)j] & 0xC0) != 0x80) ok = 0;
            i += (size_t)(k ? k : 1);
        }
        if (ok) { memcpy(tmp, s, len); tmp[len] = 0; }
        else if (st_macroman_to_utf8(s, len, tmp, sizeof tmp) < 0) return -1;
    }
    for (char *q = tmp; *q; q++) if (*q == '\\') *q = '/';
    const char *r = tmp;
    while (*r == '/') r++;
    while (!strncmp(r, "./", 2)) r += 2;
    if (strlen(r) >= cap) return -1;
    strcpy(out, r);
    /* Reject traversal outright rather than silently dropping parts. */
    for (const char *c = out; *c;) {
        const char *e = strchr(c, '/');
        size_t l = e ? (size_t)(e - c) : strlen(c);
        if (l == 2 && c[0] == '.' && c[1] == '.') return -1;
        c = e ? e + 1 : c + l;
    }
    return 0;
}

typedef struct { char *path; StMacFile meta; } ZipSide;

static int apply_side(StArchive *a, ZipSide *sd, const StLimits *lim, StErr *err) {
    for (size_t i = 0; i < a->n; i++) {
        if (strcmp(a->v[i].path, sd->path)) continue;
        StMacFile *f = &a->v[i].file;
        memcpy(f->finfo, sd->meta.finfo, 16);
        memcpy(f->fxinfo, sd->meta.fxinfo, 16);
        if (sd->meta.ctime) f->ctime = sd->meta.ctime;
        if (sd->meta.mtime) f->mtime = sd->meta.mtime;
        st_bytes_free(&f->rsrc);
        f->rsrc = sd->meta.rsrc;
        memset(&sd->meta.rsrc, 0, sizeof sd->meta.rsrc);
        a->total += f->rsrc.n;
        return ST_OK;
    }
    /* Companion without a data file: keep it as a resource-only file. */
    const char *base = strrchr(sd->path, '/');
    snprintf(sd->meta.name, sizeof sd->meta.name, "%s", base ? base + 1 : sd->path);
    return st_archive_add(a, sd->path, 0, &sd->meta, lim, err);
}

int st_zip_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    size_t e;
    if (find_eocd(p, n, &e)) return st_fail(err, ST_EFORMAT, "ZIP end of central directory not found");
    uint64_t count = st_le16(p + e + 10), cd_size = st_le32(p + e + 12), cd_off = st_le32(p + e + 16);
    if ((count == 0xFFFF || cd_off == 0xFFFFFFFFu) && e >= 20 && st_le32(p + e - 20) == 0x07064b50u) {
        uint64_t z64 = st_le64(p + e - 20 + 8);
        if (!st_span(n, z64, 56) || st_le32(p + z64) != 0x06064b50u)
            return st_fail(err, ST_EFORMAT, "ZIP64 end record is invalid");
        count = st_le64(p + z64 + 32);
        cd_size = st_le64(p + z64 + 40);
        cd_off = st_le64(p + z64 + 48);
    }
    if (!st_span(n, cd_off, cd_size)) return st_fail(err, ST_EFORMAT, "ZIP central directory is truncated");
    if (count > lim->max_entries) return st_fail(err, ST_ELIMIT, "ZIP has %llu entries", (unsigned long long)count);
    ZipSide *side = calloc(count ? count : 1, sizeof *side);
    if (!side) return st_fail(err, ST_ENOMEM, "out of memory");
    size_t nside = 0;
    int rc = ST_OK;
    uint64_t q = cd_off;
    for (uint64_t i = 0; i < count && rc == ST_OK; i++) {
        if (!st_span(n, q, 46) || st_le32(p + q) != 0x02014b50u) {
            rc = st_fail(err, ST_EFORMAT, "ZIP central entry %llu is invalid", (unsigned long long)i);
            break;
        }
        const uint8_t *c = p + q;
        uint16_t flags = st_le16(c + 8), method = st_le16(c + 10);
        uint64_t crc = st_le32(c + 16), csize = st_le32(c + 20), usize = st_le32(c + 24);
        uint16_t nl = st_le16(c + 28), xl = st_le16(c + 30), kl = st_le16(c + 32);
        uint32_t ext = st_le32(c + 38);
        uint64_t loff = st_le32(c + 42);
        if (!st_span(n, q + 46, (uint64_t)nl + xl + kl)) { rc = st_fail(err, ST_EFORMAT, "ZIP name is truncated"); break; }
        for (size_t x = 0; x + 4 <= xl;) { /* ZIP64 extra field */
            const uint8_t *f = c + 46 + nl + x;
            uint16_t id = st_le16(f), sz = st_le16(f + 2);
            if (x + 4 + sz > xl) break;
            if (id == 0x0001) {
                size_t o = 4;
                if (usize == 0xFFFFFFFFu && o + 8 <= 4u + sz) { usize = st_le64(f + o); o += 8; }
                if (csize == 0xFFFFFFFFu && o + 8 <= 4u + sz) { csize = st_le64(f + o); o += 8; }
                if (loff == 0xFFFFFFFFu && o + 8 <= 4u + sz) loff = st_le64(f + o);
            }
            x += 4u + sz;
        }
        char path[1024];
        if (zip_name(c + 46, nl, flags & 0x800, path, sizeof path)) {
            rc = st_fail(err, ST_EPATH, "ZIP entry %llu has an unsafe name", (unsigned long long)i);
            break;
        }
        q += 46u + nl + xl + kl;
        size_t pl = strlen(path);
        int is_dir = pl && path[pl - 1] == '/';
        if (is_dir) path[pl - 1] = 0;
        if (!path[0]) continue;
        if (((ext >> 16) & 0170000) == 0120000) continue; /* symlink: never followed or created */
        if (flags & 1) { rc = st_fail(err, ST_EUNSUPPORTED, "ZIP entry %s is encrypted", path); break; }
        int is_side = 0;
        char real[1024];
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (!strncmp(base, "._", 2) && base[2]) {
            const char *rel = path;
            if (!strncmp(rel, "__MACOSX/", 9)) rel += 9;
            size_t dl = (size_t)(base - path) - (size_t)(rel - path);
            snprintf(real, sizeof real, "%.*s%s", (int)dl, rel, base + 2);
            is_side = 1;
        } else if (!strncmp(path, "__MACOSX", 8) && (path[8] == 0 || path[8] == '/')) {
            continue;
        }
        if (is_dir) {
            StMacFile d;
            st_macfile_init(&d);
            snprintf(d.name, sizeof d.name, "%s", base);
            rc = st_archive_add(out, path, 1, &d, lim, err);
            continue;
        }
        if (!st_span(n, loff, 30) || st_le32(p + loff) != 0x04034b50u) {
            rc = st_fail(err, ST_EFORMAT, "ZIP local header for %s is invalid", path);
            break;
        }
        uint64_t doff = loff + 30 + st_le16(p + loff + 26) + st_le16(p + loff + 28);
        if (!st_span(n, doff, csize)) { rc = st_fail(err, ST_EFORMAT, "ZIP data for %s is truncated", path); break; }
        if (usize > lim->max_entry) { rc = st_fail(err, ST_ELIMIT, "ZIP entry %s is too large", path); break; }
        StBytes body = {0};
        if (method == 0) {
            if (csize != usize) rc = st_fail(err, ST_EFORMAT, "stored ZIP entry %s has mismatched sizes", path);
            else rc = st_take(&body, p + doff, (size_t)csize, lim, err);
        } else if (method == 8) {
            rc = inflate_raw(p + doff, (size_t)csize, &body, usize, err);
        } else {
            rc = st_fail(err, ST_EUNSUPPORTED, "ZIP method %u for %s", method, path);
        }
        if (rc == ST_OK && st_crc32(0, body.p, body.n) != crc)
            rc = st_fail(err, ST_ECHECKSUM, "ZIP CRC mismatch for %s", path);
        if (rc) { st_bytes_free(&body); break; }
        StMacFile f;
        st_macfile_init(&f);
        if (is_side) {
            StMacFile meta;
            if (st_applesingle_detect(body.p, body.n) == 2 &&
                st_applesingle_decode(body.p, body.n, &meta, lim, NULL) == ST_OK) {
                side[nside].path = strdup(real);
                side[nside++].meta = meta;
            }
            st_bytes_free(&body);
            continue;
        }
        snprintf(f.name, sizeof f.name, "%s", base);
        f.data = body;
        rc = st_archive_add(out, path, 0, &f, lim, err);
        if (rc) st_macfile_free(&f);
    }
    /* Companions are applied after their data files, whatever the order. */
    for (size_t i = 0; i < nside; i++) {
        if (rc == ST_OK && side[i].path) rc = apply_side(out, &side[i], lim, err);
        free(side[i].path);
        st_macfile_free(&side[i].meta);
    }
    free(side);
    if (rc) st_archive_free(out);
    return rc;
}
