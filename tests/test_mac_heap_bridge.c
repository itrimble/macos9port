#include "mac_runtime.h"

#include <stdio.h>
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

int main(void) {
    handle_lifecycle(); legacy_managers_can_see_handles(); pointers_and_protection(); dynamic_and_reclaiming();
    compaction_updates_masters(); resize_and_state(); memory_reports();
    guest_cannot_corrupt_bookkeeping(); runtime_owned_data(); legacy_untouched();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("heap bridge: all checks passed");
    return 0;
}
