/* File-level unpacking, host-tree import, and the optional `unar`
 * fallback for StuffIt methods we do not decode (13, 15, StuffIt X).
 * The tool runs without a shell, inside a private temporary folder. */
#include "os999_unpack.h"
#include "st_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int is_sidecar(const char *name) {
    size_t n = strlen(name);
    return n > 5 && !strcasecmp(name + n - 5, ".rsrc");
}

static int read_at(int dir, const char *name, StBytes *b, const StLimits *lim, StErr *err) {
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return st_fail(err, ST_EIO, "cannot open %s", name);
    struct stat st;
    int rc = ST_OK;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) rc = st_fail(err, ST_EIO, "%s is not a file", name);
    else if ((uint64_t)st.st_size > lim->max_entry) rc = st_fail(err, ST_ELIMIT, "%s is too large", name);
    else if (st_bytes_reserve(b, (size_t)st.st_size + 1)) rc = st_fail(err, ST_ENOMEM, "out of memory");
    b->n = 0;
    while (rc == ST_OK && b->n < (size_t)st.st_size) {
        ssize_t k = read(fd, b->p + b->n, (size_t)st.st_size - b->n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) rc = st_fail(err, ST_EIO, "read %s failed", name);
        else b->n += (size_t)k;
    }
    close(fd);
    return rc;
}

static void apply_sidecar(StMacFile *f, StBytes *s, const StLimits *lim) {
    StMacFile meta;
    if (st_applesingle_detect(s->p, s->n) && st_applesingle_decode(s->p, s->n, &meta, lim, NULL) == ST_OK) {
        memcpy(f->finfo, meta.finfo, 16);
        memcpy(f->fxinfo, meta.fxinfo, 16);
        if (meta.ctime) f->ctime = meta.ctime;
        if (meta.mtime) f->mtime = meta.mtime;
        st_bytes_free(&f->rsrc);
        f->rsrc = meta.rsrc;
        st_bytes_free(&meta.data);
        st_bytes_free(s);
    } else {
        st_bytes_free(&f->rsrc);
        f->rsrc = *s;
        memset(s, 0, sizeof *s);
    }
}

static int import_dir(int fd, const char *prefix, StArchive *out, const StLimits *lim, StErr *err, int depth) {
    if ((uint32_t)depth > lim->max_depth) return st_fail(err, ST_ELIMIT, "folders nest too deeply");
    int dfd = dup(fd);
    DIR *d = dfd >= 0 ? fdopendir(dfd) : NULL;
    if (!d) { if (dfd >= 0) close(dfd); return st_fail(err, ST_EIO, "cannot list folder"); }
    rewinddir(d);
    struct dirent *e;
    int rc = ST_OK;
    while (rc == ST_OK && (e = readdir(d)) != NULL) {
        const char *nm = e->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || !strcmp(nm, ".DS_Store")) continue;
        struct stat st;
        if (fstatat(fd, nm, &st, AT_SYMLINK_NOFOLLOW)) continue;
        char path[4096];
        snprintf(path, sizeof path, "%s%s%s", prefix, *prefix ? "/" : "", nm);
        StMacFile f;
        st_macfile_init(&f);
        snprintf(f.name, sizeof f.name, "%s", nm);
        if (S_ISDIR(st.st_mode)) {
            rc = st_archive_add(out, path, 1, &f, lim, err);
            int sub = openat(fd, nm, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (rc == ST_OK && sub >= 0) rc = import_dir(sub, path, out, lim, err, depth + 1);
            if (sub >= 0) close(sub);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue; /* symlinks, devices: skipped */
        char side[300];
        if (is_sidecar(nm)) {
            /* Orphan sidecar: a resource-only file. */
            snprintf(side, sizeof side, "%.*s", (int)strlen(nm) - 5, nm);
            struct stat s2;
            if (!fstatat(fd, side, &s2, AT_SYMLINK_NOFOLLOW)) continue;
            snprintf(f.name, sizeof f.name, "%s", side);
            snprintf(path, sizeof path, "%s%s%s", prefix, *prefix ? "/" : "", side);
            StBytes s = {0};
            rc = read_at(fd, nm, &s, lim, err);
            if (rc == ST_OK) apply_sidecar(&f, &s, lim);
            st_bytes_free(&s);
        } else {
            rc = read_at(fd, nm, &f.data, lim, err);
            snprintf(side, sizeof side, "%s.rsrc", nm);
            StBytes s = {0};
            if (rc == ST_OK && read_at(fd, side, &s, lim, NULL) == ST_OK) apply_sidecar(&f, &s, lim);
            st_bytes_free(&s);
        }
        if (rc == ST_OK) rc = st_archive_add(out, path, 0, &f, lim, err);
        if (rc) st_macfile_free(&f);
    }
    closedir(d);
    return rc;
}

int st_import_tree(const char *dir, StArchive *out, const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return st_fail(err, ST_EIO, "cannot open %s", dir);
    int rc = import_dir(fd, "", out, lim, err, 0);
    close(fd);
    if (rc) st_archive_free(out);
    return rc;
}

static void remove_tree(int fd, const char *name) {
    int sub = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (sub >= 0) {
        int dfd = dup(sub);
        DIR *d = dfd >= 0 ? fdopendir(dfd) : NULL;
        if (d) rewinddir(d);
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL)
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) remove_tree(sub, e->d_name);
        if (d) closedir(d);
        close(sub);
        unlinkat(fd, name, AT_REMOVEDIR);
    } else {
        unlinkat(fd, name, 0);
    }
}

