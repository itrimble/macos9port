/* Bounded ISO9660 primary-volume reader (ECMA-119). HFS hybrid discs
 * take the HFS path first, preserving their actual Mac forks/metadata.
 * No Rock Ridge symlinks, Joliet names, interleaving or multi-extent files. */
#include "st_int.h"
#include <stdio.h>
#include <stdlib.h>

#define ISO_BLOCK 2048u
int st_iso_detect(const uint8_t *p, size_t n) {
    return n >= 17 * ISO_BLOCK && !memcmp(p + 16 * ISO_BLOCK + 1, "CD001", 5);
}

typedef struct {
    const uint8_t *p;
    size_t n;
    const StLimits *lim;
    StArchive *a;
    uint32_t ancestors[65];
    uint32_t records;
} Iso;

static int both32(const uint8_t *p, uint32_t *out) {
    *out = st_le32(p);
    return *out == st_be32(p + 4) ? 0 : -1;
}
static int extent(Iso *v, const uint8_t *r, size_t rn, uint32_t *block, uint32_t *len, StErr *e) {
    if (rn < 34 || r[0] < 34 || r[0] > rn || !r[32] || (size_t)r[32] > r[0] - 33u ||
        both32(r + 2, block) || both32(r + 10, len))
        return st_fail(e, ST_EFORMAT, "invalid ISO directory record");
    if (r[1] || r[26] || r[27] || (r[25] & 0x84) || st_le16(r + 28) != 1 || st_be16(r + 30) != 1)
        return st_fail(e, ST_EUNSUPPORTED, "ISO extended attributes, interleaving, multi-extent or multi-volume file");
    if (!st_span(v->n, (uint64_t)*block * ISO_BLOCK, *len))
        return st_fail(e, ST_EFORMAT, "ISO extent outside image");
    return ST_OK;
}
static int read_dir(Iso *v, uint32_t block, uint32_t len, const char *prefix, unsigned depth, StErr *err) {
    if (depth > v->lim->max_depth || depth >= 64) return st_fail(err, ST_ELIMIT, "ISO folder depth exceeded");
    for (unsigned i = 0; i < depth; i++)
        if (v->ancestors[i] == block) return st_fail(err, ST_EFORMAT, "ISO directory cycle");
    v->ancestors[depth] = block;
    const uint8_t *dir = v->p + (size_t)block * ISO_BLOCK;
    for (size_t off = 0; off < len;) {
        size_t rn = dir[off];
        if (!rn) { off += ISO_BLOCK - off % ISO_BLOCK; continue; }
        if (rn > len - off || rn > ISO_BLOCK - off % ISO_BLOCK)
            return st_fail(err, ST_EFORMAT, "ISO record crosses sector boundary");
        const uint8_t *r = dir + off;
        uint32_t child, size;
        int rc = extent(v, r, rn, &child, &size, err);
        if (rc) return rc;
        off += rn;
        if (r[32] == 1 && r[33] <= 1) continue; /* dot and parent */
        if (++v->records > v->lim->max_entries) return st_fail(err, ST_ELIMIT, "too many ISO records");
        char name[256], path[4096];
        size_t nl = r[32];
        memcpy(name, r + 33, nl);
        name[nl] = 0;
        for (size_t i = 0; i < nl; i++)
            if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126 || name[i] == '/' || name[i] == '\\')
                return st_fail(err, ST_EPATH, "unsafe ISO identifier");
        char *version = strchr(name, ';');
        if (version) {
            for (char *s = version + 1; *s; s++)
                if (*s < '0' || *s > '9') return st_fail(err, ST_EFORMAT, "invalid ISO version");
            if (!version[1]) return st_fail(err, ST_EFORMAT, "empty ISO version");
            *version = 0;
        }
        size_t nn = strlen(name);
        if (nn && name[nn - 1] == '.') name[--nn] = 0;
        if (!nn || !strcmp(name, ".") || !strcmp(name, "..")) return st_fail(err, ST_EPATH, "unsafe ISO name");
        int k = snprintf(path, sizeof path, "%s%s%s", prefix, *prefix ? "/" : "", name);
        if (k < 0 || (size_t)k >= sizeof path) return st_fail(err, ST_ELIMIT, "ISO path too long");
        StMacFile f;
        st_macfile_init(&f);
        snprintf(f.name, sizeof f.name, "%s", name);
        int is_dir = (r[25] & 2) != 0;
        if (!is_dir) rc = st_take(&f.data, v->p + (size_t)child * ISO_BLOCK, size, v->lim, err);
        if (!rc) rc = st_archive_add(v->a, path, is_dir, &f, v->lim, err);
        if (rc) { st_macfile_free(&f); return rc; }
        if (is_dir && (rc = read_dir(v, child, size, path, depth + 1, err))) return rc;
    }
    return ST_OK;
}
int st_iso_read(const uint8_t *p, size_t n, StArchive *a, char *name, size_t cap,
                const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    if (!st_iso_detect(p, n)) return st_fail(err, ST_EFORMAT, "no ISO volume descriptor");
    const uint8_t *primary = NULL;
    int ended = 0;
    for (size_t i = 16; i < 16 + 64 && st_span(n, i * ISO_BLOCK, ISO_BLOCK); i++) {
        const uint8_t *d = p + i * ISO_BLOCK;
        if (memcmp(d + 1, "CD001", 5) || d[6] != 1) return st_fail(err, ST_EFORMAT, "invalid ISO descriptor");
        if (d[0] == 1) { if (primary) return st_fail(err, ST_EFORMAT, "duplicate ISO primary descriptor"); primary = d; }
        if (d[0] == 255) { ended = 1; break; }
    }
    if (!primary || !ended) return st_fail(err, ST_EFORMAT, "incomplete ISO descriptors");
    uint32_t blocks;
    if (both32(primary + 80, &blocks) || !blocks || (uint64_t)blocks * ISO_BLOCK > n ||
        st_le16(primary + 128) != ISO_BLOCK || st_be16(primary + 130) != ISO_BLOCK)
        return st_fail(err, ST_EFORMAT, "invalid ISO volume size");
    Iso v = {p, (size_t)blocks * ISO_BLOCK, lim, a, {0}, 0};
    uint32_t block, len;
    int rc = extent(&v, primary + 156, 34, &block, &len, err);
    if (!rc && !(primary[181] & 2)) rc = st_fail(err, ST_EFORMAT, "ISO root is not a directory");
    if (!rc) rc = read_dir(&v, block, len, "", 0, err);
    if (!rc && name && cap) {
        size_t l = 32;
        while (l && primary[40 + l - 1] == ' ') l--;
        if (l >= cap) l = cap - 1;
        memcpy(name, primary + 40, l); name[l] = 0;
        if (!name[0]) snprintf(name, cap, "ISO Disc");
    }
    if (rc) st_archive_free(a);
    return rc;
}
