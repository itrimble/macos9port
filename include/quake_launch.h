#ifndef OS999_QUAKE_LAUNCH_H
#define OS999_QUAKE_LAUNCH_H
#include <stddef.h>

/* Quake III Arena runs as its own host process (GPL-2, built from the separate
 * quake/ repository). OS999 only finds and starts it; no Quake code links here. */
typedef struct {
    char binary[1024];   /* the quake3 executable */
    char basepath[1024]; /* directory holding baseq3/ and demota/ game modules */
    char datapath[1024]; /* user directory searched for pak0.pk3 (fs_cdpath) */
    char game[16];       /* "baseq3" (full game) or "demota" (demo) */
} QuakeLaunch;

/* Resolve paths. exe_dir is the directory of the running OS999 binary; env
 * OS999_QUAKE3 (binary) and OS999_QUAKE3_DATA (data) override. Returns 0 when
 * ready, -1 when the engine is missing, -2 when no pak0.pk3 game data exists. */
int quake_launch_resolve(QuakeLaunch *q, const char *exe_dir, const char *home);
/* Build argv for the resolved launch; returns the argument count. */
int quake_launch_argv(const QuakeLaunch *q, char *argv[], int max);
#endif
