#ifndef OS999_NATIVE_POLICY_H
#define OS999_NATIVE_POLICY_H

#include "launch_profile.h"
#include "native_settings.h"
#include <stddef.h>

/* Classify the application at `full` (inside `root`) from its real forks and decide its launch profile.
 * 1: not an executable (no code in either fork): the caller continues exactly as before.
 * 0: launch with `policy` (extra runtime arguments in policy->args); `logline` is the line for the private runtime log.
 * -1: refuse; `err` carries the user-facing reason. There is no fallback to another runtime for a refused program. */
int native_policy_prepare(const char *root, const char *full, const NativeSettings *settings, LaunchPolicy *policy,
                          char *logline, size_t logline_n, char *err, size_t err_n);

/* The classification step alone, shared with the Get Info panel. 0 classified, 1 not an executable, 2 CODE 0 header the
 * classifier does not accept. `canon` (PATH_MAX bytes) receives the canonical volume root; `slot` is the path hash. */
int native_policy_classify(const char *root, const char *full, AppClassification *c, uint64_t *slot, char *canon, size_t canon_n);

#endif