int st_lsar_merge(const char *json, size_t n, StArchive *a);

/* Run one of the unar tools without a shell; stdout goes to out_path
 * (or /dev/null). Homebrew locations are tried because GUI apps on
 * macOS start with a minimal PATH. */
static int spawn_tool(const char *tool, char **args, const char *out_path) {
    char env_name[32], path[512];
    snprintf(env_name, sizeof env_name, "OS999_%s", !strcmp(tool, "unar") ? "UNAR" : "LSAR");
    const char *env = getenv(env_name);
    const char *dirs[] = {"/opt/homebrew/bin/", "/usr/local/bin/", "/usr/bin/", ""};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, out_path ? out_path : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    int rc = -1;
    for (int i = -1; i < 4 && rc < 0; i++) {
        if (i < 0) { if (!env || !*env) continue; snprintf(path, sizeof path, "%s", env); }
        else snprintf(path, sizeof path, "%s%s", dirs[i], tool);
        if (strchr(path, '/') && access(path, X_OK)) continue;
        args[0] = path;
        pid_t pid;
        if (posix_spawnp(&pid, path, &fa, NULL, args, environ) != 0) continue;
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        rc = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
    }
    posix_spawn_file_actions_destroy(&fa);
    return rc;
}

static int run_unar(const char *archive, const char *outdir, StErr *err) {
    char *args[] = {NULL, "-q", "-f", "-D", "-k", "visible", "-o", (char *)outdir, "--", (char *)archive, NULL};
    int rc = spawn_tool("unar", args, NULL);
    if (rc < 0) return st_fail(err, ST_EUNSUPPORTED, "unar is not installed");
    return rc ? st_fail(err, ST_EFORMAT, "unar could not expand the archive") : ST_OK;
}

/* Fill in type, creator and Finder flags that the extraction lost. */
static void merge_lsar(const char *archive, const char *tmpdir, StArchive *a) {
    char out[4200];
    snprintf(out, sizeof out, "%s.json", tmpdir);
    char *args[] = {NULL, "-j", "--", (char *)archive, NULL};
    if (spawn_tool("lsar", args, out) == 0) {
        StBytes j = {0};
        if (st_read_file(out, 256u << 20, &j, NULL) == ST_OK) st_lsar_merge((const char *)j.p, j.n, a);
        st_bytes_free(&j);
    }
    unlink(out);
}

int st_unpack_file(const char *path, int allow_external, StArchive *out, StUnpackInfo *info,
                   const StLimits *lim, StErr *err) {
    StLimits def = st_limits_default();
    if (!lim) lim = &def;
    StBytes b = {0}, r = {0};
    int rc = st_read_file(path, lim->max_entry, &b, err);
    if (rc) return rc;
    char side[4096];
    snprintf(side, sizeof side, "%s.rsrc", path);
    if (st_read_file(side, lim->max_entry, &r, NULL) == ST_OK) {
        StMacFile meta;
        if (st_applesingle_detect(r.p, r.n) && st_applesingle_decode(r.p, r.n, &meta, lim, NULL) == ST_OK) {
            st_bytes_free(&r);
            r = meta.rsrc;
            st_bytes_free(&meta.data);
        }
    }
    const char *base = strrchr(path, '/');
    StUnpackInfo local;
    if (!info) info = &local;
    rc = st_unpack(b.p, b.n, r.p, r.n, base ? base + 1 : path, out, info, lim, err);
    st_bytes_free(&b);
    st_bytes_free(&r);
    if (rc == ST_OK && allow_external >= ST_EXT_ARCHIVE && !info->chain[0]) {
        st_archive_free(out);
        rc = st_fail(err, ST_EUNSUPPORTED, "not an archive format OS999 decodes");
    }
    if (rc != ST_EUNSUPPORTED || !allow_external) return rc;
    const char *tmp = getenv("TMPDIR");
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/os999-unpack-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dir)) return st_fail(err, ST_EIO, "cannot create a temporary folder");
    char why[sizeof err->msg];
    snprintf(why, sizeof why, "%s", err ? err->msg : "");
    rc = run_unar(path, dir, err);
    if (rc == ST_OK) rc = st_import_tree(dir, out, lim, err);
    if (rc == ST_OK) merge_lsar(path, dir, out);
    if (rc == ST_OK && info) {
        info->external = 1;
        size_t l = strlen(info->chain);
        snprintf(info->chain + l, sizeof info->chain - l, " (unar: %s)", why);
    }
    char *slash = strrchr(dir, '/');
    *slash = 0;
    int parent = open(dir, O_RDONLY | O_DIRECTORY);
    if (parent >= 0) { remove_tree(parent, slash + 1); close(parent); }
    return rc;
}
