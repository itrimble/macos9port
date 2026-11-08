#include "finder_files.h"
#include "volume_directory.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#else
#include <sys/syscall.h>
#endif

static int name_ok(const char *s) {
    return s && s[0] && s[0] != '.' && strlen(s) < OSPI_NAME && !strchr(s, '/') &&
           !strchr(s, '\n') && !strchr(s, '\r');
}
int finder_directory_open(const char *root, const char *rel) {
    return volume_directory_open(root, rel);
}
static int absent(int fd, const char *name) {
    struct stat st;
    return fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT;
}
static int sidecar(char out[OSPI_NAME + 6], const char *name) {
    int n = snprintf(out, OSPI_NAME + 6, "%s.rsrc", name);
    return n > 0 && n < OSPI_NAME + 6;
}
int finder_identity(const char *root, const char *rel, const char *name, FinderUndo *identity) {
    if (!identity || !name_ok(name)) return -1;
    identity->identity_valid = 0;
    int fd = finder_directory_open(root, rel);
    if (fd < 0) return -1;
    struct stat st;
    int result = -1;
    if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) ||
        (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) goto done;
    identity->device = (uint64_t)st.st_dev; identity->inode = (uint64_t)st.st_ino;
    identity->file_kind = S_ISDIR(st.st_mode) ? 2 : 1;
    char fork[OSPI_NAME + 6]; sidecar(fork, name);
    identity->fork_present = 0;
    if (!fstatat(fd, fork, &st, AT_SYMLINK_NOFOLLOW)) {
        if (!S_ISREG(st.st_mode)) goto done;
        identity->fork_present = 1;
        identity->fork_device = (uint64_t)st.st_dev; identity->fork_inode = (uint64_t)st.st_ino;
    } else if (errno != ENOENT) goto done;
    identity->identity_valid = 1; result = 0;
done:
    close(fd); return result;
}
static int move_exclusive(int fd, const char *from, const char *to) {
#ifdef __APPLE__
    return renameatx_np(fd, from, fd, to, RENAME_EXCL);
#elif defined(SYS_renameat2)
    return (int)syscall(SYS_renameat2, fd, from, fd, to, 1);
#else
    (void)fd; (void)from; (void)to; errno = ENOTSUP; return -1;
#endif
}

int finder_rename(const char *root, const char *rel, const char *name, const char *next) {
    if (!name_ok(name) || !name_ok(next) || !strcmp(name, "Trash") ||
        !strcmp(name, "System Folder") || !strcmp(next, "Trash")) return -1;
    if (!strcmp(name, next)) return 0;
    int fd = finder_directory_open(root, rel);
    if (fd < 0) return -1;
    char oldfork[OSPI_NAME + 6], newfork[OSPI_NAME + 6];
    sidecar(oldfork, name); sidecar(newfork, next);
    struct stat st;
    int fork = fstatat(fd, oldfork, &st, AT_SYMLINK_NOFOLLOW) == 0;
    int result = -1;
    if (!absent(fd, next) || !absent(fd, newfork) || (fork && !S_ISREG(st.st_mode))) goto done;
    if (move_exclusive(fd, name, next)) goto done;
    if (fork && move_exclusive(fd, oldfork, newfork)) {
        /* Never replace another item during rollback. */
        move_exclusive(fd, next, name); goto done;
    }
    result = 0;
done:
    close(fd); return result;
}

