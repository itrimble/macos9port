#include "mac_runtime.h"

#include <stdio.h>
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
        CHECK(call(&rt, op, 0x1234, span + 1) == -1);
        CHECK(mac_cpu_halted(rt.cpu));
        CHECK(strstr(mac_cpu_error(rt.cpu), compact ? "compaction" : "reclamation") != NULL);
        CHECK(rt.heap_next == next && memcmp(&saved, &rt.handles[0], sizeof saved) == 0);
        CHECK(mac_cpu_get(rt.cpu, MAC_D0) == span + 1);
        CHECK(mac_cpu_ram(rt.cpu)[pointer] == 0xa7);
        mac_runtime_destroy(&rt);
    }
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536);
    CHECK(rt.cpu != NULL); if (!rt.cpu) return;
    rt.heap_next = 0xfffc; rt.heap_limit = UINT32_MAX;
    CHECK(call(&rt,0xa04c,0,4) == 1);
    CHECK(mac_cpu_get(rt.cpu,MAC_D0) == 4 && mac_cpu_get(rt.cpu,MAC_A0) == 0xfffc);
    CHECK(call(&rt,0xa04d,0,5) == -1); /* RAM bounds constrain the report. */
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
    CHECK(call(&rt, 0xa064, h, 0) == -1 && mac_cpu_halted(rt.cpu));
    CHECK(!rt.heap_high && rt.heap_next == saved_next);
    mac_runtime_destroy(&rt);
}

int main(void) {
    bounded_reclamation();
    high_heap();
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
