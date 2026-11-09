#include "mac_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

static int call(MacRuntime *rt, uint16_t op, uint32_t a0, uint32_t d0) {
    mac_cpu_set(rt->cpu, MAC_A0, a0);
    mac_cpu_set(rt->cpu, MAC_D0, d0);
    return mac_memory_service(rt, op);
}

static void error_is(MacRuntime *rt, int16_t expected) {
    CHECK(mac_cpu_read(rt->cpu, 0x220, 2) == (uint16_t)expected);
    CHECK(mac_cpu_get(rt->cpu, MAC_D0) == (uint32_t)(int32_t)expected);
    CHECK((mac_cpu_get(rt->cpu, MAC_SR) & 15) == (expected < 0 ? 8 : expected == 0 ? 4 : 0));
}

static void bounded_reclamation(void) {
    for (unsigned compact = 0; compact < 2; compact++) {
        MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536);
        CHECK(rt.cpu != NULL); if (!rt.cpu) return;
        rt.heap_next = 0x1000; rt.heap_limit = 0x8000;
        CHECK(call(&rt, 0xa022, 0, 16) == 1);
        uint32_t handle = mac_cpu_get(rt.cpu, MAC_A0);
        uint32_t pointer = mac_cpu_read(rt.cpu, handle, 4);
        memset(mac_cpu_ram(rt.cpu) + pointer, 0xa7, 16);
        rt.handles[0].state = 0xc0; rt.handles[0].loaded_code = 1;
        rt.heap_high = 0x7000; rt.heap_high_end = 0x8000;
        MacHandle saved = rt.handles[0]; uint32_t next = rt.heap_next;
        uint32_t span = rt.heap_high - next;
        uint16_t op = compact ? 0xa04c : 0xa04d;
        mac_cpu_write(rt.cpu, 0x220, 2, 0xff94);
        CHECK(call(&rt, op, 0x1234, span) == 1);
        CHECK(mac_cpu_read(rt.cpu, 0x220, 2) == 0);
        CHECK(mac_cpu_get(rt.cpu, MAC_D0) == (compact ? span : 0));
        CHECK(mac_cpu_get(rt.cpu, MAC_A0) == (compact ? next : 0x1234));
        CHECK(rt.heap_next == next && rt.heap_high == 0x7000 && rt.heap_high_end == 0x8000);
        CHECK(memcmp(&saved, &rt.handles[0], sizeof saved) == 0);
        CHECK(mac_cpu_read(rt.cpu, handle, 4) == pointer);
        for (unsigned i=0;i<16;i++) CHECK(mac_cpu_read(rt.cpu,pointer+i,1) == 0xa7);
        CHECK(call(&rt, op | 0x400, 0x1234, span) == 0);
        /* Nothing can be compacted or purged, so an over-large request is advisory:
         * CompactMem reports the largest free block, PurgeMem reports memFullErr. */
        CHECK(call(&rt, op, 0x1234, span + 1) == 1);
        CHECK(!mac_cpu_halted(rt.cpu));
        CHECK(rt.heap_next == next && memcmp(&saved, &rt.handles[0], sizeof saved) == 0);
        CHECK(mac_cpu_get(rt.cpu, MAC_D0) == (compact ? span : (uint32_t)-108));
        CHECK(mac_cpu_ram(rt.cpu)[pointer] == 0xa7);
        mac_runtime_destroy(&rt);
    }
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0xfffc; rt.heap_limit = UINT32_MAX;
    CHECK(call(&rt,0xa04c,0,4) == 1);
    CHECK(mac_cpu_get(rt.cpu,MAC_D0) == 4 && mac_cpu_get(rt.cpu,MAC_A0) == 0xfffc);
    CHECK(call(&rt,0xa04d,0,5) == 1 && mac_cpu_get(rt.cpu,MAC_D0) == (uint32_t)-108); /* RAM bounds constrain the report: memFullErr. */
    mac_runtime_destroy(&rt);
}

