/* HFS+ / HFSX reader (Mac OS Extended). Names are UTF-16 in the
 * catalog and become UTF-8 here; the journal is not replayed. */
#include "st_hfs_int.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint32_t id, parent;
    char name[256];
    uint8_t finfo[16], fxinfo[16];
    uint32_t ctime, mtime;
    uint64_t dsize, rsize;
    StExt dext[8], rext[8];
} HpFile;

typedef struct {
    StHfsDirs dirs;
    HpFile *files;
    size_t nfiles, cap;
    const StLimits *lim;
} HpCtx;

static void ext8(const uint8_t *p, StExt *e) {
    for (int i = 0; i < 8; i++) { e[i].start = st_be32(p + 8 * i); e[i].count = st_be32(p + 8 * i + 4); }
}

/* UTF-16BE (with surrogates) to UTF-8; unpaired surrogates become U+FFFD. */
static int utf16_name(const uint8_t *p, size_t units, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < units; i++) {
        uint32_t c = st_be16(p + 2 * i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < units) {
            uint32_t lo = st_be16(p + 2 * i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
            else c = 0xFFFD;
        } else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
        if (c == 0) c = 0xFFFD; /* private-data folders start with NULs */
        uint8_t t[4];
        size_t k;
        if (c < 0x80) { t[0] = (uint8_t)c; k = 1; }
        else if (c < 0x800) { t[0] = (uint8_t)(0xC0 | c >> 6); t[1] = (uint8_t)(0x80 | (c & 63)); k = 2; }
        else if (c < 0x10000) { t[0] = (uint8_t)(0xE0 | c >> 12); t[1] = (uint8_t)(0x80 | ((c >> 6) & 63)); t[2] = (uint8_t)(0x80 | (c & 63)); k = 3; }
        else { t[0] = (uint8_t)(0xF0 | c >> 18); t[1] = (uint8_t)(0x80 | ((c >> 12) & 63)); t[2] = (uint8_t)(0x80 | ((c >> 6) & 63)); t[3] = (uint8_t)(0x80 | (c & 63)); k = 4; }
        if (o + k >= cap) return -1;
        memcpy(out + o, t, k);
        o += k;
    }
    out[o] = 0;
    return 0;
}

static int xt_leaf(void *c, const uint8_t *r, size_t len, StErr *err) {
    StHfsVol *v = c;
    if (len < 12 + 64 || st_be16(r) < 10) return ST_OK;
    StOverflow *nv = realloc(v->ovf, (v->novf + 1) * sizeof *nv);
    if (!nv) return st_fail(err, ST_ENOMEM, "out of memory");
    v->ovf = nv;
    StOverflow *o = &v->ovf[v->novf++];
    o->fork = r[2];
    o->file_id = st_be32(r + 4);
    o->start_block = st_be32(r + 8);
    ext8(r + 12, o->ext);
    return ST_OK;
}

static int cat_leaf(void *c, const uint8_t *r, size_t len, StErr *err) {
    HpCtx *x = c;
    if (len < 8) return ST_OK;
    size_t kl = st_be16(r), units = st_be16(r + 6);
    if (kl < 6 || 8 + units * 2 > 2 + kl || 2 + kl + 2 > len) return st_fail(err, ST_EFORMAT, "HFS+ catalog key is invalid");
    const uint8_t *rec = r + 2 + kl;
    size_t rl = len - 2 - kl;
    uint16_t type = st_be16(rec);
    if (type != 1 && type != 2) return ST_OK;
    char name[256];
    if (utf16_name(r + 8, units, name, sizeof name)) return st_fail(err, ST_EFORMAT, "HFS+ name is too long");
    uint32_t parent = st_be32(r + 2);
    if (type == 1 && rl >= 88) {
        int rc = st_hfs_dir_add(&x->dirs, st_be32(rec + 8), parent, name, err);
        if (rc) return rc;
        StMacFile *m = &x->dirs.v[x->dirs.n - 1].meta;
        memcpy(m->finfo, rec + 48, 16);
        memcpy(m->fxinfo, rec + 64, 16);
        m->ctime = st_be32(rec + 12);
        m->mtime = st_be32(rec + 16);
        return ST_OK;
    }
    if (type != 2 || rl < 248) return ST_OK;
    if (x->nfiles >= x->lim->max_entries) return st_fail(err, ST_ELIMIT, "too many files");
    if (x->nfiles == x->cap) {
        size_t cap = x->cap ? x->cap * 2 : 64;
        HpFile *nv = realloc(x->files, cap * sizeof *nv);
        if (!nv) return st_fail(err, ST_ENOMEM, "out of memory");
        x->files = nv;
        x->cap = cap;
    }
    HpFile *f = &x->files[x->nfiles++];
    f->id = st_be32(rec + 8);
    f->parent = parent;
    memcpy(f->name, name, sizeof f->name);
    f->ctime = st_be32(rec + 12);
    f->mtime = st_be32(rec + 16);
    memcpy(f->finfo, rec + 48, 16);
    memcpy(f->fxinfo, rec + 64, 16);
    f->dsize = st_be64(rec + 88);
    ext8(rec + 88 + 16, f->dext);
    f->rsize = st_be64(rec + 168);
    ext8(rec + 168 + 16, f->rext);
    return ST_OK;
}

