#ifndef NATIVE_LAUNCH_INCLUDED
#define NATIVE_LAUNCH_INCLUDED

#include <stddef.h>

/* 1: not an extracted classic application, 0: launched, -1: launch failure.
 * A launch is not gameplay acceptance. Early failures include a private log;
 * failures occurring after the bounded startup check remain in that log. */
int native_open(const char *root, const char *full, char *err, size_t n);

#endif
