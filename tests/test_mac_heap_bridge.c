#include "mac_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

#define LO 0x80000u
#define HI 0x100000u

static int call(MacRuntime *rt, uint16_t op, uint32_t a0, uint32_t d0) {
    mac_cpu_set(rt->cpu, MAC_A0, a0);
    mac_cpu_set(rt->cpu, MAC_D0, d0);
    return mac_memory_service(rt, op);
}
static int16_t err(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0x220, 2); }
static uint32_t a0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_A0); }
static uint32_t d0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_D0); }

static int setup(MacRuntime *rt) {
    memset(rt, 0, sizeof *rt);
    rt->cpu = mac_cpu_create(HI);
    if (!rt->cpu) return 0;
    return mac_heap_attach(rt, LO, HI) == 0;
}

static uint32_t new_handle(MacRuntime *rt, uint32_t size, uint8_t seed) {
    CHECK(call(rt, 0xa122, 0, size) == 1);
    uint32_t h = a0(rt);
    if (err(rt) != 0 || !h) return 0;
    uint32_t p = mac_cpu_read(rt->cpu, h, 4);
    for (uint32_t i = 0; i < size; i++) mac_cpu_ram(rt->cpu)[p + i] = (uint8_t)(seed + i);
    return h;
}
static int intact(MacRuntime *rt, uint32_t h, uint32_t size, uint8_t seed) {
    uint32_t p = mac_cpu_read(rt->cpu, h, 4);
    if (p < LO || p + size > HI) return 0;
    for (uint32_t i = 0; i < size; i++) if (mac_cpu_ram(rt->cpu)[p + i] != (uint8_t)(seed + i)) return 0;
    return 1;
}

