/* HFS (Mac OS Standard) reader. Dispatches to HFS+ for 'H+'/'HX'
 * volumes and for HFS wrappers that embed an HFS+ volume. */
#include "st_hfs_int.h"

#include <stdio.h>
#include <stdlib.h>

int st_hfsplus_read(const uint8_t *vol, size_t n, StArchive *out, char *vol_name, size_t vol_cap,
                    const StLimits *lim, StErr *err);

typedef struct {
    uint32_t id, parent;
    char name[256];
    uint8_t finfo[16], fxinfo[16];
    uint32_t ctime, mtime;
    uint32_t dsize, rsize;
    StExt dext[3], rext[3];
} HfsFile;

typedef struct {
    StHfsVol *v;
    StHfsDirs dirs;
    HfsFile *files;
    size_t nfiles, cap;
    const StLimits *lim;
} HfsCtx;

static void ext3(const uint8_t *p, StExt *e) {
    for (int i = 0; i < 3; i++) { e[i].start = st_be16(p + 4 * i); e[i].count = st_be16(p + 4 * i + 2); }
}

static int xt_leaf(void *c, const uint8_t *r, size_t len, StErr *err) {
    StHfsVol *v = c;
    if (len < 8 + 12 || r[0] < 7) return ST_OK;
    StOverflow *nv = realloc(v->ovf, (v->novf + 1) * sizeof *nv);
    if (!nv) return st_fail(err, ST_ENOMEM, "out of memory");
    v->ovf = nv;
    StOverflow *o = &v->ovf[v->novf++];
    memset(o, 0, sizeof *o);
    o->fork = r[1];
    o->file_id = st_be32(r + 2);
    o->start_block = st_be16(r + 6);
    ext3(r + 8, o->ext);
    return ST_OK;
}

static int cat_leaf(void *c, const uint8_t *r, size_t len, StErr *err) {
    HfsCtx *x = c;
    if (len < 8 || r[0] < 6) return ST_OK;
    size_t kl = r[0], nl = r[6];
    if (nl > 31 || 7 + nl > 1 + kl) return st_fail(err, ST_EFORMAT, "HFS catalog key is invalid");
    size_t d = (1 + kl + 1) & ~(size_t)1;
    if (d + 2 > len) return st_fail(err, ST_EFORMAT, "HFS catalog record is truncated");
    const uint8_t *rec = r + d;
    size_t rl = len - d;
    char name[256];
    if (st_macroman_to_utf8(r + 7, nl, name, sizeof name) < 0) return st_fail(err, ST_EFORMAT, "HFS name is invalid");
    uint32_t parent = st_be32(r + 2);
    if (rec[0] == 1 && rl >= 70) {
        int rc = st_hfs_dir_add(&x->dirs, st_be32(rec + 6), parent, name, err);
        if (rc) return rc;
        StMacFile *m = &x->dirs.v[x->dirs.n - 1].meta;
        memcpy(m->finfo, rec + 22, 16);
        memcpy(m->fxinfo, rec + 38, 16);
        m->ctime = st_be32(rec + 10);
        m->mtime = st_be32(rec + 14);
    } else if (rec[0] == 2 && rl >= 102) {
        if (x->nfiles == x->cap) {
            size_t cap = x->cap ? x->cap * 2 : 64;
            HfsFile *nv = realloc(x->files, cap * sizeof *nv);
            if (!nv) return st_fail(err, ST_ENOMEM, "out of memory");
            x->files = nv;
            x->cap = cap;
        }
        if (x->nfiles >= x->lim->max_entries) return st_fail(err, ST_ELIMIT, "too many files");
        HfsFile *f = &x->files[x->nfiles++];
        memset(f, 0, sizeof *f);
        f->id = st_be32(rec + 20);
        f->parent = parent;
        memcpy(f->name, name, sizeof f->name);
        memcpy(f->finfo, rec + 4, 16);
        memcpy(f->fxinfo, rec + 56, 16);
        f->dsize = st_be32(rec + 26);
        f->rsize = st_be32(rec + 36);
        f->ctime = st_be32(rec + 44);
        f->mtime = st_be32(rec + 48);
        ext3(rec + 74, f->dext);
        ext3(rec + 86, f->rext);
    }
    return ST_OK;
}

