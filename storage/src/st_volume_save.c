/* Guest volume writes: journaled atomic saves, rename, remove, recovery
 * after an interrupted save, and all-or-nothing archive installs. */
#include "st_volume_int.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* Resolve a root-relative host path (already mapped) without creating. */
static int host_parent(StVolume *v, const char *path, int *dirfd, const char **leaf) {
    char buf[4096];
    if (strlen(path) >= sizeof buf) return -1;
    strcpy(buf, path);
    int fd = dup(v->root);
    char *s = buf;
    const char *last = strrchr(path, '/');
    *leaf = last ? last + 1 : path;
    while (fd >= 0) {
        char *slash = strchr(s, '/');
        if (!slash) break;
        *slash = 0;
        if (!strcmp(s, "..") || !strcmp(s, ".")) { close(fd); return -1; }
        int next = openat(fd, s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        fd = next;
        s = slash + 1;
    }
    if (fd < 0 || !**leaf || !strcmp(*leaf, "..")) { if (fd >= 0) close(fd); return -1; }
    *dirfd = fd;
    return 0;
}

int st_vol_journal_commit(StVolume *v, const char *plan, StErr *err) {
    int fd = openat(v->root, ST_VOL_JOURNAL, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return st_fail(err, ST_EIO, "cannot write the journal: %s", strerror(errno));
    size_t n = strlen(plan);
    int ok = write(fd, "OS999J1\n", 8) == 8 && write(fd, plan, n) == (ssize_t)n && write(fd, "COMMIT\n", 7) == 7;
    ok = ok && fsync(fd) == 0;
    close(fd);
    if (!ok || st_vol_fsync_dir(v->root)) return st_fail(err, ST_EIO, "cannot flush the journal");
    return ST_OK;
}

int st_vol_journal_apply(StVolume *v, const char *plan, int committed, StErr *err) {
    char line[8400];
    int first = 1;
    for (const char *s = plan; *s;) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len >= sizeof line) return st_fail(err, ST_EFORMAT, "journal line too long");
        memcpy(line, s, len);
        line[len] = 0;
        s = e ? e + 1 : s + len;
        int a, b;
        const char *la, *lb;
        if (line[0] == 'R' && line[1] == ' ') {
            char *tab = strchr(line + 2, '\t');
            if (!tab) continue;
            *tab = 0;
            if (host_parent(v, line + 2, &a, &la)) continue;
            if (!committed) { unlinkat(a, la, 0); close(a); continue; }
            if (host_parent(v, tab + 1, &b, &lb)) { close(a); continue; }
            if (renameat(a, la, b, lb) != 0 && errno != ENOENT) {
                int e2 = errno;
                close(a); close(b);
                return st_fail(err, ST_EIO, "rename failed: %s", strerror(e2));
            }
            st_vol_fsync_dir(b);
            close(a); close(b);
        } else if (line[0] == 'D' && line[1] == ' ' && committed) {
            if (host_parent(v, line + 2, &a, &la)) continue;
            unlinkat(a, la, 0);
            st_vol_fsync_dir(a);
            close(a);
        } else {
            continue;
        }
        if (committed && first) {
            first = 0;
            int rc = st_vol_step(v, err);
            if (rc) return rc;
        }
    }
    unlinkat(v->root, ST_VOL_JOURNAL, 0);
    st_vol_fsync_dir(v->root);
    return ST_OK;
}

int st_vol_recover(StVolume *v, StErr *err) {
    int fd = openat(v->root, ST_VOL_JOURNAL, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return ST_OK;
    char buf[65536];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n < 0) return st_fail(err, ST_EIO, "cannot read the journal");
    buf[n] = 0;
    int committed = n >= 15 && !strncmp(buf, "OS999J1\n", 8) && !strcmp(buf + n - 7, "COMMIT\n");
    if (committed) buf[n - 7] = 0;
    v->use.recovered++;
    int saved = v->crash_steps;
    v->crash_steps = 0;
    int rc = st_vol_journal_apply(v, strncmp(buf, "OS999J1\n", 8) ? "" : buf + 8, committed, err);
    v->crash_steps = saved;
    return rc;
}