static void handle_lifecycle(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t h = new_handle(&rt, 100, 3);
    CHECK(h >= LO && h < HI && (h & 3) == 0 && intact(&rt, h, 100, 3));
    CHECK(call(&rt, 0xa025, h, 0) == 1 && err(&rt) == 0 && d0(&rt) == 100);
    CHECK(call(&rt, 0xa023, h, 0) == 1 && err(&rt) == 0);
    CHECK(mac_cpu_read(rt.cpu, h, 4) == 0);                           /* master cleared */
    CHECK(call(&rt, 0xa023, h, 0) == 1 && err(&rt) == -109);          /* double dispose */
    CHECK(call(&rt, 0xa025, h, 0) == 1 && err(&rt) == -109);
    CHECK(call(&rt, 0xa025, LO + 0x4000, 0) == 1 && err(&rt) == -109); /* never-allocated slot */
    CHECK(call(&rt, 0xa025, LO + 6, 0) == 1 && err(&rt) == -109);      /* misaligned */
    CHECK(!mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static const MacHandle *record(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == h) return &rt->handles[i];
    return NULL;
}

static void legacy_managers_can_see_handles(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t a = new_handle(&rt, 100, 1), b = new_handle(&rt, 100, 2), x = new_handle(&rt, 100, 3);
    const MacHandle *m = record(&rt, a);
    CHECK(m && m->pointer == mac_cpu_read(rt.cpu, a, 4) && m->size == 100 && !m->resource && !m->type && !m->loaded_code);
    CHECK(call(&rt, 0xa024, a, 24) == 1 && err(&rt) == 0);               /* resize */
    m = record(&rt, a); CHECK(m && m->size == 24 && m->pointer == mac_cpu_read(rt.cpu, a, 4));
    CHECK(call(&rt, 0xa029, b, 0) == 1);                                 /* lock */
    m = record(&rt, b); CHECK(m && (m->state & 0x80));
    CHECK(call(&rt, 0xa023, x, 0) == 1 && err(&rt) == 0);                /* dispose clears the record */
    CHECK(record(&rt, x) == NULL);
    CHECK(call(&rt, 0xa04c, 0, 0) == 1);                                 /* compaction moves a... */
    m = record(&rt, a); CHECK(m && m->pointer == mac_cpu_read(rt.cpu, a, 4));   /* ...record follows */
    m = record(&rt, b); CHECK(m && m->pointer == mac_cpu_read(rt.cpu, b, 4));
    mac_runtime_destroy(&rt);
}

static void pointers_and_protection(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t h = new_handle(&rt, 16, 1);                  /* first master == start of a master page */
    CHECK(call(&rt, 0xa11e, 0, 40) == 1 && err(&rt) == 0);
    uint32_t p = a0(&rt);
    CHECK(p >= LO && p < HI);
    CHECK(call(&rt, 0xa021, p, 0) == 1 && d0(&rt) == 40 && err(&rt) == 0);
    CHECK(call(&rt, 0xa021, p + 8, 0) == 1 && err(&rt) == -109);       /* interior pointer */
    CHECK(call(&rt, 0xa01f, h, 0) == 1 && err(&rt) == -109);           /* page base is not a pointer */
    CHECK(call(&rt, 0xa01f, mac_cpu_read(rt.cpu, h, 4), 0) == 1 && err(&rt) == -109); /* data of a handle */
    CHECK(intact(&rt, h, 16, 1));                                       /* handle survived */
    {   /* legacy managers see pointers as {handle 0, pointer, size} */
        size_t found = 0;
        for (size_t i = 0; i < rt.handle_count; i++)
            if (!rt.handles[i].handle && rt.handles[i].pointer == p) { found++; CHECK(rt.handles[i].size == 40); }
        CHECK(found == 1);
    }
    CHECK(call(&rt, 0xa01f, p, 0) == 1 && err(&rt) == 0);
    for (size_t i = 0; i < rt.handle_count; i++) CHECK(!(rt.handles[i].pointer == p));   /* record cleared */
    CHECK(call(&rt, 0xa01f, p, 0) == 1 && err(&rt) == -109);
    CHECK(call(&rt, 0xa11e, 0, 0x7fffffff) == 1 && err(&rt) == -108 && a0(&rt) == 0);
    CHECK(!mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static void dynamic_and_reclaiming(void) {
    MacRuntime rt; CHECK(setup(&rt));
    enum { N = 3000 };                                    /* far past the legacy 512-entry table */
    static uint32_t hs[N];
    for (int i = 0; i < N; i++) {
        hs[i] = new_handle(&rt, 16, (uint8_t)i);
        if (!hs[i]) { CHECK(hs[i] != 0); break; }
    }
    CHECK(rt.handle_count <= MAC_RUNTIME_HANDLES);        /* mirror table is capped; the heap is not */
    for (int i = 0; i < N; i++) CHECK(intact(&rt, hs[i], 16, (uint8_t)i));
    for (int i = 0; i < N; i += 2) CHECK(call(&rt, 0xa023, hs[i], 0) == 1 && err(&rt) == 0);
    for (int i = 1; i < N; i += 2) CHECK(intact(&rt, hs[i], 16, (uint8_t)i));
    for (int i = 0; i < N; i += 2) {                       /* freed masters/space are reused */
        hs[i] = new_handle(&rt, 16, (uint8_t)(i + 9));
        CHECK(hs[i] != 0);
    }
    for (int i = 0; i < N; i++) CHECK(call(&rt, 0xa023, hs[i], 0) == 1 && err(&rt) == 0);

    /* Exhaustion is reported, and freeing makes room again (the bump heap never did). */
    uint32_t big[64]; int n = 0;
    while (n < 64 && (call(&rt, 0xa11e, 0, 0x10000) == 1) && err(&rt) == 0) big[n++] = a0(&rt);
    CHECK(n > 0 && n < 64 && err(&rt) == -108 && a0(&rt) == 0);
    CHECK(call(&rt, 0xa01f, big[0], 0) == 1 && err(&rt) == 0);
    CHECK(call(&rt, 0xa11e, 0, 0x10000) == 1 && err(&rt) == 0 && a0(&rt) == big[0]);
    mac_runtime_destroy(&rt);
}

static void compaction_updates_masters(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t a = new_handle(&rt, 200, 10), b = new_handle(&rt, 200, 20);
    uint32_t x = new_handle(&rt, 200, 40), c = new_handle(&rt, 200, 30);
    uint32_t b_addr = mac_cpu_read(rt.cpu, b, 4), c_addr = mac_cpu_read(rt.cpu, c, 4);
    CHECK(call(&rt, 0xa023, a, 0) == 1 && call(&rt, 0xa023, x, 0) == 1);
    CHECK(call(&rt, 0xa029, b, 0) == 1 && err(&rt) == 0);              /* HLock b */
    CHECK(call(&rt, 0xa04c, 0, 0) == 1 && err(&rt) == 0 && d0(&rt) > 0); /* CompactMem */
    CHECK(mac_cpu_read(rt.cpu, b, 4) == b_addr && intact(&rt, b, 200, 20));   /* pinned in place */
    CHECK(mac_cpu_read(rt.cpu, c, 4) < c_addr && intact(&rt, c, 200, 30));    /* slid down, master followed */
    CHECK(call(&rt, 0xa02a, b, 0) == 1);                                        /* HUnlock */
    CHECK(call(&rt, 0xa04c, 0, 0) == 1);
    CHECK(mac_cpu_read(rt.cpu, b, 4) < b_addr && intact(&rt, b, 200, 20) && intact(&rt, c, 200, 30));
    mac_runtime_destroy(&rt);
}

static void resize_and_state(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t a = new_handle(&rt, 64, 7), b = new_handle(&rt, 64, 8);
    uint32_t old = mac_cpu_read(rt.cpu, a, 4);
    CHECK(call(&rt, 0xa069, a, 0) == 1 && d0(&rt) == 0);                /* HGetState */
    CHECK(call(&rt, 0xa029, a, 0) == 1 && call(&rt, 0xa069, a, 0) == 1 && d0(&rt) == 0x80);
    CHECK(call(&rt, 0xa024, a, 4096) == 1 && err(&rt) == -117);        /* locked and boxed in */
    CHECK(mac_cpu_read(rt.cpu, a, 4) == old && intact(&rt, a, 64, 7));
    CHECK(call(&rt, 0xa06a, a, 0x40) == 1 && err(&rt) == 0);            /* HSetState: unlocked */
    CHECK(call(&rt, 0xa024, a, 4096) == 1 && err(&rt) == 0);            /* relocates */
    CHECK(mac_cpu_read(rt.cpu, a, 4) != old && intact(&rt, a, 64, 7));  /* master follows the block */
    CHECK(call(&rt, 0xa025, a, 0) == 1 && d0(&rt) == 4096);
    CHECK(call(&rt, 0xa024, a, 0x7fffffff) == 1 && err(&rt) == -108);   /* failure leaves it intact */
    CHECK(call(&rt, 0xa025, a, 0) == 1 && d0(&rt) == 4096 && intact(&rt, a, 64, 7));
    CHECK(call(&rt, 0xa024, a, 8) == 1 && err(&rt) == 0 && intact(&rt, a, 8, 7)); /* shrink */
    CHECK(call(&rt, 0xa064, a, 0) == 1 && err(&rt) == 0);               /* MoveHHi accepted */
    CHECK(intact(&rt, b, 64, 8));
    mac_runtime_destroy(&rt);
}

static void memory_reports(void) {
    MacRuntime rt; CHECK(setup(&rt));
    CHECK(call(&rt, 0xa01c, 0, 0) == 1); uint32_t free0 = d0(&rt);
    CHECK(free0 > 0 && free0 <= HI - LO);
    uint32_t h = new_handle(&rt, 10000, 1);
    CHECK(call(&rt, 0xa01c, 0, 0) == 1 && d0(&rt) < free0);
    CHECK(call(&rt, 0xa061, 0, 0) == 1 && d0(&rt) > 0 && d0(&rt) <= free0);   /* MaxBlock */
    {uint32_t free_now, block; CHECK(call(&rt, 0xa01c, 0, 0) == 1); free_now = d0(&rt); CHECK(call(&rt, 0xa061, 0, 0) == 1); block = d0(&rt);
     mac_cpu_write(rt.cpu, 0x220, 2, 0xfffc);                                                         /* MemError = -4: PurgeSpace must not touch it */
     CHECK(call(&rt, 0xa162, 0, 0) == 1 && d0(&rt) == free_now && mac_cpu_get(rt.cpu, MAC_A0) == block && mac_cpu_read(rt.cpu, 0x220, 2) == 0xfffc);
     CHECK(call(&rt, 0xa562, 0, 0) == 0);}                                                            /* the system-zone form is not modelled */
    CHECK(call(&rt, 0xa040, 0, 1000) == 1 && err(&rt) == 0);                   /* ReserveMem */
    CHECK(call(&rt, 0xa040, 0, HI) == 1 && err(&rt) == -108);
    CHECK(call(&rt, 0xa04d, 0, 1000) == 1 && err(&rt) == 0);                   /* PurgeMem */
    CHECK(call(&rt, 0xa02d, 0x1234, 0) == 1 && err(&rt) == 0);                 /* SetApplLimit: no cap */
    call(&rt, 0xa023, h, 0);
    CHECK(call(&rt, 0xa01c, 0, 0) == 1 && d0(&rt) == free0 - 1024);   /* only the master page remains */
    mac_runtime_destroy(&rt);
}

static void guest_cannot_corrupt_bookkeeping(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t a = new_handle(&rt, 32, 1), b = new_handle(&rt, 32, 2);
    mac_cpu_write(rt.cpu, a, 4, 0xffffffff);                           /* guest trashes a master */
    CHECK(call(&rt, 0xa025, a, 0) == 1 && err(&rt) == 0 && d0(&rt) == 32);   /* size is not in guest memory */
    CHECK(call(&rt, 0xa023, a, 0) == 1 && err(&rt) == 0);
    CHECK(intact(&rt, b, 32, 2));
    memset(mac_cpu_ram(rt.cpu) + LO, 0xff, HI - LO);                    /* ...then scribbles over the whole heap */
    CHECK(call(&rt, 0xa023, b, 0) == 1 && err(&rt) == 0);              /* frees still work */
    uint32_t c = new_handle(&rt, 64, 0);
    CHECK(c != 0);
    uint32_t p = mac_cpu_read(rt.cpu, c, 4);
    for (int i = 0; i < 64; i++) CHECK(mac_cpu_ram(rt.cpu)[p + i] == i);  /* (seed 0 pattern written by helper) */
    CHECK(call(&rt, 0xa11e, 0, 64) == 1 && err(&rt) == 0);
    uint32_t q = a0(&rt);
    for (int i = 0; i < 64; i++) CHECK(mac_cpu_ram(rt.cpu)[q + i] == 0);  /* fresh memory is zeroed, not stale */
    CHECK(!mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
}

static void runtime_owned_data(void) {
    MacRuntime rt; CHECK(setup(&rt));
    uint32_t p = mac_runtime_alloc_data(&rt, 300);
    CHECK(mac_heap_owns(&rt, p));
    mac_cpu_ram(rt.cpu)[p] = 0x5a;
    CHECK(call(&rt, 0xa021, p, 0) == 1 && err(&rt) == -109);          /* guest can't size it */
    CHECK(call(&rt, 0xa01f, p, 0) == 1 && err(&rt) == -109);          /* ...or free it */
    CHECK(mac_cpu_ram(rt.cpu)[p] == 0x5a);
    {   /* a legacy resource-style record whose body lives in the heap: MoveHHi is accepted as a no-op */
        rt.heap_next = 0x1000; rt.heap_limit = 0x8000;                  /* bump area holds masters, as in a real runtime */
        uint32_t master = mac_runtime_alloc(&rt, 4);
        CHECK(master == 0x1000 && !mac_heap_owns(&rt, master));
        mac_cpu_write(rt.cpu, master, 4, p);
        rt.handles[0] = (MacHandle){.handle = master, .pointer = p, .size = 300, .type = 0x54455354, .resource = 1};
        rt.handle_count = 1;
        CHECK(call(&rt, 0xa064, master, 0) == 1 && err(&rt) == 0 && !mac_cpu_halted(rt.cpu));
        CHECK(mac_cpu_read(rt.cpu, master, 4) == p);
        memset(&rt.handles[0], 0, sizeof rt.handles[0]); rt.handle_count = 0;
    }
    mac_runtime_free_data(&rt, p);
    CHECK(mac_cpu_ram(rt.cpu)[p] == 0);                                 /* freed storage is wiped */
    mac_runtime_free_data(&rt, p);                                      /* double free is harmless */
    mac_runtime_free_data(&rt, 0x1234);                                 /* foreign address ignored */
    mac_runtime_destroy(&rt);

    MacRuntime bare = {0};                                              /* no heap: falls back to bump */
    bare.cpu = mac_cpu_create(65536); bare.heap_next = 0x1000; bare.heap_limit = 0x8000;
    uint32_t q = mac_runtime_alloc_data(&bare, 64);
    CHECK(q == 0x1000 && bare.heap_next == 0x1040);
    mac_runtime_free_data(&bare, q);                                    /* no-op: bump never reclaims */
    CHECK(bare.heap_next == 0x1040);
    mac_runtime_destroy(&bare);
}

static void legacy_untouched(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(65536);
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000;
    CHECK(call(&rt, 0xa122, 0, 16) == 1 && err(&rt) == 0 && rt.handle_count == 1);  /* bump path */
    CHECK(call(&rt, 0xa04c, 0, 0) == 1);
    CHECK(mac_heap_owns(&rt, 0x2000) == 0);
    mac_heap_detach(&rt);                                                            /* no-op */
    mac_runtime_destroy(&rt);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

enum { DATA = 256, MAP = 384, ZERO = DATA + 4, SEG = DATA + 32, TOTAL = MAP + 62 };

/* A minimal one-segment CODE fork (CODE 0 jump table and CODE 1), built in place. */
static void fill_fork(uint8_t *b) {
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
}

/* Protected-heap bridge across every partition size, with guard redzones on and off. */
static int16_t memerr(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0x220, 2); }
static uint32_t get_a0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_A0); }
static uint32_t get_d0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_D0); }

