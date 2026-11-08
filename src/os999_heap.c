#include "os999_heap.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGN 8u
#define IDX_BITS 20u
#define IDX_MASK ((UINT32_C(1) << IDX_BITS) - 1)
#define GEN_MASK 0xfffu
#define HARD_MAX_HANDLES ((size_t)IDX_MASK)

typedef struct {
    uint32_t off, size, alen; /* offset in arena, requested size, aligned length */
    uint16_t gen;
    uint8_t used, locked, fixed;
} Entry;

struct Os999Heap {
    uint8_t *base;
    size_t committed, max_bytes, grow_chunk, max_handles;
    Entry *e;
    uint32_t *order, *freelist; /* order: live entry indices sorted by offset */
    size_t ecount, ecap, nlive, nfree;
    size_t compactions, grows;
};

struct Os999Space {
    Os999Heap heap;
    Os999SpaceState state;
    Os999Fault fault;
    size_t reserved;
};

static size_t g_live_spaces, g_reserved;

static size_t page_size(void) {
    long p = sysconf(_SC_PAGESIZE);
    return p > 0 ? (size_t)p : 4096;
}
static size_t round_up(size_t v, size_t to) { return (v + to - 1) / to * to; }

Os999Config os999_default_config(void) {
    return (Os999Config){.initial_commit = 256 * 1024, .grow_chunk = 64 * 1024,
                         .max_bytes = 256u * 1024 * 1024, .max_handles = 65536};
}

/* ---- space lifetime ---------------------------------------------------- */

Os999Space *os999_space_create(const Os999Config *cfg_in) {
    Os999Config cfg = cfg_in ? *cfg_in : os999_default_config();
    size_t pg = page_size();
    if (!cfg.max_bytes || cfg.max_bytes > UINT32_MAX - OS999_GUEST_BASE) return NULL;
    if (!cfg.grow_chunk) cfg.grow_chunk = 64 * 1024;
    cfg.grow_chunk = round_up(cfg.grow_chunk, pg);
    size_t max = cfg.max_bytes / pg * pg;
    size_t init = round_up(cfg.initial_commit ? cfg.initial_commit : cfg.grow_chunk, pg);
    if (!max || init > max) return NULL;
    size_t maxh = cfg.max_handles ? cfg.max_handles : 65536;
    if (maxh > HARD_MAX_HANDLES) maxh = HARD_MAX_HANDLES;

    Os999Space *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    void *m = mmap(NULL, max, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) { free(s); return NULL; }
    if (mprotect(m, init, PROT_READ | PROT_WRITE) != 0) { munmap(m, max); free(s); return NULL; }
    s->heap = (Os999Heap){.base = m, .committed = init, .max_bytes = max,
                          .grow_chunk = cfg.grow_chunk, .max_handles = maxh};
    s->state = OS999_SPACE_ALIVE;
    s->reserved = max;
    __atomic_add_fetch(&g_live_spaces, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_reserved, max, __ATOMIC_RELAXED);
    return s;
}

void os999_space_terminate(Os999Space *s) {
    if (!s || !s->heap.base) return;
    Os999Heap *h = &s->heap;
    munmap(h->base, h->max_bytes);
    free(h->e); free(h->order); free(h->freelist);
    *h = (Os999Heap){0}; /* base == NULL: every heap call now reports DEAD */
    if (s->state == OS999_SPACE_ALIVE) s->state = OS999_SPACE_TERMINATED;
    __atomic_sub_fetch(&g_live_spaces, 1, __ATOMIC_RELAXED);
    __atomic_sub_fetch(&g_reserved, s->reserved, __ATOMIC_RELAXED);
}

void os999_space_destroy(Os999Space *s) {
    if (!s) return;
    os999_space_terminate(s);
    free(s);
}

Os999SpaceState os999_space_state(const Os999Space *s) { return s ? s->state : OS999_SPACE_TERMINATED; }
const Os999Fault *os999_space_fault(const Os999Space *s) { return s ? &s->fault : NULL; }
Os999Heap *os999_space_heap(Os999Space *s) {
    return s && s->state == OS999_SPACE_ALIVE && s->heap.base ? &s->heap : NULL;
}
size_t os999_live_spaces(void) { return __atomic_load_n(&g_live_spaces, __ATOMIC_RELAXED); }
size_t os999_reserved_bytes(void) { return __atomic_load_n(&g_reserved, __ATOMIC_RELAXED); }

