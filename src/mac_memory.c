#include "mac_runtime.h"

#include <string.h>

/* Non-compacting development heap: guest handles are stable master pointers.
 * Disposal invalidates records, but allocation space is not yet reclaimed.
 * Report exhausted space instead of promising successful allocation. */
static MacHandle *lookup(MacRuntime *rt, uint32_t address, int handle) {
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *h = &rt->handles[i];
        if (handle ? h->handle == address && address != 0 :
                     h->handle == 0 && h->pointer == address && address != 0) return h;
    }
    return NULL;
}

static void result(MacCpu *cpu, int16_t err) {
    mac_cpu_write(cpu, 0x220, 2, (uint16_t)err);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)err);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (err < 0 ? 8u : err == 0 ? 4u : 0u));
}

static MacHandle *slot(MacRuntime *rt) {
    for (size_t i = 0; i < rt->handle_count; i++)
        if (!rt->handles[i].handle && !rt->handles[i].pointer) return &rt->handles[i];
    if (rt->handle_count == MAC_RUNTIME_HANDLES) return NULL;
    return &rt->handles[rt->handle_count++];
}

static uint32_t ceiling(MacRuntime *rt) {
    uint32_t limit = rt->heap_limit;
    if (rt->heap_high && rt->heap_high < limit) limit = rt->heap_high;
    if (limit > mac_cpu_ram_size(rt->cpu)) limit = (uint32_t)mac_cpu_ram_size(rt->cpu);
    return limit;
}

static uint32_t available(MacRuntime *rt) {
    uint32_t limit = ceiling(rt);
    return rt->heap_next <= limit ? limit - rt->heap_next : 0;
}

static void stamp_guard(MacRuntime *rt, uint32_t pointer, uint32_t size) {
    if (!rt->guard_allocs || !pointer) return;
    uint32_t aligned = (size + 3) & ~UINT32_C(3);
    if (!aligned) aligned = 4;
    uint32_t end = pointer + aligned + 16u;
    if (end <= mac_cpu_ram_size(rt->cpu) && pointer + size <= end)
        memset(mac_cpu_ram(rt->cpu) + pointer + size, 0xA5, end - (pointer + size));
}

static int verify_guard(MacRuntime *rt, const MacHandle *h) {
    if (!rt->guard_allocs || !h || h->resource || !h->pointer) return 0;
    uint32_t aligned = (h->size + 3) & ~UINT32_C(3);
    if (!aligned) aligned = 4;
    uint32_t end = h->pointer + aligned + 16u;
    if (end > mac_cpu_ram_size(rt->cpu)) return -1;
    const uint8_t *ram = mac_cpu_ram(rt->cpu);
    for (uint32_t p = h->pointer + h->size; p < end; p++)
        if (ram[p] != 0xA5) return -1;
    return 0;
}

/* Bounded MoveHHi: the trailing movable block has no intervening allocation
 * barriers. Other low-heap layouts need compaction, which we do not fake. */
static int move_high(MacRuntime *rt, uint32_t address) {
    MacCpu *cpu = rt->cpu;
    MacHandle *h = lookup(rt, address, 1);
    if (!h || !h->pointer) { result(cpu, -109); return 1; }
    if (h->loaded_code || (h->state & 0x80)) { result(cpu, -117); return 1; }
    if (h->size > UINT32_MAX - 32) { result(cpu, -108); return 1; }
    uint32_t amount = (h->size + 3) & ~UINT32_C(3);
    if (!amount) amount = 4;
    uint32_t stride = amount + (rt->guard_allocs ? 16u : 0u);
    size_t ram = mac_cpu_ram_size(cpu);
    if ((h->handle & 1) || h->handle > ram || ram - h->handle < 4 ||
        h->pointer > ram || stride > ram - h->pointer ||
        mac_cpu_read(cpu, h->handle, 4) != h->pointer) {
        mac_cpu_halt(cpu, "MoveHHi handle storage is invalid."); return -1;
    }
    if (rt->heap_high && h->pointer >= rt->heap_high) { result(cpu, 0); return 1; }
    if (h->pointer + stride != rt->heap_next) {
        mac_cpu_halt(cpu, "MoveHHi nontrailing blocks require heap compaction."); return -1;
    }
    uint32_t top = ceiling(rt) & ~UINT32_C(3);
    if (top < stride || top - stride < h->pointer) { result(cpu, -108); return 1; }
    uint32_t destination = top - stride;
    memmove(mac_cpu_ram(cpu) + destination, mac_cpu_ram(cpu) + h->pointer, h->size);
    stamp_guard(rt, destination, h->size);
    rt->heap_next = h->pointer; /* Reclaim the known trailing low allocation. */
    rt->heap_high = destination;
    if (top > rt->heap_high_end) rt->heap_high_end = top;
    h->pointer = destination;
    mac_cpu_write(cpu, h->handle, 4, destination);
    result(cpu, 0);
    return 1;
}

