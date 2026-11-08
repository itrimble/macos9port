#ifndef ST_VOLUME_INT_H
#define ST_VOLUME_INT_H

#include "os999_volume.h"
#include "st_int.h"

#define ST_VOL_JOURNAL ".os999-journal"
#define ST_VOL_LOCK ".os999-lock"
#define ST_VOL_TMP ".os999-tmp-"
#define ST_VOL_MAX_DEPTH 64

struct StVolume {
    int root;            /* O_DIRECTORY fd of the volume root */
    int lock;            /* fd holding an exclusive flock */
    StVolumeOptions opt;
    StVolumeUsage use;
    unsigned counter;
    int crash_steps;     /* >0: fail after this many durable steps */
};

/* Validate a guest name and map it to its host form. "X.rsrc" names
 * would collide with sidecars, so they gain a trailing '_'. */
int st_vol_host_name(const char *guest, char *host, size_t cap);
void st_vol_guest_name(const char *host, char *guest, size_t cap);
/* Open the parent folder of rel (creating folders when asked) and
 * return its fd plus the host leaf name. rel "" is refused. */
int st_vol_parent(StVolume *v, const char *rel, int create, int *dirfd, char *leaf, size_t cap, StErr *err);
/* Durable-step accounting for crash simulation. */
int st_vol_step(StVolume *v, StErr *err);
int st_vol_fsync_dir(int fd);
/* Journal: write the plan, then apply it. Lines are "R tmp\tfinal" or
 * "D path", paths relative to the root. */
int st_vol_journal_commit(StVolume *v, const char *plan, StErr *err);
int st_vol_journal_apply(StVolume *v, const char *plan, int committed, StErr *err);
int st_vol_recover(StVolume *v, StErr *err);
int st_vol_scan(StVolume *v, StErr *err);
/* Undo an install that never finished (manifest left behind). */
int st_vol_install_recover(StVolume *v, StErr *err);
#define ST_VOL_INSTALL ".os999-install"
/* Full host-relative path of a leaf under a folder given by guest rel. */
int st_vol_host_path(const char *rel, char *out, size_t cap);

#endif
