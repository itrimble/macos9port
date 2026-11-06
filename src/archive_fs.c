#include "archive_fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARCHIVE_COPY_MAX_FILES 4096u
#define ARCHIVE_COPY_MAX_BYTES (512ull * 1024ull * 1024ull)
#define ARCHIVE_COPY_MAX_DEPTH 8

int archive_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    int fd = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) { fclose(in); return -1; }
    FILE *out = fdopen(fd, "wb");
    if (!out) { close(fd); unlink(dst); fclose(in); return -1; }
    char buf[8192];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    if (ferror(in)) rc = -1;
    if (fclose(in) != 0) rc = -1;
    if (fclose(out) != 0) rc = -1;
    if (rc) unlink(dst);
    return rc;
}

static int copy_tree(const char *src, const char *dst, int depth, ArchiveCopyStats *stats) {
    if (depth > ARCHIVE_COPY_MAX_DEPTH) return -1;
    struct stat st;
    if (lstat(src, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode)) return -1;
    if (S_ISREG(st.st_mode)) {
        if (st.st_size < 0 || stats->files >= ARCHIVE_COPY_MAX_FILES ||
            (uint64_t)st.st_size > ARCHIVE_COPY_MAX_BYTES - stats->bytes ||
            archive_copy_file(src, dst) != 0) return -1;
        stats->files++;
        stats->bytes += (uint64_t)st.st_size;
        return 0;
    }
    if (!S_ISDIR(st.st_mode)) return -1;
    if (mkdir(dst, 0755) != 0) {
        struct stat existing;
        if (errno != EEXIST || lstat(dst, &existing) != 0 || !S_ISDIR(existing.st_mode)) return -1;
    }
    DIR *d = opendir(src);
    if (!d) return -1;
    int rc = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (strchr(de->d_name, '/')) { rc = -1; break; }
        char from[4096], to[4096];
        int a = snprintf(from, sizeof from, "%s/%s", src, de->d_name);
        int b = snprintf(to, sizeof to, "%s/%s", dst, de->d_name);
        if (a < 0 || (size_t)a >= sizeof from || b < 0 || (size_t)b >= sizeof to ||
            copy_tree(from, to, depth + 1, stats) != 0) { rc = -1; break; }
    }
    closedir(d);
    return rc;
}

int archive_copy_tree(const char *src, const char *dst, ArchiveCopyStats *stats) {
    ArchiveCopyStats local = {0};
    if (!stats) stats = &local;
    stats->files = 0;
    stats->bytes = 0;
    if (copy_tree(src, dst, 0, stats) != 0 || stats->files == 0) return -1;
    return 0;
}

static int remove_tree(const char *path, int depth) {
    struct stat st;
    if (depth > ARCHIVE_COPY_MAX_DEPTH || lstat(path, &st) != 0) return -1;
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) return unlink(path);
    DIR *d = opendir(path);
    if (!d) return -1;
    int rc = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char child[4096];
        int n = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof child || remove_tree(child, depth + 1) != 0) rc = -1;
    }
    closedir(d);
    return rmdir(path) == 0 ? rc : -1;
}

int archive_remove_tree(const char *path) { return remove_tree(path, 0); }