static void high_heap(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000; rt.stack_base = 0x1f000;
    CHECK(call(&rt, 0xa022, 0, 16) == 1); error_is(&rt, 0);
    uint32_t h = mac_cpu_get(rt.cpu, MAC_A0), old = mac_cpu_read(rt.cpu, h, 4);
    for (unsigned i = 0; i < 16; i++) mac_cpu_write(rt.cpu, old + i, 1, 0xa0 + i);
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, 0);
    uint32_t high = mac_cpu_read(rt.cpu, h, 4);
    CHECK(high == 0x7ff0 && rt.heap_high == high && rt.heap_next == old);
    for (unsigned i = 0; i < 16; i++) CHECK(mac_cpu_read(rt.cpu, high + i, 1) == 0xa0 + i);
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, 0); /* Already at high boundary. */
    CHECK(mac_cpu_read(rt.cpu, h, 4) == high && rt.heap_next == old);
    CHECK(call(&rt, 0xa02d, 0xf000, 0) == 1); error_is(&rt, 0);
    CHECK(rt.heap_limit == 0xf000 && rt.heap_high == high && rt.heap_high_end == 0x8000);
    CHECK(call(&rt, 0xa02d, 0x7fff, 0) == 1); error_is(&rt, -108);
    CHECK(rt.heap_limit == 0xf000 && mac_cpu_read(rt.cpu, 0x130, 4) == 0xf000);
    CHECK(mac_cpu_read(rt.cpu, h, 4) == high);
    CHECK(call(&rt, 0xa01c, 0, 0) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == high - old);
    CHECK(call(&rt, 0xa040, 0, high - old + 1) == 1); error_is(&rt, -108);
    CHECK(mac_runtime_alloc(&rt, high - old) == old);
    CHECK(mac_runtime_alloc(&rt, 4) == 0 && rt.heap_next == high);
    CHECK(mac_cpu_read(rt.cpu, high, 4) == 0xa0a1a2a3);
    rt.handles[0].loaded_code = 1;
    CHECK(call(&rt, 0xa02a, h, 0) == 1); /* HUnlock does not release execution pin. */
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, -117);
    CHECK(call(&rt, 0xa024, h, 4) == 1); error_is(&rt, -117);
    CHECK(call(&rt, 0xa024, h, 32) == 1); error_is(&rt, -117);
    CHECK(call(&rt, 0xa023, h, 0) == 1); error_is(&rt, -117);
    CHECK(mac_cpu_read(rt.cpu, h, 4) == high && rt.handles[0].size == 16);
    mac_runtime_destroy(&rt);

    memset(&rt, 0, sizeof rt); rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000;
    CHECK(call(&rt, 0xa022, 0, 16) == 1); h = mac_cpu_get(rt.cpu, MAC_A0);
    old = mac_cpu_read(rt.cpu, h, 4);
    CHECK(call(&rt, 0xa029, h, 0) == 1);
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, -117);
    CHECK(!rt.heap_high && mac_cpu_read(rt.cpu, h, 4) == old);
    CHECK(call(&rt, 0xa02a, h, 0) == 1);
    rt.heap_limit = old + 8; /* Impossible limit cannot cause a partial move. */
    uint32_t saved_next = rt.heap_next;
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, -108);
    CHECK(!rt.heap_high && rt.heap_next == saved_next && mac_cpu_read(rt.cpu, h, 4) == old);
    rt.heap_limit = 0xffffffff; rt.heap_next = 65532;
    CHECK(mac_runtime_alloc(&rt, 4) == 65532);
    CHECK(mac_runtime_alloc(&rt, 1) == 0 && rt.heap_next == 65536);
    mac_runtime_destroy(&rt);

    memset(&rt, 0, sizeof rt); rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000; rt.stack_base = 0x1f000;
    CHECK(call(&rt, 0xa022, 0, 16) == 1); h = mac_cpu_get(rt.cpu, MAC_A0);
    old = mac_cpu_read(rt.cpu, h, 4); mac_cpu_write(rt.cpu, old, 4, 0x12345678);
    CHECK(call(&rt, 0xa064, h, 0) == 1); error_is(&rt, 0);
    high = rt.heap_high;
    CHECK(call(&rt, 0xa024, h, 32) == 1); error_is(&rt, 0);
    uint32_t resized = mac_cpu_read(rt.cpu, h, 4);
    CHECK(resized < high && mac_cpu_read(rt.cpu, resized, 4) == 0x12345678);
    CHECK(rt.heap_high == high && rt.heap_high_end == 0x8000);
    CHECK(call(&rt, 0xa023, h, 0) == 1); error_is(&rt, 0);
    CHECK(call(&rt, 0xa02d, 0x7fff, 0) == 1); error_is(&rt, -108);
    CHECK(rt.heap_limit == 0x8000 && rt.heap_high_end == 0x8000);
    CHECK(call(&rt, 0xa02d, 0xf000, 0) == 1); error_is(&rt, 0);
    CHECK(mac_runtime_alloc(&rt, high - rt.heap_next) != 0);
    CHECK(mac_runtime_alloc(&rt, 4) == 0); /* Retired high storage remains reserved. */
    mac_runtime_destroy(&rt);

    memset(&rt, 0, sizeof rt); rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000;
    CHECK(call(&rt, 0xa022, 0, 16) == 1); h = mac_cpu_get(rt.cpu, MAC_A0);
    CHECK(mac_runtime_alloc(&rt, 4) != 0); /* A later allocation is a barrier. */
    saved_next = rt.heap_next;
    /* MoveHHi is advisory: a block behind a later allocation stays where it is. */
    CHECK(call(&rt, 0xa064, h, 0) == 1 && !mac_cpu_halted(rt.cpu));
    error_is(&rt, 0);
    CHECK(!rt.heap_high && rt.heap_next == saved_next);
    mac_runtime_destroy(&rt);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

