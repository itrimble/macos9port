#ifndef MAC_DIRS_H
#define MAC_DIRS_H
/* Hierarchical directories for the guest File Manager (mac_dirs.c, mac_dirs_name.c).
 * Every guest path becomes a host-relative path of '/'-separated components ("Textures/Wood/oak"). Each operation
 * walks the components one at a time with O_NOFOLLOW|O_DIRECTORY from a pinned root descriptor, so a symbolic link
 * in the middle of a path is refused and nothing resolves outside the root it started from. Directory IDs are a
 * per-volume mapping from relative paths to numbers: the root is 2, every other directory gets a number derived
 * from its path (stable across launches), registered in a small table when first handed to the guest. */
#include <dirent.h>
#include <stdint.h>
#include <sys/stat.h>

enum { DIRS_PATH_MAX = 256, DIRS_DEPTH_MAX = 8, DIRS_SLOTS = 512, DIRS_WD_MAX = 32, DIRS_ROOT_ID = 2, DIRS_WD_BASE = 100 };

typedef struct { uint32_t id; char path[DIRS_PATH_MAX]; } DirSlot;
typedef struct {
    DirSlot slot[DIRS_SLOTS];
    unsigned next;                   /* round-robin victim when the table is full */
    char current[DIRS_PATH_MAX];     /* the default directory (PBHSetVol), "" = the root */
    uint32_t wd[DIRS_WD_MAX];        /* open working directories: slot -> directory ID, 0 = free; the reference is -(100 + slot) */
} DirTable;

/* Secure primitives on a pinned root (errno as the underlying call; EINVAL for a malformed path). rel "" is the root. */
int mac_dirs_openat(int root, const char *rel, int flags, mode_t mode);
int mac_dirs_stat(int root, const char *rel, struct stat *st);          /* lstat semantics */
int mac_dirs_unlink(int root, const char *rel);
int mac_dirs_rmdir(int root, const char *rel);
int mac_dirs_rename(int root, const char *from, const char *to);
int mac_dirs_mkdir(int root, const char *rel);
DIR *mac_dirs_opendir(int root, const char *rel);
int mac_dirs_path_ok(const char *rel);                                  /* well-formed relative path: no empty, "." or ".." component, depth bounded */
void mac_dirs_parent(const char *rel, char *parent, const char **leaf); /* split "a/b/c" into "a/b" and "c" */

/* The directory-ID table. */
uint32_t mac_dirs_id(DirTable *t, const char *rel);                     /* registers the path; "" is 2 */
int mac_dirs_path(const DirTable *t, uint32_t id, char *out);           /* 0, or -120 (dirNFErr) for an ID never handed out */
uint32_t mac_dirs_wd_dir(const DirTable *t, int vref);                  /* the directory ID of an open working-directory reference, or 0 */
int mac_dirs_wd_open(DirTable *t, uint32_t dir_id);                     /* a reference for the directory (reused if already open), or 0 when the table is full */
#endif
