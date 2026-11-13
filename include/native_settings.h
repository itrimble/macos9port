#ifndef OS999_NATIVE_SETTINGS_H
#define OS999_NATIVE_SETTINGS_H
#include "control_panel_store.h"
#include <stdio.h>
#include <stdlib.h>

/* ram: saved machine RAM in MiB ("" = unset); addr: 0 unset, 24 or 32.
 * Both come from the store keys memory/guest_machine_mb and memory/addr_bits (no UI yet); unset means the built-in default (256 MB, 32-bit); values are clamped by guest_memory.h. */
typedef struct { char heap[16]; int guard, isolated; char ram[16]; int addr; } NativeSettings;
/* Parse before fork using the same typed, whole-document parser as the UI.
 * Invalid or missing documents retain conservative defaults. */
static inline NativeSettings native_settings_load(const char *root) {
    NativeSettings result = {"8", 1, 1, "", 0};
    os999_cp_store_t store;
    if (os999_cp_store_init(&store, root) != 0) return result;
    if (os999_cp_store_load(&store) == 0) {
        int mb = os999_cp_store_get_int(&store, "memory", "guest_ram_mb", 8);
        if (mb == 2 || mb == 4 || mb == 8 || mb == 12 || mb == 16 || mb == 32 || mb == 64 || mb == 128)
            snprintf(result.heap, sizeof result.heap, "%d", mb);
        int ram = os999_cp_store_get_int(&store, "memory", "guest_machine_mb", 0);
        if (ram >= 1 && ram <= 1000000) snprintf(result.ram, sizeof result.ram, "%d", ram);
        int bits = os999_cp_store_get_int(&store, "memory", "addr_bits", 0);
        result.addr = bits == 24 || bits == 32 ? bits : 0;
        result.guard = os999_cp_store_get_bool(&store, "memory", "heap_guard", 1);
        result.isolated = os999_cp_store_get_bool(&store, "network", "guest_isolated", 1);
    }
    os999_cp_store_destroy(&store);
    return result;
}
static inline void native_settings_apply(const NativeSettings *s) {
    if (!getenv("OS999_GUEST_HEAP_MB")) setenv("OS999_GUEST_HEAP_MB", s->heap, 1);
    if (s->ram[0] && !getenv("OS999_GUEST_RAM_MB")) setenv("OS999_GUEST_RAM_MB", s->ram, 1);
    if (s->addr && !getenv("OS999_GUEST_ADDRESSING")) setenv("OS999_GUEST_ADDRESSING", s->addr == 32 ? "32" : "24", 1);
    if (!getenv("OS999_GUEST_GUARD")) setenv("OS999_GUEST_GUARD", s->guard ? "1" : "0", 1);
    if (!getenv("OS999_MACTCP_ISOLATED")) setenv("OS999_MACTCP_ISOLATED", s->isolated ? "1" : "0", 1);
}
#endif