static void runtime_policy_and_guard_checks(void) {
    enum { DATA = 256, MAP = 384, ZERO = DATA + 4, SEG = DATA + 32, TOTAL = MAP + 62 };
    uint8_t b[TOTAL] = {0};
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 44); put32(b + 12, 62);
    put32(b + DATA, 24);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100);
    put32(b + ZERO + 8, 8); put32(b + ZERO + 12, 32);
    put16(b + ZERO + 16, 0); put16(b + ZERO + 18, 0x3f3c);
    put16(b + ZERO + 20, 1); put16(b + ZERO + 22, 0xa9f0);
    put32(b + DATA + 28, 12);
    put16(b + SEG, 0); put16(b + SEG + 2, 1);
    put16(b + SEG + 4, 0x7007); put16(b + SEG + 6, 0xa9f4);
    put16(b + SEG + 8, 0x4e71); put16(b + SEG + 10, 0x4e71);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 62);
    put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    put16(b + MAP + 40, 0xffff);
    put16(b + MAP + 50, 1); put16(b + MAP + 52, 0xffff);
    b[MAP + 57] = 28;

    MacResourceFork f;
    char err[240];
    CHECK(mac_resource_open(&f, b, TOTAL, err, sizeof err) == 0);

    unsetenv("OS999_GUEST_HEAP_MB");
    unsetenv("OS999_GUEST_GUARD");
    mac_runtime_set_memory_policy(2, 0);
    MacRuntime rt;
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(rt.a5 == 0x200000 && rt.guard_allocs == 0);
    CHECK(mac_cpu_read(rt.cpu, 0x108, 4) == 0x200000);
    mac_cpu_set(rt.cpu, MAC_D0, 0x72616d20);
    CHECK(mac_system_service(&rt, 0xa1ad) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 0 && mac_cpu_get(rt.cpu, MAC_A0) == 0x200000);
    mac_runtime_destroy(&rt);

    /* A large below-A5 area triggers policy fallback, but still reserves stack space. */
    put32(b + ZERO + 4, 0x100000);
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(rt.a5 == 0x800000 && rt.stack_base == 0x700000);
    CHECK(rt.heap_limit <= rt.stack_base - 0x10000);
    mac_runtime_destroy(&rt);
    put32(b + ZERO + 4, 0x100);

    setenv("OS999_GUEST_HEAP_MB", "12", 1);
    setenv("OS999_GUEST_GUARD", "1", 1);
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(rt.a5 == 0xc00000 && rt.guard_allocs == 1);
    CHECK(mac_cpu_read(rt.cpu, 0x108, 4) == 0xc00000);
    mac_cpu_set(rt.cpu, MAC_D0, 0x72616d20);
    CHECK(mac_system_service(&rt, 0xa1ad) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_A0) == 0xc00000);
    CHECK(call(&rt, 0xa01c, 0, 0) == 1 && mac_cpu_get(rt.cpu, MAC_D0) > 0x900000);

    CHECK(call(&rt, 0xa01e, 0, 13) == 1); error_is(&rt, 0);
    uint32_t p_ok = mac_cpu_get(rt.cpu, MAC_A0);
    memset(mac_cpu_ram(rt.cpu) + p_ok, 0x55, 13);
    CHECK(mac_cpu_ram(rt.cpu)[p_ok + 13] == 0xa5);
    CHECK(call(&rt, 0xa01f, p_ok, 0) == 1); error_is(&rt, 0);

    CHECK(call(&rt, 0xa022, 0, 10) == 1); error_is(&rt, 0);
    uint32_t h_ok = mac_cpu_get(rt.cpu, MAC_A0);
    CHECK(call(&rt, 0xa024, h_ok, 18) == 1); error_is(&rt, 0);
    CHECK(call(&rt, 0xa064, h_ok, 0) == 1); error_is(&rt, 0);
    uint32_t hp = mac_cpu_read(rt.cpu, h_ok, 4);
    memset(mac_cpu_ram(rt.cpu) + hp, 0x33, 18);
    CHECK(mac_cpu_ram(rt.cpu)[hp + 18] == 0xa5);
    CHECK(call(&rt, 0xa024, h_ok, 6) == 1); error_is(&rt, 0);
    CHECK(mac_cpu_ram(rt.cpu)[hp + 6] == 0xa5);
    CHECK(call(&rt, 0xa023, h_ok, 0) == 1); error_is(&rt, 0);

    CHECK(call(&rt, 0xa01e, 0, 13) == 1); error_is(&rt, 0);
    uint32_t p_bad = mac_cpu_get(rt.cpu, MAC_A0);
    mac_cpu_ram(rt.cpu)[p_bad + 13] = 0x42;
    CHECK(call(&rt, 0xa01f, p_bad, 0) == -1);
    CHECK(mac_cpu_halted(rt.cpu));
    CHECK(strstr(mac_cpu_error(rt.cpu), "redzone") != NULL);
    mac_runtime_destroy(&rt);

    unsetenv("OS999_GUEST_HEAP_MB");
    unsetenv("OS999_GUEST_GUARD");
    mac_runtime_set_memory_policy(0, 0);
}