int st_hfs_read(const uint8_t *vol, size_t n, StArchive *out, char *vol_name, size_t vol_cap,
                const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    if (n < 1024 + 512) return st_fail(err, ST_EFORMAT, "volume is too small");
    const uint8_t *m = vol + 1024;
    uint16_t sig = st_be16(m);
    if (sig == 0x482B || sig == 0x4858) return st_hfsplus_read(vol, n, out, vol_name, vol_cap, lim, err);
    if (sig != 0x4244) return st_fail(err, ST_EFORMAT, "no HFS master directory block");
    uint32_t blksz = st_be32(m + 20);
    uint64_t base = (uint64_t)st_be16(m + 28) * 512;
    if (!blksz || blksz % 512) return st_fail(err, ST_EFORMAT, "HFS allocation block size %u is invalid", blksz);
    if (st_be16(m + 124) == 0x482B) { /* HFS wrapper around HFS+ */
        uint64_t off = base + (uint64_t)st_be16(m + 126) * blksz;
        uint64_t len = (uint64_t)st_be16(m + 128) * blksz;
        if (!st_span(n, off, len)) return st_fail(err, ST_EFORMAT, "embedded HFS+ volume lies outside the image");
        return st_hfsplus_read(vol + off, (size_t)len, out, vol_name, vol_cap, lim, err);
    }
    if (vol_name && vol_cap) {
        size_t vl = m[36] > 27 ? 27 : m[36];
        if (st_macroman_to_utf8(m + 37, vl, vol_name, vol_cap) < 0) vol_name[0] = 0;
    }
    StHfsVol v = {vol, n, base, blksz, NULL, 0};
    HfsCtx x;
    memset(&x, 0, sizeof x);
    x.v = &v;
    x.lim = lim;
    StBytes xt = {0}, cat = {0};
    StExt e[3];
    ext3(m + 134, e);
    int rc = st_hfs_read_fork(&v, 3, 0, e, 3, st_be32(m + 130), &xt, lim, err);
    if (rc == ST_OK && xt.n) rc = st_btree_leaves(xt.p, xt.n, xt_leaf, &v, err);
    if (rc == ST_OK) {
        ext3(m + 150, e);
        rc = st_hfs_read_fork(&v, 4, 0, e, 3, st_be32(m + 146), &cat, lim, err);
    }
    if (rc == ST_OK) rc = st_btree_leaves(cat.p, cat.n, cat_leaf, &x, err);
    char dir[4096], path[4096];
    for (size_t i = 0; rc == ST_OK && i < x.dirs.n; i++) {
        if (x.dirs.v[i].id == 2) continue;
        if (st_hfs_dir_path(&x.dirs, x.dirs.v[i].id, path, sizeof path)) {
            rc = st_fail(err, ST_EFORMAT, "HFS folder %u has a broken parent chain", x.dirs.v[i].id);
            break;
        }
        StMacFile meta = x.dirs.v[i].meta;
        snprintf(meta.name, sizeof meta.name, "%s", x.dirs.v[i].name);
        rc = st_archive_add(out, path, 1, &meta, lim, err);
    }
    for (size_t i = 0; rc == ST_OK && i < x.nfiles; i++) {
        HfsFile *f = &x.files[i];
        if (st_hfs_dir_path(&x.dirs, f->parent, dir, sizeof dir) || st_hfs_join(path, sizeof path, dir, f->name)) {
            rc = st_fail(err, ST_EFORMAT, "HFS file %s has a broken parent chain", f->name);
            break;
        }
        StMacFile mf;
        st_macfile_init(&mf);
        memcpy(mf.name, f->name, sizeof mf.name);
        memcpy(mf.finfo, f->finfo, 16);
        memcpy(mf.fxinfo, f->fxinfo, 16);
        mf.ctime = f->ctime;
        mf.mtime = f->mtime;
        rc = st_hfs_read_fork(&v, f->id, 0, f->dext, 3, f->dsize, &mf.data, lim, err);
        if (rc == ST_OK) rc = st_hfs_read_fork(&v, f->id, 0xFF, f->rext, 3, f->rsize, &mf.rsrc, lim, err);
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
