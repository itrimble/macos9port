#include "st_int.h"

int st_sidecar_apply(StMacFile *f, const StBytes *s, const StLimits *lim, StErr *err) {
    uint32_t magic = s->n >= 4 ? st_be32(s->p) : 0;
    if (magic != 0x00051600u && magic != 0x00051607u)
        return st_take(&f->rsrc, s->p, s->n, lim, err);
    StMacFile meta;
    int rc = st_applesingle_decode(s->p, s->n, &meta, lim, err);
    if (rc) return rc;
    memcpy(f->finfo, meta.finfo, 16);
    memcpy(f->fxinfo, meta.fxinfo, 16);
    f->ctime = meta.ctime;
    f->mtime = meta.mtime;
    st_bytes_free(&f->rsrc);
    f->rsrc = meta.rsrc;
    st_bytes_free(&meta.data);
    return ST_OK;
}