/* ---- block table ------------------------------------------------------- */

/* Position in order[] of the first live block with offset >= off. */
static size_t lower_bound(const Os999Heap *h, uint32_t off) {
    size_t lo = 0, hi = h->nlive;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (h->e[h->order[mid]].off < off) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static Entry *lookup_handle(const Os999Heap *h, uint32_t handle) {
    uint32_t idx = handle & IDX_MASK;
    if (!idx || idx > h->ecount) return NULL;
    Entry *e = &h->e[idx - 1];
    return e->used && !e->fixed && e->gen == (handle >> IDX_BITS) ? e : NULL;
}

static uint32_t last_end(const Os999Heap *h) {
    if (!h->nlive) return 0;
    const Entry *e = &h->e[h->order[h->nlive - 1]];
    return e->off + e->alen;
}

/* First-fit search through the gaps between live blocks and the tail. */
static int find_gap(const Os999Heap *h, uint32_t alen, uint32_t *off, size_t *pos) {
    uint32_t prev = 0;
    for (size_t i = 0; i < h->nlive; i++) {
        const Entry *e = &h->e[h->order[i]];
        if (e->off - prev >= alen) { *off = prev; *pos = i; return 1; }
        prev = e->off + e->alen;
    }
    if (h->committed >= prev && h->committed - prev >= alen) { *off = prev; *pos = h->nlive; return 1; }
    return 0;
}

static int grow(Os999Heap *h, uint32_t alen) {
    size_t need = (size_t)last_end(h) + alen;
    if (need > h->max_bytes) return 0;
    size_t target = round_up(need, h->grow_chunk);
    if (target > h->max_bytes) target = h->max_bytes;
    if (target <= h->committed) return 1;
    if (mprotect(h->base + h->committed, target - h->committed, PROT_READ | PROT_WRITE) != 0) return 0;
    h->committed = target;
    h->grows++;
    return 1;
}

static void zero(Os999Heap *h, size_t from, size_t to) { if (to > from) memset(h->base + from, 0, to - from); }

size_t os999_compact_step(Os999Heap *h, size_t budget) {
    if (!h || !h->base) return 0;
    size_t moved = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < h->nlive; i++) {
        Entry *e = &h->e[h->order[i]];
        if (!e->locked && !e->fixed && e->off > prev) {
            uint32_t old = e->off;
            memmove(h->base + prev, h->base + old, e->alen);
            zero(h, (size_t)prev + e->alen > old ? (size_t)prev + e->alen : old, (size_t)old + e->alen);
            e->off = prev;
            moved += e->alen;
            if (moved >= budget) break;
        }
        prev = e->off + e->alen;
    }
    if (moved) h->compactions++;
    return moved;
}

/* Find room for alen bytes: gap, then grow (cheap), then compact (blocking). */
static int reserve(Os999Heap *h, uint32_t alen, uint32_t *off, size_t *pos) {
    if (find_gap(h, alen, off, pos)) return OS999_OK;
    if (grow(h, alen) && find_gap(h, alen, off, pos)) return OS999_OK;
    while (os999_compact_step(h, SIZE_MAX)) {}
    if (grow(h, alen) && find_gap(h, alen, off, pos)) return OS999_OK;
    return OS999_ERR_MEMFULL;
}

static int aligned_len(const Os999Heap *h, uint32_t size, uint32_t *alen) {
    if (size > h->max_bytes) return OS999_ERR_MEMFULL;
    uint64_t a = ((uint64_t)size + ALIGN - 1) & ~(uint64_t)(ALIGN - 1);
    *alen = a ? (uint32_t)a : ALIGN;
    return OS999_OK;
}