static int special_fork(const StHfsVol *v, const uint8_t *fd, uint32_t id, StBytes *out, const StLimits *lim, StErr *err) {
    StExt e[8];
    ext8(fd + 16, e);
    return st_hfs_read_fork(v, id, 0, e, 8, st_be64(fd), out, lim, err);
}

int st_hfsplus_read(const uint8_t *vol, size_t n, StArchive *out, char *vol_name, size_t vol_cap,
                    const StLimits *lim, StErr *err) {
    const uint8_t *h = vol + 1024;
    uint32_t blksz = st_be32(h + 40);
    if (blksz < 512 || (blksz & (blksz - 1))) return st_fail(err, ST_EFORMAT, "HFS+ block size %u is invalid", blksz);
    StHfsVol v = {vol, n, 0, blksz, NULL, 0};
    HpCtx x;
    memset(&x, 0, sizeof x);
    x.lim = lim;
    StBytes xt = {0}, cat = {0};
    int rc = special_fork(&v, h + 192, 3, &xt, lim, err);
    if (rc == ST_OK && xt.n) rc = st_btree_leaves(xt.p, xt.n, xt_leaf, &v, err);
    if (rc == ST_OK) rc = special_fork(&v, h + 272, 4, &cat, lim, err);
    if (rc == ST_OK) rc = st_btree_leaves(cat.p, cat.n, cat_leaf, &x, err);
    char dir[4096], path[4096];
    for (size_t i = 0; rc == ST_OK && i < x.dirs.n; i++) {
        StHfsDir *d = &x.dirs.v[i];
        if (d->id == 2) {
            if (vol_name && vol_cap) snprintf(vol_name, vol_cap, "%s", d->name);
            continue;
        }
        if (strstr(d->name, "HFS+ Private D")) continue; /* hard-link stores */
        if (st_hfs_dir_path(&x.dirs, d->id, path, sizeof path) || strstr(path, "HFS+ Private D")) continue;
        StMacFile meta = d->meta;
        snprintf(meta.name, sizeof meta.name, "%s", d->name);
        rc = st_archive_add(out, path, 1, &meta, lim, err);
    }
    for (size_t i = 0; rc == ST_OK && i < x.nfiles; i++) {
        HpFile *f = &x.files[i];
        if (st_hfs_dir_path(&x.dirs, f->parent, dir, sizeof dir) || strstr(dir, "HFS+ Private D")) continue;
        if (!strncmp(f->name, ".journal", 8) && f->parent == 2) continue;
        if (!memcmp(f->finfo, "slnkrhap", 8)) continue; /* symlinks are never materialized */
        if (st_hfs_join(path, sizeof path, dir, f->name)) { rc = st_fail(err, ST_ELIMIT, "HFS+ path is too long"); break; }
        StMacFile mf;
        st_macfile_init(&mf);
        memcpy(mf.name, f->name, sizeof mf.name);
        memcpy(mf.finfo, f->finfo, 16);
        memcpy(mf.fxinfo, f->fxinfo, 16);
        mf.ctime = f->ctime;
        mf.mtime = f->mtime;
        rc = st_hfs_read_fork(&v, f->id, 0, f->dext, 8, f->dsize, &mf.data, lim, err);
        if (rc == ST_OK) rc = st_hfs_read_fork(&v, f->id, 0xFF, f->rext, 8, f->rsize, &mf.rsrc, lim, err);
        if (rc == ST_OK) rc = st_archive_add(out, path, 0, &mf, lim, err);
        if (rc) st_macfile_free(&mf);
    }
    st_bytes_free(&xt);
    st_bytes_free(&cat);
    free(v.ovf);
    free(x.dirs.v);
    free(x.files);
    if (rc) st_archive_free(out);
    return rc;
}
