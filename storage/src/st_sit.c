/* StuffIt archives: classic (SIT!, ST4x/ST6x variants) and StuffIt 5.
 * Folders, Finder info, dates and both forks are preserved. Methods we
 * do not decode are reported by name with ST_EUNSUPPORTED. */
#include "st_sit.h"

#include <stdio.h>
#include <stdlib.h>

static const char sit5_magic[] = "StuffIt (c)1997-";

int st_sit_detect(const uint8_t *p, size_t n) {
    if (n >= 22 && !memcmp(p + 10, "rLau", 4)) {
        static const char *tags[] = {"SIT!", "ST46", "ST50", "ST60", "ST65", "STin", "STi2", "STi3", "STi4"};
        for (size_t i = 0; i < sizeof tags / sizeof *tags; i++)
            if (!memcmp(p, tags[i], 4)) return 1;
    }
    if (n >= 100 && !memcmp(p, sit5_magic, sizeof sit5_magic - 1)) return 5;
    if (n >= 8 && !memcmp(p, "StuffIt!", 8)) return 10;
    return 0;
}

static int join(char *out, size_t cap, const char *dir, const char *name) {
    char comp[256];
    snprintf(comp, sizeof comp, "%s", name);
    for (char *c = comp; *c; c++) if (*c == '/') *c = ':';
    int k = snprintf(out, cap, "%s%s%s", dir, *dir ? "/" : "", comp);
    return k < 0 || (size_t)k >= cap ? -1 : 0;
}

static int classic(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err) {
    char dir[4096] = "";
    size_t off = 22;
    int depth = 0, rc = ST_OK;
    while (off + 112 <= n && rc == ST_OK) {
        const uint8_t *h = p + off;
        if (st_crc16_arc(0, h, 110) != st_be16(h + 110))
            return st_fail(err, ST_ECHECKSUM, "StuffIt entry header at %zu fails its CRC", off);
        int rm = h[0], dm = h[1];
        StMacFile f;
        st_macfile_init(&f);
        int nl = h[2] > 63 ? 63 : h[2];
        if ((rm == 33 || dm == 33)) { /* end of folder */
            off += 112;
            if (depth == 0) return st_fail(err, ST_EFORMAT, "StuffIt folder end without a start");
            char *s = strrchr(dir, '/');
            if (s) *s = 0; else dir[0] = 0;
            depth--;
            continue;
        }
        if ((rc = st_name_from_macroman(&f, h + 3, (size_t)nl, err))) return rc;
        memcpy(f.finfo, h + 66, 10);
        f.ctime = st_be32(h + 76);
        f.mtime = st_be32(h + 80);
        uint32_t rlen = st_be32(h + 84), dlen = st_be32(h + 88);
        uint32_t rclen = st_be32(h + 92), dclen = st_be32(h + 96);
        char path[4096];
        if (join(path, sizeof path, dir, f.name)) return st_fail(err, ST_ELIMIT, "StuffIt path too long");
        off += 112;
        if (rm == 32 || dm == 32) {
            if ((rc = st_archive_add(out, path, 1, &f, lim, err))) return rc;
            snprintf(dir, sizeof dir, "%s", path);
            depth++;
            continue;
        }
        if (!st_span(n, off, (uint64_t)rclen + dclen))
            return st_fail(err, ST_EFORMAT, "StuffIt forks of %s are truncated", path);
        char what[300];
        snprintf(what, sizeof what, "resource fork of %.200s", f.name);
        rc = st_sit_unpack(rm, p + off, rclen, rlen, st_be16(h + 100), 1, &f.rsrc, lim, what, err);
        if (rc == ST_OK) {
            snprintf(what, sizeof what, "data fork of %.200s", f.name);
            rc = st_sit_unpack(dm, p + off + rclen, dclen, dlen, st_be16(h + 102), 1, &f.data, lim, what, err);
        }
        if (rc == ST_OK) rc = st_archive_add(out, path, 0, &f, lim, err);
        if (rc) st_macfile_free(&f);
        off += (size_t)rclen + dclen;
    }
    if (rc == ST_OK && depth) return st_fail(err, ST_EFORMAT, "StuffIt archive ends inside a folder");
    return rc;
}

/* StuffIt 5: entries start with A5A5A5A5 and link to their parent by
 * offset. A folder entry's +34 field holds its first child's offset;
 * 0xFFFFFFFF there marks a folder-end record with no body. */
typedef struct { uint32_t off; char *path; } Sit5Dir;

static const char *sit5_parent(Sit5Dir *d, size_t nd, uint32_t off) {
    if (!off) return "";
    for (size_t i = 0; i < nd; i++) if (d[i].off == off) return d[i].path;
    return NULL;
}

