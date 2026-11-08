/* Guest volume: opening, containment, listing, reading and usage. */
#include "st_volume_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static int ends_rsrc(const char *s) {
    size_t n = strlen(s);
    return n >= 5 && !strcasecmp(s + n - 5, ".rsrc");
}

int st_vol_host_name(const char *guest, char *host, size_t cap) {
    if (!guest || !*guest || strchr(guest, '/')) return -1;
    if (!strcmp(guest, ".") || !strcmp(guest, "..") || !strncmp(guest, ".os999-", 7)) return -1;
    for (const unsigned char *c = (const unsigned char *)guest; *c; c++)
        if (*c < 0x20 || *c == 0x7F) return -1;
    int k = snprintf(host, cap, "%s%s", guest, ends_rsrc(guest) ? "_" : "");
    return k < 0 || (size_t)k >= cap || k > 255 ? -1 : 0;
}

void st_vol_guest_name(const char *host, char *guest, size_t cap) {
    snprintf(guest, cap, "%s", host);
    size_t n = strlen(guest);
    if (n > 6 && guest[n - 1] == '_') {
        guest[n - 1] = 0;
        if (!ends_rsrc(guest)) guest[n - 1] = '_';
    }
}

int st_vol_host_path(const char *rel, char *out, size_t cap) {
    size_t o = 0;
    out[0] = 0;
    for (const char *s = rel; *s;) {
        const char *e = strchr(s, '/');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len) {
            char g[256], h[260];
            if (len >= sizeof g) return -1;
            memcpy(g, s, len);
            g[len] = 0;
            if (st_vol_host_name(g, h, sizeof h)) return -1;
            size_t hl = strlen(h);
            if (o + hl + 2 > cap) return -1;
            if (o) out[o++] = '/';
            memcpy(out + o, h, hl + 1);
            o += hl;
        }
        s = e ? e + 1 : s + len;
    }
    return 0;
}

int st_vol_fsync_dir(int fd) { return fsync(fd) == 0 || errno == EINVAL ? 0 : -1; }

int st_vol_step(StVolume *v, StErr *err) {
    if (v->crash_steps > 0 && --v->crash_steps == 0)
        return st_fail(err, ST_EIO, "simulated crash");
    return ST_OK;
}

void st_vol_crash_after(StVolume *v, int steps) { v->crash_steps = steps; }

int st_vol_parent(StVolume *v, const char *rel, int create, int *dirfd, char *leaf, size_t cap, StErr *err) {
    char host[4096];
    if (st_vol_host_path(rel, host, sizeof host) || !host[0])
        return st_fail(err, ST_EPATH, "unsafe guest path \"%s\"", rel);
    int fd = dup(v->root), depth = 0;
    if (fd < 0) return st_fail(err, ST_EIO, "dup: %s", strerror(errno));
    char *s = host;
    for (;;) {
        char *slash = strchr(s, '/');
        if (!slash) break;
        *slash = 0;
        if (++depth > ST_VOL_MAX_DEPTH) { close(fd); return st_fail(err, ST_ELIMIT, "folders nest too deeply"); }
        int next = openat(fd, s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0 && errno == ENOENT && create && !v->opt.read_only) {
            if (mkdirat(fd, s, 0755) != 0 && errno != EEXIST) {
                int e = errno;
                close(fd);
                return st_fail(err, ST_EIO, "mkdir %s: %s", s, strerror(e));
            }
            v->use.dirs++;
            next = openat(fd, s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (next < 0) {
            int e = errno;
            close(fd);
            if (e == ELOOP || e == ENOTDIR) return st_fail(err, ST_EPATH, "%s is not a folder (symlinks are refused)", s);
            return st_fail(err, e == ENOENT ? ST_ENOENT : ST_EIO, "%s: %s", s, strerror(e));
        }
        close(fd);
        fd = next;
        s = slash + 1;
    }
    if (strlen(s) >= cap) { close(fd); return st_fail(err, ST_EPATH, "name too long"); }
    strcpy(leaf, s);
    *dirfd = fd;
    return ST_OK;
}

/* Walk the tree: count usage, delete stale temporaries, ignore symlinks. */
static int scan_dir(StVolume *v, int fd, int depth, StErr *err) {
    if (depth > ST_VOL_MAX_DEPTH) return st_fail(err, ST_ELIMIT, "folders nest too deeply");
    int dup_fd = dup(fd);
    DIR *d = dup_fd >= 0 ? fdopendir(dup_fd) : NULL;
    if (!d) { if (dup_fd >= 0) close(dup_fd); return st_fail(err, ST_EIO, "cannot list folder"); }
    rewinddir(d);
    struct dirent *e;
    int rc = ST_OK;
    while (rc == ST_OK && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!strncmp(e->d_name, ST_VOL_TMP, strlen(ST_VOL_TMP))) {
            if (!v->opt.read_only) { unlinkat(fd, e->d_name, 0); v->use.recovered++; }
            continue;
        }
        if (depth == 0 && !strncmp(e->d_name, ".os999-", 7)) continue;
        struct stat st;
        if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            v->use.dirs++;
            int sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (sub >= 0) { rc = scan_dir(v, sub, depth + 1, err); close(sub); }
        } else if (S_ISREG(st.st_mode)) {
            v->use.bytes += (uint64_t)st.st_size;
            if (!ends_rsrc(e->d_name)) v->use.files++;
        }
    }
    closedir(d);
    return rc;
}

