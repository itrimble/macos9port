/* What differs between the phone builds (iOS, Android) and the desk simulator. */
#ifndef OS999_MOBILE_PLATFORM_H
#define OS999_MOBILE_PLATFORM_H

#include <stddef.h>

#if defined(__ANDROID__)
#define MOBILE_PHONE 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define MOBILE_PHONE 1
#endif
#endif
#ifndef MOBILE_PHONE
#define MOBILE_PHONE 0
#endif

/* Make fonts/, assets/ and build/wallpapers resolve from the current
 * directory: the app bundle on iOS, files unpacked from the APK on Android,
 * --resources DIR (or the repository root) on the desk. */
int mobile_platform_resources(int argc, char **argv);
/* The desktop volume: the app's private storage on a phone, --volume on the desk. */
int mobile_platform_volume(int argc, char **argv, char *out, size_t n);
/* Hook up haptics and the first safe-area / density report. */
void mobile_platform_start(void);
/* Once a frame: pick up safe-area changes (iOS rotates without telling SDL). */
void mobile_platform_poll(void);

#if defined(__APPLE__) && MOBILE_PHONE
void mobile_ios_start(void);
void mobile_ios_poll(void);
#endif

#endif
