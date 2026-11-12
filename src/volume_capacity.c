#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "volume_capacity.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#define CAPACITY_FILE "capacity"
#define CAPACITY_MAGIC "OS999-CAPACITY-1\n"
#define WALK_DEPTH 64

/* ---- pure rules ---------------------------------------------------------- */

void volume_space_compute(uint64_t capacity, uint64_t used, uint64_t host_free, VolumeSpace *out) {
    uint64_t room = capacity > used ? capacity - used : 0;
    out->capacity = capacity;
    out->used = used;
    out->host_free = host_free;
    out->free = room < host_free ? room : host_free;
}

/* The larger of 2 GiB and (used + 512 MiB), but never more than the host can
 * actually supply on top of what is already used. Always >= used. */
uint64_t volume_capacity_default_for(uint64_t used, uint64_t host_free) {
    uint64_t want = used + VOLUME_CAPACITY_HEADROOM_BYTES;
    if (want < VOLUME_CAPACITY_DEFAULT_BYTES) want = VOLUME_CAPACITY_DEFAULT_BYTES;
    uint64_t ceiling = used + host_free;
    if (ceiling < used) ceiling = UINT64_MAX;
    return want < ceiling ? want : ceiling;
}

void volume_format_bytes(char *out, size_t n, uint64_t b) {
    double gb = (double)b / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) snprintf(out, n, "%.1f GB", gb);
    else snprintf(out, n, "%llu MB", (unsigned long long)(b / (1024ull * 1024ull)));
}

static void say(char *msg, size_t n, const char *fmt, const char *a, const char *b) {
    if (msg && n) snprintf(msg, n, fmt, a, b);
}

int volume_capacity_check(uint64_t requested, uint64_t used, uint64_t host_free, char *msg, size_t n) {
    char a[32], b[32];
    if (requested < used) {
        volume_format_bytes(a, sizeof a, used);
        volume_format_bytes(b, sizeof b, requested);
        say(msg, n, "The disk cannot be made smaller than the %s already on it (%s was requested).", a, b);
        return -1;
    }
    if (requested < VOLUME_CAPACITY_MIN_BYTES || requested > VOLUME_CAPACITY_MAX_BYTES) {
        volume_format_bytes(a, sizeof a, VOLUME_CAPACITY_MIN_BYTES);
        volume_format_bytes(b, sizeof b, VOLUME_CAPACITY_MAX_BYTES);
        say(msg, n, "The disk size must be between %s and %s.", a, b);
        return -1;
    }
    uint64_t ceiling = used + host_free;
    if (ceiling < used) ceiling = UINT64_MAX;
    if (requested > ceiling) {
        volume_format_bytes(a, sizeof a, ceiling);
        volume_format_bytes(b, sizeof b, host_free);
        say(msg, n, "This Mac can only supply %s for the disk (%s free on the host).", a, b);
        return -1;
    }
    return 0;
}

/* ---- classic block view ---------------------------------------------------- */

/* Classic HFS counts allocation blocks in 16 bits and old callers multiply in
 * 32 bits, so Mac OS (8.1 and later) reports large volumes clipped to just
 * under 2 GiB, with the block size raised (in 512-byte steps) until the count
 * fits in 65535. Do the same. */
void volume_classic_view(const VolumeSpace *space, VolumeClassic *out) {
    const uint64_t cap = UINT64_C(0x7FFFFE00);
    uint64_t total = space->capacity < cap ? space->capacity : cap;
    uint64_t block = (total + 65534) / 65535;
    block = (block + 511) / 512 * 512;
    if (block < 512) block = 512;
    uint64_t blocks = total / block;
    if (blocks > 65535) blocks = 65535;
    uint64_t free_bytes = space->free < total ? space->free : total;
    uint64_t free_blocks = free_bytes / block;
    if (free_blocks > blocks) free_blocks = blocks;
    out->block_size = (uint32_t)block;
    out->total_blocks = (uint16_t)blocks;
    out->free_blocks = (uint16_t)free_blocks;
    out->total_bytes = (uint32_t)(blocks * block);
}

/* ---- measuring ----------------------------------------------------------- */

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t fork_bytes(int fd) {
#ifdef __APPLE__
    ssize_t n = fgetxattr(fd, "com.apple.ResourceFork", NULL, 0, 0, 0);
#else
    ssize_t n = fgetxattr(fd, "user.com.apple.ResourceFork", NULL, 0);
#endif
    return n > 0 ? (uint64_t)n : 0;
}

