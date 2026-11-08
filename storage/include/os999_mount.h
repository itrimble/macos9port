/* Disposable userspace image mounts. No host mount syscall or installer runs.
 * Source images are never modified. The caller owns a private scratch parent;
 * every mount gets a new directory, removed by st_mount_close. */
#ifndef OS999_MOUNT_H
#define OS999_MOUNT_H
#include "os999_unpack.h"
#include "os999_volume.h"
typedef struct StMount StMount;
/* HFS/HFS+ in raw/.iso, DC42/.img, NDIF and UDIF/.dmg containers.
 * ISO9660 data discs are supported without inventing missing Mac metadata.
 * opt.read_only defaults to 1; writable mounts change only the snapshot. */
int st_mount_open(const char *image, const char *scratch, const StVolumeOptions *opt,
                  const StLimits *lim, StMount **out, StErr *err);
StVolume *st_mount_volume(StMount *m); /* borrowed, do not close separately */
const char *st_mount_path(const StMount *m);
const StUnpackInfo *st_mount_info(const StMount *m);
int st_mount_close(StMount *m, StErr *err);
#endif
