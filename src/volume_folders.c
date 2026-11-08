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

static uint64_t dir_used_bytes(const char *path, int depth) {
    if (depth > 16) return 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    uint64_t total = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") ||
            !strcmp(de->d_name, RAMDISK_QUOTA_MARK) || !strcmp(de->d_name, FOLDER_MARK))
            continue;
        char child[OSPI_PATH];
        int k = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (k < 0 || (size_t)k >= sizeof child) continue;
        struct stat st;
        if (lstat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) total += dir_used_bytes(child, depth + 1);
        else if (S_ISREG(st.st_mode) && st.st_size > 0) total += (uint64_t)st.st_size;
    }
    closedir(d);
    return total;
}

int volume_ram_disk_sync(const char *root, int enabled, int quota_mb, int wipe_contents) {
    char dir[OSPI_PATH], qpath[OSPI_PATH + 32];
    if (volume_join(dir, sizeof dir, root, "RAM Disk") != 0 ||
        snprintf(qpath, sizeof qpath, "%s/%s", dir, RAMDISK_QUOTA_MARK) >= (int)sizeof qpath)
        return -1;
    if (!enabled) {
        (void)unlink(qpath);
        char fmark[OSPI_PATH + 32];
        if (snprintf(fmark, sizeof fmark, "%s/%s", dir, FOLDER_MARK) < (int)sizeof fmark)
            (void)unlink(fmark);
        if (wipe_contents) (void)wipe_at(dir, 0);
        else (void)rmdir(dir);
        return 0;
    }
    int mb = quota_mb < 1 ? 4 : quota_mb > 256 ? 256 : quota_mb;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    if (wipe_contents) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") ||
                    !strcmp(de->d_name, RAMDISK_QUOTA_MARK)) continue;
                char child[OSPI_PATH];
                if (snprintf(child, sizeof child, "%s/%s", dir, de->d_name) < (int)sizeof child)
                    (void)wipe_at(child, 0);
            }
            closedir(d);
        }
    }
    FILE *f = fopen(qpath, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%d\n", mb) > 0;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int volume_ram_disk_usage(const char *root, uint64_t *used_bytes, int *quota_mb) {
    if (used_bytes) *used_bytes = 0;
    if (quota_mb) *quota_mb = 0;
    char dir[OSPI_PATH], qpath[OSPI_PATH + 32], buf[32] = {0};
    if (volume_join(dir, sizeof dir, root, "RAM Disk") != 0 ||
        snprintf(qpath, sizeof qpath, "%s/%s", dir, RAMDISK_QUOTA_MARK) >= (int)sizeof qpath)
        return -1;
    FILE *f = fopen(qpath, "r");
    if (!f) return -1;
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = 0;
    int mb = atoi(buf);
    if (mb <= 0) return -1;
    if (quota_mb) *quota_mb = mb;
    if (used_bytes) *used_bytes = dir_used_bytes(dir, 0);
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