static void corrupted_guard_cannot_be_restamped(void) {
    const uint16_t ops[] = {0xa024, 0xa024, 0xa064};
    const uint32_t sizes[] = {8, 32, 0};
    for (unsigned i = 0; i < 3; i++) {
        MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536);
        CHECK(rt.cpu != NULL); if (!rt.cpu) return;
        rt.heap_next = 0x1000; rt.heap_limit = 0x8000; rt.guard_allocs = 1;
        CHECK(call(&rt, 0xa022, 0, 16) == 1);
        uint32_t handle = mac_cpu_get(rt.cpu, MAC_A0);
        uint32_t pointer = mac_cpu_read(rt.cpu, handle, 4), next = rt.heap_next;
        uint8_t master[4]; memcpy(master, mac_cpu_ram(rt.cpu) + handle, sizeof master);
        mac_cpu_ram(rt.cpu)[pointer + 16] = 0x42;
        CHECK(call(&rt, ops[i], handle, sizes[i]) == -1);
        CHECK(mac_cpu_halted(rt.cpu));
        CHECK(strstr(mac_cpu_error(rt.cpu), "redzone") != NULL);
        CHECK(rt.heap_next == next && !rt.heap_high);
        CHECK(!memcmp(master, mac_cpu_ram(rt.cpu) + handle, sizeof master));
        CHECK(rt.handles[0].size == 16);
        CHECK(mac_cpu_ram(rt.cpu)[pointer + 16] == 0x42);
        mac_runtime_destroy(&rt);
    }
}

