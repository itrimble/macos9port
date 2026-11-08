#ifndef OS999_QUAKE_DATA_H
#define OS999_QUAKE_DATA_H
#include <sys/types.h>

/* The Arena 1.11 demo's pak0.pk3 is separately supplied guest data. It is accepted only when its
 * size and SHA-256 match the original, then installed where the launcher looks for it. */

/* 1 when the regular file at path has exactly this size and SHA-256 (lower-case hex). */
int quake_data_check(const char *path, off_t size, const char *sha256_hex);
/* 1 when path is the original Arena 1.11 demo pak0.pk3. */
int quake_data_demo_ok(const char *path);
/* Search dirs (each as dir/pak0.pk3 and dir/demoq3/pak0.pk3) for a file matching size and hash, and
 * install a verified copy as <home>/<support>/Quake3/demoq3/pak0.pk3. Returns 0 when installed,
 * 1 when a verified copy was already installed, -1 when nothing matched or the copy failed. */
int quake_data_import(const char *home, const char *const dirs[], int ndirs, off_t size, const char *sha256_hex);
/* The same, for the real demo, searching the volume and the user's Downloads and Desktop. */
int quake_data_import_demo(const char *home, const char *volume);
#endif
