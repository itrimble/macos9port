#include "mac_runtime.h"
#include "os999_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Connects the protected heap (os999_heap) to the guest Memory Manager traps.
 * The heap owns a window of guest RAM [lo, hi). Guest handles are addresses of
 * master pointers; masters live in fixed 1 KB pages inside the window while all
 * bookkeeping (tokens, state bytes, free slots) stays in host memory, so a guest
 * scribbling over a master pointer or a page cannot corrupt the allocator. */

#define SLOTS 256u
#define PAGE_BYTES (SLOTS * 4u)

typedef struct {
    uint32_t base;
    uint32_t token[SLOTS]; /* 0 = free slot */
    uint8_t state[SLOTS];  /* HGetState byte; 0x80 mirrors the heap lock */
    uint16_t mirror[SLOTS]; /* 1 + index of the rt->handles[] record shadowing this handle; 0 = none */
} Page;

struct MacHeapBridge {
    MacRuntime *rt;
    Os999Space *space;
    Os999Heap *heap;
    uint32_t lo, hi;
    Page **pages;
    size_t npages, cap;
    uint32_t *master_of; /* token index -> master address */
    size_t mcap;
    uint32_t *freestack;
    size_t nfree, fcap;
};

static Page *find_slot(const MacHeapBridge *b, uint32_t master, unsigned *slot);

/* Other managers (menus, resources, windows...) validate guest handles by looking
 * them up in rt->handles[]. Each heap handle is therefore shadowed by a record
 * there, kept in step on move, resize, lock and dispose. When the table is full the
 * handle still works for the traps served here but is invisible to those managers. */
static MacHandle *shadow(MacHeapBridge *b, const Page *p, unsigned slot) {
    return p->mirror[slot] ? &b->rt->handles[p->mirror[slot] - 1] : NULL;
}

static void shadow_create(MacHeapBridge *b, Page *p, unsigned slot, uint32_t master) {
    MacRuntime *rt = b->rt;
    size_t i;
    for (i = 0; i < rt->handle_count; i++) if (!rt->handles[i].handle && !rt->handles[i].pointer) break;
    if (i == rt->handle_count) { if (i == MAC_RUNTIME_HANDLES) return; rt->handle_count++; }
    rt->handles[i] = (MacHandle){.handle = master};
    p->mirror[slot] = (uint16_t)(i + 1);
}

static void shadow_sync(MacHeapBridge *b, const Page *p, unsigned slot) {
    MacHandle *m = shadow(b, p, slot);
    uint32_t addr, size;
    if (!m || os999_deref(b->heap, p->token[slot], &addr) || os999_handle_size(b->heap, p->token[slot], &size)) return;
    m->pointer = addr; m->size = size; m->state = p->state[slot];
}

static void shadow_drop(MacHeapBridge *b, Page *p, unsigned slot) {
    MacHandle *m = shadow(b, p, slot);
    if (m) memset(m, 0, sizeof *m);
    p->mirror[slot] = 0;
}

/* Pointers are shadowed the same way ({handle 0, pointer, size}); they never move. */
static void ptr_shadow_create(MacHeapBridge *b, uint32_t address, uint32_t size) {
    MacRuntime *rt = b->rt;
    size_t i;
    for (i = 0; i < rt->handle_count; i++) if (!rt->handles[i].handle && !rt->handles[i].pointer) break;
    if (i == rt->handle_count) { if (i == MAC_RUNTIME_HANDLES) return; rt->handle_count++; }
    rt->handles[i] = (MacHandle){.pointer = address, .size = size};
}

static void ptr_shadow_drop(MacHeapBridge *b, uint32_t address) {
    MacRuntime *rt = b->rt;
    for (size_t i = 0; i < rt->handle_count; i++)
        if (!rt->handles[i].handle && rt->handles[i].pointer == address) { memset(&rt->handles[i], 0, sizeof rt->handles[i]); return; }
}

static void move_cb(void *user, uint32_t token, uint32_t address) {
    MacHeapBridge *b = user;
    size_t idx = (token & 0xfffffu) - 1;
    if (idx >= b->mcap || !b->master_of[idx]) return;
    uint32_t master = b->master_of[idx];
    mac_cpu_write(b->rt->cpu, master, 4, address);
    unsigned slot;
    Page *p = find_slot(b, master, &slot);
    MacHandle *m = p ? shadow(b, p, slot) : NULL;
    if (m) m->pointer = address;
}

int mac_heap_attach(MacRuntime *rt, uint32_t lo, uint32_t hi) {
    if (!rt || !rt->cpu || rt->heap_bridge || lo >= hi || (lo & 7) || hi > mac_cpu_ram_size(rt->cpu)) return -1;
    MacHeapBridge *b = calloc(1, sizeof *b);
    if (!b) return -1;
    b->rt = rt; b->lo = lo; b->hi = hi;
    memset(mac_cpu_ram(rt->cpu) + lo, 0, hi - lo); /* the heap invariant: free space is zero */
    Os999Config c = {.initial_commit = 64 * 1024, .grow_chunk = 64 * 1024, .max_handles = 0xfffff,
                     .ext_base = mac_cpu_ram(rt->cpu) + lo, .ext_size = hi - lo, .guest_base = lo,
                     .on_move = move_cb, .on_move_user = b};
    b->space = os999_space_create(&c);
    b->heap = b->space ? os999_space_heap(b->space) : NULL;
    if (!b->heap) { os999_space_destroy(b->space); free(b); return -1; }
    rt->heap_bridge = b;
    return 0;
}