static int write_tmp(int dir, const char *name, const StBytes *b, uint32_t mtime, StErr *err) {
    int fd = openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return st_fail(err, ST_EIO, "cannot create %s: %s", name, strerror(errno));
    size_t off = 0;
    while (off < b->n) {
        ssize_t k = write(fd, b->p + off, b->n - off);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) { close(fd); unlinkat(dir, name, 0); return st_fail(err, ST_EIO, "write failed: %s", strerror(errno)); }
        off += (size_t)k;
    }
    if (mtime > ST_MAC_EPOCH_DELTA) {
        struct timespec ts[2] = {{0, UTIME_OMIT}, {(time_t)(mtime - ST_MAC_EPOCH_DELTA), 0}};
        futimens(fd, ts);
    }
    if (fsync(fd) != 0) { close(fd); unlinkat(dir, name, 0); return st_fail(err, ST_EIO, "fsync failed"); }
    close(fd);
    return ST_OK;
}

static uint64_t size_at(int dir, const char *name) {
    struct stat st;
    return fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0;
}

static int needs_sidecar(const StMacFile *f) {
    static const uint8_t zero[16];
    return f->rsrc.n || memcmp(f->finfo, zero, 16) || memcmp(f->fxinfo, zero, 16) || f->ctime;
}

int st_vol_write(StVolume *v, const char *rel, const StMacFile *f, StErr *err) {
    if (v->opt.read_only) return st_fail(err, ST_EINVAL, "volume is read-only");
    char leaf[300], side[310], hrel[4096], tmpd[64], tmpr[64];
    if (st_vol_host_path(rel, hrel, sizeof hrel)) return st_fail(err, ST_EPATH, "unsafe guest path \"%s\"", rel);
    int dir, rc = st_vol_parent(v, rel, 1, &dir, leaf, sizeof leaf, err);
    if (rc) return rc;
    snprintf(side, sizeof side, "%s.rsrc", leaf);
    struct stat st;
    int exists = fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0;
    if (exists && !S_ISREG(st.st_mode)) { close(dir); return st_fail(err, ST_EEXIST, "%s exists and is not a file", rel); }
    StBytes sc = {0};
    int has_side = needs_sidecar(f);
    if (has_side && (rc = st_applesingle_encode(f, 1, &sc, err))) { close(dir); return rc; }
    uint64_t old = size_at(dir, leaf) + size_at(dir, side), add = f->data.n + sc.n;
    if (v->opt.quota_bytes && v->use.bytes - old + add > v->opt.quota_bytes) {
        close(dir);
        st_bytes_free(&sc);
        return st_fail(err, ST_EQUOTA, "saving %s needs %llu bytes; quota is %llu, %llu in use", rel,
                       (unsigned long long)add, (unsigned long long)v->opt.quota_bytes, (unsigned long long)v->use.bytes);
    }
    if (!exists && v->opt.quota_files && v->use.files + 1 > v->opt.quota_files) {
        close(dir);
        st_bytes_free(&sc);
        return st_fail(err, ST_EQUOTA, "the volume holds its quota of %llu files", (unsigned long long)v->opt.quota_files);
    }
    unsigned id = ++v->counter;
    snprintf(tmpd, sizeof tmpd, "%s%ld-%u-d", ST_VOL_TMP, (long)getpid(), id);
    snprintf(tmpr, sizeof tmpr, "%s%ld-%u-r", ST_VOL_TMP, (long)getpid(), id);
    rc = write_tmp(dir, tmpd, &f->data, f->mtime, err);
    if (rc == ST_OK) rc = st_vol_step(v, err);
    if (rc == ST_OK && has_side) {
        rc = write_tmp(dir, tmpr, &sc, f->mtime, err);
        if (rc == ST_OK) rc = st_vol_step(v, err);
    }
    if (rc == ST_OK) st_vol_fsync_dir(dir);
    char *plan = rc ? NULL : malloc(4 * sizeof hrel + 64);
    if (rc == ST_OK && !plan) rc = st_fail(err, ST_ENOMEM, "out of memory");
    if (rc == ST_OK) {
        char base[4096];
        snprintf(base, sizeof base, "%s", hrel);
        char *slash = strrchr(base, '/');
        if (slash) slash[1] = 0; else base[0] = 0;
        int k = snprintf(plan, 4 * sizeof hrel + 64, "R %s%s\t%s\n", base, tmpd, hrel);
        if (has_side) snprintf(plan + k, 4 * sizeof hrel + 64 - (size_t)k, "R %s%s\t%s.rsrc\n", base, tmpr, hrel);
        else snprintf(plan + k, 4 * sizeof hrel + 64 - (size_t)k, "D %s.rsrc\n", hrel);
        rc = st_vol_journal_commit(v, plan, err);
        if (rc == ST_OK) rc = st_vol_step(v, err);
        if (rc == ST_OK) rc = st_vol_journal_apply(v, plan, 1, err);
    }
    if (rc && rc != ST_EIO) { unlinkat(dir, tmpd, 0); unlinkat(dir, tmpr, 0); }
    if (rc == ST_OK) {
        v->use.bytes = v->use.bytes - old + add;
        if (!exists) v->use.files++;
    }
    free(plan);
    st_bytes_free(&sc);
    close(dir);
    return rc;
}

