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

uint32_t mac_partition_preferred(const MacResourceFork *fork) {
    MacResource r;
    if (!fork || mac_resource_find(fork, MAC_RESOURCE_TYPE('S', 'I', 'Z', 'E'), -1, &r) != 0 || r.size != 10) return 0;
    return (uint32_t)r.data[2] << 24 | (uint32_t)r.data[3] << 16 | (uint32_t)r.data[4] << 8 | r.data[5];
}

/* Where the application partition sits. Two layouts:
 *  - Low (2, 4, 8, 12 MB): A5 at that many MB, inside the 24-bit region. This is the original layout, kept byte for
 *    byte so every existing program, save and test behaves as before, in either addressing mode.
 *  - Extended (16 MB and up, 32-bit addressing only): the partition occupies [16 MB, 16 MB + size), the A5 world and
 *    stack at its top and the protected heap below them. It needs RAM above the 24-bit region, so it exists only when the
 *    machine has it. A request that does not fit is clamped to the largest partition that does.
 * A program that asks for more than the default 8 MB in its SIZE resource gets an extended partition of that size when the
 * default 8 MB partition was left selected (an explicit 2, 4 or 12 MB choice is honoured as chosen). */
void mac_partition_layout(int heap_mb, uint32_t size_preferred, uint32_t ram_bytes, int addr32,
                          uint32_t below, MacPartitionLayout *out) {
    const uint32_t region = 0x1000000, mb = 0x100000;
    int mbs = heap_mb;
    uint32_t room = addr32 && ram_bytes > region ? (ram_bytes - region) / mb : 0; /* whole MB above the low region */
    if (mbs != 2 && mbs != 4 && mbs != 8 && mbs != 12 && !(mbs >= 16 && mbs <= 1008)) mbs = 8;
    if (mbs == 8 && room >= 16 && size_preferred > 8u * mb && size_preferred <= room * (uint64_t)mb)
        mbs = (int)((size_preferred + mb - 1) / mb); /* the program asked for more than the default partition */
    *out = (MacPartitionLayout){0};
    if (mbs >= 16 && room >= 16) {
        uint32_t p = (uint32_t)mbs > room ? room : (uint32_t)mbs;
        out->extended = 1; out->heap_mb = (int)p; out->base = region;
        out->a5 = region + p * mb - mb;       /* the top MB holds the A5 world's above-A5 data and the jump table */
        out->heap_limit = 0x400000;           /* bump region for runtime records: same size as the 8 MB layout */
        if (out->a5 >= region + below + 0x400000) return;
        out->extended = 0;                    /* globals too big for it: fall back to the default layout */
        mbs = 8;
    }
    if (mbs > 12) mbs = 8;
    out->heap_mb = mbs;
    if (mbs == 2) { out->a5 = 0x200000; out->heap_limit = 0x1c0000; }
    else if (mbs == 4) { out->a5 = 0x400000; out->heap_limit = 0x300000; }
    else if (mbs == 12) { out->a5 = 0xc00000; out->heap_limit = 0xb00000; }
    else { out->a5 = 0x800000; out->heap_limit = 0x700000; }
    if (out->a5 < below + 0x140000) { out->a5 = 0x800000; out->heap_limit = 0x700000; }
    if (out->heap_limit > out->a5 - below - 0x10000) out->heap_limit = out->a5 - below - 0x10000;
}
