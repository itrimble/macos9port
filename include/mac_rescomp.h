#ifndef MAC_RESCOMP_H
#define MAC_RESCOMP_H

#include "mac_runtime.h"

/* Load-time work on a freshly read resource, run as guest code: see mac_rescomp.c.
 *
 * A caller that has just made a resource's handle (GetResource, LoadResource, LoadSeg) asks mac_rescomp_begin whether anything must happen before the
 * program may see the bytes. Return 0: nothing (the caller carries on inline; `done` is not called). Return 1: guest code was started, or the work finished
 * inline and `done` already ran; either way the caller must return "handled" at once and finish its trap in `done`. Return -1: the machine halted.
 * `done` gets 0 or a File/Resource Manager error. */
typedef int (*MacRescompDone)(MacRuntime *rt, void *ctx, int error);
int mac_rescomp_begin(MacRuntime *rt, uint32_t handle, MacRescompDone done, void *ctx);
void mac_rescomp_destroy(MacRuntime *rt);
/* Called when the program installs a load hook (OS trap $A0FC): runs the hook over every marked resource of the application's own fork now, as guest
 * calls, then replaces the fork the runtime reads (rt->fork) with a copy that holds the expanded bytes, so that every reader sees them. Returns 0 if there
 * is nothing to do, 1 if guest code was started (the trap finishes by itself), -1 if the machine halted. */
int mac_rescomp_prepass(MacRuntime *rt);
/* Why the last failed expansion failed, for a halt message; empty if none. */
const char *mac_rescomp_why(const MacRuntime *rt);
/* OS trap $A0FC reached through the chain from a program's patch: the Resource Manager's own load hook, which does nothing here. */
int mac_rescomp_service(MacRuntime *rt, uint16_t opcode);

/* A copy of a fork with the data of some resources replaced (mac_rescomp_fork.c). `*out` is malloc'd; 0 on success. */
typedef struct { uint32_t type; int16_t id; const uint8_t *bytes; size_t size; } MacResPatch;
int mac_rescomp_build_fork(const MacResourceFork *old, const MacResPatch *patches, size_t patch_count, uint8_t **out, size_t *out_size);

#endif