int st_vol_mkdirs(StVolume *v, const char *rel, StErr *err) {
    if (v->opt.read_only) return st_fail(err, ST_EINVAL, "volume is read-only");
    if (!rel || !*rel) return ST_OK;
    char leaf[300];
    int dir, rc = st_vol_parent(v, rel, 1, &dir, leaf, sizeof leaf, err);
    if (rc) return rc;
    if (mkdirat(dir, leaf, 0755) == 0) v->use.dirs++;
    else if (errno != EEXIST) rc = st_fail(err, ST_EIO, "mkdir %s: %s", rel, strerror(errno));
    struct stat st;
    if (rc == ST_OK && (fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode)))
        rc = st_fail(err, ST_EEXIST, "%s exists and is not a folder", rel);
    st_vol_fsync_dir(dir);
    close(dir);
    return rc;
}

int st_vol_remove(StVolume *v, const char *rel, StErr *err) {
    if (v->opt.read_only) return st_fail(err, ST_EINVAL, "volume is read-only");
    char leaf[300], hrel[4096];
    if (st_vol_host_path(rel, hrel, sizeof hrel)) return st_fail(err, ST_EPATH, "unsafe guest path");
    int dir, rc = st_vol_parent(v, rel, 0, &dir, leaf, sizeof leaf, err);
    if (rc) return rc;
    struct stat st;
    if (fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) != 0) { close(dir); return st_fail(err, ST_ENOENT, "%s not found", rel); }
    if (S_ISDIR(st.st_mode)) {
        if (unlinkat(dir, leaf, AT_REMOVEDIR) != 0) rc = st_fail(err, ST_EIO, "cannot remove folder %s: %s", rel, strerror(errno));
        else v->use.dirs--;
        close(dir);
        return rc;
    }
    char side[310];
    snprintf(side, sizeof side, "%s.rsrc", leaf);
    uint64_t old = size_at(dir, leaf) + size_at(dir, side);
    char plan[8400];
    snprintf(plan, sizeof plan, "D %s\nD %s.rsrc\n", hrel, hrel);
    rc = st_vol_journal_commit(v, plan, err);
    if (rc == ST_OK) rc = st_vol_journal_apply(v, plan, 1, err);
    if (rc == ST_OK) { v->use.bytes -= old; v->use.files--; }
    close(dir);
    return rc;
}

int st_vol_rename(StVolume *v, const char *from, const char *to, StErr *err) {
    if (v->opt.read_only) return st_fail(err, ST_EINVAL, "volume is read-only");
    char la[300], lb[310], ha[4096], hb[4096];
    if (st_vol_host_path(from, ha, sizeof ha) || st_vol_host_path(to, hb, sizeof hb))
        return st_fail(err, ST_EPATH, "unsafe guest path");
    int a, b, rc = st_vol_parent(v, from, 0, &a, la, sizeof la, err);
    if (rc) return rc;
    if ((rc = st_vol_parent(v, to, 1, &b, lb, sizeof lb, err))) { close(a); return rc; }
    struct stat st;
    if (fstatat(b, lb, &st, AT_SYMLINK_NOFOLLOW) == 0) rc = st_fail(err, ST_EEXIST, "%s already exists", to);
    else if (fstatat(a, la, &st, AT_SYMLINK_NOFOLLOW) != 0) rc = st_fail(err, ST_ENOENT, "%s not found", from);
    else if (S_ISDIR(st.st_mode)) {
        if (renameat(a, la, b, lb) != 0) rc = st_fail(err, ST_EIO, "rename failed: %s", strerror(errno));
        st_vol_fsync_dir(b);
    } else {
        char plan[17000];
        snprintf(plan, sizeof plan, "R %s\t%s\nR %s.rsrc\t%s.rsrc\n", ha, hb, ha, hb);
        rc = st_vol_journal_commit(v, plan, err);
        if (rc == ST_OK) rc = st_vol_journal_apply(v, plan, 1, err);
    }
    close(a);
    close(b);
    return rc;
}
