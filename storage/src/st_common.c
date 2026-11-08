#include "st_int.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

const char *st_status_text(int code) {
    switch (code) {
    case ST_OK: return "ok";
    case ST_EFORMAT: return "malformed input";
    case ST_EUNSUPPORTED: return "unsupported feature";
    case ST_ELIMIT: return "limit exceeded";
    case ST_ECHECKSUM: return "checksum mismatch";
    case ST_EIO: return "I/O error";
    case ST_ENOMEM: return "out of memory";
    case ST_EPATH: return "unsafe path";
    case ST_EQUOTA: return "quota exceeded";
    case ST_EEXIST: return "already exists";
    case ST_ENOENT: return "not found";
    case ST_EINVAL: return "invalid argument";
    default: return "unknown error";
    }
}

StLimits st_limits_default(void) {
    StLimits l;
    l.max_total = 2ull << 30;  /* 2 GiB */
    l.max_entry = 1ull << 30;  /* 1 GiB */
    l.max_entries = 100000;
    l.max_depth = 32;
    return l;
}

int st_fail(StErr *err, int code, const char *fmt, ...) {
    if (err) {
        err->code = code;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->msg, sizeof err->msg, fmt, ap);
        va_end(ap);
    }
    return code;
}

void st_bytes_free(StBytes *b) {
    if (!b) return;
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

int st_bytes_reserve(StBytes *b, size_t n) {
    if (n <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < n) {
        if (cap > SIZE_MAX / 2) { cap = n; break; }
        cap *= 2;
    }
    uint8_t *p = realloc(b->p, cap);
    if (!p) return -1;
    b->p = p;
    b->cap = cap;
    return 0;
}

int st_bytes_append(StBytes *b, const void *src, size_t n) {
    if (n > SIZE_MAX - b->n) return -1;
    if (st_bytes_reserve(b, b->n + n) != 0) return -1;
    if (n) memcpy(b->p + b->n, src, n);
    b->n += n;
    return 0;
}

int st_take(StBytes *b, const uint8_t *src, size_t n, const StLimits *lim, StErr *err) {
    if (lim && n > lim->max_entry)
        return st_fail(err, ST_ELIMIT, "fork of %zu bytes exceeds the %llu byte limit", n,
                       (unsigned long long)lim->max_entry);
    b->n = 0;
    if (st_bytes_append(b, src, n) != 0) return st_fail(err, ST_ENOMEM, "out of memory");
    return ST_OK;
}

int st_read_file(const char *path, uint64_t max, StBytes *out, StErr *err) {
    FILE *f = fopen(path, "rb");
    if (!f) return st_fail(err, ST_EIO, "cannot open %s: %s", path, strerror(errno));
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
        fclose(f);
        return st_fail(err, ST_EIO, "%s is not a regular file", path);
    }
    if ((uint64_t)st.st_size > max) {
        fclose(f);
        return st_fail(err, ST_ELIMIT, "%s is larger than %llu bytes", path, (unsigned long long)max);
    }
    out->n = 0;
    if (st_bytes_reserve(out, (size_t)st.st_size + 1) != 0) {
        fclose(f);
        return st_fail(err, ST_ENOMEM, "out of memory");
    }
    size_t got = fread(out->p, 1, (size_t)st.st_size, f);
    int bad = ferror(f);
    fclose(f);
    if (bad || got != (size_t)st.st_size) return st_fail(err, ST_EIO, "short read on %s", path);
    out->n = got;
    return ST_OK;
}

void st_macfile_init(StMacFile *f) { memset(f, 0, sizeof *f); }

void st_macfile_free(StMacFile *f) {
    st_bytes_free(&f->data);
    st_bytes_free(&f->rsrc);
}

uint32_t st_mac_type(const StMacFile *f) { return st_be32(f->finfo); }
uint32_t st_mac_creator(const StMacFile *f) { return st_be32(f->finfo + 4); }
uint16_t st_mac_flags(const StMacFile *f) { return st_be16(f->finfo + 8); }
void st_mac_set_type_creator(StMacFile *f, uint32_t type, uint32_t creator) {
    st_put32(f->finfo, type);
    st_put32(f->finfo + 4, creator);
}

int st_name_from_macroman(StMacFile *f, const uint8_t *name, size_t n, StErr *err) {
    if (n == 0) return st_fail(err, ST_EFORMAT, "empty file name");
    if (st_macroman_to_utf8(name, n, f->name, sizeof f->name) < 0)
        return st_fail(err, ST_EFORMAT, "file name does not fit");
    return ST_OK;
}

void st_archive_free(StArchive *a) {
    if (!a) return;
    for (size_t i = 0; i < a->n; i++) {
        free(a->v[i].path);
        st_macfile_free(&a->v[i].file);
    }
    free(a->v);
    memset(a, 0, sizeof *a);
}

int st_archive_add(StArchive *a, const char *path, int is_dir, StMacFile *file,
                   const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    if (a->n >= lim->max_entries)
        return st_fail(err, ST_ELIMIT, "more than %u entries", lim->max_entries);
    uint64_t add = file ? (uint64_t)file->data.n + file->rsrc.n : 0;
    if (add > lim->max_total || a->total > lim->max_total - add)
        return st_fail(err, ST_ELIMIT, "archive expands past %llu bytes",
                       (unsigned long long)lim->max_total);
    /* Sanitize every component and count the depth. */
    size_t plen = strlen(path);
    char *safe = malloc(plen * 3 + 8);
    if (!safe) return st_fail(err, ST_ENOMEM, "out of memory");
    size_t o = 0;
    uint32_t depth = 0;
    const char *s = path;
    while (*s) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len) {
            char comp[256], host[260];
            if (len >= sizeof comp) len = sizeof comp - 1;
            memcpy(comp, s, len);
            comp[len] = 0;
            if (st_host_component(comp, host, sizeof host) < 0) {
                free(safe);
                return st_fail(err, ST_EPATH, "unsafe name in %s", path);
            }
            size_t hl = strlen(host);
            if (o) safe[o++] = '/';
            memcpy(safe + o, host, hl);
            o += hl;
            if (++depth > lim->max_depth) {
                free(safe);
                return st_fail(err, ST_ELIMIT, "folders nest deeper than %u", lim->max_depth);
            }
        }
        s = e ? e + 1 : s + strlen(s);
    }
    safe[o] = 0;
    if (!o) {
        free(safe);
        return st_fail(err, ST_EPATH, "empty path");
    }
    if (a->n == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 32;
        StEntry *v = realloc(a->v, cap * sizeof *v);
        if (!v) {
            free(safe);
            return st_fail(err, ST_ENOMEM, "out of memory");
        }
        a->v = v;
        a->cap = cap;
    }
    StEntry *en = &a->v[a->n++];
    memset(en, 0, sizeof *en);
    en->path = safe;
    en->is_dir = is_dir;
    if (file) {
        en->file = *file;
        memset(&file->data, 0, sizeof file->data);
        memset(&file->rsrc, 0, sizeof file->rsrc);
    }
    a->total += add;
    return ST_OK;
}
