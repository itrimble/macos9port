#ifndef OS999_NATIVE_SETTINGS_H
#define OS999_NATIVE_SETTINGS_H
#include "control_panel_store.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct { char heap[16]; int guard, isolated; } NativeSettings;
/* Parse before fork using the same typed, whole-document parser as the UI.
 * Invalid or missing documents retain conservative defaults. */
static inline NativeSettings native_settings_load(const char *root) {
    NativeSettings result = {"8", 1, 1};
    os999_cp_store_t store;
    if (os999_cp_store_init(&store, root) != 0) return result;
    if (os999_cp_store_load(&store) == 0) {
        int mb = os999_cp_store_get_int(&store, "memory", "guest_ram_mb", 8);
        if (mb == 2 || mb == 4 || mb == 8 || mb == 12)
            snprintf(result.heap, sizeof result.heap, "%d", mb);
        result.guard = os999_cp_store_get_bool(&store, "memory", "heap_guard", 1);
        result.isolated = os999_cp_store_get_bool(&store, "network", "guest_isolated", 1);
    }
    os999_cp_store_destroy(&store);
    return result;
}
static inline void native_settings_apply(const NativeSettings *s) {
    if (!getenv("OS999_GUEST_HEAP_MB")) setenv("OS999_GUEST_HEAP_MB", s->heap, 1);
    if (!getenv("OS999_GUEST_GUARD")) setenv("OS999_GUEST_GUARD", s->guard ? "1" : "0", 1);
    if (!getenv("OS999_MACTCP_ISOLATED")) setenv("OS999_MACTCP_ISOLATED", s->isolated ? "1" : "0", 1);
}
#endif
