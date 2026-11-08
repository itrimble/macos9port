/* Shared HFS/HFS+ machinery: B-tree leaf walks, fork assembly from
 * extents (with overflow records) and folder path reconstruction. */
#include "st_hfs_int.h"

#include <stdio.h>
#include <stdlib.h>

int st_btree_leaves(const uint8_t *t, size_t n, StLeafFn fn, void *ctx, StErr *err) {
    if (n < 512) return st_fail(err, ST_EFORMAT, "B-tree is truncated");
    if ((int8_t)t[8] != 1) return st_fail(err, ST_EFORMAT, "B-tree header node is missing");
    const uint8_t *h = t + 14;
    uint32_t leaf = st_be32(h + 10), total = st_be32(h + 22);
    uint16_t ns = st_be16(h + 18);
    if (ns < 512 || ns > 32768 || (ns & (ns - 1))) return st_fail(err, ST_EFORMAT, "B-tree node size %u is invalid", ns);
    uint32_t visited = 0;
    while (leaf) {
        if ((uint64_t)leaf * ns + ns > n || leaf >= total) return st_fail(err, ST_EFORMAT, "B-tree leaf %u is outside the tree", leaf);
        if (++visited > total) return st_fail(err, ST_EFORMAT, "B-tree leaf chain loops");
        const uint8_t *node = t + (size_t)leaf * ns;
        if ((int8_t)node[8] != -1) return st_fail(err, ST_EFORMAT, "B-tree node %u is not a leaf", leaf);
        uint16_t nrec = st_be16(node + 10);
        if ((size_t)nrec * 2 + 14 > ns) return st_fail(err, ST_EFORMAT, "B-tree node %u has too many records", leaf);
        for (uint16_t r = 0; r < nrec; r++) {
            uint16_t a = st_be16(node + ns - 2 * (r + 1)), b = st_be16(node + ns - 2 * (r + 2));
            if (a < 14 || b <= a || b > ns - 2 * (nrec + 1)) {
                if (r + 1 == nrec && a >= 14 && a < ns - 2 * (nrec + 1)) b = (uint16_t)(ns - 2 * (nrec + 1));
                else return st_fail(err, ST_EFORMAT, "B-tree record %u of node %u is invalid", r, leaf);
            }
            int rc = fn(ctx, node + a, (size_t)(b - a), err);
            if (rc) return rc;
        }
        leaf = st_be32(node);
    }
    return ST_OK;
}

int st_hfs_read_fork(const StHfsVol *v, uint32_t file_id, uint8_t fork, const StExt *first, int nfirst,
                     uint64_t size, StBytes *out, const StLimits *lim, StErr *err) {
    out->n = 0;
    if (!size) return ST_OK;
    if (lim && size > lim->max_entry) return st_fail(err, ST_ELIMIT, "fork of %llu bytes is too large", (unsigned long long)size);
    if (st_bytes_reserve(out, (size_t)size)) return st_fail(err, ST_ENOMEM, "out of memory");
    uint32_t logical = 0;
    const StExt *ext = first;
    int next = -1, guard = 0;
    for (;;) {
        for (int i = 0; i < nfirst && out->n < size; i++) {
            if (!ext[i].count) continue;
            uint64_t off = v->base + (uint64_t)ext[i].start * v->blksz;
            uint64_t len = (uint64_t)ext[i].count * v->blksz;
            if (len > size - out->n) len = size - out->n;
            if (!st_span(v->n, off, len)) return st_fail(err, ST_EFORMAT, "extent of file %u lies outside the volume", file_id);
            memcpy(out->p + out->n, v->vol + off, (size_t)len);
            out->n += (size_t)len;
            logical += ext[i].count;
        }
        if (out->n >= size) break;
        /* Continue with the overflow record that starts at `logical`. */
        next = -1;
        for (size_t k = 0; k < v->novf; k++)
            if (v->ovf[k].file_id == file_id && v->ovf[k].fork == fork && v->ovf[k].start_block == logical) {
                next = (int)k;
                break;
            }
        if (next < 0 || ++guard > 100000) return st_fail(err, ST_EFORMAT, "file %u is missing extents", file_id);
        ext = v->ovf[next].ext;
        nfirst = 8;
    }
    return ST_OK;
}

int st_hfs_dir_add(StHfsDirs *d, uint32_t id, uint32_t parent, const char *name, StErr *err) {
    if (d->n == d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 64;
        StHfsDir *nv = realloc(d->v, cap * sizeof *nv);
        if (!nv) return st_fail(err, ST_ENOMEM, "out of memory");
        d->v = nv;
        d->cap = cap;
    }
    StHfsDir *e = &d->v[d->n++];
    memset(e, 0, sizeof *e);
    e->id = id;
    e->parent = parent;
    snprintf(e->name, sizeof e->name, "%s", name);
    return ST_OK;
}

static const StHfsDir *dir_find(const StHfsDirs *d, uint32_t id) {
    for (size_t i = 0; i < d->n; i++) if (d->v[i].id == id) return &d->v[i];
    return NULL;
}

int st_hfs_join(char *out, size_t cap, const char *dir, const char *name) {
    char comp[256];
    snprintf(comp, sizeof comp, "%s", name);
    for (char *c = comp; *c; c++) if (*c == '/') *c = ':';
    int k = snprintf(out, cap, "%s%s%s", dir, *dir ? "/" : "", comp);
    return k < 0 || (size_t)k >= cap ? -1 : 0;
}

int st_hfs_dir_path(const StHfsDirs *d, uint32_t id, char *out, size_t cap) {
    const StHfsDir *chain[64];
    int depth = 0;
    while (id != 2 && id != 1) {
        const StHfsDir *e = dir_find(d, id);
        if (!e || depth == 64) return -1;
        chain[depth++] = e;
        id = e->parent;
    }
    out[0] = 0;
    for (int i = depth - 1; i >= 0; i--) {
        char tmp[4096];
        if (st_hfs_join(tmp, sizeof tmp, out, chain[i]->name)) return -1;
        if (strlen(tmp) >= cap) return -1;
        strcpy(out, tmp);
    }
    return 0;
}
