/* All-or-nothing archive installs. Before writing, the install records
 * every file and folder it may create in ".os999-install". On success
 * the manifest is deleted; after a failure (or at the next open, after a
 * crash) everything it lists is removed again. */
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

static int exists_rel(StVolume *v, const char *rel, int *is_dir) {
    int d;
    char leaf[300];
    if (st_vol_parent(v, rel, 0, &d, leaf, sizeof leaf, NULL) != ST_OK) return 0;
    struct stat st;
    int ok = fstatat(d, leaf, &st, AT_SYMLINK_NOFOLLOW) == 0;
    if (ok && is_dir) *is_dir = S_ISDIR(st.st_mode);
    close(d);
    return ok;
}

static int add_line(StBytes *m, char kind, const char *rel) {
    char host[4096];
    if (st_vol_host_path(rel, host, sizeof host)) return -1;
    char k[3] = {kind, ' ', 0};
    return st_bytes_append(m, k, 2) || st_bytes_append(m, host, strlen(host)) || st_bytes_append(m, "\n", 1);
}

/* Folders the install may create, outermost first, if absent today. */
static int plan_dirs(StVolume *v, const char *path, StBytes *m, StBytes *seen) {
    char buf[8192];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *s = buf;; s++) {
        if (*s && *s != '/') continue;
        char c = *s;
        *s = 0;
        int is_dir = 0;
        int already = 0;
        for (size_t i = 0; i < seen->n;) {
            const char *line = (const char *)seen->p + i;
            if (!strcmp(line, buf)) { already = 1; break; }
            i += strlen(line) + 1;
        }
        if (!already && buf[0] && !exists_rel(v, buf, &is_dir)) {
            if (add_line(m, 'P', buf) || st_bytes_append(seen, buf, strlen(buf) + 1)) return -1;
        }
        *s = c;
        if (!c) break;
    }
    return 0;
}

