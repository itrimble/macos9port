#ifndef ST_HFS_INT_H
#define ST_HFS_INT_H

#include "st_int.h"

typedef struct { uint32_t start, count; } StExt;

/* One extents-overflow record, HFS (3 extents) or HFS+ (8). */
typedef struct {
    uint32_t file_id;
    uint8_t fork;        /* 0 data, 0xFF resource */
    uint32_t start_block; /* logical allocation block this record begins at */
    StExt ext[8];
    int next;
} StOverflow;

typedef struct {
    const uint8_t *vol;  /* volume start (allocation block 0 base) */
    size_t n;            /* bytes available from vol */
    uint64_t base;       /* byte offset of allocation block 0 from vol */
    uint32_t blksz;
    StOverflow *ovf;
    size_t novf;
} StHfsVol;

/* Read a fork: `first` holds the in-record extents. */
int st_hfs_read_fork(const StHfsVol *v, uint32_t file_id, uint8_t fork, const StExt *first, int nfirst,
                     uint64_t size, StBytes *out, const StLimits *lim, StErr *err);

typedef int (*StLeafFn)(void *ctx, const uint8_t *rec, size_t len, StErr *err);
/* Walk every leaf record of a B-tree held in memory (whole tree file). */
int st_btree_leaves(const uint8_t *tree, size_t n, StLeafFn fn, void *ctx, StErr *err);

/* Directory bookkeeping shared by HFS and HFS+ readers. */
typedef struct { uint32_t id, parent; char name[256]; StMacFile meta; } StHfsDir;
typedef struct {
    StHfsDir *v;
    size_t n, cap;
} StHfsDirs;
int st_hfs_dir_add(StHfsDirs *d, uint32_t id, uint32_t parent, const char *name, StErr *err);
/* Build "a/b/c" for the folder id (root folder id 2 yields ""). */
int st_hfs_dir_path(const StHfsDirs *d, uint32_t id, char *out, size_t cap);
int st_hfs_join(char *out, size_t cap, const char *dir, const char *name);

#endif
