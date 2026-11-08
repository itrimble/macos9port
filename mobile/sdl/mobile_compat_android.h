/* Android only, force-included by CMakeLists.txt. Bionic has no POSIX shared
 * memory, which the desktop's runtime bridge uses; mobile_compat_android.c
 * backs shm_open/shm_unlink with files in the app's private storage. */
#ifndef OS999_MOBILE_COMPAT_ANDROID_H
#define OS999_MOBILE_COMPAT_ANDROID_H
#if defined(__ANDROID__)
#include <sys/types.h>
int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);
#endif
#endif
