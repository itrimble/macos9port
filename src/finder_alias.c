#include "finder_files.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { uint64_t device, inode; int64_t birth; long nanos; } Identity;
static Identity identity(const struct stat *st) {
    Identity id = {(uint64_t)st->st_dev, (uint64_t)st->st_ino, 0, 0};
#ifdef __APPLE__
    id.birth = st->st_birthtimespec.tv_sec; id.nanos = st->st_birthtimespec.tv_nsec;
#endif
    return id;
}
static int matches(const struct stat *st, const Identity *wanted) {
    Identity id = identity(st);
    return (S_ISREG(st->st_mode) || S_ISDIR(st->st_mode)) && id.device == wanted->device &&
        id.inode == wanted->inode && id.birth == wanted->birth && id.nanos == wanted->nanos;
}
static int name_ok(const char *name) {
    return name && name[0] && name[0] != '.' && strlen(name) < OSPI_NAME && !strpbrk(name, "/\n\r");
}
static int path_ok(const char *path) {
    if (!path || !path[0] || strlen(path) >= OSPI_PATH) return 0;
    char copy[OSPI_PATH]; strcpy(copy, path);
    char *part = copy;
    while (part) {
        char *slash = strchr(part, '/'); if (slash) *slash = 0;
        if (!name_ok(part)) return 0;
        part = slash ? slash + 1 : NULL;
    }
    return 1;
}
static char *read_identity(char *text, Identity *id) {
    char *end;
    if (*text < '0' || *text > '9') return NULL;
    errno = 0; id->device = strtoull(text, &end, 10);
    if (errno || *end != ' ') return NULL;
    text = end + 1;
    if (*text < '0' || *text > '9') return NULL;
    errno = 0; id->inode = strtoull(text, &end, 10);
    if (errno || !id->inode || *end != ' ') return NULL;
    text = end + 1;
    if ((*text < '0' || *text > '9') && *text != '-') return NULL;
    errno = 0; id->birth = strtoll(text, &end, 10);
    if (errno || end == text || *end != ' ') return NULL;
    text = end + 1;
    if (*text < '0' || *text > '9') return NULL;
    errno = 0; id->nanos = strtol(text, &end, 10);
    if (errno || id->nanos < 0 || id->nanos >= 1000000000 || *end != '\n') return NULL;
    return end + 1;
}
static int read_alias(const char *root, const char *rel, const char *name,
                      char path[OSPI_PATH], Identity *id) {
    if (!name_ok(name)) return -1;
    int dir = finder_directory_open(root, rel); if (dir < 0) return -1;
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return -1;
    char record[OSPI_PATH + 160]; struct stat st; ssize_t n = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size < (off_t)sizeof record - 1)
        n = read(fd, record, sizeof record - 1);
    close(fd);
    if (n < 14) return 0;
    record[n] = 0;
    int version = !strncmp(record, "OS999-ALIAS-1\n", 14) ? 1 :
                  !strncmp(record, "OS999-ALIAS-2\n", 14) ? 2 : 0;
    if (!version) return 0;
    if (memchr(record, 0, (size_t)n)) return -1;
    char *location = record + 14;
    if (version == 2) {
        location = read_identity(location, id);
        if (!location) return -1;
    }
    if (!path_ok(location)) return -1;
    strcpy(path, location); return version;
}
int finder_is_alias(const char *root, const char *rel, const char *name) {
    char path[OSPI_PATH]; Identity id;
    return read_alias(root, rel, name, path, &id) > 0;
}
/* Search only on explicit Open/Show Original. Never follow links or leave the volume. */
static int search(int fd, const char *parent, const Identity *id, char target[OSPI_PATH],
                  unsigned *remaining, int depth) {
    if (depth > 32 || !*remaining) return 0;
    int copy = dup(fd); if (copy < 0) return 0;
    DIR *dir = fdopendir(copy); if (!dir) { close(copy); return 0; }
    struct dirent *entry; int found = 0;
    while (*remaining && (entry = readdir(dir))) {
        --*remaining;
        if (!name_ok(entry->d_name)) continue;
        struct stat st;
        if (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) continue;
        char path[OSPI_PATH];
        int n = snprintf(path, sizeof path, "%s%s%s", parent, parent[0] ? "/" : "", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof path) continue;
        if (matches(&st, id)) { strcpy(target, path); found = 1; break; }
        if (!S_ISDIR(st.st_mode) || (uint64_t)st.st_dev != id->device) continue;
        int child = openat(fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (child < 0) continue;
        struct stat live;
        if (!fstat(child, &live) && live.st_dev == st.st_dev && live.st_ino == st.st_ino)
            found = search(child, path, id, target, remaining, depth + 1);
        close(child); if (found) break;
    }
    closedir(dir); return found;
}
int finder_alias_target(const char *root, const char *rel, const char *name, char target[OSPI_PATH]) {
    if (!target) return -1;
    char path[OSPI_PATH], parent[OSPI_PATH]; Identity id = {0};
    int version = read_alias(root, rel, name, path, &id);
    if (version <= 0) return version;
    strcpy(parent, path); char *leaf = strrchr(parent, '/');
    if (leaf) *leaf++ = 0; else { leaf = path; parent[0] = 0; }
    int dir = finder_directory_open(root, parent); struct stat st;
    int valid = dir >= 0 && !fstatat(dir, leaf, &st, AT_SYMLINK_NOFOLLOW) &&
        (version == 2 ? matches(&st, &id) : S_ISREG(st.st_mode) || S_ISDIR(st.st_mode));
    if (dir >= 0) close(dir);
    if (valid) { strcpy(target, path); return 1; }
    if (version == 1) return -1;
    dir = finder_directory_open(root, ""); if (dir < 0) return -1;
    unsigned remaining = 16384;
    valid = search(dir, "", &id, target, &remaining, 0); close(dir);
    return valid ? 1 : -1;
}

int finder_alias(const char *root, const char *rel, const char *name, int favorite, char out[OSPI_NAME]) {
    if (!name_ok(name) || !rel || !out) return -1;
    int from = finder_directory_open(root, rel); if (from < 0) return -1;
    struct stat st;
    if (fstatat(from, name, &st, AT_SYMLINK_NOFOLLOW) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) { close(from); return -1; }
    int to = from;
    if (favorite) {
        int base = finder_directory_open(root, "");
        if (base < 0) { close(from); return -1; }
        if (mkdirat(base, "Favorites", 0700) && errno != EEXIST) { close(base); close(from); return -1; }
        to = openat(base, "Favorites", O_RDONLY | O_DIRECTORY | O_NOFOLLOW); close(base);
        if (to < 0) { close(from); return -1; }
    }
    char record[OSPI_PATH + 160], next[OSPI_NAME];
    Identity id = identity(&st);
    int n = snprintf(record, sizeof record, "OS999-ALIAS-2\n%llu %llu %lld %ld\n%s%s%s",
        (unsigned long long)id.device, (unsigned long long)id.inode,
        (long long)id.birth, id.nanos, rel, rel[0] ? "/" : "", name);
    int result = -1;
    if (n <= 0 || (size_t)n >= sizeof record) goto done;
    for (int i = 1; i <= 100; i++) {
        int length = i == 1 ? snprintf(next, sizeof next, "%s alias", name) : snprintf(next, sizeof next, "%s alias %d", name, i);
        if (length < 0 || (size_t)length >= sizeof next) break;
        int fd = openat(to, next, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) { if (errno == EEXIST) continue; break; }
        int written = write(fd, record, (size_t)n) == n && !fsync(fd);
        close(fd);
        if (!written) { unlinkat(to, next, 0); break; }
        strcpy(out, next); result = 0; break;
    }
done:
    if (to != from) close(to);
    close(from); return result;
}
