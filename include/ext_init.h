#ifndef OS999_EXT_INIT_H
#define OS999_EXT_INIT_H

#include "ext_registry.h"
#include "mac_runtime.h"

/* Run the enabled, ordered extensions of `list` (ext_order must have run) in a
 * throwaway host runtime and record LOADED/FAILED per entry. A halting
 * extension is marked FAILED and the host is rebuilt by replaying the ones
 * that already loaded. Returns the number loaded. */
int ext_init_load_all(ExtList *list);

/* Apply the entries already marked LOADED to a live runtime (before its
 * application starts). Returns the number applied, -1 if one failed. */
int ext_init_apply(ExtList *list, MacRuntime *rt, char *err, size_t n);

#endif