void mac_heap_detach(MacRuntime *rt) {
    MacHeapBridge *b = rt ? rt->heap_bridge : NULL;
    if (!b) return;
    for (size_t i = 0; i < b->npages; i++) free(b->pages[i]);
    free(b->pages); free(b->master_of); free(b->freestack);
    os999_space_destroy(b->space);
    free(b);
    rt->heap_bridge = NULL;
}

uint32_t mac_heap_alloc(MacRuntime *rt, uint32_t size) {
    uint32_t address;
    if (!rt || !rt->heap_bridge || os999_new_internal(rt->heap_bridge->heap, size, &address)) return 0;
    return address;
}

void mac_heap_free(MacRuntime *rt, uint32_t address) {
    if (rt && rt->heap_bridge && address) os999_dispose_internal(rt->heap_bridge->heap, address);
}

void mac_heap_report(const MacRuntime *rt, char *out, size_t n) {
    const MacHeapBridge *b = rt ? rt->heap_bridge : NULL;
    Os999Stats st;
    if (!b) { snprintf(out, n, "heap: legacy bump only, bump_used=%u", rt ? rt->heap_next - 0x100000u : 0u); return; }
    os999_heap_stats(b->heap, &st);
    snprintf(out, n, "heap: bump_used=%u of %u; protected used=%zu committed=%zu of %zu, blocks=%zu, compactions=%zu, grows=%zu, shadow_records=%zu",
             rt->heap_next - 0x100000u, rt->heap_limit - 0x100000u, st.used, st.committed, st.max_bytes,
             st.live_blocks, st.compactions, st.grows, rt->handle_count);
}

int mac_heap_owns(const MacRuntime *rt, uint32_t address) {
    return rt && rt->heap_bridge && address >= rt->heap_bridge->lo && address < rt->heap_bridge->hi;
}

static int grow_array(void **arr, size_t *cap, size_t need, size_t elem) {
    if (need <= *cap) return 1;
    size_t n = *cap ? *cap : 64;
    while (n < need) n *= 2;
    void *p = realloc(*arr, n * elem);
    if (!p) return 0;
    *arr = p; *cap = n;
    return 1;
}

static Page *find_slot(const MacHeapBridge *b, uint32_t master, unsigned *slot) {
    if (master & 3) return NULL;
    for (size_t i = 0; i < b->npages; i++) {
        Page *p = b->pages[i];
        if (master >= p->base && master - p->base < PAGE_BYTES) { *slot = (master - p->base) / 4; return p; }
    }
    return NULL;
}

static int take_slot(MacHeapBridge *b, uint32_t *master) {
    if (!b->nfree) {
        uint32_t base;
        if (os999_new_internal(b->heap, PAGE_BYTES, &base) != OS999_OK) return 0;
        Page *p = calloc(1, sizeof *p);
        if (!p || !grow_array((void **)&b->pages, &b->cap, b->npages + 1, sizeof *b->pages) ||
            !grow_array((void **)&b->freestack, &b->fcap, (b->npages + 1) * SLOTS, sizeof *b->freestack)) { /* room for every slot: pushes can't fail */
            free(p); os999_dispose_internal(b->heap, base); return 0;
        }
        p->base = base;
        b->pages[b->npages++] = p;
        for (unsigned i = SLOTS; i-- > 0;) b->freestack[b->nfree++] = base + 4 * i;
    }
    *master = b->freestack[--b->nfree];
    return 1;
}

static void result(MacRuntime *rt, int16_t err) { mac_memory_result(rt->cpu, err); }

static int fail_code(MacRuntime *rt, int rc) {
    if (rc == OS999_ERR_DEAD) { mac_cpu_halt(rt->cpu, "Guest heap was torn down."); return -1; }
    result(rt, rc == OS999_ERR_LOCKED ? -117 : rc == OS999_ERR_NILHANDLE ? -109 : -108);
    return 1;
}

static int compact_all(MacHeapBridge *b) {
    int moved = 0;
    while (os999_compact_step(b->heap, SIZE_MAX)) moved = 1;
    return moved;
}