static void undo(StVolume *v, const char *plan) {
    /* Files (and their sidecars) first, then folders innermost-first. */
    for (int pass = 0; pass < 2; pass++) {
        const char *lines[4096];
        size_t nl = 0;
        for (const char *s = plan; *s && nl < 4096;) {
            if ((pass == 0 && s[0] == 'F') || (pass == 1 && s[0] == 'P')) lines[nl++] = s + 2;
            const char *e = strchr(s, '\n');
            s = e ? e + 1 : s + strlen(s);
        }
        for (size_t i = nl; i-- > 0;) {
            char path[4200];
            const char *e = strchr(lines[i], '\n');
            snprintf(path, sizeof path, "%.*s", e ? (int)(e - lines[i]) : (int)strlen(lines[i]), lines[i]);
            char *slash = strrchr(path, '/');
            const char *leaf = slash ? slash + 1 : path;
            int d = dup(v->root);
            if (slash) {
                *slash = 0;
                /* walk folders without following links */
                char *s = path;
                while (d >= 0 && *s) {
                    char *q = strchr(s, '/');
                    if (q) *q = 0;
                    int nd = openat(d, s, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                    close(d);
                    d = nd;
                    if (!q) break;
                    s = q + 1;
                }
            }
            if (d < 0) continue;
            if (pass == 0) {
                char side[310];
                snprintf(side, sizeof side, "%s.rsrc", leaf);
                unlinkat(d, leaf, 0);
                unlinkat(d, side, 0);
            } else {
                unlinkat(d, leaf, AT_REMOVEDIR);
            }
            st_vol_fsync_dir(d);
            close(d);
        }
    }
}

int st_vol_install_recover(StVolume *v, StErr *err) {
    int fd = openat(v->root, ST_VOL_INSTALL, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return ST_OK;
    StBytes m = {0};
    char buf[65536];
    ssize_t k;
    while ((k = read(fd, buf, sizeof buf)) > 0)
        if (st_bytes_append(&m, buf, (size_t)k)) { close(fd); st_bytes_free(&m); return st_fail(err, ST_ENOMEM, "out of memory"); }
    close(fd);
    st_bytes_append(&m, "", 1);
    undo(v, (const char *)m.p);
    st_bytes_free(&m);
    unlinkat(v->root, ST_VOL_INSTALL, 0);
    st_vol_fsync_dir(v->root);
    v->use.recovered++;
    return ST_OK;
}

static int write_manifest(StVolume *v, const StBytes *m, StErr *err) {
    int fd = openat(v->root, ST_VOL_INSTALL, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return st_fail(err, ST_EIO, "another install is unfinished: %s", strerror(errno));
    int ok = write(fd, m->p, m->n) == (ssize_t)m->n && fsync(fd) == 0;
    close(fd);
    if (!ok || st_vol_fsync_dir(v->root)) return st_fail(err, ST_EIO, "cannot record the install");
    return ST_OK;
}

int st_vol_install(StVolume *v, const char *dest, const StArchive *ar, StErr *err) {
    if (v->opt.read_only) return st_fail(err, ST_EINVAL, "volume is read-only");
    uint64_t need = 0, files = 0;
    char rel[8192];
    for (size_t i = 0; i < ar->n; i++) {
        if (ar->v[i].is_dir) continue;
        need += ar->v[i].file.data.n + ar->v[i].file.rsrc.n + 400;
        files++;
    }
    if (v->opt.quota_bytes && v->use.bytes + need > v->opt.quota_bytes)
        return st_fail(err, ST_EQUOTA, "installing needs about %llu bytes; %llu of %llu are in use",
                       (unsigned long long)need, (unsigned long long)v->use.bytes, (unsigned long long)v->opt.quota_bytes);
    if (v->opt.quota_files && v->use.files + files > v->opt.quota_files)
        return st_fail(err, ST_EQUOTA, "installing %llu files exceeds the file quota", (unsigned long long)files);
    StBytes m = {0}, seen = {0};
    int rc = ST_OK;
    if (*dest && plan_dirs(v, dest, &m, &seen)) rc = st_fail(err, ST_EPATH, "unsafe destination \"%s\"", dest);
    for (size_t i = 0; rc == ST_OK && i < ar->n; i++) {
        snprintf(rel, sizeof rel, "%s%s%s", dest, *dest ? "/" : "", ar->v[i].path);
        int is_dir = 0;
        if (exists_rel(v, rel, &is_dir) && !(ar->v[i].is_dir && is_dir)) {
            rc = st_fail(err, ST_EEXIST, "%s already exists", rel);
            break;
        }
        char parent[8192];
        snprintf(parent, sizeof parent, "%s", rel);
        char *slash = strrchr(parent, '/');
        if (ar->v[i].is_dir) slash = parent + strlen(parent);
        if (slash) { *slash = 0; if (plan_dirs(v, parent, &m, &seen)) rc = st_fail(err, ST_EPATH, "unsafe path %s", rel); }
        if (rc == ST_OK && !ar->v[i].is_dir && add_line(&m, 'F', rel)) rc = st_fail(err, ST_EPATH, "unsafe path %s", rel);
    }
    st_bytes_free(&seen);
    /* Recovery has a bounded 4096-entry plan. Reject oversized installs
     * before any filesystem mutation rather than leaving untracked files. */
    size_t planned = 0;
    for (size_t i = 0; i < m.n; i++) if (m.p[i] == '\n') planned++;
    if (rc == ST_OK && planned > 4096)
        rc = st_fail(err, ST_ELIMIT, "install exceeds the 4096-entry recovery plan");
    if (rc == ST_OK) rc = write_manifest(v, &m, err);
    if (rc) { st_bytes_free(&m); return rc; }
    if (*dest) rc = st_vol_mkdirs(v, dest, err);
    for (size_t i = 0; rc == ST_OK && i < ar->n; i++) {
        snprintf(rel, sizeof rel, "%s%s%s", dest, *dest ? "/" : "", ar->v[i].path);
        rc = ar->v[i].is_dir ? st_vol_mkdirs(v, rel, err) : st_vol_write(v, rel, &ar->v[i].file, err);
    }
    if (rc == ST_OK) {
        unlinkat(v->root, ST_VOL_INSTALL, 0);
        st_vol_fsync_dir(v->root);
    } else {
        int saved = v->crash_steps;
        v->crash_steps = 0;
        st_vol_recover(v, NULL); /* settle an interrupted save first */
        st_bytes_append(&m, "", 1);
        undo(v, (const char *)m.p);
        unlinkat(v->root, ST_VOL_INSTALL, 0);
        v->crash_steps = saved;
    }
    st_bytes_free(&m);
    st_vol_scan(v, NULL);
    return rc;
}
