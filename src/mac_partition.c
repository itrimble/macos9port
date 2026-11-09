#include "mac_runtime.h"
#include "mac_resource.h"

/* The application's memory partition, from its SIZE resource (id -1): flags word, then the preferred and the minimum partition size as longs (10 bytes).
 * The guest heap normally spans [lo, floor_hi) (3 MB). A program whose preferred size is larger gets a heap that large (the stack lives in the partition too, so
 * `ceiling` is where the heap must stop to leave it room), because it sizes its own buffers against FreeMem and refuses features when it finds less than it asked
 * for. Programs whose preferred size fits the default are untouched, so their behaviour does not change. Without a usable SIZE resource: the default. */
uint32_t mac_partition_heap_hi(const MacResourceFork *fork, uint32_t lo, uint32_t floor_hi, uint32_t ceiling) {
    MacResource r;
    if (!fork || mac_resource_find(fork, MAC_RESOURCE_TYPE('S', 'I', 'Z', 'E'), -1, &r) != 0 || r.size != 10) return floor_hi;
    uint32_t preferred = (uint32_t)r.data[2] << 24 | (uint32_t)r.data[3] << 16 | (uint32_t)r.data[4] << 8 | r.data[5];
    if (preferred > 64u * 1024 * 1024) return floor_hi;                 /* nonsense: ignore */
    uint64_t hi = (uint64_t)lo + preferred;
    if (hi > ceiling) hi = ceiling;
    hi &= ~(uint64_t)7;
    return hi > floor_hi ? (uint32_t)hi : floor_hi;
}