/* Descriptor-relative walk; links are never followed. */
static void walk(int dir, int depth, uint64_t *total) {
    if (depth > WALK_DEPTH) return;
    int scan = openat(dir, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR *d = scan < 0 ? NULL : fdopendir(scan);
    if (!d) { if (scan >= 0) close(scan); return; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        struct stat st;
        if (fstatat(dir, de->d_name, &st, AT_SYMLINK_NOFOLLOW)) continue;
        if (S_ISDIR(st.st_mode)) {
            int child = openat(dir, de->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (child >= 0) { walk(child, depth + 1, total); close(child); }
        } else if (S_ISREG(st.st_mode)) {
            *total += (uint64_t)st.st_size;
            int f = openat(dir, de->d_name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
            if (f >= 0) { *total += fork_bytes(f); close(f); }
        }
    }
    closedir(d);
}

static int tree_bytes(const char *path, uint64_t *total) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    walk(fd, 0, total);
    close(fd);
    return 0;
}

int volume_used_bytes(const char *root, uint64_t *out) {
    if (!root || !root[0] || !out) return -1;
    uint64_t total = 0;
    if (tree_bytes(root, &total)) return -1;
    char saves[PATH_MAX];
    size_t len = strlen(root);
    while (len > 1 && root[len - 1] == '/') len--;
    const char *slash = NULL;
    for (size_t i = len; i > 0; i--) if (root[i - 1] == '/') { slash = root + i - 1; break; }
    int k;
    if (!slash) k = snprintf(saves, sizeof saves, "../Saves");
    else if (slash == root) k = snprintf(saves, sizeof saves, "/Saves");
    else k = snprintf(saves, sizeof saves, "%.*s/Saves", (int)(slash - root), root);
    if (k > 0 && (size_t)k < sizeof saves) (void)tree_bytes(saves, &total);
    *out = total;
    return 0;
}

int volume_host_free(const char *root, uint64_t *out) {
    struct statvfs st;
    if (!root || !out || statvfs(root, &st) != 0) return -1;
    unsigned long bs = st.f_frsize ? st.f_frsize : st.f_bsize;
    *out = (uint64_t)st.f_bavail * (uint64_t)bs;
    return 0;
}

/* ---- persistence ----------------------------------------------------------- */

static int capacity_path(const char *root, char *out, size_t n, int make) {
    char dir[PATH_MAX];
    if (!root || !root[0]) return -1;
    int k = snprintf(dir, sizeof dir, "%s/.os999", root);
    if (k < 0 || (size_t)k >= sizeof dir) return -1;
    if (make && mkdir(dir, 0700) && errno != EEXIST) return -1;
    k = snprintf(out, n, "%s/" CAPACITY_FILE, dir);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

static int read_stored(const char *root, uint64_t *out) {
    char path[PATH_MAX], buf[64];
    if (capacity_path(root, path, sizeof path, 0)) return -1;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat st;
    ssize_t got = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) ? read(fd, buf, sizeof buf - 1) : -1;
    close(fd);
    size_t magic = strlen(CAPACITY_MAGIC);
    if (got <= (ssize_t)magic) return -1;
    buf[got] = 0;
    if (memcmp(buf, CAPACITY_MAGIC, magic)) return -1;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(buf + magic, &end, 10);
    if (errno || end == buf + magic || (*end != '\n' && *end != 0)) return -1;
    if (v < VOLUME_CAPACITY_MIN_BYTES || v > VOLUME_CAPACITY_MAX_BYTES) return -1;
    *out = v;
    return 0;
}

static int store(const char *root, uint64_t bytes) {
    char path[PATH_MAX], tmp[PATH_MAX + 8], body[64];
    if (capacity_path(root, path, sizeof path, 1)) return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int n = snprintf(body, sizeof body, CAPACITY_MAGIC "%llu\n", (unsigned long long)bytes);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    int ok = write(fd, body, (size_t)n) == n && fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (!ok || rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int volume_capacity_load(const char *root, uint64_t *out, int *stored) {
    if (!root || !out) return -1;
    if (stored) *stored = 0;
    if (read_stored(root, out) == 0) { if (stored) *stored = 1; return 0; }
    uint64_t used = 0, host = 0;
    if (volume_used_bytes(root, &used) || volume_host_free(root, &host)) return -1;
    *out = volume_capacity_default_for(used, host);
    return 0;
}

int volume_capacity_ensure(const char *root) {
    uint64_t cap;
    int stored;
    if (volume_capacity_load(root, &cap, &stored)) return -1;
    return stored ? 0 : store(root, cap < VOLUME_CAPACITY_MIN_BYTES ? VOLUME_CAPACITY_MIN_BYTES : cap);
}

static struct { char root[PATH_MAX]; int64_t stamp; VolumeSpace space; } g_cache;
void volume_space_invalidate(void) { g_cache.stamp = 0; }

int volume_capacity_set(const char *root, uint64_t bytes, char *msg, size_t n) {
    uint64_t used = 0, host = 0;
    if (volume_used_bytes(root, &used) || volume_host_free(root, &host)) {
        if (msg && n) snprintf(msg, n, "The disk could not be measured.");
        return -1;
    }
    if (volume_capacity_check(bytes, used, host, msg, n)) return -1;
    if (store(root, bytes)) {
        if (msg && n) snprintf(msg, n, "The new disk size could not be saved.");
        return -1;
    }
    volume_space_invalidate();
    return 0;
}

int volume_space_at(const char *root, uint64_t host_free, VolumeSpace *out) {
    uint64_t used = 0, cap = 0;
    if (!out || volume_used_bytes(root, &used)) return -1;
    if (read_stored(root, &cap) != 0) cap = volume_capacity_default_for(used, host_free);
    volume_space_compute(cap, used, host_free, out);
    return 0;
}

int volume_space(const char *root, int fresh, VolumeSpace *out) {
    if (!root || !out) return -1;
    int64_t now = now_ms();
    if (!fresh && g_cache.stamp && now - g_cache.stamp < 5000 && !strcmp(g_cache.root, root)) {
        *out = g_cache.space;
        return 0;
    }
    uint64_t host;
    if (volume_host_free(root, &host) || volume_space_at(root, host, out)) return -1;
    snprintf(g_cache.root, sizeof g_cache.root, "%s", root);
    g_cache.space = *out;
    g_cache.stamp = now ? now : 1;
    return 0;
}

static char g_notice[200];
const char *volume_disk_full_notice(int clear) {
    const char *r = g_notice[0] ? g_notice : NULL;
    if (clear && r) { static char held[200]; snprintf(held, sizeof held, "%s", g_notice); g_notice[0] = 0; return held; }
    return r;
}

int volume_space_reserve(const char *root, uint64_t bytes, char *msg, size_t n) {
    VolumeSpace sp;
    if (volume_space(root, 1, &sp)) return 0;   /* cannot measure: do not invent a refusal */
    if (bytes <= sp.free) return 0;
    char a[32], b[32];
    volume_format_bytes(a, sizeof a, bytes);
    volume_format_bytes(b, sizeof b, sp.free);
    static const char fmt[] = "The disk is full: %s needed, %s available. Make the disk larger in the Memory control panel or remove items.";
    snprintf(g_notice, sizeof g_notice, fmt, a, b);
    say(msg, n, fmt, a, b);
    return VOLUME_DISK_FULL;
}

/* ---- per-process budget ------------------------------------------------------ */

static int budget_refresh(VolumeBudget *b) {
    VolumeSpace sp;
    if (volume_space(b->root, 1, &sp)) return -1;
    b->free = sp.free;
    b->spent = 0;
    b->stamp_ms = now_ms();
    return 0;
}

int volume_budget_open(VolumeBudget *b, const char *root) {
    if (!b || !root || strlen(root) >= sizeof b->root) return -1;
    memset(b, 0, sizeof *b);
    snprintf(b->root, sizeof b->root, "%s", root);
    if (budget_refresh(b)) return -1;
    b->active = 1;
    return 0;
}

int volume_budget_spend(VolumeBudget *b, uint64_t bytes) {
    if (!b || !b->active) return 0;
    if (now_ms() - b->stamp_ms >= 1000) (void)budget_refresh(b);
    uint64_t left = b->free > b->spent ? b->free - b->spent : 0;
    if (bytes > left) {
        if (budget_refresh(b)) return 0;   /* the walk may show space was freed */
        left = b->free;
        if (bytes > left) return VOLUME_DISK_FULL;
    }
    b->spent += bytes;
    return 0;
}

int volume_budget_space(VolumeBudget *b, VolumeSpace *out) {
    if (!b || !b->active || !out) return -1;
    return volume_space(b->root, 1, out);
}