int st_vol_scan(StVolume *v, StErr *err) {
    int rec = v->use.recovered;
    memset(&v->use, 0, sizeof v->use);
    v->use.recovered = rec;
    return scan_dir(v, v->root, 0, err);
}

int st_vol_open(const char *root, const StVolumeOptions *opt, StVolume **out, StErr *err) {
    *out = NULL;
    StVolume *v = calloc(1, sizeof *v);
    if (!v) return st_fail(err, ST_ENOMEM, "out of memory");
    if (opt) v->opt = *opt;
    v->lock = -1;
    v->root = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (v->root < 0) {
        int e = errno;
        free(v);
        return st_fail(err, ST_EIO, "cannot open volume %s: %s", root, strerror(e));
    }
    int rc = ST_OK;
    if (!v->opt.read_only) {
        v->lock = openat(v->root, ST_VOL_LOCK, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (v->lock < 0 || flock(v->lock, LOCK_EX | LOCK_NB) != 0)
            rc = st_fail(err, ST_EIO, "volume %s is in use by another process", root);
        if (rc == ST_OK) rc = st_vol_recover(v, err);
        if (rc == ST_OK) rc = st_vol_install_recover(v, err);
    }
    if (rc == ST_OK) rc = st_vol_scan(v, err);
    if (rc) { st_vol_close(v); return rc; }
    *out = v;
    return ST_OK;
}

void st_vol_close(StVolume *v) {
    if (!v) return;
    if (v->lock >= 0) close(v->lock);
    if (v->root >= 0) close(v->root);
    free(v);
}

void st_vol_usage(const StVolume *v, StVolumeUsage *u) { *u = v->use; }

static int read_fd(int fd, StBytes *b, const StLimits *lim, StErr *err) {
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) return st_fail(err, ST_EPATH, "not a regular file");
    if (lim && (uint64_t)st.st_size > lim->max_entry) return st_fail(err, ST_ELIMIT, "file is too large");
    b->n = 0;
    if (st_bytes_reserve(b, (size_t)st.st_size + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t k = read(fd, b->p + got, (size_t)st.st_size - got);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return st_fail(err, ST_EIO, "read failed"); }
        got += (size_t)k;
    }
    b->n = got;
    return ST_OK;
}