static void bridge_matrix_one(const MacResourceFork *f, int mb, int guard, uint32_t a5, uint32_t top, uint32_t lo) {
    char err[240], mbs[8];
    snprintf(mbs, sizeof mbs, "%d", mb);
    setenv("OS999_GUEST_HEAP_MB", mbs, 1);
    setenv("OS999_GUEST_GUARD", guard ? "1" : "0", 1);
    MacRuntime rt;
    CHECK(mac_runtime_init(&rt, f, err, sizeof err) == 0);
    CHECK(rt.a5 == a5 && rt.guard_allocs == guard);
    CHECK(rt.heap_bridge != NULL);                                  /* attaches for every size and guard setting */
    if (!rt.heap_bridge) { mac_runtime_destroy(&rt); return; }
    CHECK(rt.heap_limit == lo && mac_cpu_read(rt.cpu, 0x130, 4) == top);
    CHECK(!mac_heap_owns(&rt, lo - 4) && mac_heap_owns(&rt, lo) && !mac_heap_owns(&rt, top));
    const uint32_t rz = guard ? 16u : 0u;
    uint8_t *ram = mac_cpu_ram(rt.cpu);

    /* Handle: allocate, fill, resize up and down, MoveHHi, redzone present and moves with compaction. */
    CHECK(call(&rt, 0xa022, 0, 100) == 1 && memerr(&rt) == 0);
    uint32_t h = get_a0(&rt), p = mac_cpu_read(rt.cpu, h, 4);
    CHECK(mac_heap_owns(&rt, h) && p >= lo && p + 100 + rz <= top);
    memset(ram + p, 0x5a, 100);
    for (uint32_t i = 0; i < rz; i++) CHECK(ram[p + 100 + i] == 0xa5);
    CHECK(call(&rt, 0xa025, h, 0) == 1 && get_d0(&rt) == 100);
    CHECK(call(&rt, 0xa024, h, 300) == 1 && memerr(&rt) == 0);
    p = mac_cpu_read(rt.cpu, h, 4);
    CHECK(ram[p] == 0x5a && ram[p + 99] == 0x5a && ram[p + 100] == 0 && ram[p + 299] == 0);  /* grown space is zero, not stale redzone */
    for (uint32_t i = 0; i < rz; i++) CHECK(ram[p + 300 + i] == 0xa5);
    CHECK(call(&rt, 0xa025, h, 0) == 1 && get_d0(&rt) == 300);
    CHECK(call(&rt, 0xa024, h, 50) == 1 && memerr(&rt) == 0);
    p = mac_cpu_read(rt.cpu, h, 4);
    for (uint32_t i = 0; i < rz; i++) CHECK(ram[p + 50 + i] == 0xa5);
    CHECK(call(&rt, 0xa064, h, 0) == 1 && memerr(&rt) == 0 && !mac_cpu_halted(rt.cpu));

    /* Compaction slides a block down; its redzone must go with it and still verify. */
    CHECK(call(&rt, 0xa022, 0, 24) == 1); uint32_t h2 = get_a0(&rt);
    CHECK(call(&rt, 0xa022, 0, 40) == 1); uint32_t h3 = get_a0(&rt);
    CHECK(call(&rt, 0xa023, h2, 0) == 1 && memerr(&rt) == 0);
    CHECK(call(&rt, 0xa04c, 0, 0) == 1);
    uint32_t p3 = mac_cpu_read(rt.cpu, h3, 4);
    for (uint32_t i = 0; i < rz; i++) CHECK(ram[p3 + 40 + i] == 0xa5);
    CHECK(call(&rt, 0xa023, h3, 0) == 1 && memerr(&rt) == 0 && !mac_cpu_halted(rt.cpu));

    /* Pointers: size reports exclude the redzone; huge requests fail cleanly (no 32-bit wrap with the redzone added). */
    CHECK(call(&rt, 0xa01e, 0, 13) == 1 && memerr(&rt) == 0);
    uint32_t q = get_a0(&rt);
    CHECK(q >= lo && q + 13 + rz <= top);
    CHECK(call(&rt, 0xa021, q, 0) == 1 && get_d0(&rt) == 13);
    CHECK(call(&rt, 0xa01e, 0, UINT32_MAX - 8) == 1 && memerr(&rt) == -108 && get_a0(&rt) == 0);
    CHECK(call(&rt, 0xa022, 0, UINT32_MAX - 8) == 1 && memerr(&rt) == -108 && get_a0(&rt) == 0);
    CHECK(call(&rt, 0xa01f, q, 0) == 1 && memerr(&rt) == 0);

    /* Bounds: fill the heap with 4 KB pointers; every block stays inside [lo, top) and the heap is mostly used. */
    uint32_t n = 0, highest = 0, *blocks = malloc(sizeof *blocks * ((top - lo) / 4096 + 8));
    CHECK(blocks != NULL);
    for (;;) {
        CHECK(call(&rt, 0xa01e, 0, 4096) == 1);
        if (memerr(&rt) != 0) { CHECK(memerr(&rt) == -108 && get_a0(&rt) == 0); break; }
        uint32_t a = get_a0(&rt);
        CHECK(a >= lo && a + 4096 + rz <= top);
        if (a + 4096 + rz > highest) highest = a + 4096 + rz;
        blocks[n++] = a;
        if (n > (top - lo) / 4096) { CHECK(0); break; }
    }
    CHECK(n > ((top - lo) / 4096) * 3 / 4);
    CHECK(highest <= top);
    if (guard && n) {                                   /* underflow: a write just below block k lands in block k-1's redzone */
        uint32_t k = n / 2;
        CHECK(blocks[k] > blocks[k - 1]);
        ram[blocks[k] - 1] ^= 0xff;
        CHECK(blocks[k] - blocks[k - 1] == 4096 + 16);  /* adjacent: the byte below is the last redzone byte of k-1 */
        CHECK(call(&rt, 0xa01f, blocks[k - 1], 0) == -1 && mac_cpu_halted(rt.cpu));
        CHECK(strstr(mac_cpu_error(rt.cpu), "redzone") != NULL);
    } else if (n) {                                      /* guard off: no redzone, so no corruption check either */
        ram[blocks[n / 2] + 4096] ^= 0xff;
        CHECK(call(&rt, 0xa01f, blocks[n / 2], 0) == 1 && memerr(&rt) == 0);
    }
    free(blocks);
    mac_runtime_destroy(&rt);

    /* Overflow past the end of a pointer or handle is caught by Dispose, SetHandleSize and MoveHHi, before any bridge free. */
    if (guard) {
        for (unsigned which = 0; which < 4; which++) {
            CHECK(mac_runtime_init(&rt, f, err, sizeof err) == 0 && rt.heap_bridge != NULL);
            ram = mac_cpu_ram(rt.cpu);
            uint32_t blk, a0v, size = which == 3 ? 13 : 20;
            CHECK(call(&rt, which == 3 ? 0xa01e : 0xa022, 0, size) == 1 && memerr(&rt) == 0);
            a0v = get_a0(&rt);
            blk = which == 3 ? a0v : mac_cpu_read(rt.cpu, a0v, 4);
            ram[blk + size] = 0x42;
            uint8_t master[4] = {0}; if (which != 3) memcpy(master, ram + a0v, 4);
            uint16_t op = which == 0 ? 0xa023 : which == 1 ? 0xa024 : which == 2 ? 0xa064 : 0xa01f;
            CHECK(call(&rt, op, a0v, 64) == -1 && mac_cpu_halted(rt.cpu));
            CHECK(strstr(mac_cpu_error(rt.cpu), "redzone") != NULL);
            CHECK(ram[blk + size] == 0x42);                         /* not restamped */
            if (which != 3) CHECK(!memcmp(master, ram + a0v, 4));   /* master pointer untouched: nothing was freed or moved */
            mac_runtime_destroy(&rt);
        }
    }
}

