#include "mac_dirs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* See include/mac_dirs.h. Independent implementation; the only contract is the ID model described there. */

int mac_dirs_path_ok(const char *rel) {
    size_t n = strlen(rel);
    if (n >= DIRS_PATH_MAX || rel[0] == '/' || (n && rel[n - 1] == '/')) return 0;
    if (!n) return 1;
    int depth = 1, length = 0;
    for (const char *p = rel;; p++) {
        if (*p == '/' || !*p) {
            if (!length) return 0;
            const char *c = p - length;
            if ((length == 1 && c[0] == '.') || (length == 2 && c[0] == '.' && c[1] == '.')) return 0;
            if (!*p) break;
            if (++depth > DIRS_DEPTH_MAX) return 0;
            length = 0;
        } else length++;
    }
    return 1;
}

void mac_dirs_parent(const char *rel, char *parent, const char **leaf) {
    const char *slash = strrchr(rel, '/');
    if (!slash) { parent[0] = 0; *leaf = rel; return; }
    size_t n = (size_t)(slash - rel);
    memcpy(parent, rel, n);
    parent[n] = 0;
    *leaf = slash + 1;
}

/* Open the directory that holds the last component. Returns -1 (errno set), 0 when *parent is the root itself (do not close) or 1 when it is a
 * descriptor to close. Every intermediate component is opened without following links. */
static int open_parent(int root, const char *rel, int *parent, const char **leaf) {
    if (!mac_dirs_path_ok(rel) || !rel[0]) { errno = EINVAL; return -1; }
    char dir[DIRS_PATH_MAX];
    mac_dirs_parent(rel, dir, leaf);
    *leaf = rel + (*leaf - rel);   /* the leaf pointer is into rel, not into dir */
    if (!dir[0]) { *parent = root; return 0; }
    int cur = root, owned = 0;
    char *save = NULL;
    for (char *c = strtok_r(dir, "/", &save); c; c = strtok_r(NULL, "/", &save)) {
        int next = openat(cur, c, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int saved = errno;
        if (owned) close(cur);
        if (next < 0) { errno = saved; return -1; }
        cur = next; owned = 1;
    }
    *parent = cur;
    return 1;
}
#define WITH_PARENT(root, rel, call) do { \
        int parent_, owned_ = open_parent(root, rel, &parent_, &leaf); \
        if (owned_ < 0) return -1; \
        int result_ = (call); int saved_ = errno; \
        if (owned_) close(parent_); \
        errno = saved_; return result_; } while (0)

int mac_dirs_openat(int root, const char *rel, int flags, mode_t mode) {
    const char *leaf;
    if (!rel[0]) return openat(root, ".", flags | O_NOFOLLOW | O_CLOEXEC);
    WITH_PARENT(root, rel, openat(parent_, leaf, flags | O_NOFOLLOW | O_CLOEXEC, mode));
}
int mac_dirs_stat(int root, const char *rel, struct stat *st) {
    const char *leaf;
    if (!rel[0]) return fstat(root, st);
    WITH_PARENT(root, rel, fstatat(parent_, leaf, st, AT_SYMLINK_NOFOLLOW));
}
int mac_dirs_unlink(int root, const char *rel) {
    const char *leaf;
    WITH_PARENT(root, rel, unlinkat(parent_, leaf, 0));
}
int mac_dirs_rmdir(int root, const char *rel) {
    const char *leaf;
    WITH_PARENT(root, rel, unlinkat(parent_, leaf, AT_REMOVEDIR));
}
int mac_dirs_mkdir(int root, const char *rel) {
    const char *leaf;
    WITH_PARENT(root, rel, mkdirat(parent_, leaf, 0700));
}
int mac_dirs_rename(int root, const char *from, const char *to) {
    int p1, p2, o1, o2;
    const char *l1, *l2;
    if ((o1 = open_parent(root, from, &p1, &l1)) < 0) return -1;
    if ((o2 = open_parent(root, to, &p2, &l2)) < 0) { int e = errno; if (o1) close(p1); errno = e; return -1; }
    int result = renameat(p1, l1, p2, l2), saved = errno;
    if (o1) close(p1);
    if (o2) close(p2);
    errno = saved;
    return result;
}
DIR *mac_dirs_opendir(int root, const char *rel) {
    int fd = mac_dirs_openat(root, rel, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return NULL;
    DIR *dir = fdopendir(fd);
    if (!dir) close(fd);
    return dir;
}

/* Directory IDs. 0 and 1 are reserved (1 is the volume's virtual parent, 2 the root); other IDs come from an FNV-1a hash of the path, so the same path
 * has the same ID in every session, with linear probing past the rare collision. A hash is not invertible, so the table keeps the path for each ID
 * that has been handed out; an ID the guest remembers from an earlier session is unknown until its directory has been looked up again (dirNFErr). */
static uint32_t hash_path(const char *p) {
    uint32_t h = 2166136261u;
    for (; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}
uint32_t mac_dirs_id(DirTable *t, const char *rel) {
    if (!rel[0]) return DIRS_ROOT_ID;
    uint32_t id = 0x100 + hash_path(rel) % 0x7ffffe00u;
    for (;;) {
        DirSlot *hit = NULL;
        for (unsigned i = 0; i < DIRS_SLOTS; i++) if (t->slot[i].id == id) { hit = &t->slot[i]; break; }
        if (!hit) break;
        if (!strcmp(hit->path, rel)) return id;
        id++;
    }
    unsigned victim = DIRS_SLOTS;
    for (unsigned i = 0; i < DIRS_SLOTS; i++) if (!t->slot[i].id) { victim = i; break; }
    if (victim == DIRS_SLOTS) {   /* full: recycle the oldest slot that no working directory holds */
        for (unsigned n = 0; n < DIRS_SLOTS; n++) {
            unsigned i = (t->next + n) % DIRS_SLOTS; int held = 0;
            for (unsigned w = 0; w < DIRS_WD_MAX; w++) if (t->wd[w] == t->slot[i].id) held = 1;
            if (!held) { victim = i; break; }
        }
        if (victim == DIRS_SLOTS) victim = t->next;
        t->next = (victim + 1) % DIRS_SLOTS;
    }
    t->slot[victim].id = id;
    snprintf(t->slot[victim].path, DIRS_PATH_MAX, "%s", rel);
    return id;
}
int mac_dirs_path(const DirTable *t, uint32_t id, char *out) {
    if (id == DIRS_ROOT_ID) { out[0] = 0; return 0; }
    for (unsigned i = 0; i < DIRS_SLOTS; i++)
        if (id && t->slot[i].id == id) { memcpy(out, t->slot[i].path, DIRS_PATH_MAX); return 0; }
    return -120;
}
uint32_t mac_dirs_wd_dir(const DirTable *t, int vref) {
    long slot = -(long)vref - DIRS_WD_BASE;
    return slot >= 0 && slot < DIRS_WD_MAX ? t->wd[slot] : 0;
}
int mac_dirs_wd_open(DirTable *t, uint32_t dir_id) {
    int free_slot = -1;
    for (int i = 0; i < DIRS_WD_MAX; i++) {
        if (t->wd[i] == dir_id) return -(DIRS_WD_BASE + i);
        if (!t->wd[i] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return 0;
    t->wd[free_slot] = dir_id;
    return -(DIRS_WD_BASE + free_slot);
}