int mac_heap_service(MacRuntime *rt, uint16_t opcode) {
    MacHeapBridge *b = rt->heap_bridge;
    if (!b || opcode >= 0xa800) return 0;
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), d0 = mac_cpu_get(cpu, MAC_D0), v, addr;
    unsigned call = opcode & 0xff, slot = 0;
    Os999Stats st;
    int rc;

    switch (call) {
    case 0x1e: /* NewPtr */
        rc = os999_new_ptr(b->heap, d0, &addr);
        mac_cpu_set(cpu, MAC_A0, rc ? 0 : addr);
        if (rc) return fail_code(rt, rc);
        ptr_shadow_create(b, addr, d0);
        result(rt, 0); return 1;
    case 0x22: { /* NewHandle */
        uint32_t master, token;
        if (!take_slot(b, &master)) { mac_cpu_set(cpu, MAC_A0, 0); result(rt, -108); return 1; }
        rc = os999_new_handle(b->heap, d0, &token);
        if (rc) { b->freestack[b->nfree++] = master; mac_cpu_set(cpu, MAC_A0, 0); return fail_code(rt, rc); }
        Page *p = find_slot(b, master, &slot);
        size_t idx = (token & 0xfffffu) - 1;
        if (!grow_array((void **)&b->master_of, &b->mcap, idx + 1, sizeof *b->master_of)) {
            os999_dispose_handle(b->heap, token); b->freestack[b->nfree++] = master;
            mac_cpu_set(cpu, MAC_A0, 0); result(rt, -108); return 1;
        }
        p->token[slot] = token; p->state[slot] = 0; b->master_of[idx] = master;
        os999_deref(b->heap, token, &addr);
        mac_cpu_write(cpu, master, 4, addr);
        shadow_create(b, p, slot, master);
        shadow_sync(b, p, slot);
        mac_cpu_set(cpu, MAC_A0, master);
        result(rt, 0); return 1;
    }
    case 0x1f: case 0x21: /* DisposePtr / GetPtrSize */
        if (!mac_heap_owns(rt, a0)) return 0;
        rc = call == 0x1f ? os999_dispose_ptr(b->heap, a0) : os999_ptr_size(b->heap, a0, &v);
        if (rc) return fail_code(rt, rc);
        if (call == 0x1f) ptr_shadow_drop(b, a0);
        result(rt, 0);
        if (call == 0x21) mac_cpu_set(cpu, MAC_D0, v);
        return 1;
    case 0x23: case 0x25: case 0x24: case 0x29: case 0x2a: case 0x49: case 0x4a: case 0x69: case 0x6a:
    case 0x64: { /* handle-addressed calls */
        if (!mac_heap_owns(rt, a0)) return 0;
        Page *p = find_slot(b, a0, &slot);
        uint32_t token = p ? p->token[slot] : 0;
        if (!token) { result(rt, -109); return 1; }
        uint8_t *st8 = &p->state[slot];
        if (call == 0x23) {
            rc = os999_dispose_handle(b->heap, token);
            if (rc) return fail_code(rt, rc);
            b->master_of[(token & 0xfffffu) - 1] = 0;
            shadow_drop(b, p, slot);
            p->token[slot] = 0; *st8 = 0;
            b->freestack[b->nfree++] = a0;
            mac_cpu_write(cpu, a0, 4, 0);
            result(rt, 0);
        } else if (call == 0x25) {
            rc = os999_handle_size(b->heap, token, &v);
            if (rc) return fail_code(rt, rc);
            result(rt, 0); mac_cpu_set(cpu, MAC_D0, v);
        } else if (call == 0x24) {
            rc = os999_set_handle_size(b->heap, token, d0); /* master is fixed up by move_cb */
            if (rc) return fail_code(rt, rc);
            shadow_sync(b, p, slot);
            result(rt, 0);
        } else if (call == 0x64) {
            result(rt, 0); /* MoveHHi is a hint: this heap compacts on its own */
        } else {
            uint8_t ns = *st8;
            if (call == 0x29) ns |= 0x80;
            if (call == 0x2a) ns &= (uint8_t)~0x80;
            if (call == 0x49) ns |= 0x40;
            if (call == 0x4a) ns &= (uint8_t)~0x40;
            if (call == 0x6a) ns = (uint8_t)d0;
            if (ns & 0x80) os999_lock(b->heap, token); else os999_unlock(b->heap, token);
            *st8 = ns;
            shadow_sync(b, p, slot);
            result(rt, 0);
            if (call == 0x69) mac_cpu_set(cpu, MAC_D0, ns);
        }
        return 1;
    }
    case 0x1c: case 0x1d: case 0x61: /* FreeMem / MaxMem / MaxBlock */
        if (call == 0x1d) compact_all(b);
        os999_heap_stats(b->heap, &st);
        result(rt, 0);
        mac_cpu_set(cpu, MAC_D0, (uint32_t)(call == 0x1c ? st.max_bytes - st.used : st.largest_alloc));
        if (call == 0x1d) mac_cpu_set(cpu, MAC_A0, 0);
        return 1;
    case 0x4c: case 0x4d: case 0x40: /* CompactMem / PurgeMem / ReserveMem */
        if (opcode & 0x400) return 0; /* system zone: not modelled */
        compact_all(b);
        os999_heap_stats(b->heap, &st);
        if (call == 0x4c) { result(rt, 0); mac_cpu_set(cpu, MAC_D0, (uint32_t)st.largest_alloc); }
        else result(rt, d0 <= st.largest_alloc ? 0 : -108); /* nothing is purgeable yet */
        return 1;
    case 0x2d: /* SetApplLimit: heaps grow on demand, so the limit no longer binds */
        result(rt, 0);
        return 1;
    default:
        return 0;
    }
}