static void bridge_partition_matrix(void) {
    uint8_t b[TOTAL] = {0};
    fill_fork(b);
    MacResourceFork f;
    char err[240];
    CHECK(mac_resource_open(&f, b, TOTAL, err, sizeof err) == 0);
    mac_runtime_set_memory_policy(0, 0);
    /* Half of the heap span (rounded to 64 KiB) stays with the bump allocator; the other half is protected. */
    static const struct { int mb; uint32_t a5, top, lo; } cfg[] = {
        {2, 0x200000, 0x1c0000, 0x160000}, {4, 0x400000, 0x300000, 0x200000},
        {8, 0x800000, 0x700000, 0x400000}, {12, 0xc00000, 0xb00000, 0x600000},
    };
    for (unsigned i = 0; i < 4; i++)
        for (int guard = 0; guard < 2; guard++) bridge_matrix_one(&f, cfg[i].mb, guard, cfg[i].a5, cfg[i].top, cfg[i].lo);
    unsetenv("OS999_GUEST_HEAP_MB");
    unsetenv("OS999_GUEST_GUARD");
}

int main(void) {
    handle_lifecycle(); legacy_managers_can_see_handles(); pointers_and_protection(); dynamic_and_reclaiming();
    compaction_updates_masters(); resize_and_state(); memory_reports();
    bridge_partition_matrix();
    guest_cannot_corrupt_bookkeeping(); runtime_owned_data(); legacy_untouched();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("heap bridge: all checks passed");
    return 0;
}
