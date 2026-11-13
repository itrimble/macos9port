/* Content-based detection and the unwrap chain. */
#include "os999_unpack.h"
#include "st_int.h"
#include "st_image.h"

#include <stdio.h>
#include <stdlib.h>

StFormat st_detect(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n) {
    int s = st_sit_detect(p, n);
    if (s == 1) return ST_FMT_SIT;
    if (s == 5) return ST_FMT_SIT5;
    if (s == 10) return ST_FMT_SITX;
    if (st_cpt_detect(p, n)) return ST_FMT_CPT;
    if (st_zip_detect(p, n)) return ST_FMT_ZIP;
    int as = st_applesingle_detect(p, n);
    if (as == 1) return ST_FMT_APPLESINGLE;
    if (as == 2) return ST_FMT_APPLEDOUBLE;
    switch (st_image_detect(p, n, rsrc, rsrc_n)) {
    case ST_IMG_UDIF: return ST_FMT_UDIF;
    case ST_IMG_DC42: return ST_FMT_DC42;
    case ST_IMG_NDIF: return ST_FMT_NDIF;
    case ST_IMG_RAW: return ST_FMT_HFS_IMAGE;
    default: break;
    }
    if (st_iso_detect(p, n)) return ST_FMT_ISO;
    if (st_macbinary_detect(p, n) || (n >= 128 && p[0] == 0 && !memcmp(p + 102, "mBIN", 4)))
        return ST_FMT_MACBINARY;
    if (st_binhex_detect(p, n)) return ST_FMT_BINHEX;
    return ST_FMT_UNKNOWN;
}

const char *st_format_name(StFormat f) {
    static const char *names[] = {"plain file", "BinHex", "MacBinary", "AppleSingle", "AppleDouble",
                                  "ZIP", "StuffIt", "StuffIt 5", "StuffIt X", "Compact Pro",
                                  "Disk Copy 4.2", "NDIF disk image", "UDIF disk image", "HFS disk image", "ISO9660 disk image"};
    return (unsigned)f < sizeof names / sizeof *names ? names[f] : "unknown";
}

static void chain_add(StUnpackInfo *info, const char *what) {
    if (!info) return;
    size_t l = strlen(info->chain);
    snprintf(info->chain + l, sizeof info->chain - l, "%s%s", l ? " > " : "", what);
}

static int single(StArchive *out, StMacFile *f, const char *fallback, const StLimits *lim, StErr *err) {
    if (!f->name[0]) snprintf(f->name, sizeof f->name, "%s", fallback && *fallback ? fallback : "Untitled");
    char comp[256];
    snprintf(comp, sizeof comp, "%s", f->name);
    for (char *c = comp; *c; c++) if (*c == '/') *c = ':';
    return st_archive_add(out, comp, 0, f, lim, err);
}

static int unwrap(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rn, const char *name,
                  StArchive *out, StUnpackInfo *info, const StLimits *lim, StErr *err, uint32_t depth) {
    if (depth > lim->max_depth) return st_fail(err, ST_ELIMIT, "wrappers nest deeper than %u", lim->max_depth);
    StFormat fmt = st_detect(p, n, rsrc, rn);
    StMacFile f;
    int rc;
    switch (fmt) {
    case ST_FMT_BINHEX: case ST_FMT_MACBINARY: case ST_FMT_APPLESINGLE:
        chain_add(info, st_format_name(fmt));
        rc = fmt == ST_FMT_BINHEX ? st_binhex_decode(p, n, &f, lim, err)
           : fmt == ST_FMT_MACBINARY ? st_macbinary_decode(p, n, &f, lim, err)
           : st_applesingle_decode(p, n, &f, lim, err);
        if (rc) return rc;
        if (st_detect(f.data.p, f.data.n, f.rsrc.p, f.rsrc.n) != ST_FMT_UNKNOWN) {
            StArchive inner = {0};
            rc = unwrap(f.data.p, f.data.n, f.rsrc.p, f.rsrc.n, f.name, &inner, info, lim, err, depth + 1);
            if (rc == ST_OK) {
                st_archive_free(out);
                *out = inner;
                st_macfile_free(&f);
                return ST_OK;
            }
            st_archive_free(&inner);
            st_macfile_free(&f);
            return rc;
        }
        rc = single(out, &f, name, lim, err);
        if (rc) st_macfile_free(&f);
        return rc;
    case ST_FMT_ZIP: chain_add(info, "ZIP"); return st_zip_decode(p, n, out, lim, err);
    case ST_FMT_SIT: case ST_FMT_SIT5: case ST_FMT_SITX:
        chain_add(info, st_format_name(fmt));
        return st_sit_decode(p, n, out, lim, err);
    case ST_FMT_CPT: chain_add(info, "Compact Pro"); return st_cpt_decode(p, n, out, lim, err);
    case ST_FMT_UDIF: case ST_FMT_DC42: case ST_FMT_NDIF: case ST_FMT_HFS_IMAGE: {
        chain_add(info, st_format_name(fmt));
        StBytes raw = {0};
        rc = st_image_decode(p, n, rsrc, rn, &raw, lim, err);
        size_t off = 0, len = 0;
        if (rc == ST_OK) rc = st_find_hfs(raw.p, raw.n, &off, &len, err);
        if (rc == ST_OK) {
            char vol[256] = "";
            StArchive vola = {0};
            rc = st_hfs_read(raw.p + off, len, &vola, vol, sizeof vol, lim, err);
            if (rc == ST_EFORMAT) st_explain_short_hfs(raw.p + off, len, err);
            if (rc == ST_OK) {
                chain_add(info, raw.p[off + 1024] == 'B' ? "HFS" : "HFS+");
                if (info) snprintf(info->volume, sizeof info->volume, "%s", vol);
                st_archive_free(out);
                *out = vola;
            }
        }
        st_bytes_free(&raw);
        return rc;
    }
    case ST_FMT_ISO: {
        char volume[256];
        chain_add(info, st_format_name(fmt));
        rc = st_iso_read(p, n, out, volume, sizeof volume, lim, err);
        if (!rc && info) snprintf(info->volume, sizeof info->volume, "%s", volume);
        return rc;
    }
    case ST_FMT_APPLEDOUBLE:
        return st_fail(err, ST_EINVAL, "an AppleDouble file holds metadata only; open its data file");
    default:
        st_macfile_init(&f);
        if ((rc = st_take(&f.data, p, n, lim, err))) return rc;
        if (rsrc && rn && (rc = st_take(&f.rsrc, rsrc, rn, lim, err))) { st_macfile_free(&f); return rc; }
        rc = single(out, &f, name, lim, err);
        if (rc) st_macfile_free(&f);
        return rc;
    }
}

int st_unpack(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n, const char *name,
              StArchive *out, StUnpackInfo *info, const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    if (info) memset(info, 0, sizeof *info);
    memset(out, 0, sizeof *out);
    int rc = unwrap(p, n, rsrc, rsrc_n, name, out, info, lim, err, 0);
    if (rc) st_archive_free(out);
    return rc;
}