int main(void) {
    bounded_reclamation();
    high_heap();
    runtime_policy_and_guard_checks();
    corrupted_guard_cannot_be_restamped();
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    rt.heap_next = 0x1000;
    rt.heap_limit = 0x8000;
    mac_cpu_set(rt.cpu, MAC_SR, 0x10); /* X is not cleared by MemErr handling. */
    CHECK(call(&rt, 0xa11e, 0, 8) == 1); /* NewPtr, system-zone flag. */
    error_is(&rt, 0);
    CHECK(mac_cpu_get(rt.cpu, MAC_SR) & 0x10);
    uint32_t pointer = mac_cpu_get(rt.cpu, MAC_A0);
    CHECK(pointer == 0x1000 && rt.heap_next == 0x1008);
    CHECK(mac_cpu_read(rt.cpu, pointer, 4) == 0);
    CHECK(call(&rt, 0xa021, pointer, 0) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 8);
    CHECK(mac_cpu_read(rt.cpu, 0x220, 2) == 0);

    uint32_t old_heap = rt.heap_next;
    CHECK(call(&rt, 0xa01e, 0, UINT32_MAX) == 1);
    error_is(&rt, -108);
    CHECK(mac_cpu_get(rt.cpu, MAC_A0) == 0 && rt.heap_next == old_heap);
    CHECK(call(&rt, 0xa122, 0, UINT32_MAX) == 1);
    error_is(&rt, -108);
    CHECK(mac_cpu_get(rt.cpu, MAC_A0) == 0 && rt.heap_next == old_heap);
    CHECK(call(&rt, 0xa01e, 0, rt.heap_limit - old_heap + 1) == 1);
    error_is(&rt, -108);
    CHECK(rt.heap_next == old_heap);

    CHECK(call(&rt, 0xa322, 0, 8) == 1); /* NewHandleClear. */
    error_is(&rt, 0);
    uint32_t handle = mac_cpu_get(rt.cpu, MAC_A0);
    uint32_t data = mac_cpu_read(rt.cpu, handle, 4);
    CHECK(handle == old_heap && data == old_heap + 4);
    CHECK(mac_cpu_read(rt.cpu, data, 4) == 0);
    mac_cpu_write(rt.cpu, data, 4, 0x12345678);
    mac_cpu_write(rt.cpu, data + 4, 4, 0xabcdef01);
    CHECK(call(&rt, 0xa024, handle, 16) == 1);
    error_is(&rt, 0);
    uint32_t grown = mac_cpu_read(rt.cpu, handle, 4);
    CHECK(grown != data);
    CHECK(mac_cpu_read(rt.cpu, grown, 4) == 0x12345678);
    CHECK(mac_cpu_read(rt.cpu, grown + 4, 4) == 0xabcdef01);
    CHECK(mac_cpu_read(rt.cpu, grown + 8, 4) == 0);
    CHECK(call(&rt, 0xa025, handle, 0) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 16);

    CHECK(call(&rt, 0xa029, handle, 0) == 1); /* HLock. */
    error_is(&rt, 0);
    CHECK(call(&rt, 0xa069, handle, 0) == 1);
    CHECK(mac_cpu_get(rt.cpu, MAC_D0) == 0x80);
    old_heap = rt.heap_next;
    CHECK(call(&rt, 0xa024, handle, 20) == 1);
    error_is(&rt, -117);
    CHECK(rt.heap_next == old_heap && mac_cpu_read(rt.cpu, handle, 4) == grown);
    CHECK(call(&rt, 0xa024, handle, 4) == 1); /* Locked shrink is in-place. */
    error_is(&rt, 0);
    CHECK(mac_cpu_read(rt.cpu, handle, 4) == grown);
    CHECK(call(&rt, 0xa02a, handle, 0) == 1); /* HUnlock. */
    CHECK(call(&rt, 0xa024, handle, UINT32_MAX) == 1);
    error_is(&rt, -108);
    CHECK(rt.heap_next == old_heap && mac_cpu_read(rt.cpu, handle, 4) == grown);

    /* Resource metadata must survive resizing, and DisposePtr cannot release
     * the data pointer owned by a handle (resource or ordinary). */
    MacHandle *record = NULL;
    for (size_t i = 0; i < rt.handle_count; i++)
        if (rt.handles[i].handle == handle) record = &rt.handles[i];
    CHECK(record != NULL);
    if (record) {
        record->resource = 1; record->type = MAC_CODE_TYPE; record->id = 42;
        CHECK(call(&rt, 0xa024, handle, 12) == 1);
        CHECK(record->resource == 1 && record->type == MAC_CODE_TYPE && record->id == 42);
        CHECK(call(&rt, 0xa01f, record->pointer, 0) == 1);
        error_is(&rt, -109);
        CHECK(record->handle == handle);
    }
    CHECK(call(&rt, 0xa023, 0, 0) == 1); error_is(&rt, -109);
    CHECK(call(&rt, 0xa023, pointer, 0) == 1); error_is(&rt, -109);
    CHECK(call(&rt, 0xa023, handle, 0) == 1); error_is(&rt, 0);
    CHECK(mac_cpu_read(rt.cpu, handle, 4) == 0);
    CHECK(call(&rt, 0xa023, handle, 0) == 1); error_is(&rt, -109);
    CHECK(call(&rt, 0xa01f, pointer, 0) == 1); error_is(&rt, 0);
    CHECK(call(&rt, 0xa021, pointer, 0) == 1); error_is(&rt, -109);

    memcpy(mac_cpu_ram(rt.cpu) + 0x500, "abcdefgh", 8);
    mac_cpu_set(rt.cpu, MAC_A1, 0x502);
    CHECK(call(&rt, 0xa02e, 0x500, 6) == 1); error_is(&rt, 0);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + 0x500, "ababcdef", 8) == 0);
    mac_cpu_set(rt.cpu, MAC_A1, 0x500);
    CHECK(call(&rt, 0xa02e, 0x502, 6) == 1); error_is(&rt, 0);
    CHECK(memcmp(mac_cpu_ram(rt.cpu) + 0x500, "abcdefef", 8) == 0);
    CHECK(call(&rt, 0xa9f4, 0, 0) == 0); /* Not a Memory Manager call. */
    CHECK(call(&rt, 0xa0ff, 0, 0) == 0);
    /* A02D changes the limit; A064 is MoveHHi and must not shrink the heap. */
    uint32_t before_limit = rt.heap_limit;
    mac_cpu_write(rt.cpu, 0x130, 4, before_limit);
    CHECK(call(&rt, 0xa064, 0x2000, 0xabcdef01) == 1); error_is(&rt, -109);
    CHECK(rt.heap_limit == before_limit && mac_cpu_read(rt.cpu, 0x130, 4) == before_limit);
    CHECK(call(&rt, 0xa02d, 0x7000, 0) == 1); error_is(&rt, 0);
    CHECK(rt.heap_limit == 0x7000 && mac_cpu_read(rt.cpu, 0x130, 4) == 0x7000);
    CHECK(call(&rt, 0xa02d, rt.heap_next - 1, 0) == 1); error_is(&rt, -108);
    CHECK(rt.heap_limit == 0x7000 && mac_cpu_read(rt.cpu, 0x130, 4) == 0x7000);
    CHECK(call(&rt, 0xa02d, rt.heap_next, 0) == 1); error_is(&rt, 0);
    CHECK(rt.heap_limit == rt.heap_next);
    CHECK(call(&rt, 0xa01e, 0, 2) == 1); error_is(&rt, -108);
    /* The hard ceiling never grows past RAM, even with a malformed host bound. */
    rt.stack_base = 0xffffffff;
    CHECK(call(&rt, 0xa02d, 0xffffffff, 0) == 1); error_is(&rt, 0);
    CHECK(rt.heap_limit == mac_cpu_ram_size(rt.cpu));
    /* A realistic stack ceiling keeps a full 64 KiB below A5/stack storage. */
    rt.stack_base = 0x18000;
    CHECK(call(&rt, 0xa02d, 0x17000, 0) == 1); error_is(&rt, 0);
    CHECK(rt.heap_limit == 0x8000);
    mac_cpu_set(rt.cpu, MAC_A1, 65535);
    CHECK(call(&rt, 0xa02e, 0x500, 2) == -1);
    CHECK(mac_cpu_halted(rt.cpu));
    CHECK(strstr(mac_cpu_error(rt.cpu), "BlockMove range") != NULL);
    mac_runtime_destroy(&rt);
    printf("Memory checks: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
