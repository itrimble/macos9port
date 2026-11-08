#include "ospi.h"
#include "volume_directory.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/xattr.h>

static int wipe_at(const char *path, int depth) {
    struct stat st;
    if (depth > 16 || lstat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[OSPI_PATH];
        int k = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (k < 0 || (size_t)k >= sizeof child || wipe_at(child, depth + 1) != 0) {
            rc = -1;
        }
    }
    closedir(d);
    if (rc != 0 || rmdir(path) != 0) {
        return -1;
    }
    return 0;
}

int volume_empty_trash(const char *root) {
    char trash[OSPI_PATH];
    if (volume_join(trash, sizeof trash, root, "Trash") != 0) {
        return -1;
    }
    DIR *d = opendir(trash);
    if (!d) {
        return -1;
    }
    struct dirent *de;
    int rc = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[OSPI_PATH];
        int k = snprintf(child, sizeof child, "%s/%s", trash, de->d_name);
        if (k < 0 || (size_t)k >= sizeof child || wipe_at(child, 0) != 0) {
            rc = -1;
        }
    }
    closedir(d);
    return rc;
}

/* Special folders keep their identity through renames, as Mac OS 9's did.
 * A hidden ".os999-folder" file inside a folder records the special icon it
 * carries (written when such a folder is first renamed), and FindFolder-style
 * lookups follow that mark before falling back to the usual name. */

#define FOLDER_MARK ".os999-folder"

/* Icon names are lowercase asset ids ending in "_folder", nothing else. */
static int valid_icon(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n < 8 || n >= OSPI_SPECIAL || strcmp(s + n - 7, "_folder") != 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) return 0;
    return 1;
}

static int mark_path(char *out, size_t n, const char *root, const char *rel) {
    char dir[OSPI_PATH];
    if (volume_join(dir, sizeof dir, root, rel) != 0) return -1;
    return snprintf(out, n, "%s/%s", dir, FOLDER_MARK) < (int)n ? 0 : -1;
}