int mac_memory_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode >= 0xa800) return 0;
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), d0 = mac_cpu_get(cpu, MAC_D0);
    unsigned call = opcode & 0xff;
    if (call == 0x4c || call == 0x4d) {
        /* CompactMem/PurgeMem may stop immediately when the requested free
         * span already exists. No allocations are moved or purged here.
         * https://dev.os9.ca/techpubs/mac/Memory/Memory-118.html
         * https://leopard-adc.pepas.com/documentation/mac/Memory/Memory-120.html
         * Separate system zones and fragmented-heap recovery are unsupported. */
        if (opcode & 0x400) return 0;
        uint32_t free_bytes = available(rt) & ~UINT32_C(3);
        if (d0 > free_bytes) {
            mac_cpu_halt(cpu, call == 0x4c ?
                "CompactMem requires unsupported heap compaction." :
                "PurgeMem requires unsupported heap reclamation.");
            return -1;
        }
        result(cpu, 0);
        if (call == 0x4c) {
            mac_cpu_set(cpu, MAC_D0, free_bytes);
            mac_cpu_set(cpu, MAC_A0, free_bytes ? rt->heap_next : 0);
        }
        return 1;
    }
    if (call == 0x64) return move_high(rt, a0);
    if (call == 0x55) { /* StripAddress in our initial 24-bit guest mode. */
        mac_cpu_set(cpu, MAC_A0, a0 & 0xffffffu);
        return 1;
    }
    if (call == 0x2d) { /* SetApplLimit, reserving space below the A5 world. */
        uint32_t maximum = rt->stack_base > 0x10000 ? rt->stack_base - 0x10000 : rt->heap_limit;
        if (maximum > mac_cpu_ram_size(cpu)) maximum = (uint32_t)mac_cpu_ram_size(cpu);
        uint32_t limit = a0 < maximum ? a0 : maximum;
        if (limit < rt->heap_next || limit < rt->heap_high_end) result(cpu, -108);
        else { rt->heap_limit = limit; mac_cpu_write(cpu, 0x130, 4, limit); result(cpu, 0); }
        return 1;
    }
    if (call == 0x1e || call == 0x22) { /* NewPtr/NewHandle, including clear variants. */
        uint32_t old = rt->heap_next;
        uint32_t handle = call == 0x22 ? mac_runtime_alloc(rt, 4) : 0;
        uint32_t pointer = mac_runtime_alloc(rt, d0);
        MacHandle *h = NULL;
        if (pointer && (call != 0x22 || handle)) h = slot(rt);
        if (!h) {
            rt->heap_next = old;
            mac_cpu_set(cpu, MAC_A0, 0);
            result(cpu, -108);
        } else {
            *h = (MacHandle){.handle = handle, .pointer = pointer, .size = d0};
            stamp_guard(rt, pointer, d0);
            if (handle) mac_cpu_write(cpu, handle, 4, pointer);
            mac_cpu_set(cpu, MAC_A0, handle ? handle : pointer);
            result(cpu, 0);
        }
        return 1;
    }
    if (call == 0x1f || call == 0x23) { /* DisposePtr / DisposeHandle. */
        MacHandle *h = lookup(rt, a0, call == 0x23);
        if (!h) result(cpu, -109);
        else if (h->loaded_code) result(cpu, -117);
        else if (verify_guard(rt, h) != 0) {
            mac_cpu_halt(cpu, "Guest heap guard redzone corrupted.");
            return -1;
        } else {
            if (h->handle) mac_cpu_write(cpu, h->handle, 4, 0);
            memset(h, 0, sizeof *h);
            result(cpu, 0);
        }
        return 1;
    }
    if (call == 0x21 || call == 0x25) { /* GetPtrSize / GetHandleSize. */
        MacHandle *h = lookup(rt, a0, call == 0x25);
        result(cpu, h ? 0 : -109);
        if (h) mac_cpu_set(cpu, MAC_D0, h->size);
        return 1;
    }
    if (call == 0x24) { /* SetHandleSize preserves contents and lock constraints. */
        MacHandle *h = lookup(rt, a0, 1);
        if (!h) result(cpu, -109);
        else if (h->loaded_code && d0 != h->size) result(cpu, -117);
        else if (d0 <= h->size) { h->size = d0; stamp_guard(rt, h->pointer, d0); result(cpu, 0); }
        else if (h->state & 0x80) result(cpu, -117);
        else {
            uint32_t p = mac_runtime_alloc(rt, d0);
            if (!p) result(cpu, -108);
            else {
                memcpy(mac_cpu_ram(cpu) + p, mac_cpu_ram(cpu) + h->pointer, h->size);
                stamp_guard(rt, p, d0);
                h->pointer = p;
                h->size = d0;
                mac_cpu_write(cpu, h->handle, 4, p);
                result(cpu, 0);
            }
        }
        return 1;
    }
    if (call == 0x29 || call == 0x2a || call == 0x49 || call == 0x4a ||
        call == 0x69 || call == 0x6a) { /* HLock/HUnlock/HPurge/HNoPurge/Get/SetHState. */
        MacHandle *h = lookup(rt, a0, 1);
        if (!h) result(cpu, -109);
        else {
            if (call == 0x29) h->state |= 0x80;
            if (call == 0x2a) h->state &= ~0x80;
            if (call == 0x49) h->state |= 0x40;
            if (call == 0x4a) h->state &= ~0x40;
            if (call == 0x6a) h->state = (uint8_t)d0;
            result(cpu, 0);
            if (call == 0x69) mac_cpu_set(cpu, MAC_D0, h->state);
        }
        return 1;
    }
    if (call == 0x2e) { /* BlockMove: checked overlapping guest ranges. */
        uint32_t a1 = mac_cpu_get(cpu, MAC_A1);
        size_t ram_size = mac_cpu_ram_size(cpu);
        if (a0 > ram_size || d0 > ram_size - a0 || a1 > ram_size || d0 > ram_size - a1) {
            mac_cpu_halt(cpu, "BlockMove range exceeds guest RAM.");
            return -1;
        }
        memmove(mac_cpu_ram(cpu) + a1, mac_cpu_ram(cpu) + a0, d0);
        result(cpu, 0);
        return 1;
    }
    if (call == 0x1c || call == 0x1d || call == 0x61) { /* FreeMem / MaxMem / MaxBlock. */
        result(cpu, 0);
        mac_cpu_set(cpu, MAC_D0, available(rt));
        if (call == 0x1d) mac_cpu_set(cpu, MAC_A0, 0); /* No further arena growth. */
        return 1;
    }
    if (call == 0x40) { /* ReserveMem: sufficient contiguous space for a later allocation. */
        result(cpu, d0 <= available(rt) ? 0 : -108);
        return 1;
    }
    if (call == 0x63) { /* MaxApplZone: the arena already occupies its allowed extent. */
        result(cpu, 0);
        return 1;
    }
    return 0;
}