static int new_block(Os999Heap *h, uint32_t size, int fixed, uint32_t *idx_out) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    uint32_t alen, off; size_t pos;
    int rc = aligned_len(h, size, &alen);
    if (rc) return rc;
    if (h->nlive >= h->max_handles) return OS999_ERR_MEMFULL;
    uint32_t idx;
    if (h->nfree) idx = h->freelist[--h->nfree];
    else {
        if (h->ecount == h->ecap) {
            size_t cap = h->ecap ? h->ecap * 2 : 64;
            if (cap > HARD_MAX_HANDLES) cap = HARD_MAX_HANDLES;
            Entry *e = realloc(h->e, cap * sizeof *e);
            if (!e) return OS999_ERR_MEMFULL;
            h->e = e;
            uint32_t *o = realloc(h->order, cap * sizeof *o);
            if (!o) return OS999_ERR_MEMFULL;
            h->order = o;
            uint32_t *f = realloc(h->freelist, cap * sizeof *f);
            if (!f) return OS999_ERR_MEMFULL;
            h->freelist = f; h->ecap = cap;
        }
        idx = (uint32_t)h->ecount++;
        h->e[idx] = (Entry){0};
    }
    rc = reserve(h, alen, &off, &pos);
    if (rc) { h->e[idx].used = 0; h->freelist[h->nfree++] = idx; return rc; }
    Entry *e = &h->e[idx];
    *e = (Entry){.off = off, .size = size, .alen = alen, .gen = e->gen, .used = 1, .fixed = (uint8_t)fixed};
    memmove(&h->order[pos + 1], &h->order[pos], (h->nlive - pos) * sizeof *h->order);
    h->order[pos] = idx;
    h->nlive++;
    *idx_out = idx;
    return OS999_OK;
}

static void free_block(Os999Heap *h, uint32_t idx) {
    Entry *e = &h->e[idx];
    size_t pos = lower_bound(h, e->off);
    memmove(&h->order[pos], &h->order[pos + 1], (h->nlive - pos - 1) * sizeof *h->order);
    h->nlive--;
    zero(h, e->off, (size_t)e->off + e->alen);
    e->used = 0; e->locked = 0;
    e->gen = (e->gen + 1) & GEN_MASK;
    h->freelist[h->nfree++] = idx;
}

/* ---- public block API -------------------------------------------------- */

int os999_new_handle(Os999Heap *h, uint32_t size, uint32_t *handle) {
    uint32_t idx;
    int rc = new_block(h, size, 0, &idx);
    if (rc) { if (handle) *handle = 0; return rc; }
    *handle = ((uint32_t)h->e[idx].gen << IDX_BITS) | (idx + 1);
    return OS999_OK;
}

int os999_dispose_handle(Os999Heap *h, uint32_t handle) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    free_block(h, (uint32_t)(e - h->e));
    return OS999_OK;
}

int os999_deref(const Os999Heap *h, uint32_t handle, uint32_t *address) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    const Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    *address = OS999_GUEST_BASE + e->off;
    return OS999_OK;
}

int os999_handle_size(const Os999Heap *h, uint32_t handle, uint32_t *size) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    const Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    *size = e->size;
    return OS999_OK;
}

int os999_lock(Os999Heap *h, uint32_t handle) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    e->locked = 1;
    return OS999_OK;
}

int os999_unlock(Os999Heap *h, uint32_t handle) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    e->locked = 0;
    return OS999_OK;
}

int os999_set_handle_size(Os999Heap *h, uint32_t handle, uint32_t size) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    Entry *e = lookup_handle(h, handle);
    if (!e) return OS999_ERR_NILHANDLE;
    uint32_t idx = (uint32_t)(e - h->e), alen;
    int rc = aligned_len(h, size, &alen);
    if (rc) return rc;
    if (alen <= e->alen) {                       /* shrink (or same) in place */
        zero(h, (size_t)e->off + size, (size_t)e->off + e->alen);
        e->alen = alen; e->size = size;
        return OS999_OK;
    }
    size_t pos = lower_bound(h, e->off);
    uint32_t limit = pos + 1 < h->nlive ? h->e[h->order[pos + 1]].off : (uint32_t)h->committed;
    if (limit - e->off >= alen) { e->alen = alen; e->size = size; return OS999_OK; } /* extend in place */
    if (e->locked) return OS999_ERR_LOCKED;      /* a locked block must not move */
    uint32_t off; size_t npos;
    rc = reserve(h, alen, &off, &npos);          /* may compact: re-fetch e after */
    if (rc) return rc;
    e = &h->e[idx];
    size_t opos = lower_bound(h, e->off);
    memcpy(h->base + off, h->base + e->off, e->size < size ? e->size : size);
    zero(h, e->off, (size_t)e->off + e->alen);
    memmove(&h->order[opos], &h->order[opos + 1], (h->nlive - opos - 1) * sizeof *h->order);
    h->nlive--;
    if (opos < npos) npos--;
    memmove(&h->order[npos + 1], &h->order[npos], (h->nlive - npos) * sizeof *h->order);
    h->order[npos] = idx;
    h->nlive++;
    e->off = off; e->alen = alen; e->size = size;
    return OS999_OK;
}