typedef struct { unsigned files; uint64_t bytes, limit; int has_limit; } Budget;
static void remove_owned(int fd, const char *name, int depth) {
    if (depth > 18) return;
    int child = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (child < 0) { unlinkat(fd, name, 0); return; }
    DIR *dir = fdopendir(child);
    if (!dir) { close(child); return; }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        remove_owned(child, entry->d_name, depth + 1);
    }
    closedir(dir); unlinkat(fd, name, AT_REMOVEDIR);
}
static int copy_entry(int from, const char *name, int to, const char *next, int depth, Budget *budget) {
    struct stat st;
    if (depth > 16 || budget->files++ >= 4096 || fstatat(from, name, &st, AT_SYMLINK_NOFOLLOW)) return -1;
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) return -1;
    int in = openat(from, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (in < 0) return -1;
    struct stat live;
    if (fstat(in, &live) || live.st_ino != st.st_ino || live.st_dev != st.st_dev) { close(in); return -1; }
    int result = -1;
    if (S_ISDIR(st.st_mode)) {
        if (mkdirat(to, next, 0700)) { close(in); return -1; }
        int out = openat(to, next, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        DIR *dir = fdopendir(in);
        if (out < 0 || !dir) {
            if (out >= 0) close(out);
            if (dir) closedir(dir); else close(in);
            remove_owned(to, next, 0); return -1;
        }
        result = 0;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (copy_entry(in, entry->d_name, out, entry->d_name, depth + 1, budget)) { result = -1; break; }
        }
        fchmod(out, st.st_mode & 0777);
        close(out); closedir(dir);
    } else {
        uint64_t cap = budget->has_limit ? budget->limit : 512ull * 1024 * 1024;
        if (st.st_size < 0 || budget->bytes > cap || (uint64_t)st.st_size > cap - budget->bytes) { close(in); return -1; }
        budget->bytes += (uint64_t)st.st_size;
        int out = openat(to, next, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (out < 0) { close(in); return -1; }
#ifdef __APPLE__
        result = fcopyfile(in, out, NULL, COPYFILE_ALL);
#else
        char buf[16384]; ssize_t n;
        result = 0;
        uint64_t copied = 0;
        while ((n = read(in, buf, sizeof buf)) > 0) {
            copied += (uint64_t)n;
            if (copied > (uint64_t)st.st_size) { result = -1; break; }
            ssize_t at = 0;
            while (at < n) { ssize_t written = write(out, buf + at, (size_t)(n - at));
                if (written <= 0) { result = -1; break; } at += written; }
            if (result) break;
        }
        if (n < 0) result = -1;
        if (!result) fchmod(out, st.st_mode & 0777);
#endif
        if (close(out)) result = -1;
        close(in);
    }
    if (result) remove_owned(to, next, 0);
    return result;
}
/* Reject copying a folder into itself or any descendant, including aliases
 * in the spelling of a case-insensitive host path. Inspect directory identities. */
static int inside_source(int from, const char *name, int to) {
    struct stat source;
    if (fstatat(from, name, &source, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(source.st_mode)) return 0;
    int current = dup(to); if (current < 0) return 1;
    for (int depth = 0; depth < 128; depth++) {
        struct stat st, up;
        if (fstat(current, &st)) break;
        if (st.st_dev == source.st_dev && st.st_ino == source.st_ino) { close(current); return 1; }
        int parent = openat(current, "..", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (parent < 0) break;
        if (fstat(parent, &up)) { close(parent); break; }
        if (st.st_dev == up.st_dev && st.st_ino == up.st_ino) { close(parent); close(current); return 0; }
        close(current); current = parent;
    }
    close(current); return 1;
}
static int is_ramdisk_path(const char *path) {
    return path && (!strcasecmp(path, "RAM Disk") || !strncasecmp(path, "RAM Disk/", 9));
}
static uint64_t entry_bytes(int parent, const char *name, int depth) {
    if (depth > 64) return UINT64_MAX;
    int fd = openat(parent, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return UINT64_MAX;
    struct stat st;
    uint64_t total = UINT64_MAX;
    if (fstat(fd, &st) != 0) goto done;
    if (S_ISREG(st.st_mode) && st.st_size >= 0) {
        total = (uint64_t)st.st_size;
#ifdef __APPLE__
        ssize_t fork = fgetxattr(fd, "com.apple.ResourceFork", NULL, 0, 0, 0);
#else
        ssize_t fork = fgetxattr(fd, "user.com.apple.ResourceFork", NULL, 0);
#endif
        if (fork > 0) total += (uint64_t)fork;
    } else if (S_ISDIR(st.st_mode)) {
        DIR *d = fdopendir(fd);
        if (!d) goto done;
        total = 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            uint64_t size = entry_bytes(fd, e->d_name, depth + 1);
            if (size == UINT64_MAX || size > UINT64_MAX - total) { total = UINT64_MAX; break; }
            total += size;
        }
        closedir(d); return total;
    }
done:
    close(fd); return total;
}
static int ramdisk_remaining(const char *root, const char *dest, uint64_t *avail) {
    if (!is_ramdisk_path(dest)) return 0;
    uint64_t used = 0; int mb = 0;
    /* Ownership and regular nonblocking metadata validation are centralized. */
    if (volume_ram_disk_usage(root, &used, &mb) != 0) return -1;
    uint64_t quota = (uint64_t)mb * 1024ull * 1024ull;
    *avail = quota > used ? quota - used : 0;
    return 1;
}

static int copy_to(const char *root, const char *rel, const char *name, const char *destination,
                   int duplicate, char out[OSPI_NAME]) {
    if (!name_ok(name) || !out || !strcmp(name, "Trash")) return -1;
    int from = finder_directory_open(root, rel), to = finder_directory_open(root, destination);
    if (from < 0 || to < 0) { if (from >= 0) close(from); if (to >= 0) close(to); return -1; }
    char sourcefork[OSPI_NAME + 6], destfork[OSPI_NAME + 6], next[OSPI_NAME];
    sidecar(sourcefork, name); int result = -1;
    if (inside_source(from, name, to)) goto done;
    uint64_t rd_avail = 0;
    int rd_lim = ramdisk_remaining(root, destination, &rd_avail);
    if (rd_lim < 0) goto done;
    if (rd_lim) {
        uint64_t size = entry_bytes(from, name, 0);
        uint64_t fork_size = absent(from, sourcefork) ? 0 : entry_bytes(from, sourcefork, 0);
        if (size == UINT64_MAX || fork_size == UINT64_MAX || size > rd_avail || fork_size > rd_avail - size)
            goto done;
    }
    for (int i = duplicate ? 1 : 0; i <= 100; i++) {
        int n = i == 0 ? snprintf(next, sizeof next, "%s", name) :
                i == 1 ? snprintf(next, sizeof next, "%s copy", name) :
                         snprintf(next, sizeof next, "%s copy %d", name, i);
        if (n < 0 || (size_t)n >= sizeof next) break;
        sidecar(destfork, next);
        if (!absent(to, next) || !absent(to, destfork)) continue;
        Budget budget = {.limit = rd_avail, .has_limit = rd_lim};
        if (copy_entry(from, name, to, next, 0, &budget)) break;
        if (!absent(from, sourcefork) && copy_entry(from, sourcefork, to, destfork, 0, &budget)) {
            remove_owned(to, next, 0); break;
        }
        strcpy(out, next); result = 0; break;
    }
done:
    close(from); close(to); return result;
}
int finder_duplicate(const char *root, const char *rel, const char *name, char out[OSPI_NAME]) {
    return copy_to(root, rel, name, rel, 1, out);
}
int finder_copy_to(const char *root, const char *rel, const char *name, const char *to, char out[OSPI_NAME]) {
    return copy_to(root, rel, name, to, 0, out);
}

static int move_between(int from, const char *name, int to, const char *next) {
#ifdef __APPLE__
    return renameatx_np(from, name, to, next, RENAME_EXCL);
#elif defined(SYS_renameat2)
    return (int)syscall(SYS_renameat2, from, name, to, next, 1);
#else
    (void)from; (void)name; (void)to; (void)next; errno = ENOTSUP; return -1;
#endif
}
static int move_pair(int from, const char *name, int to, const char *next) {
    char oldfork[OSPI_NAME + 6], newfork[OSPI_NAME + 6];
    sidecar(oldfork, name); sidecar(newfork, next);
    struct stat st;
    int fork = fstatat(from, oldfork, &st, AT_SYMLINK_NOFOLLOW) == 0;
    if (!absent(to, next) || !absent(to, newfork) || (fork && !S_ISREG(st.st_mode))) return -1;
    if (move_between(from, name, to, next)) return -1;
    if (fork && move_between(from, oldfork, to, newfork)) {
        move_between(to, next, from, name); return -1;
    }
    return 0;
}
int finder_trash(const char *root, const char *rel, const char *name, char out[OSPI_NAME]) {
    if (!name_ok(name) || !rel || !out || !strcmp(rel, "Trash") || !strncmp(rel, "Trash/", 6) ||
        (!rel[0] && (!strcmp(name, "Trash") || !strcmp(name, "System Folder")))) return -1;
    int from = finder_directory_open(root, rel), base = finder_directory_open(root, "");
    if (from < 0 || base < 0) { if (from >= 0) close(from); if (base >= 0) close(base); return -1; }
    if (mkdirat(base, "Trash", 0700) && errno != EEXIST) { close(from); close(base); return -1; }
    int trash = openat(base, "Trash", O_RDONLY | O_DIRECTORY | O_NOFOLLOW); close(base);
    if (trash < 0) { close(from); return -1; }
    int result = -1;
    for (int i = 1; i <= 100; i++) {
        char next[OSPI_NAME], origin[OSPI_NAME + 20], fork[OSPI_NAME + 6];
        int n = i == 1 ? snprintf(next, sizeof next, "%s", name) : snprintf(next, sizeof next, "%s %d", name, i);
        if (n < 0 || (size_t)n >= sizeof next) break;
        sidecar(fork, next);
        if (!absent(trash, next) || !absent(trash, fork)) continue;
        snprintf(origin, sizeof origin, ".os999-origin-%s", next);
        int meta = openat(trash, origin, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (meta < 0) continue;
        char record[OSPI_PATH + OSPI_NAME + 2];
        n = snprintf(record, sizeof record, "%s\n%s", rel, name);
        int saved = n > 0 && (size_t)n < sizeof record && write(meta, record, (size_t)n) == n && !fsync(meta);
        close(meta);
        if (!saved || move_pair(from, name, trash, next)) { unlinkat(trash, origin, 0); break; }
        strcpy(out, next); result = 0; break;
    }
    close(from); close(trash); return result;
}
int finder_put_away(const char *root, const char *name, char parent[OSPI_PATH], char out[OSPI_NAME]) {
    if (!name_ok(name) || !parent || !out) return -1;
    int trash = finder_directory_open(root, "Trash"); if (trash < 0) return -1;
    char origin[OSPI_NAME + 20], record[OSPI_PATH + OSPI_NAME + 2];
    snprintf(origin, sizeof origin, ".os999-origin-%s", name);
    int meta = openat(trash, origin, O_RDONLY | O_NOFOLLOW | O_NONBLOCK), result = -1;
    if (meta < 0) { close(trash); return -1; }
    struct stat st;
    ssize_t n = -1;
    if (!fstat(meta, &st) && S_ISREG(st.st_mode) && st.st_size < (off_t)sizeof record - 1)
        n = read(meta, record, sizeof record - 1);
    close(meta);
    if (n <= 0) goto done;
    record[n] = 0;
    char *split = strchr(record, '\n');
    if (!split) goto done;
    *split++ = 0;
    if (strlen(record) >= OSPI_PATH || !name_ok(split)) goto done;
    int to = finder_directory_open(root, record);
    if (to < 0) goto done;
    if (!move_pair(trash, name, to, split)) {
        strcpy(parent, record); strcpy(out, split); unlinkat(trash, origin, 0); result = 0;
    }
    close(to);
done:
    close(trash); return result;
}

int finder_label(const char *root, const char *rel, const char *name, int value) {
    if (!name_ok(name) || value > 7) return -1;
    int dir = finder_directory_open(root, rel); if (dir < 0) return -1;
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return -1;
    unsigned char label = value < 0 ? 0 : (unsigned char)value;
    int result;
#ifdef __APPLE__
    if (value < 0) result = fgetxattr(fd, "user.os999.label", &label, 1, 0, 0) == 1 ? label : 0;
    else result = fsetxattr(fd, "user.os999.label", &label, 1, 0, 0);
#else
    if (value < 0) result = fgetxattr(fd, "user.os999.label", &label, 1) == 1 ? label : 0;
    else result = fsetxattr(fd, "user.os999.label", &label, 1, 0);
#endif
    close(fd); return value < 0 && (result < 0 || result > 7) ? 0 : result;
}

int finder_move(const char *root, const char *rel, const char *name, const char *destination) {
    if (!name_ok(name) || !rel || !destination || (!rel[0] &&
        (!strcmp(name, "Trash") || !strcmp(name, "System Folder")))) return -1;
    int from = finder_directory_open(root, rel), to = finder_directory_open(root, destination);
    if (from < 0 || to < 0) { if (from >= 0) close(from); if (to >= 0) close(to); return -1; }
    struct stat a, b; int result = -1;
    uint64_t rd_avail = 0;
    int limited = ramdisk_remaining(root, destination, &rd_avail);
    if (limited < 0) { close(from); close(to); return -1; }
    if (limited && !is_ramdisk_path(rel)) {
        uint64_t size = entry_bytes(from, name, 0);
        char fork[OSPI_NAME + 6]; sidecar(fork, name);
        uint64_t fork_size = absent(from, fork) ? 0 : entry_bytes(from, fork, 0);
        if (size == UINT64_MAX || fork_size == UINT64_MAX || size > rd_avail || fork_size > rd_avail - size) {
            close(from); close(to); return -1;
        }
    }
    if (!fstat(from, &a) && !fstat(to, &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino) result = 0;
    else if (!inside_source(from, name, to)) result = move_pair(from, name, to, name);
    close(from); close(to); return result;
}
