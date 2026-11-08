#include "mac_runtime.h"

#include <stdlib.h>
#include <string.h>

/* Resource-name overrides written by SetResInfo. The fork bytes are immutable, so a
 * renamed resource keeps its new name here, keyed by the resource's master pointer.
 * Kept in host memory: guest code cannot reach or corrupt it. Masters are never
 * reused by the resource loader, so a stale entry cannot attach to another resource. */
typedef struct { uint32_t handle; uint8_t name[256]; } MacResName; /* name[0] = Pascal length */
struct MacResNames { MacResName *items; size_t count, cap; };

int mac_resname_set(MacRuntime *rt, uint32_t handle, const uint8_t *pascal) {
    struct MacResNames *t = rt->res_names;
    if (!t && !(t = rt->res_names = calloc(1, sizeof *t))) return -1;
    size_t i;
    for (i = 0; i < t->count; i++) if (t->items[i].handle == handle) break;
    if (i == t->count) {
        if (t->count == t->cap) {
            size_t cap = t->cap ? t->cap * 2 : 16;
            MacResName *items = realloc(t->items, cap * sizeof *items);
            if (!items) return -1;
            t->items = items; t->cap = cap;
        }
        t->count++;
    }
    t->items[i].handle = handle;
    memcpy(t->items[i].name, pascal, (size_t)pascal[0] + 1);
    return 0;
}

const uint8_t *mac_resname_get(const MacRuntime *rt, uint32_t handle) {
    const struct MacResNames *t = rt->res_names;
    if (t) for (size_t i = 0; i < t->count; i++) if (t->items[i].handle == handle) return t->items[i].name;
    return NULL;
}

void mac_resname_destroy(MacRuntime *rt) {
    if (!rt->res_names) return;
    free(rt->res_names->items);
    free(rt->res_names);
    rt->res_names = NULL;
}
