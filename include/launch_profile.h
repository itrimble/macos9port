#ifndef OS999_LAUNCH_PROFILE_H
#define OS999_LAUNCH_PROFILE_H

#include "app_classify.h"

/* Launch policy: turns a classification (and an optional saved per-app override) into the runtime arguments for ONE
 * launch. It is pure decision code: it never touches CPU or addressing state, and a launch is always its own
 * ospi-runtime process, so profiles coexist across apps without any shared state being flipped.
 *
 * Defaults are preserved: a classic 68K application with nothing special about it gets no extra arguments, so it
 * starts exactly as it did before this layer existed. Extra arguments appear only when the profile requires
 * something different from the session machine (Memory panel / saved keys). A failed or crashed launch is never
 * retried under another profile. */

#define LAUNCH_MAX_ARGS 4
#define LAUNCH_ARG_LEN 24

/* What the session (Memory panel store) asks for. 0 = unset: the built-in 16 MB, 24-bit machine. */
typedef struct { int addr; unsigned ram_mb; } LaunchSession;

/* A saved choice for one executable. Keyed on the classifier identity, so renaming or moving the application keeps
 * it and changing the executable drops it. `slot` is a hash of the application's path inside the volume; it exists
 * only to notice that the executable at that place changed (and say so), never to select an override. */
typedef struct {
    uint64_t identity, slot;
    AppProfile profile;     /* A, B or C */
    int addr;               /* 0 = profile default, 24 or 32 */
    unsigned ram_mb;        /* 0 = profile default; only meaningful with addr 32 */
} LaunchOverride;

typedef struct {
    AppProfile profile;     /* profile used */
    const char *source;     /* "classifier" or "override" */
    int refuse;             /* do not launch */
    int addr;               /* 0 = leave to the session default, else 24 or 32 */
    unsigned ram_mb;        /* 0 = leave to the session default */
    int nargs;
    char args[LAUNCH_MAX_ARGS][LAUNCH_ARG_LEN];   /* "--addressing", "24", "--ram-mb", "256" */
    char reason[APP_TEXT * 2];
    char message[APP_TEXT * 3];   /* shown to the user when refusing */
} LaunchPolicy;

/* `override_` may be NULL. An override whose profile cannot apply to this binary (for example C for a 68K-only
 * program) is ignored, and the reason says so. */
void launch_policy_decide(const AppClassification *c, const LaunchOverride *override_, const LaunchSession *session,
                          LaunchPolicy *out);

/* One log line, no newline. */
void launch_policy_describe(const LaunchPolicy *p, const AppClassification *c, char *out, size_t n);

/* Hash of a path (stable across renames of the volume root: pass the path relative to it). */
uint64_t launch_slot_key(const char *relative_path);

/* Override store: <root>/.os999/launch-profiles (an older <root>/.os999-launch-profiles is migrated on first read), one line per application, written atomically, never followed
 * through symlinks. find returns 1 found, 0 none, 2 stale (an entry for this slot had another identity: it was removed
 * and *stale_identity names it), -1 error. */
int launch_override_find(const char *root, uint64_t identity, uint64_t slot, LaunchOverride *out, uint64_t *stale_identity);
int launch_override_set(const char *root, const LaunchOverride *o);
int launch_override_clear(const char *root, uint64_t identity);

#endif
