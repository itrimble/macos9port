#include "os999_heap.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

#define PG ((size_t)sysconf(_SC_PAGESIZE))

static Os999Space *make(size_t init, size_t chunk, size_t max, size_t handles) {
    Os999Config c = {.initial_commit = init, .grow_chunk = chunk, .max_bytes = max, .max_handles = handles};
    Os999Space *s = os999_space_create(&c);
    CHECK(s != NULL);
    return s;
}

static void fill(Os999Space *s, uint32_t addr, uint32_t n, uint8_t seed) {
    uint8_t *p = os999_translate(s, addr, n, 1);
    CHECK(p != NULL);
    for (uint32_t i = 0; p && i < n; i++) p[i] = (uint8_t)(seed + i);
}
static int verify(Os999Space *s, uint32_t addr, uint32_t n, uint8_t seed) {
    uint8_t *p = os999_translate(s, addr, n, 0);
    if (!p) return 0;
    for (uint32_t i = 0; i < n; i++) if (p[i] != (uint8_t)(seed + i)) return 0;
    return 1;
}

static void basics(void) {
    Os999Space *s = make(PG, PG, 16 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    uint32_t hd, a, sz;
    CHECK(os999_new_handle(h, 100, &hd) == OS999_OK && hd != 0);
    CHECK(os999_deref(h, hd, &a) == OS999_OK && a >= OS999_GUEST_BASE && a % 8 == 0);
    CHECK(os999_handle_size(h, hd, &sz) == OS999_OK && sz == 100);
    fill(s, a, 100, 7);
    CHECK(verify(s, a, 100, 7));
    uint32_t z;
    CHECK(os999_new_handle(h, 0, &z) == OS999_OK);       /* zero-size is legal */
    uint32_t za; CHECK(os999_deref(h, z, &za) == OS999_OK && za != a);
    uint32_t p1 = 0, p2 = 0;
    CHECK(os999_new_ptr(h, 33, &p1) == OS999_OK && os999_new_ptr(h, 33, &p2) == OS999_OK);
    CHECK(p2 - p1 == 40);                                 /* 8-byte alignment, no overlap */
    CHECK(os999_heap_check(h));
    os999_space_destroy(s);
}

static void limits(void) {
    Os999Space *s = make(PG, PG, 4 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    uint32_t v, n = 0;
    CHECK(os999_new_handle(h, UINT32_MAX, &v) == OS999_ERR_MEMFULL && v == 0);
    CHECK(os999_new_handle(h, UINT32_MAX - 3, &v) == OS999_ERR_MEMFULL);   /* alignment overflow */
    CHECK(os999_new_handle(h, (uint32_t)(4 * PG + 1), &v) == OS999_ERR_MEMFULL);
    uint32_t ptrs[64];
    while (n < 64 && os999_new_ptr(h, PG / 4, &ptrs[n]) == OS999_OK) n++;
    CHECK(n == 16);                                       /* exactly the 4-page ceiling */
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(st.committed == 4 * PG && st.free_total == 0);
    CHECK(os999_new_ptr(h, 1, &v) == OS999_ERR_MEMFULL);  /* failure leaves heap intact */
    CHECK(os999_heap_check(h));
    CHECK(os999_dispose_ptr(h, ptrs[3]) == OS999_OK);
    CHECK(os999_new_ptr(h, PG / 4, &v) == OS999_OK && v == ptrs[3]);       /* hole reused */
    os999_space_destroy(s);

    s = make(PG, PG, 64 * PG, 4);                         /* handle-count cap */
    h = os999_space_heap(s);
    uint32_t hs[5];
    for (int i = 0; i < 4; i++) CHECK(os999_new_handle(h, 8, &hs[i]) == OS999_OK);
    CHECK(os999_new_handle(h, 8, &hs[4]) == OS999_ERR_MEMFULL);
    CHECK(os999_dispose_handle(h, hs[0]) == OS999_OK);
    CHECK(os999_new_handle(h, 8, &hs[4]) == OS999_OK);
    os999_space_destroy(s);

    CHECK(os999_space_create(&(Os999Config){.initial_commit = PG * 8, .grow_chunk = PG, .max_bytes = PG * 4}) == NULL);  /* init > max */
    CHECK(os999_space_create(&(Os999Config){0}) == NULL);
}

static void auto_grow(void) {
    Os999Space *s = make(PG, PG, 64 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(st.committed == PG && st.grows == 0);
    uint32_t big, a;
    CHECK(os999_new_handle(h, (uint32_t)(10 * PG), &big) == OS999_OK);      /* no preset partition */
    os999_heap_stats(h, &st);
    CHECK(st.committed >= 10 * PG && st.committed <= 64 * PG && st.grows >= 1);
    CHECK(os999_deref(h, big, &a) == OS999_OK);
    fill(s, a, (uint32_t)(10 * PG), 3);
    CHECK(verify(s, a, (uint32_t)(10 * PG), 3));
    CHECK(os999_heap_check(h));
    os999_space_destroy(s);
}

static void boundaries(void) {
    for (int mode = 0; mode < 2; mode++) {
        uint8_t *(*xl)(Os999Space *, uint32_t, uint32_t, int) = mode ? os999_translate_block : os999_translate;
        Os999Space *s = make(PG, PG, 4 * PG, 0);
        Os999Heap *h = os999_space_heap(s);
        uint32_t p, q; os999_new_ptr(h, 100, &p); os999_new_ptr(h, 100, &q);
        CHECK(xl(s, p, 100, 1) != NULL);                  /* exact fit */
        CHECK(xl(s, p + 99, 1, 0) != NULL);               /* last byte */
        CHECK(xl(s, p + 100, 0, 0) != NULL);              /* zero-length at end */
        CHECK(os999_space_state(s) == OS999_SPACE_ALIVE);
        CHECK((xl(s, p, 101, 1) == NULL) == (mode == 1));  /* one past block end */
        /* translate() sees committed arena (still valid); the block check faults. */
        CHECK(os999_space_state(s) == (mode ? OS999_SPACE_FAULTED : OS999_SPACE_ALIVE));
        os999_space_destroy(s);
    }
    /* Arena-level bounds: below base, at commit end, and 32-bit wraparound. */
    uint32_t bad[][2] = {{0, 1}, {OS999_GUEST_BASE - 1, 1}, {OS999_GUEST_BASE, 0xffffffffu},
                         {0xffffffffu, 2}, {0xfffffff0u, 0x20}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        Os999Space *s = make(PG, PG, 4 * PG, 0);
        CHECK(os999_translate(s, bad[i][0], bad[i][1], 0) == NULL);
        CHECK(os999_space_state(s) == OS999_SPACE_FAULTED);
        CHECK(os999_space_fault(s)->kind == OS999_FAULT_UNMAPPED);
        CHECK(os999_space_fault(s)->address == bad[i][0]);
        os999_space_destroy(s);
    }
    Os999Space *s = make(PG, PG, 4 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(os999_translate(s, OS999_GUEST_BASE + (uint32_t)st.committed - 1, 1, 0) != NULL);
    CHECK(os999_translate(s, OS999_GUEST_BASE + (uint32_t)st.committed, 1, 0) == NULL);
    os999_space_destroy(s);

    /* Block-level: a gap between blocks and a freed block are both out-of-block. */
    s = make(PG, PG, 4 * PG, 0); h = os999_space_heap(s);
    uint32_t a, b; os999_new_ptr(h, 16, &a); os999_new_ptr(h, 16, &b);
    CHECK(os999_translate_block(s, b, 8, 0) != NULL);        /* neighbour start is valid */
    CHECK(os999_translate_block(s, a + 8, 16, 0) == NULL);   /* straddles into neighbour */
    CHECK(os999_space_fault(s)->kind == OS999_FAULT_OUT_OF_BLOCK);
    os999_space_destroy(s);
    s = make(PG, PG, 4 * PG, 0); h = os999_space_heap(s);
    os999_new_ptr(h, 16, &a);
    CHECK(os999_dispose_ptr(h, a) == OS999_OK);
    CHECK(os999_translate_block(s, a, 1, 0) == NULL);         /* use after free */
    os999_space_destroy(s);
}

static void handles(void) {
    Os999Space *s = make(PG, PG, 16 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    uint32_t a, b, p, addr, sz;
    CHECK(os999_deref(h, 0, &addr) == OS999_ERR_NILHANDLE);
    CHECK(os999_new_handle(h, 16, &a) == OS999_OK);
    CHECK(os999_dispose_handle(h, a) == OS999_OK);
    CHECK(os999_dispose_handle(h, a) == OS999_ERR_NILHANDLE);     /* double dispose */
    CHECK(os999_new_handle(h, 16, &b) == OS999_OK);               /* reuses the slot */
    CHECK(b != a);                                                /* generation differs */
    CHECK(os999_deref(h, a, &addr) == OS999_ERR_NILHANDLE);       /* stale token rejected */
    CHECK(os999_handle_size(h, a, &sz) == OS999_ERR_NILHANDLE);
    CHECK(os999_deref(h, 0x7ffff, &addr) == OS999_ERR_NILHANDLE); /* out-of-table index */
    CHECK(os999_new_ptr(h, 16, &p) == OS999_OK);
    CHECK(os999_dispose_handle(h, p) == OS999_ERR_NILHANDLE);     /* pointers are not handles */
    CHECK(os999_dispose_ptr(h, p + 8) == OS999_ERR_NILHANDLE);    /* interior address */
    CHECK(os999_deref(h, b, &addr) == OS999_OK);
    CHECK(os999_dispose_ptr(h, addr) == OS999_ERR_NILHANDLE);     /* handles are not pointers */
    CHECK(os999_dispose_ptr(h, 0) == OS999_ERR_NILHANDLE);
    CHECK(os999_dispose_ptr(h, 0xffffffffu) == OS999_ERR_NILHANDLE);
    CHECK(os999_heap_check(h));
    os999_space_destroy(s);
}

static void resize_and_lock(void) {
    Os999Space *s = make(PG, PG, 16 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    uint32_t a, b, addr, addr2;
    os999_new_handle(h, 64, &a); os999_new_handle(h, 64, &b);
    os999_deref(h, a, &addr); fill(s, addr, 64, 9);
    CHECK(os999_set_handle_size(h, a, 32) == OS999_OK);           /* shrink in place */
    os999_deref(h, a, &addr2); CHECK(addr2 == addr && verify(s, addr, 32, 9));
    CHECK(os999_set_handle_size(h, a, 64) == OS999_OK);           /* regrow in place */
    CHECK(os999_deref(h, a, &addr2) == OS999_OK && addr2 == addr);
    uint8_t *p = os999_translate(s, addr + 32, 32, 0);
    for (int i = 0; p && i < 32; i++) CHECK(p[i] == 0);           /* slack was wiped */
    CHECK(os999_lock(h, a) == OS999_OK);
    CHECK(os999_set_handle_size(h, a, 4096) == OS999_ERR_LOCKED); /* b blocks growth, a pinned */
    CHECK(os999_unlock(h, a) == OS999_OK);
    CHECK(os999_set_handle_size(h, a, 4096) == OS999_OK);         /* relocates */
    os999_deref(h, a, &addr2);
    CHECK(addr2 != addr && verify(s, addr2, 32, 9));
    CHECK(os999_heap_check(h));
    uint32_t sz; CHECK(os999_handle_size(h, a, &sz) == OS999_OK && sz == 4096);
    CHECK(os999_set_handle_size(h, a, UINT32_MAX) == OS999_ERR_MEMFULL);
    CHECK(os999_handle_size(h, a, &sz) == OS999_OK && sz == 4096 && verify(s, addr2, 32, 9));
    CHECK(os999_set_handle_size(h, 0xdead, 8) == OS999_ERR_NILHANDLE);
    os999_space_destroy(s);
}

#define N 40
static void defragmentation(void) {
    /* Ceiling-limited heap: only compaction can satisfy the big request. */
    Os999Space *s = make(8 * PG, PG, 8 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    uint32_t hd[N], addr, big, lockedh = 0;
    uint32_t sz = (uint32_t)(8 * PG / N) & ~7u;
    for (int i = 0; i < N; i++) {
        CHECK(os999_new_handle(h, sz, &hd[i]) == OS999_OK);
        os999_deref(h, hd[i], &addr); fill(s, addr, sz, (uint8_t)i);
    }
    lockedh = hd[11]; CHECK(os999_lock(h, lockedh) == OS999_OK);
    uint32_t locked_addr; os999_deref(h, lockedh, &locked_addr);
    for (int i = 0; i < N; i += 2) if (hd[i] != lockedh) CHECK(os999_dispose_handle(h, hd[i]) == OS999_OK);
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(st.free_total >= 8 * sz && st.largest_free < 4 * sz);   /* fragmented */
    size_t before = st.compactions;
    /* Incremental, budgeted steps do bounded work and converge. */
    size_t total = 0, steps = 0, m;
    while ((m = os999_compact_step(h, sz)) != 0) {
        CHECK(m <= 2 * sz); total += m; steps++;
        CHECK(os999_heap_check(h));
        CHECK(steps < 1000);
    }
    CHECK(steps > 1 && total > 0);
    os999_heap_stats(h, &st);
    CHECK(st.compactions > before);
    CHECK(os999_compact_step(h, SIZE_MAX) == 0);                  /* idempotent once compact */
    os999_deref(h, lockedh, &addr); CHECK(addr == locked_addr);   /* locked block never moved */
    for (int i = 1; i < N; i += 2) {                              /* survivors keep contents */
        CHECK(os999_deref(h, hd[i], &addr) == OS999_OK);
        CHECK(verify(s, addr, sz, (uint8_t)i));
    }
    os999_space_destroy(s);

    /* Synchronous fallback: allocation compacts when the ceiling is hit. */
    s = make(8 * PG, PG, 8 * PG, 0); h = os999_space_heap(s);
    for (int i = 0; i < N; i++) {
        CHECK(os999_new_handle(h, sz, &hd[i]) == OS999_OK);
        os999_deref(h, hd[i], &addr); fill(s, addr, sz, (uint8_t)(i + 50));
    }
    for (int i = 0; i < N; i += 2) os999_dispose_handle(h, hd[i]);
    os999_heap_stats(h, &st);
    size_t want = st.free_total;
    CHECK(want > st.largest_free);
    CHECK(os999_new_handle(h, (uint32_t)(want & ~7u), &big) == OS999_OK);  /* needs compaction */
    for (int i = 1; i < N; i += 2) {
        os999_deref(h, hd[i], &addr);
        CHECK(verify(s, addr, sz, (uint8_t)(i + 50)));
    }
    CHECK(os999_heap_check(h));
    os999_space_destroy(s);
}

static void fault_containment(void) {
    size_t spaces0 = os999_live_spaces(), bytes0 = os999_reserved_bytes();
    Os999Space *a = make(PG, PG, 8 * PG, 0), *b = make(PG, PG, 8 * PG, 0);
    CHECK(os999_live_spaces() == spaces0 + 2);
    Os999Heap *ha = os999_space_heap(a), *hb = os999_space_heap(b);
    uint32_t xa, xb, pa, pb;
    os999_new_handle(ha, 64, &xa); os999_new_handle(hb, 64, &xb);
    os999_deref(ha, xa, &pa); os999_deref(hb, xb, &pb);
    fill(a, pa, 64, 1); fill(b, pb, 64, 2);
    CHECK(os999_translate(a, 0xdeadbeef, 4, 1) == NULL);          /* wild store in A */
    CHECK(os999_space_state(a) == OS999_SPACE_FAULTED);
    CHECK(os999_space_fault(a)->kind == OS999_FAULT_UNMAPPED);
    CHECK(os999_space_fault(a)->address == 0xdeadbeef && os999_space_fault(a)->write == 1);
    CHECK(os999_space_heap(a) == NULL);
    CHECK(os999_translate(a, pa, 1, 0) == NULL);                  /* dead space stays dead */
    uint32_t t;
    CHECK(os999_new_handle(ha, 8, &t) == OS999_ERR_DEAD);         /* stale Heap* is safe */
    CHECK(os999_deref(ha, xa, &t) == OS999_ERR_DEAD);
    CHECK(os999_compact_step(ha, 10) == 0);
    CHECK(os999_live_spaces() == spaces0 + 1);                    /* A released immediately */
    CHECK(os999_space_state(b) == OS999_SPACE_ALIVE);             /* B untouched */
    CHECK(verify(b, pb, 64, 2) && os999_heap_check(hb));
    CHECK(os999_new_handle(hb, 8, &t) == OS999_OK);
    os999_space_terminate(a); os999_space_terminate(a);           /* idempotent */
    os999_space_destroy(a);
    os999_space_destroy(b);
    CHECK(os999_live_spaces() == spaces0 && os999_reserved_bytes() == bytes0);
    os999_space_destroy(NULL);

    /* Churn: faulted, terminated and healthy spaces all return their memory. */
    for (int i = 0; i < 300; i++) {
        Os999Space *s = make(PG, PG, 4 * PG, 0);
        Os999Heap *h = os999_space_heap(s);
        uint32_t v; os999_new_handle(h, 1000, &v);
        if (i % 3 == 0) os999_translate(s, 0, 1, 0);
        else if (i % 3 == 1) os999_space_terminate(s);
        os999_space_destroy(s);
    }
    CHECK(os999_live_spaces() == spaces0 && os999_reserved_bytes() == bytes0);
}

static void hardware_fault(void) {
    /* Real MMU-level containment: touching uncommitted reserve kills only the child. */
    Os999Space *s = make(PG, PG, 8 * PG, 0);
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        Os999Heap *h = os999_space_heap(s);
        Os999Stats st; os999_heap_stats(h, &st);
        volatile uint8_t *base = os999_translate(s, OS999_GUEST_BASE, 1, 1);
        base[st.committed] = 1;                                   /* one byte past commit */
        _exit(0);
    }
    int status = 0;
    CHECK(pid > 0 && waitpid(pid, &status, 0) == pid);
    CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));      /* child did not survive */
    CHECK(os999_space_state(s) == OS999_SPACE_ALIVE);             /* parent unaffected */
    uint32_t v; CHECK(os999_new_handle(os999_space_heap(s), 8, &v) == OS999_OK);
    os999_space_destroy(s);
}

static struct { uint32_t handle, addr; int calls; } moved;
static void on_move(void *u, uint32_t handle, uint32_t addr) { (void)u; moved.handle = handle; moved.addr = addr; moved.calls++; }

static void external_memory(void) {
    enum { SIZE = 64 * 1024, GB = 0x400000 };
    uint8_t *ram = calloc(SIZE + 64, 1);
    CHECK(ram != NULL); if (!ram) return;
    for (int i = 0; i < 64; i++) ram[SIZE + i] = 0xee;            /* canary past the region */
    Os999Config c = {.initial_commit = 4096, .grow_chunk = 4096, .ext_base = ram, .ext_size = SIZE,
                     .guest_base = GB, .on_move = on_move};
    size_t spaces0 = os999_live_spaces(), reserved0 = os999_reserved_bytes();
    Os999Space *s = os999_space_create(&c);
    CHECK(s != NULL); if (!s) { free(ram); return; }
    CHECK(os999_reserved_bytes() == reserved0);                   /* nothing mapped by us */
    Os999Heap *h = os999_space_heap(s);
    uint32_t a, b, addr, sz;
    CHECK(os999_new_handle(h, 100, &a) == OS999_OK && os999_new_handle(h, 100, &b) == OS999_OK);
    CHECK(os999_deref(h, a, &addr) == OS999_OK && addr == GB);
    fill(s, addr, 100, 5);
    CHECK(ram[0] == 5 && ram[99] == (uint8_t)(5 + 99));           /* lives in caller memory */
    uint32_t p; CHECK(os999_new_ptr(h, 50, &p) == OS999_OK);
    CHECK(os999_ptr_size(h, p, &sz) == OS999_OK && sz == 50);
    CHECK(os999_ptr_size(h, p + 8, &sz) == OS999_ERR_NILHANDLE);
    CHECK(os999_ptr_size(h, GB - 1, &sz) == OS999_ERR_NILHANDLE);
    CHECK(os999_dispose_handle(h, a) == OS999_OK);
    memset(&moved, 0, sizeof moved);
    CHECK(os999_compact_step(h, SIZE_MAX) > 0);                   /* b slides down, callback fires */
    CHECK(moved.calls == 1 && moved.handle == b && moved.addr == GB);
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(st.largest_alloc == SIZE - (p - GB) - 56);              /* tail after pinned p, incl. uncommitted */
    CHECK(os999_new_handle(h, SIZE, &a) == OS999_ERR_MEMFULL);    /* ceiling is the region */
    CHECK(os999_translate(s, GB + SIZE, 1, 0) == NULL);           /* region end is a fault */
    for (int i = 0; i < 64; i++) CHECK(ram[SIZE + i] == 0xee);    /* never wrote past it */
    os999_space_destroy(s);
    CHECK(os999_live_spaces() == spaces0);
    free(ram);
}

static uint32_t rng = 12345;
static uint32_t rnd(void) { return rng = rng * 1664525u + 1013904223u; }

static void stress(void) {
    Os999Space *s = make(PG, PG, 64 * PG, 0);
    Os999Heap *h = os999_space_heap(s);
    enum { SLOTS = 200 };
    uint32_t hd[SLOTS] = {0}, size[SLOTS] = {0};
    for (int iter = 0; iter < 20000; iter++) {
        unsigned i = rnd() % SLOTS, op = rnd() % 8;
        uint32_t addr;
        if (!hd[i]) {
            uint32_t n = rnd() % 3000;
            if (os999_new_handle(h, n, &hd[i]) == OS999_OK) {
                size[i] = n; os999_deref(h, hd[i], &addr); fill(s, addr, n, (uint8_t)i);
            } else hd[i] = 0;
        } else if (op < 3) {
            os999_deref(h, hd[i], &addr);
            CHECK(verify(s, addr, size[i], (uint8_t)i));
            CHECK(os999_dispose_handle(h, hd[i]) == OS999_OK); hd[i] = 0;
        } else if (op < 6) {
            uint32_t n = rnd() % 3000, keep = n < size[i] ? n : size[i];
            if (os999_set_handle_size(h, hd[i], n) == OS999_OK) {
                size[i] = n; os999_deref(h, hd[i], &addr);
                CHECK(verify(s, addr, keep, (uint8_t)i));
                fill(s, addr, n, (uint8_t)i);
            }
        } else os999_compact_step(h, 512);
        if (iter % 500 == 0) CHECK(os999_heap_check(h));
    }
    for (int i = 0; i < SLOTS; i++) if (hd[i]) {
        uint32_t addr; os999_deref(h, hd[i], &addr);
        CHECK(verify(s, addr, size[i], (uint8_t)i));
        os999_dispose_handle(h, hd[i]);
    }
    Os999Stats st; os999_heap_stats(h, &st);
    CHECK(st.live_blocks == 0 && st.used == 0 && os999_heap_check(h));
    CHECK(os999_space_state(s) == OS999_SPACE_ALIVE);
    os999_space_destroy(s);
}

int main(void) {
    size_t spaces0 = os999_live_spaces(), bytes0 = os999_reserved_bytes();
    basics(); limits(); auto_grow(); boundaries(); handles(); resize_and_lock();
    defragmentation(); external_memory(); fault_containment(); hardware_fault(); stress();
    CHECK(os999_live_spaces() == spaces0 && os999_reserved_bytes() == bytes0);
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("os999 heap: all checks passed");
    return 0;
}