int volume_folder_icon(const char *root, const char *rel, char *out, size_t n) {
    char path[OSPI_PATH + 16], buf[OSPI_SPECIAL + 8] = {0};
    if (!out || n == 0 || mark_path(path, sizeof path, root, rel) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t k = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    buf[strcspn(buf, " \t\r\n")] = 0;
    if (!valid_icon(buf) || strlen(buf) >= n) return 0;
    memcpy(out, buf, strlen(buf) + 1);
    return 1;
}

int volume_set_folder_icon(const char *root, const char *rel, const char *icon) {
    char path[OSPI_PATH + 16];
    if (!valid_icon(icon) || mark_path(path, sizeof path, root, rel) != 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%s\n", icon) > 0;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

/* The top-level folder playing a role: the one marked with `icon`, else the
 * unmarked one with the usual name, else a new folder with that name. */
int volume_special_folder(const char *root, const char *icon, const char *name, char *rel, size_t n) {
    DIR *d = opendir(root);
    char named[OSPI_NAME] = {0};
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.' || strlen(de->d_name) >= OSPI_NAME) continue;
            char path[OSPI_PATH], mark[OSPI_SPECIAL];
            struct stat st;
            if (volume_join(path, sizeof path, root, de->d_name) != 0 || stat(path, &st) != 0 ||
                !S_ISDIR(st.st_mode)) continue;
            int marked = volume_folder_icon(root, de->d_name, mark, sizeof mark);
            if (marked && strcmp(mark, icon) == 0) {
                int ok = snprintf(rel, n, "%s", de->d_name) < (int)n;   /* before closedir frees it */
                closedir(d);
                return ok ? 0 : -1;
            }
            if (!marked && strcasecmp(de->d_name, name) == 0) snprintf(named, sizeof named, "%s", de->d_name);
        }
        closedir(d);
    }
    if (named[0]) return snprintf(rel, n, "%s", named) < (int)n ? 0 : -1;
    char path[OSPI_PATH];
    if (volume_join(path, sizeof path, root, name) != 0 || (mkdir(path, 0755) != 0 && errno != EEXIST))
        return -1;
    return snprintf(rel, n, "%s", name) < (int)n ? 0 : -1;
}

int volume_clear_folder_icon(const char *root, const char *rel) {
    int fd = volume_directory_open(root, rel);
    if (fd < 0) return -1;
    int ok = unlinkat(fd, ".os999-folder", 0) == 0 || errno == ENOENT;
    close(fd);
    return ok ? 0 : -1;
}

#define RAMDISK_QUOTA_MARK ".os999-ramdisk-mb"

#define RAMDISK_OWNER_MARK ".os999-ramdisk-owner"

/* Only directories created here are disposable. An old quota marker alone
 * does not establish ownership; never adopt existing user folders. */
static int ramdisk_owner(int dir, int create) {
    struct stat st, marker;
    char expected[96], actual[96] = {0};
    if (fstat(dir, &st) != 0) return -1;
    int n = snprintf(expected, sizeof expected, "OS999 RAM disk 1 %llu %llu\n",
                     (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
    int flags = create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY;
    int fd = openat(dir, RAMDISK_OWNER_MARK, flags | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return -1;
    int ok = fstat(fd, &marker) == 0 && S_ISREG(marker.st_mode) && marker.st_nlink == 1;
    if (ok && create) ok = write(fd, expected, (size_t)n) == n;
    else if (ok) ok = read(fd, actual, sizeof actual) == n && !memcmp(actual, expected, (size_t)n);
    if (close(fd) != 0) ok = 0;
    return ok ? 0 : -1;
}

/* Descriptor-relative traversal never follows links, including during cleanup. */
static int ramdisk_walk(int dir, int depth, int wipe, uint64_t *used) {
    if (depth > 64) return -1;
    int scan = openat(dir, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR *d = scan < 0 ? NULL : fdopendir(scan);
    if (!d) { if (scan >= 0) close(scan); return -1; }
    int rc = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") ||
            (depth == 0 && (!strcmp(name, RAMDISK_OWNER_MARK) || !strcmp(name, RAMDISK_QUOTA_MARK))))
            continue;
        struct stat st;
        if (fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) != 0) { rc = -1; break; }
        if (S_ISDIR(st.st_mode)) {
            int child = openat(dir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (child < 0) { rc = -1; break; }
            int result = ramdisk_walk(child, depth + 1, wipe, used);
            close(child);
            if (result != 0 || (wipe && unlinkat(dir, name, AT_REMOVEDIR) != 0)) { rc = -1; break; }
        } else if (wipe) {
            if (unlinkat(dir, name, 0) != 0) { rc = -1; break; }
        } else if (S_ISREG(st.st_mode) && st.st_size >= 0) {
            int file = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
            struct stat live;
            if (file < 0) { rc = -1; break; }
            if (fstat(file, &live) != 0 || !S_ISREG(live.st_mode) || live.st_size < 0) {
                close(file); rc = -1; break;
            }
#ifdef __APPLE__
            ssize_t fork = fgetxattr(file, "com.apple.ResourceFork", NULL, 0, 0, 0);
#else
            ssize_t fork = fgetxattr(file, "user.com.apple.ResourceFork", NULL, 0);
#endif
            close(file);
            *used += (uint64_t)live.st_size + (fork > 0 ? (uint64_t)fork : 0);
        }
    }
    closedir(d);
    return rc;
}

static int ramdisk_empty(int dir) {
    int scan = openat(dir, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR *d = scan < 0 ? NULL : fdopendir(scan);
    if (!d) { if (scan >= 0) close(scan); return 0; }
    int empty = 1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") &&
            strcmp(de->d_name, RAMDISK_OWNER_MARK) && strcmp(de->d_name, RAMDISK_QUOTA_MARK)) {
            empty = 0; break;
        }
    }
    closedir(d);
    return empty;
}

int volume_ram_disk_sync(const char *root, int enabled, int quota_mb, int wipe_contents) {
    int base = volume_directory_open(root, "");
    if (base < 0) return -1;
    int created = enabled && mkdirat(base, "RAM Disk", 0700) == 0;
    int dir = openat(base, "RAM Disk", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) { int ok = !enabled && errno == ENOENT; close(base); return ok ? 0 : -1; }
    int rc = -1;
    if (ramdisk_owner(dir, created) != 0) goto done;
    /* Validate before truncation: reject links, FIFOs and hardlinked markers. */
    int fd = openat(dir, RAMDISK_QUOTA_MARK, O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) goto done;
    struct stat st;
    int safe = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1;
    if (!safe) { close(fd); goto done; }
    uint64_t used = 0;
    if (wipe_contents && ramdisk_walk(dir, 0, 1, &used) != 0) { close(fd); goto done; }
    if (enabled) {
        char buf[16];
        int mb = quota_mb < 1 ? 4 : quota_mb > 256 ? 256 : quota_mb;
        int n = snprintf(buf, sizeof buf, "%d\n", mb);
        rc = ftruncate(fd, 0) == 0 && write(fd, buf, (size_t)n) == n ? 0 : -1;
        if (close(fd) != 0) rc = -1;
    } else {
        close(fd);
        /* Preserve ownership and contents when disabling a nonempty disk. */
        if (!ramdisk_empty(dir)) goto done;
        if (unlinkat(dir, RAMDISK_QUOTA_MARK, 0) != 0 ||
            unlinkat(dir, RAMDISK_OWNER_MARK, 0) != 0) goto done;
        rc = unlinkat(base, "RAM Disk", AT_REMOVEDIR);
    }
done:
    close(dir); close(base);
    return rc;
}

int volume_ram_disk_usage(const char *root, uint64_t *used_bytes, int *quota_mb) {
    if (used_bytes) *used_bytes = 0;
    if (quota_mb) *quota_mb = 0;
    int dir = volume_directory_open(root, "RAM Disk");
    if (dir < 0) return -1;
    if (ramdisk_owner(dir, 0) != 0) { close(dir); return -1; }
    int fd = openat(dir, RAMDISK_QUOTA_MARK, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    char buf[32] = {0}, *end;
    ssize_t got = fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_nlink == 1 ?
                  read(fd, buf, sizeof buf - 1) : -1;
    if (fd >= 0) close(fd);
    long mb = got > 0 ? strtol(buf, &end, 10) : 0;
    int valid = mb >= 1 && mb <= 256 && (*end == '\n' ? end[1] == 0 : *end == 0);
    uint64_t used = 0;
    int rc = valid ? ramdisk_walk(dir, 0, 0, &used) : -1;
    close(dir);
    if (rc != 0) return -1;
    if (used_bytes) *used_bytes = used;
    if (quota_mb) *quota_mb = (int)mb;
    return 0;
}

enum { DCACHE_SLOTS = 32 };
typedef struct {
    char path[OSPI_PATH];
    uint64_t dev, ino, size;
    int64_t msec, mnsec;
    size_t req_n, len;
    uint32_t stamp;
    char *data;
} DiskCacheSlot;

static DiskCacheSlot g_dcache[DCACHE_SLOTS];
static size_t g_dcache_cap = 512u * 1024u, g_dcache_used;
static unsigned g_dcache_hits;
static uint32_t g_dcache_clock;

static void dcache_drop(int idx) {
    if (!g_dcache[idx].data) return;
    if (g_dcache_used >= g_dcache[idx].len) g_dcache_used -= g_dcache[idx].len;
    else g_dcache_used = 0;
    free(g_dcache[idx].data);
    memset(&g_dcache[idx], 0, sizeof g_dcache[idx]);
}

static void dcache_evict_to(size_t target) {
    while (g_dcache_used > target) {
        int oldest = -1;
        for (int i = 0; i < DCACHE_SLOTS; i++)
            if (g_dcache[i].data && (oldest < 0 || g_dcache[i].stamp < g_dcache[oldest].stamp))
                oldest = i;
        if (oldest < 0) { g_dcache_used = 0; break; }
        dcache_drop(oldest);
    }
}

void volume_disk_cache_set_kb(int kb) {
    int clamped = kb < 64 ? 64 : kb > 16384 ? 16384 : kb;
    g_dcache_cap = (size_t)clamped * 1024u;
    dcache_evict_to(g_dcache_cap);
}

void volume_disk_cache_stats(size_t *used_bytes, size_t *cap_bytes, unsigned *hits) {
    if (used_bytes) *used_bytes = g_dcache_used;
    if (cap_bytes) *cap_bytes = g_dcache_cap;
    if (hits) *hits = g_dcache_hits;
}

static void stat_times(const struct stat *st, int64_t *sec, int64_t *nsec) {
    *sec = (int64_t)st->st_mtime;
#ifdef __APPLE__
    *nsec = (int64_t)st->st_mtimespec.tv_nsec;
#else
    *nsec = (int64_t)st->st_mtim.tv_nsec;
#endif
}

int volume_disk_cache_lookup(const char *full, size_t req_n, char *buf) {
    struct stat st;
    if (!full || !buf || strlen(full) >= OSPI_PATH || stat(full, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    int64_t sec = 0, nsec = 0;
    stat_times(&st, &sec, &nsec);
    for (int i = 0; i < DCACHE_SLOTS; i++) {
        DiskCacheSlot *s = &g_dcache[i];
        if (!s->data || strcmp(s->path, full) != 0) continue;
        if (s->dev == (uint64_t)st.st_dev && s->ino == (uint64_t)st.st_ino &&
            s->size == (uint64_t)st.st_size && s->msec == sec && s->mnsec == nsec &&
            s->req_n >= req_n && s->len <= req_n) {
            memcpy(buf, s->data, s->len);
            s->stamp = ++g_dcache_clock;
            g_dcache_hits++;
            return 0;
        }
        dcache_drop(i);
        break;
    }
    return -1;
}

void volume_disk_cache_store(const char *full, size_t req_n, const char *buf) {
    struct stat st;
    if (!full || !buf || strlen(full) >= OSPI_PATH || stat(full, &st) != 0 || !S_ISREG(st.st_mode))
        return;
    size_t len = strlen(buf) + 1;
    if (len > g_dcache_cap) return;
    for (int i = 0; i < DCACHE_SLOTS; i++)
        if (g_dcache[i].data && strcmp(g_dcache[i].path, full) == 0) dcache_drop(i);
    dcache_evict_to(g_dcache_cap - len);
    int slot = -1;
    for (int i = 0; i < DCACHE_SLOTS; i++)
        if (!g_dcache[i].data) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < DCACHE_SLOTS; i++)
            if (g_dcache[i].stamp < g_dcache[slot].stamp) slot = i;
        dcache_drop(slot);
    }
    char *copy = malloc(len);
    if (!copy) return;
    memcpy(copy, buf, len);
    int64_t sec = 0, nsec = 0;
    stat_times(&st, &sec, &nsec);
    snprintf(g_dcache[slot].path, sizeof g_dcache[slot].path, "%s", full);
    g_dcache[slot].dev = (uint64_t)st.st_dev;
    g_dcache[slot].ino = (uint64_t)st.st_ino;
    g_dcache[slot].size = (uint64_t)st.st_size;
    g_dcache[slot].msec = sec;
    g_dcache[slot].mnsec = nsec;
    g_dcache[slot].req_n = req_n;
    g_dcache[slot].len = len;
    g_dcache[slot].stamp = ++g_dcache_clock;
    g_dcache[slot].data = copy;
    g_dcache_used += len;
}