int st_vol_read(StVolume *v, const char *rel, StMacFile *out, const StLimits *lim, StErr *err) {
    int dir;
    char leaf[300], side[310];
    int rc = st_vol_parent(v, rel, 0, &dir, leaf, sizeof leaf, err);
    if (rc) return rc;
    st_macfile_init(out);
    st_vol_guest_name(leaf, out->name, sizeof out->name);
    int fd = openat(dir, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        close(dir);
        return st_fail(err, e == ENOENT ? ST_ENOENT : ST_EPATH, "%s: %s", rel, strerror(e));
    }
    rc = read_fd(fd, &out->data, lim, err);
    struct stat st;
    if (rc == ST_OK && fstat(fd, &st) == 0) out->mtime = (uint32_t)(st.st_mtime + ST_MAC_EPOCH_DELTA);
    close(fd);
    snprintf(side, sizeof side, "%s.rsrc", leaf);
    fd = rc ? -1 : openat(dir, side, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) {
        StBytes s = {0};
        rc = read_fd(fd, &s, lim, err);
        close(fd);
        StMacFile meta;
        if (rc == ST_OK && st_applesingle_detect(s.p, s.n) &&
            st_applesingle_decode(s.p, s.n, &meta, lim, NULL) == ST_OK) {
            memcpy(out->finfo, meta.finfo, 16);
            memcpy(out->fxinfo, meta.fxinfo, 16);
            if (meta.ctime) out->ctime = meta.ctime;
            if (meta.mtime) out->mtime = meta.mtime;
            out->rsrc = meta.rsrc;
            st_bytes_free(&meta.data);
            st_bytes_free(&s);
        } else if (rc == ST_OK) {
            out->rsrc = s; /* raw resource fork export */
        } else {
            st_bytes_free(&s);
        }
    }
    close(dir);
    if (rc) st_macfile_free(out);
    return rc;
}

int st_vol_list(StVolume *v, const char *rel, StVolumeEntry **out, size_t *count, StErr *err) {
    *out = NULL;
    *count = 0;
    int fd;
    if (!rel || !*rel) fd = dup(v->root);
    else {
        char leaf[300];
        int dir, rc = st_vol_parent(v, rel, 0, &dir, leaf, sizeof leaf, err);
        if (rc) return rc;
        fd = openat(dir, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(dir);
    }
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) { if (fd >= 0) close(fd); return st_fail(err, ST_ENOENT, "cannot list %s", rel); }
    rewinddir(d);
    size_t cap = 0;
    struct dirent *e;
    int rc = ST_OK;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && (!e->d_name[1] || !strcmp(e->d_name, "..") || !strncmp(e->d_name, ".os999-", 7))) continue;
        if (ends_rsrc(e->d_name)) continue;
        struct stat st;
        if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
        if (*count == cap) {
            cap = cap ? cap * 2 : 32;
            StVolumeEntry *nv = realloc(*out, cap * sizeof *nv);
            if (!nv) { rc = st_fail(err, ST_ENOMEM, "out of memory"); break; }
            *out = nv;
        }
        StVolumeEntry *x = &(*out)[(*count)++];
        memset(x, 0, sizeof *x);
        st_vol_guest_name(e->d_name, x->name, sizeof x->name);
        x->is_dir = S_ISDIR(st.st_mode);
        x->data_size = x->is_dir ? 0 : (uint64_t)st.st_size;
        char side[310];
        snprintf(side, sizeof side, "%s.rsrc", e->d_name);
        int sfd = x->is_dir ? -1 : openat(dirfd(d), side, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (sfd >= 0) {
            StBytes s = {0};
            StMacFile meta;
            if (read_fd(sfd, &s, NULL, NULL) == ST_OK && st_applesingle_detect(s.p, s.n) &&
                st_applesingle_decode(s.p, s.n, &meta, NULL, NULL) == ST_OK) {
                memcpy(x->finfo, meta.finfo, 16);
                x->rsrc_size = meta.rsrc.n;
                st_macfile_free(&meta);
            } else {
                x->rsrc_size = s.n;
            }
            st_bytes_free(&s);
            close(sfd);
        }
    }
    closedir(d);
    if (rc) { free(*out); *out = NULL; *count = 0; }
    return rc;
}
