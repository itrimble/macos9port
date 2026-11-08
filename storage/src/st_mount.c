#include "os999_mount.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct StMount {
    StVolume *volume;
    StUnpackInfo info;
    int parent, root;
    char path[4096], leaf[64];
    dev_t device;
    ino_t inode;
};

/* Descriptor-relative cleanup never follows a guest-created symlink. */
static int clear_dir(int fd, unsigned depth) {
    if (depth > 64) return -1;
    int copy = dup(fd), rc = 0;
    DIR *d = copy >= 0 ? fdopendir(copy) : NULL;
    if (!d) { if (copy >= 0) close(copy); return -1; }
    rewinddir(d);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct stat st;
        if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) { rc = -1; continue; }
        if (S_ISDIR(st.st_mode)) {
            int sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (sub < 0) { rc = -1; continue; }
            if (clear_dir(sub, depth + 1)) rc = -1;
            close(sub);
            if (unlinkat(fd, e->d_name, AT_REMOVEDIR)) rc = -1;
        } else if (unlinkat(fd, e->d_name, 0)) rc = -1;
    }
    closedir(d);
    return rc;
}

int st_mount_close(StMount *m, StErr *err) {
    if (!m) return ST_OK;
    st_vol_close(m->volume);
    int rc = ST_OK;
    if (m->root >= 0) {
        struct stat st;
        /* Refuse cleanup if the mount's directory entry was replaced. */
        if (fstatat(m->parent, m->leaf, &st, AT_SYMLINK_NOFOLLOW) ||
            !S_ISDIR(st.st_mode) || st.st_dev != m->device || st.st_ino != m->inode)
            rc = st_fail(err, ST_EPATH, "mount directory was replaced; cleanup refused");
        else if (clear_dir(m->root, 0) || unlinkat(m->parent, m->leaf, AT_REMOVEDIR))
            rc = st_fail(err, ST_EIO, "could not remove disposable mount");
        close(m->root);
    }
    if (m->parent >= 0) close(m->parent);
    free(m);
    return rc;
}

int st_mount_open(const char *image, const char *scratch, const StVolumeOptions *opt,
                  const StLimits *lim, StMount **out, StErr *err) {
    if (!out) return st_fail(err, ST_EINVAL, "missing mount output");
    *out = NULL;
    if (!image || !scratch) return st_fail(err, ST_EINVAL, "missing image or scratch folder");
    StArchive a = {0};
    StMount *m = calloc(1, sizeof *m);
    if (!m) return st_fail(err, ST_ENOMEM, "out of memory");
    m->parent = m->root = -1;
    /* Fully validate before creating any snapshot. External tools forbidden. */
    int rc = st_unpack_file(image, ST_EXT_NONE, &a, &m->info, lim, err);
    if (!rc && !m->info.volume[0]) rc = st_fail(err, ST_EUNSUPPORTED, "not a supported filesystem image");
    char parent[4096];
    struct stat scratch_stat;
    if (!rc && (lstat(scratch, &scratch_stat) || !S_ISDIR(scratch_stat.st_mode)))
        rc = st_fail(err, ST_EPATH, "scratch must be a real directory");
    if (!rc && (!realpath(scratch, parent) ||
        (m->parent = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0))
        rc = st_fail(err, ST_EPATH, "cannot open scratch folder");
    if (!rc) {
        int k = snprintf(m->path, sizeof m->path, "%s/os999-mount-XXXXXX", parent);
        if (k < 0 || (size_t)k >= sizeof m->path || !mkdtemp(m->path))
            rc = st_fail(err, ST_EIO, "cannot create disposable mount");
        else {
            snprintf(m->leaf, sizeof m->leaf, "%s", strrchr(m->path, '/') + 1);
            m->root = openat(m->parent, m->leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat st;
            if (m->root < 0 || fstat(m->root, &st)) rc = st_fail(err, ST_EIO, "cannot open disposable mount");
            else { m->device = st.st_dev; m->inode = st.st_ino; }
        }
    }
    StVolumeOptions options = opt ? *opt : (StVolumeOptions){0, 0, 1};
    int read_only = options.read_only;
    options.read_only = 0;
    if (!rc) rc = st_vol_open(m->path, &options, &m->volume, err);
    if (!rc) rc = st_vol_install(m->volume, "", &a, err);
    if (!rc && read_only) {
        st_vol_close(m->volume);
        m->volume = NULL;
        options.read_only = 1;
        rc = st_vol_open(m->path, &options, &m->volume, err);
    }
    st_archive_free(&a);
    if (rc) { st_mount_close(m, NULL); return rc; }
    *out = m;
    return ST_OK;
}
StVolume *st_mount_volume(StMount *m) { return m ? m->volume : NULL; }
const char *st_mount_path(const StMount *m) { return m ? m->path : NULL; }
const StUnpackInfo *st_mount_info(const StMount *m) { return m ? &m->info : NULL; }