static int sit5(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err) {
    if (n < 94) return st_fail(err, ST_EFORMAT, "StuffIt 5 header is truncated");
    if (p[82] != 5) return st_fail(err, ST_EUNSUPPORTED, "StuffIt 5 archive version %u", p[82]);
    if (p[83] & 0x80) return st_fail(err, ST_EUNSUPPORTED, "StuffIt 5 archive is encrypted");
    size_t off = st_be32(p + 88);
    Sit5Dir *dirs = NULL;
    size_t nd = 0, cap = 0;
    int rc = ST_OK;
    while (rc == ST_OK && off + 48 <= n && st_be32(p + off) == 0xA5A5A5A5u) {
        const uint8_t *h = p + off;
        size_t hs = st_be16(h + 6);
        uint8_t flags = h[9];
        uint32_t parent = st_be32(h + 26), a = st_be32(h + 34), c = st_be32(h + 38);
        size_t nl = st_be16(h + 30);
        if (hs < 48 + nl || !st_span(n, off, hs)) { rc = st_fail(err, ST_EFORMAT, "StuffIt 5 header at %zu is invalid", off); break; }
        if (a == 0xFFFFFFFFu) { off += hs; continue; } /* folder end */
        const char *dir = sit5_parent(dirs, nd, parent);
        if (!dir) { rc = st_fail(err, ST_EFORMAT, "StuffIt 5 entry at %zu has an unknown parent", off); break; }
        StMacFile f;
        st_macfile_init(&f);
        if ((rc = st_name_from_macroman(&f, h + 48, nl > 255 ? 255 : nl, err))) break;
        f.ctime = st_be32(h + 10);
        f.mtime = st_be32(h + 14);
        size_t q = off + hs;
        if (!st_span(n, q, 36)) { rc = st_fail(err, ST_EFORMAT, "StuffIt 5 entry %s is truncated", f.name); break; }
        uint16_t f2 = st_be16(p + q);
        memcpy(f.finfo, p + q + 4, 10);
        q += 36;
        char path[4096];
        if (join(path, sizeof path, dir, f.name)) { rc = st_fail(err, ST_ELIMIT, "StuffIt path too long"); break; }
        if (flags & 0x40) {
            if (nd == cap) {
                cap = cap ? cap * 2 : 16;
                Sit5Dir *nv = realloc(dirs, cap * sizeof *nv);
                if (!nv) { rc = st_fail(err, ST_ENOMEM, "out of memory"); break; }
                dirs = nv;
            }
            dirs[nd].off = (uint32_t)off;
            dirs[nd].path = strdup(path);
            if (!dirs[nd++].path) { rc = st_fail(err, ST_ENOMEM, "out of memory"); break; }
            rc = st_archive_add(out, path, 1, &f, lim, err);
            off = q;
            continue;
        }
        if (flags & 0x20) { rc = st_fail(err, ST_EUNSUPPORTED, "StuffIt 5 entry %s is encrypted", f.name); break; }
        uint32_t rl = 0, rcl = 0;
        uint16_t rcrc = 0;
        int rmeth = 0;
        if (f2 & 1) {
            if (!st_span(n, q, 14)) { rc = st_fail(err, ST_EFORMAT, "StuffIt 5 fork header is truncated"); break; }
            rl = st_be32(p + q);
            rcl = st_be32(p + q + 4);
            rcrc = st_be16(p + q + 8);
            rmeth = p[q + 12];
            q += 14;
        }
        if (!st_span(n, q, (uint64_t)rcl + c)) { rc = st_fail(err, ST_EFORMAT, "StuffIt 5 forks of %s are truncated", path); break; }
        char what[300];
        snprintf(what, sizeof what, "resource fork of %.200s", f.name);
        rc = st_sit_unpack(rmeth, p + q, rcl, rl, rcrc, rmeth != 15, &f.rsrc, lim, what, err);
        if (rc == ST_OK) {
            snprintf(what, sizeof what, "data fork of %.200s", f.name);
            rc = st_sit_unpack(h[46], p + q + rcl, c, a, st_be16(h + 42), h[46] != 15, &f.data, lim, what, err);
        }
        if (rc == ST_OK) rc = st_archive_add(out, path, 0, &f, lim, err);
        if (rc) st_macfile_free(&f);
        off = q + rcl + c;
    }
    for (size_t i = 0; i < nd; i++) free(dirs[i].path);
    free(dirs);
    return rc;
}

int st_sit_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err) {
    int kind = st_sit_detect(p, n);
    int rc;
    if (kind == 1) rc = classic(p, n, out, lim, err);
    else if (kind == 5) rc = sit5(p, n, out, lim, err);
    else if (kind == 10) return st_fail(err, ST_EUNSUPPORTED, "StuffIt X (.sitx) archives");
    else return st_fail(err, ST_EFORMAT, "not a StuffIt archive");
    if (rc) st_archive_free(out);
    return rc;
}
