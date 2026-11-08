/* AppleSingle (0x00051600) and AppleDouble (0x00051607), versions 1 and 2.
 * Entries: 1 data, 2 resource, 3 real name, 7 v1 file info, 8 dates,
 * 9 Finder info (32 bytes, then optional macOS extended attributes). */
#include "st_int.h"

#define AS_MAGIC_SINGLE 0x00051600u
#define AS_MAGIC_DOUBLE 0x00051607u
#define AS_EPOCH_2000 3029529600u /* 2000-01-01 in Mac 1904 seconds */

int st_applesingle_detect(const uint8_t *p, size_t n) {
    if (n < 26) return 0;
    uint32_t magic = st_be32(p), ver = st_be32(p + 4);
    if (ver != 0x00010000u && ver != 0x00020000u) return 0;
    if (magic == AS_MAGIC_SINGLE) return 1;
    if (magic == AS_MAGIC_DOUBLE) return 2;
    return 0;
}

int st_applesingle_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err) {
    int kind = st_applesingle_detect(p, n);
    if (!kind) return st_fail(err, ST_EFORMAT, "not an AppleSingle/AppleDouble file");
    st_macfile_init(out);
    uint32_t ver = st_be32(p + 4);
    uint16_t count = st_be16(p + 24);
    if (!st_span(n, 26, (uint64_t)count * 12))
        return st_fail(err, ST_EFORMAT, "AppleSingle entry table is truncated");
    int rc = ST_OK;
    for (uint16_t i = 0; i < count && rc == ST_OK; i++) {
        const uint8_t *d = p + 26 + (size_t)i * 12;
        uint32_t id = st_be32(d), off = st_be32(d + 4), len = st_be32(d + 8);
        if (!st_span(n, off, len)) {
            rc = st_fail(err, ST_EFORMAT, "AppleSingle entry %u lies outside the file", id);
            break;
        }
        const uint8_t *e = p + off;
        switch (id) {
        case 1: rc = st_take(&out->data, e, len, lim, err); break;
        case 2: rc = st_take(&out->rsrc, e, len, lim, err); break;
        case 3:
            if (len && st_macroman_to_utf8(e, len > 255 ? 255 : len, out->name, sizeof out->name) < 0)
                out->name[0] = 0;
            break;
        case 7: /* v1 Macintosh file info: create, modify, backup, attributes */
            if (len >= 8) { out->ctime = st_be32(e); out->mtime = st_be32(e + 4); }
            break;
        case 8: /* v2 dates, signed seconds from 2000-01-01 */
            if (len >= 8) {
                out->ctime = (uint32_t)((int64_t)AS_EPOCH_2000 + (int32_t)st_be32(e));
                out->mtime = (uint32_t)((int64_t)AS_EPOCH_2000 + (int32_t)st_be32(e + 4));
            }
            break;
        case 9:
            memcpy(out->finfo, e, len < 16 ? len : 16);
            if (len > 16) memcpy(out->fxinfo, e + 16, len - 16 < 16 ? len - 16 : 16);
            break;
        default: break; /* comments, icons, ProDOS, MS-DOS: preserved nowhere */
        }
    }
    (void)ver;
    if (rc) st_macfile_free(out);
    return rc;
}

typedef struct { uint32_t id; const uint8_t *p; size_t n; } AsEnt;

int st_applesingle_encode(const StMacFile *f, int is_double, StBytes *out, StErr *err) {
    uint8_t name[256];
    int nl = f->name[0] ? st_utf8_to_macroman(f->name, name, 255) : 0;
    if (nl < 0) nl = 0;
    int nent = 3 + (nl > 0) + (!is_double);
    size_t hdr = 26 + (size_t)nent * 12;
    uint8_t h[26 + 5 * 12];
    memset(h, 0, sizeof h);
    st_put32(h, is_double ? AS_MAGIC_DOUBLE : AS_MAGIC_SINGLE);
    st_put32(h + 4, 0x00020000u);
    st_put16(h + 24, (uint16_t)nent);
    uint8_t dates[16], finder[32];
    st_put32(dates, (uint32_t)((int64_t)f->ctime - AS_EPOCH_2000));
    st_put32(dates + 4, (uint32_t)((int64_t)f->mtime - AS_EPOCH_2000));
    st_put32(dates + 8, 0x80000000u); /* backup: unknown */
    st_put32(dates + 12, 0x80000000u);
    memcpy(finder, f->finfo, 16);
    memcpy(finder + 16, f->fxinfo, 16);
    AsEnt ent[5];
    int k = 0;
    if (nl > 0) ent[k++] = (AsEnt){3, name, (size_t)nl};
    ent[k++] = (AsEnt){8, dates, sizeof dates};
    ent[k++] = (AsEnt){9, finder, sizeof finder};
    ent[k++] = (AsEnt){2, f->rsrc.p, f->rsrc.n};
    if (!is_double) ent[k++] = (AsEnt){1, f->data.p, f->data.n};
    size_t off = hdr;
    for (int i = 0; i < k; i++) {
        if (ent[i].n > 0xFFFFFFFFu - off) return st_fail(err, ST_ELIMIT, "fork too large for AppleSingle");
        uint8_t *d = h + 26 + i * 12;
        st_put32(d, ent[i].id);
        st_put32(d + 4, (uint32_t)off);
        st_put32(d + 8, (uint32_t)ent[i].n);
        off += ent[i].n;
    }
    out->n = 0;
    int bad = st_bytes_append(out, h, hdr);
    for (int i = 0; i < k; i++) bad |= st_bytes_append(out, ent[i].p, ent[i].n);
    return bad ? st_fail(err, ST_ENOMEM, "out of memory") : ST_OK;
}