int os999_new_ptr(Os999Heap *h, uint32_t size, uint32_t *address) {
    uint32_t idx;
    int rc = new_block(h, size, 1, &idx);
    if (rc) { if (address) *address = 0; return rc; }
    *address = OS999_GUEST_BASE + h->e[idx].off;
    return OS999_OK;
}

int os999_dispose_ptr(Os999Heap *h, uint32_t address) {
    if (!h || !h->base) return OS999_ERR_DEAD;
    if (address < OS999_GUEST_BASE) return OS999_ERR_NILHANDLE;
    uint32_t off = address - OS999_GUEST_BASE;
    size_t pos = lower_bound(h, off);
    if (pos >= h->nlive) return OS999_ERR_NILHANDLE;
    Entry *e = &h->e[h->order[pos]];
    if (e->off != off || !e->fixed) return OS999_ERR_NILHANDLE;
    free_block(h, h->order[pos]);
    return OS999_OK;
}

/* ---- diagnostics ------------------------------------------------------- */

void os999_heap_stats(const Os999Heap *h, Os999Stats *o) {
    memset(o, 0, sizeof *o);
    if (!h || !h->base) return;
    uint32_t prev = 0;
    for (size_t i = 0; i < h->nlive; i++) {
        const Entry *e = &h->e[h->order[i]];
        if (e->off - prev > o->largest_free) o->largest_free = e->off - prev;
        o->used += e->alen;
        prev = e->off + e->alen;
    }
    if (h->committed - prev > o->largest_free) o->largest_free = h->committed - prev;
    o->committed = h->committed; o->max_bytes = h->max_bytes;
    o->free_total = h->committed - o->used; o->live_blocks = h->nlive;
    o->compactions = h->compactions; o->grows = h->grows;
}

int os999_heap_check(const Os999Heap *h) {
    if (!h || !h->base) return 0;
    uint32_t prev = 0;
    for (size_t i = 0; i <= h->nlive; i++) {
        uint32_t start = i < h->nlive ? h->e[h->order[i]].off : (uint32_t)h->committed;
        if (start < prev || start > h->committed) return 0;
        for (uint32_t b = prev; b < start; b++) if (h->base[b]) return 0;
        if (i == h->nlive) break;
        const Entry *e = &h->e[h->order[i]];
        if (!e->used || e->size > e->alen) return 0;
        prev = e->off + e->alen;
    }
    return 1;
}

/* ---- checked translation ------------------------------------------------ */

static uint8_t *fault(Os999Space *s, Os999FaultKind k, uint32_t a, uint32_t len, int w) {
    s->fault = (Os999Fault){k, a, len, w};
    s->state = OS999_SPACE_FAULTED;
    os999_space_terminate(s); /* contain: only this space is torn down */
    return NULL;
}

uint8_t *os999_translate(Os999Space *s, uint32_t addr, uint32_t len, int write) {
    if (!s || s->state != OS999_SPACE_ALIVE) return NULL;
    Os999Heap *h = &s->heap;
    if (addr < OS999_GUEST_BASE || (uint64_t)(addr - OS999_GUEST_BASE) + len > h->committed)
        return fault(s, OS999_FAULT_UNMAPPED, addr, len, write);
    return h->base + (addr - OS999_GUEST_BASE);
}

uint8_t *os999_translate_block(Os999Space *s, uint32_t addr, uint32_t len, int write) {
    if (!s || s->state != OS999_SPACE_ALIVE) return NULL;
    Os999Heap *h = &s->heap;
    if (addr < OS999_GUEST_BASE || (uint64_t)(addr - OS999_GUEST_BASE) + len > h->committed)
        return fault(s, OS999_FAULT_UNMAPPED, addr, len, write);
    uint32_t off = addr - OS999_GUEST_BASE;
    size_t pos = lower_bound(h, off);
    if (pos == h->nlive || h->e[h->order[pos]].off != off) {
        if (!pos) return fault(s, OS999_FAULT_OUT_OF_BLOCK, addr, len, write);
        pos--;
    }
    const Entry *e = &h->e[h->order[pos]];
    if (off < e->off || (uint64_t)(off - e->off) + len > e->size)
        return fault(s, OS999_FAULT_OUT_OF_BLOCK, addr, len, write);
    return h->base + off;
}
