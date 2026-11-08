/* OS999 writable guest volumes (checklist L04).
 *
 * A guest volume is a host directory. Every operation is resolved
 * relative to the volume root with openat and O_NOFOLLOW, one path
 * component at a time, so ".." and symlinks can never leave the root.
 * Data forks are plain files; the resource fork, Finder info and dates
 * live in an AppleDouble sidecar "<name>.rsrc", which the OS999
 * runtime already reads. Saves are atomic: temporaries, a journal, then
 * renames. Opening a volume replays or rolls back an interrupted save. */
#ifndef OS999_VOLUME_H
#define OS999_VOLUME_H

#include "os999_storage.h"

typedef struct StVolume StVolume;

typedef struct {
    uint64_t quota_bytes; /* 0 = unlimited; counts data files and sidecars */
    uint64_t quota_files; /* 0 = unlimited */
    int read_only;
} StVolumeOptions;

typedef struct {
    uint64_t bytes, files, dirs;
    int recovered;        /* operations completed or rolled back at open */
} StVolumeUsage;

typedef struct {
    char name[256];
    int is_dir;
    uint64_t data_size, rsrc_size;
    uint8_t finfo[16];
} StVolumeEntry;

int st_vol_open(const char *root, const StVolumeOptions *opt, StVolume **out, StErr *err);
void st_vol_close(StVolume *v);
void st_vol_usage(const StVolume *v, StVolumeUsage *u);

/* rel uses '/' between guest names; "" is the root. */
int st_vol_mkdirs(StVolume *v, const char *rel, StErr *err);
/* Atomically create or replace a file with both forks and metadata. */
int st_vol_write(StVolume *v, const char *rel, const StMacFile *f, StErr *err);
int st_vol_read(StVolume *v, const char *rel, StMacFile *out, const StLimits *lim, StErr *err);
int st_vol_remove(StVolume *v, const char *rel, StErr *err); /* file, or empty folder */
int st_vol_rename(StVolume *v, const char *from, const char *to, StErr *err);
/* List a folder; sidecars and OS999 bookkeeping files are hidden. */
int st_vol_list(StVolume *v, const char *rel, StVolumeEntry **out, size_t *count, StErr *err);

/* Copy an archive's entries under dest_rel. Fails before writing when
 * the quota would be exceeded. On failure everything created by this
 * call is removed again. */
int st_vol_install(StVolume *v, const char *dest_rel, const StArchive *a, StErr *err);

/* Testing hook: stop a save after N durable steps to simulate a crash. */
void st_vol_crash_after(StVolume *v, int steps);

#endif
