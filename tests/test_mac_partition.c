/* The heap end derived from the SIZE resource (-1): a preferred partition above the default grows the heap up to the ceiling; everything else keeps the default. */
#include "mac_runtime.h"
#include "mac_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);fails++;}}while(0)
static int fails;
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }
/* One resource 'SIZE' id -1 (or type 'XXXX' when `type` is 0) of `n` bytes in a minimal fork. */
static size_t fork_with(uint8_t *out, uint32_t type, const uint8_t *data, size_t n) {
    size_t at = 256, map = at + 4 + n, tl = map + 28, refs = tl + 10, len = (refs - map) + 12 + 2;
    memset(out, 0, map + len + 16);
    p32(out + at, (uint32_t)n); memcpy(out + at + 4, data, n);
    p32(out, 256); p32(out + 4, (uint32_t)map); p32(out + 8, (uint32_t)(4 + n)); p32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); p16(out + map + 24, 28); p16(out + map + 26, (unsigned)(len - 2)); p16(out + tl, 0);
    p32(out + tl + 2, type); p16(out + tl + 6, 0); p16(out + tl + 8, (unsigned)(refs - tl));
    p16(out + refs, 0xffff); p16(out + refs + 2, 0xffff); p32(out + refs + 4, 0);
    return map + len;
}
static uint32_t hi_for(const uint8_t *size_bytes, size_t n, uint32_t type, uint32_t floor_hi, uint32_t ceiling) {
    static uint8_t image[1024]; MacResourceFork f; char err[200];
    size_t total = fork_with(image, type, size_bytes, n);
    if (mac_resource_open(&f, image, total, err, sizeof err)) { fprintf(stderr, "open: %s\n", err); fails++; return 0; }
    return mac_partition_heap_hi(&f, 0x400000, floor_hi, ceiling);
}
int main(void) {
    const uint32_t SIZE_T = MAC_RESOURCE_TYPE('S', 'I', 'Z', 'E'), FLOOR = 0x700000, CEIL = 0x7d0000;
    uint8_t s[10] = {0x58, 0xc0};
    p32(s + 2, 4096000); p32(s + 6, 3072000);                                   /* preferred 4,096,000 (Glider PRO) */
    CHECK(hi_for(s, 10, SIZE_T, FLOOR, 0x7f0000) == 0x400000 + 4096000);
    CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == CEIL);                          /* clamped to the ceiling */
    CHECK(hi_for(s, 10, SIZE_T, FLOOR, 0x600000) == FLOOR);                     /* a ceiling below the default never shrinks it */
    p32(s + 2, 4096003); CHECK(hi_for(s, 10, SIZE_T, FLOOR, 0x7f0000) == 0x400000 + 4096000);   /* 8-byte aligned downwards */
    p32(s + 2, 1228800); CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == FLOOR);   /* a preferred size that fits the default: untouched (Monkey Island) */
    p32(s + 2, 3145728); CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == FLOOR);   /* exactly the default */
    p32(s + 2, 3145728 + 8); CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == FLOOR + 8);
    p32(s + 2, 0xffffffffu); CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == FLOOR); /* nonsense */
    p32(s + 2, 64u * 1024 * 1024 + 1); CHECK(hi_for(s, 10, SIZE_T, FLOOR, CEIL) == FLOOR);
    p32(s + 2, 4096000);
    CHECK(hi_for(s, 8, SIZE_T, FLOOR, CEIL) == FLOOR && hi_for(s, 10, MAC_RESOURCE_TYPE('X', 'X', 'X', 'X'), FLOOR, CEIL) == FLOOR);   /* a short SIZE, no SIZE */
    { uint8_t longer[12] = {0x58, 0xc0}; p32(longer + 2, 4096000); CHECK(hi_for(longer, 12, SIZE_T, FLOOR, CEIL) == FLOOR); }   /* a SIZE longer than 10 bytes is not trusted either */
    CHECK(mac_partition_heap_hi(NULL, 0x400000, FLOOR, CEIL) == FLOOR);
    if (!fails) puts("Memory partition checks passed");
    return fails ? 1 : 0;
}
