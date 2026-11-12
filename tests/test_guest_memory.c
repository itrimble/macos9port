/* Guest machine memory: 256 MB default, 32-bit addressing, the 24-bit fallback, boundaries and failure handling.
 * Headless; the 256 MB are reserved lazily by the host, so this test commits only what it touches. */
#include "guest_memory.h"
#include "mac_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)

#define MB 0x100000u
#define RAM256 (256u * MB)

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v); }

static void clear_env(void) {
    unsetenv("OS999_GUEST_RAM_MB"); unsetenv("OS999_GUEST_ADDRESSING");
    unsetenv("OS999_GUEST_HEAP_MB"); unsetenv("OS999_GUEST_GUARD");
    mac_runtime_set_memory_policy(0, 0);
    mac_runtime_set_machine_policy(0, 0);
}

/* ---- configuration: defaults, precedence, clamping ---- */
static void config_rules(void) {
    GuestMemoryConfig c = guest_memory_resolve(0, 0, NULL, NULL);       /* nothing requested: today's machine */
    CHECK(c.ram_mb == 16 && c.addr32 == 0 && !c.clamped);
    c = guest_memory_resolve(0, 0, NULL, "32");                         /* 32-bit alone asks for 256 MB */
    CHECK(c.ram_mb == 256 && c.addr32 == 1);
    c = guest_memory_resolve(0, 0, "256", NULL);                        /* a big size implies 32-bit */
    CHECK(c.ram_mb == 256 && c.addr32 == 1);
    c = guest_memory_resolve(64, 0, NULL, NULL);                        /* saved policy */
    CHECK(c.ram_mb == 64 && c.addr32 == 1);
    c = guest_memory_resolve(64, 0, "128", NULL);                       /* environment beats policy */
    CHECK(c.ram_mb == 128);
    c = guest_memory_resolve(64, 24, NULL, "32");                       /* environment mode beats policy mode */
    CHECK(c.addr32 == 1 && c.ram_mb == 64);
    c = guest_memory_resolve(0, 0, "4", "32");                          /* too small: clamped to the minimum */
    CHECK(c.ram_mb == 16 && c.clamped);
    c = guest_memory_resolve(0, 0, "99999", NULL);                      /* too large: clamped to the ceiling */
    CHECK(c.ram_mb == 1024 && c.clamped && c.addr32);
    c = guest_memory_resolve(0, 0, "banana", "32");                     /* invalid size: as unset, flagged */
    CHECK(c.ram_mb == 256 && c.clamped);
    c = guest_memory_resolve(-5, 0, NULL, NULL);                        /* negative saved value: ignored */
    CHECK(c.ram_mb == 16 && c.addr32 == 0);
    c = guest_memory_resolve(0, 0, "256", "24");                        /* 24 bits cannot reach more than 16 MB */
    CHECK(c.addr32 == 0 && c.ram_mb == 16);
    c = guest_memory_resolve(0, 24, "512", NULL);
    CHECK(c.addr32 == 0 && c.ram_mb == 16);
    c = guest_memory_resolve(0, 0, NULL, "16");                         /* unknown mode: default, flagged */
    CHECK(c.addr32 == 0 && c.clamped);
}

/* ---- CPU: reservation, address masks, boundaries ---- */
static MacCpu *cpu256(void) {
    MacCpu *cpu = mac_cpu_create_mode(RAM256, MAC_CPU_68020, MAC_ADDR_32);
    CHECK(cpu != NULL);
    return cpu;
}

static void cpu_creation_rules(void) {
    MacCpu *cpu = cpu256();
    if (!cpu) return;
    CHECK(mac_cpu_ram_size(cpu) == RAM256 && mac_cpu_addr_mode(cpu) == MAC_ADDR_32);
    CHECK(mac_cpu_addr_mask(cpu) == 0xffffffffu);
    CHECK(mac_cpu_get(cpu, MAC_A7) == RAM256);                           /* SP starts at the top */
    mac_cpu_destroy(cpu);

    CHECK(mac_cpu_create_mode(RAM256, MAC_CPU_68000, MAC_ADDR_32) == NULL);   /* a 68000 has a 24-bit bus */
    CHECK(mac_cpu_create_model(RAM256, MAC_CPU_68000) == NULL);
    CHECK(mac_cpu_create_mode(RAM256, MAC_CPU_68020, MAC_ADDR_24) == NULL);   /* 24 bits cannot reach it */
    CHECK(mac_cpu_create_mode((size_t)2u << 30, MAC_CPU_68020, MAC_ADDR_32) == NULL); /* over the host ceiling */
    CHECK(mac_cpu_create_error()[0] != 0);
    CHECK(mac_cpu_create_mode(RAM256 + 1, MAC_CPU_68020, MAC_ADDR_32) == NULL);       /* odd size */

    cpu = mac_cpu_create_model(RAM256, MAC_CPU_68020);                   /* model-only call picks 32-bit above 16 MB */
    CHECK(cpu && mac_cpu_addr_mode(cpu) == MAC_ADDR_32);
    mac_cpu_destroy(cpu);
    cpu = mac_cpu_create_model(MAC_RUNTIME_RAM, MAC_CPU_68020);          /* and 24-bit at 16 MB, as before */
    CHECK(cpu && mac_cpu_addr_mode(cpu) == MAC_ADDR_24 && mac_cpu_addr_mask(cpu) == 0xffffffu);
    mac_cpu_destroy(cpu);
}

static void cpu_helper_boundaries(void) {
    MacCpu *cpu = cpu256();
    if (!cpu) return;
    CHECK(mac_cpu_write(cpu, RAM256 - 1, 1, 0xa5) == 0 && mac_cpu_read(cpu, RAM256 - 1, 1) == 0xa5);
    CHECK(mac_cpu_write(cpu, RAM256 - 4, 4, 0xdeadbeef) == 0 && mac_cpu_read(cpu, RAM256 - 4, 4) == 0xdeadbeef);
    CHECK(mac_cpu_ram(cpu)[RAM256 - 1] == 0xef);                         /* big-endian, last byte lands at the top */
    CHECK(!mac_cpu_halted(cpu));
    CHECK(mac_cpu_read(cpu, RAM256, 1) == 0 && mac_cpu_halted(cpu));     /* one past the last valid byte */
    CHECK(strstr(mac_cpu_error(cpu), "invalid 1-byte") != NULL);
    mac_cpu_destroy(cpu);

    cpu = cpu256();
    CHECK(cpu && mac_cpu_write(cpu, RAM256 - 2, 4, 1) < 0 && mac_cpu_halted(cpu));  /* straddles the end */
    mac_cpu_destroy(cpu);
}

/* Run 68020 code through the real bus: absolute long addresses above 16 MB. */
static void cpu_bus_boundaries(void) {
    static const uint8_t last_byte[] = {                                  /* move.b #$5A,$0FFFFFFF.l ; move.b $0FFFFFFF.l,d1 */
        0x13, 0xfc, 0x00, 0x5a, 0x0f, 0xff, 0xff, 0xff, 0x12, 0x39, 0x0f, 0xff, 0xff, 0xff};
    MacCpu *cpu = cpu256();
    if (!cpu) return;
    memcpy(mac_cpu_ram(cpu) + 0x100, last_byte, sizeof last_byte);
    mac_cpu_set(cpu, MAC_PC, 0x100);
    CHECK(mac_cpu_run(cpu, 2) == 2 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_ram(cpu)[RAM256 - 1] == 0x5a && (mac_cpu_get(cpu, MAC_D1) & 0xff) == 0x5a);
    mac_cpu_destroy(cpu);

    static const uint8_t past_end[] = {0x13, 0xc0, 0x10, 0x00, 0x00, 0x00};   /* move.b d0,$10000000.l */
    cpu = cpu256();
    if (!cpu) return;
    memcpy(mac_cpu_ram(cpu) + 0x100, past_end, sizeof past_end);
    mac_cpu_set(cpu, MAC_PC, 0x100); mac_cpu_set(cpu, MAC_D0, 0x77);
    mac_cpu_run(cpu, 1);
    CHECK(mac_cpu_halted(cpu) && strstr(mac_cpu_error(cpu), "invalid 1-byte memory write") != NULL);
    mac_cpu_destroy(cpu);

    static const uint8_t straddle[] = {0x23, 0xc0, 0x0f, 0xff, 0xff, 0xfe};   /* move.l d0,$0FFFFFFE.l: two bytes past */
    cpu = cpu256();
    if (!cpu) return;
    memcpy(mac_cpu_ram(cpu) + 0x100, straddle, sizeof straddle);
    mac_cpu_set(cpu, MAC_PC, 0x100); mac_cpu_set(cpu, MAC_D0, 0x11223344);
    mac_cpu_run(cpu, 1);
    CHECK(mac_cpu_halted(cpu));
    CHECK(mac_cpu_ram(cpu)[RAM256 - 2] == 0 && mac_cpu_ram(cpu)[RAM256 - 1] == 0);   /* no partial write */
    mac_cpu_destroy(cpu);

    /* Code above 16 MB executes and the PC keeps its high bits (it was masked to 24 bits before). */
    static const uint8_t nops[] = {0x4e, 0x71, 0x4e, 0x71, 0x70, 0x09};       /* nop ; nop ; moveq #9,d0 */
    cpu = cpu256();
    if (!cpu) return;
    memcpy(mac_cpu_ram(cpu) + 0x0c000000, nops, sizeof nops);
    mac_cpu_set(cpu, MAC_PC, 0x0c000000);
    CHECK(mac_cpu_run(cpu, 3) == 3 && !mac_cpu_halted(cpu));
    CHECK(mac_cpu_get(cpu, MAC_PC) == 0x0c000006 && mac_cpu_get(cpu, MAC_D0) == 9);
    mac_cpu_destroy(cpu);
}

/* 24-bit mode wraps the high byte away; 32-bit mode faults on the same address and says why. */
static void addressing_fallback(void) {
    static const uint8_t dirty[] = {0x13, 0xfc, 0x00, 0x5a, 0xa0, 0x00, 0x00, 0x10};  /* move.b #$5A,$A0000010.l */
    MacCpu *c24 = mac_cpu_create_mode(MAC_RUNTIME_RAM, MAC_CPU_68020, MAC_ADDR_24);
    CHECK(c24 != NULL);
    if (c24) {
        memcpy(mac_cpu_ram(c24) + 0x100, dirty, sizeof dirty);
        mac_cpu_set(c24, MAC_PC, 0x100);
        CHECK(mac_cpu_run(c24, 1) == 1 && !mac_cpu_halted(c24));
        CHECK(mac_cpu_ram(c24)[0x10] == 0x5a);                            /* the flag byte is ignored, as on a Mac II */
        mac_cpu_destroy(c24);
    }
    MacCpu *c32 = cpu256();
    if (c32) {
        memcpy(mac_cpu_ram(c32) + 0x100, dirty, sizeof dirty);
        mac_cpu_set(c32, MAC_PC, 0x100);
        mac_cpu_run(c32, 1);
        CHECK(mac_cpu_halted(c32) && strstr(mac_cpu_error(c32), "24-bit-dirty") != NULL);
        CHECK(mac_cpu_ram(c32)[0x10] == 0);
        mac_cpu_destroy(c32);
    }
}

static void host_allocation_failure(void) {
    mac_cpu_test_fail_next_alloc(1);
    CHECK(mac_cpu_create_mode(RAM256, MAC_CPU_68020, MAC_ADDR_32) == NULL);
    CHECK(strstr(mac_cpu_create_error(), "Could not reserve 256 MB") != NULL);
    MacCpu *cpu = cpu256();                                              /* the failure was one-shot */
    CHECK(cpu != NULL);
    mac_cpu_destroy(cpu);
}

/* ---- layout (pure) ---- */
static void partition_layout(void) {
    MacPartitionLayout l;
    mac_partition_layout(8, 0, RAM256, 1, 0x1000, &l);                   /* default: the original low layout */
    CHECK(!l.extended && l.a5 == 0x800000 && l.heap_limit == 0x700000);
    mac_partition_layout(12, 0, RAM256, 1, 0x1000, &l);
    CHECK(!l.extended && l.a5 == 0xc00000 && l.heap_limit == 0xb00000);
    mac_partition_layout(2, 100 * MB, RAM256, 1, 0x1000, &l);            /* explicit small choice is honoured */
    CHECK(!l.extended && l.a5 == 0x200000);
    mac_partition_layout(8, 6 * MB, RAM256, 1, 0x1000, &l);              /* SIZE within the default: unchanged */
    CHECK(!l.extended && l.a5 == 0x800000);
    mac_partition_layout(8, 40 * MB, RAM256, 1, 0x1000, &l);             /* SIZE asks for more: extended partition */
    CHECK(l.extended && l.heap_mb == 40 && l.base == 0x1000000 && l.a5 == 0x1000000 + 39 * MB);
    mac_partition_layout(64, 0, RAM256, 1, 0x1000, &l);
    CHECK(l.extended && l.heap_mb == 64 && l.a5 == 0x1000000 + 63 * MB && l.heap_limit == 0x400000);
    mac_partition_layout(1000, 0, RAM256, 1, 0x1000, &l);                /* too big: clamped to the room above 16 MB */
    CHECK(l.extended && l.heap_mb == 240 && l.a5 == RAM256 - MB);
    mac_partition_layout(64, 0, RAM256, 0, 0x1000, &l);                  /* 24-bit mode: no extended partitions */
    CHECK(!l.extended && l.a5 == 0x800000);
    mac_partition_layout(64, 90 * MB, 16 * MB, 1, 0x1000, &l);           /* 16 MB machine: nowhere to put it */
    CHECK(!l.extended && l.a5 == 0x800000);
    mac_partition_layout(8, 500 * MB, RAM256, 1, 0x1000, &l);            /* absurd SIZE: ignored */
    CHECK(!l.extended && l.a5 == 0x800000);
    mac_partition_layout(7, 0, RAM256, 1, 0x1000, &l);                   /* invalid choice: the default */
    CHECK(!l.extended && l.a5 == 0x800000);
}

/* ---- runtime: a minimal one-segment CODE fork ---- */
enum { DATA = 256, MAP = 384, ZERO = DATA + 4, SEG = DATA + 32, TOTAL = MAP + 62 };

static void fill_fork(uint8_t *b) {
    put32(b, DATA); put32(b + 4, MAP); put32(b + 8, 44); put32(b + 12, 62);
    put32(b + DATA, 24);
    put32(b + ZERO, 0x100); put32(b + ZERO + 4, 0x100);
    put32(b + ZERO + 8, 8); put32(b + ZERO + 12, 32);
    put16(b + ZERO + 16, 0); put16(b + ZERO + 18, 0x3f3c);
    put16(b + ZERO + 20, 1); put16(b + ZERO + 22, 0xa9f0);
    put32(b + DATA + 28, 12);
    put16(b + SEG, 0); put16(b + SEG + 2, 1);
    put16(b + SEG + 4, 0x7007); put16(b + SEG + 6, 0xa9f4);             /* moveq #7,d0 ; _ExitToShell */
    put16(b + SEG + 8, 0x4e71); put16(b + SEG + 10, 0x4e71);
    put16(b + MAP + 24, 28); put16(b + MAP + 26, 62);
    put32(b + MAP + 30, MAC_CODE_TYPE);
    put16(b + MAP + 34, 1); put16(b + MAP + 36, 10);
    put16(b + MAP + 40, 0xffff);
    put16(b + MAP + 50, 1); put16(b + MAP + 52, 0xffff);
    b[MAP + 57] = 28;
}

static int open_fork(uint8_t *bytes, MacResourceFork *f) {
    char err[240];
    memset(bytes, 0, TOTAL);
    fill_fork(bytes);
    return mac_resource_open(f, bytes, TOTAL, err, sizeof err) == 0;
}

static int call(MacRuntime *rt, uint16_t op, uint32_t a0, uint32_t d0) {
    mac_cpu_set(rt->cpu, MAC_A0, a0);
    mac_cpu_set(rt->cpu, MAC_D0, d0);
    return mac_memory_service(rt, op);
}
static int16_t memerr(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0x220, 2); }
static uint32_t reg_a0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_A0); }
static uint32_t reg_d0(MacRuntime *rt) { return mac_cpu_get(rt->cpu, MAC_D0); }

static uint32_t gestalt(MacRuntime *rt, const char *sel, int16_t *err) {
    uint32_t v = (uint32_t)(uint8_t)sel[0] << 24 | (uint32_t)(uint8_t)sel[1] << 16 | (uint32_t)(uint8_t)sel[2] << 8 | (uint8_t)sel[3];
    mac_cpu_set(rt->cpu, MAC_D0, v);
    CHECK(mac_system_service(rt, 0xa1ad) == 1);
    *err = (int16_t)mac_cpu_get(rt->cpu, MAC_D0);
    return mac_cpu_get(rt->cpu, MAC_A0);
}

static void default_machine(void) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    clear_env();
    CHECK(open_fork(bytes, &f));
    MacRuntime d;                                                          /* nothing requested: the unchanged 16 MB machine */
    CHECK(mac_runtime_init(&d, &f, err, sizeof err) == 0);
    CHECK(mac_cpu_ram_size(d.cpu) == MAC_RUNTIME_RAM && d.addr32 == 0 && d.a5 == 0x800000);
    CHECK(mac_cpu_read(d.cpu, 0x108, 4) == 0x800000 && mac_cpu_read(d.cpu, 0x130, 4) == 0x700000);
    mac_runtime_destroy(&d);
    setenv("OS999_GUEST_ADDRESSING", "32", 1);                             /* opt in to 32-bit, 256 MB */
    MacRuntime rt;
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(mac_cpu_ram_size(rt.cpu) == RAM256 && rt.addr32 == 1 && mac_cpu_addr_mode(rt.cpu) == MAC_ADDR_32);
    CHECK(mac_cpu_read(rt.cpu, 0x108, 4) == RAM256);                      /* MemTop */
    CHECK(mac_cpu_read(rt.cpu, 0x130, 4) == 0x700000);                    /* ApplLimit: the default 8 MB partition's heap top */
    CHECK(rt.a5 == 0x800000 && rt.heap_bridge != NULL && !rt.partition_extended);
    int16_t e;
    CHECK(gestalt(&rt, "ram ", &e) == RAM256 && e == 0);                  /* Gestalt physical RAM */
    CHECK(gestalt(&rt, "lram", &e) == RAM256 && e == 0);
    CHECK(gestalt(&rt, "addr", &e) == 5 && e == 0);                       /* 32-bit addressing on, 32-bit capable */
    CHECK(call(&rt, 0xa055, 0xa0123456u, 0) == 1 && reg_a0(&rt) == 0xa0123456u);  /* StripAddress is the identity */
    uint32_t free_before; CHECK(call(&rt, 0xa01c, 0, 0) == 1); free_before = reg_d0(&rt);
    CHECK(free_before > 0 && free_before <= 0x400000);                    /* default partition: unchanged 8 MB layout */
    mac_runtime_destroy(&rt);
}

static void fallback_machine(void) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    clear_env();
    setenv("OS999_GUEST_ADDRESSING", "24", 1);                             /* explicit 24-bit, even though 256 MB is asked */
    setenv("OS999_GUEST_RAM_MB", "256", 1);
    CHECK(open_fork(bytes, &f));
    MacRuntime rt;
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(mac_cpu_ram_size(rt.cpu) == MAC_RUNTIME_RAM && rt.addr32 == 0 && mac_cpu_addr_mode(rt.cpu) == MAC_ADDR_24);
    CHECK(mac_cpu_read(rt.cpu, 0x108, 4) == 0x800000);                    /* legacy MemTop */
    int16_t e;
    CHECK(gestalt(&rt, "ram ", &e) == 0x800000 && e == 0);
    CHECK(gestalt(&rt, "addr", &e) == 4 && e == 0);                       /* capable, not in 32-bit mode */
    CHECK(call(&rt, 0xa055, 0xa0123456u, 0) == 1 && reg_a0(&rt) == 0x123456u);    /* StripAddress clears the flag byte */
    mac_runtime_destroy(&rt);

    clear_env();                                                          /* 24-bit via the saved policy, RAM request capped */
    mac_runtime_set_machine_policy(128, 24);
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(mac_cpu_ram_size(rt.cpu) == MAC_RUNTIME_RAM && rt.addr32 == 0);
    mac_runtime_destroy(&rt);
    clear_env();
}

static void configured_sizes_and_clamps(void) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    CHECK(open_fork(bytes, &f));
    MacRuntime rt;
    clear_env(); setenv("OS999_GUEST_RAM_MB", "64", 1);                  /* explicit size is kept (implies 32-bit) */
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0 && mac_cpu_ram_size(rt.cpu) == 64 * MB);
    CHECK(mac_cpu_read(rt.cpu, 0x108, 4) == 64 * MB);
    mac_runtime_destroy(&rt);
    clear_env(); setenv("OS999_GUEST_RAM_MB", "2", 1); setenv("OS999_GUEST_ADDRESSING", "32", 1);   /* too small: clamped to 16 MB */
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0 && mac_cpu_ram_size(rt.cpu) == 16 * MB);
    mac_runtime_destroy(&rt);
    clear_env(); setenv("OS999_GUEST_RAM_MB", "junk", 1); setenv("OS999_GUEST_ADDRESSING", "32", 1);   /* invalid: as unset */
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0 && mac_cpu_ram_size(rt.cpu) == RAM256);
    mac_runtime_destroy(&rt);
    clear_env(); mac_runtime_set_machine_policy(32, 0);                 /* saved policy */
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0 && mac_cpu_ram_size(rt.cpu) == 32 * MB);
    mac_runtime_destroy(&rt);
    clear_env();
}

static void allocation_failure(void) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    CHECK(open_fork(bytes, &f));
    MacRuntime rt;
    clear_env(); setenv("OS999_GUEST_RAM_MB", "256", 1);                 /* host cannot reserve it: a clear message, no crash */
    mac_cpu_test_fail_next_alloc(1);
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == -1);
    CHECK(strstr(err, "Could not allocate 256 MB of guest memory") != NULL && strstr(err, "OS999_GUEST_RAM_MB") != NULL);
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);               /* the failure was one-shot; nothing leaked */
    mac_runtime_destroy(&rt);
    clear_env();
}

/* A 240 MB partition reaches the top of RAM: allocate, use and free memory right under the last byte. */
static void top_of_ram_allocation(void) {
    uint8_t bytes[TOTAL]; MacResourceFork f; char err[240];
    CHECK(open_fork(bytes, &f));
    clear_env();
    setenv("OS999_GUEST_ADDRESSING", "32", 1);
    setenv("OS999_GUEST_HEAP_MB", "1000", 1);                            /* more than fits: clamped to 240 */
    setenv("OS999_GUEST_GUARD", "1", 1);
    MacRuntime rt;
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);
    CHECK(rt.partition_extended && rt.a5 == RAM256 - MB && rt.heap_bridge != NULL);
    uint32_t appl = mac_cpu_read(rt.cpu, 0x130, 4);
    CHECK(appl > 200 * MB && appl <= RAM256 - MB);                        /* ApplLimit is the heap top */
    CHECK(rt.stack_base == rt.a5 - 0x100 && appl <= rt.stack_base - 0x100000);
    CHECK(mac_heap_owns(&rt, 0x1000000) && mac_heap_owns(&rt, appl - 16) && !mac_heap_owns(&rt, appl));
    /* Code ran from above 16 MB: the segment was loaded into the heap and the startup jump landed there. */
    uint32_t jt = rt.jump_table;
    CHECK(mac_cpu_read(rt.cpu, jt + 2, 2) == 0x4ef9 && mac_cpu_read(rt.cpu, jt + 4, 4) >= 0x1000000);
    mac_runtime_run(&rt, 100);                                            /* moveq #7,d0 ; _ExitToShell (which halts the CPU) */
    CHECK(rt.exited && rt.traps == 1 && rt.last_trap == 0xa9f4 && mac_cpu_get(rt.cpu, MAC_D0) == 7);
    CHECK(rt.last_trap_pc >= 0x1000000);                                  /* the trap was issued from code above 16 MB */
    mac_runtime_destroy(&rt);                                             /* a halted CPU answers nothing: use a fresh machine */
    CHECK(mac_runtime_init(&rt, &f, err, sizeof err) == 0);

    CHECK(call(&rt, 0xa01c, 0, 0) == 1);                                  /* FreeMem */
    uint32_t free_all = reg_d0(&rt);
    CHECK(free_all > 180 * MB);
    uint32_t big = free_all - 8 * MB;
    CHECK(call(&rt, 0xa31e, 0, big) == 1 && memerr(&rt) == 0);            /* NewPtr: fills all but the top 8 MB */
    uint32_t pbig = reg_a0(&rt);
    CHECK(pbig >= 0x1000000 && pbig + big <= appl);
    mac_cpu_ram(rt.cpu)[pbig + big - 1] = 0x3c;                            /* its last byte */
    CHECK(call(&rt, 0xa122, 0, MB) == 1 && memerr(&rt) == 0);             /* NewHandle near the top */
    uint32_t h = reg_a0(&rt), hp = mac_cpu_read(rt.cpu, h, 4);
    CHECK(h >= 0x1000000 && hp >= pbig + big && hp + MB <= appl);
    CHECK(appl > RAM256 - 16 * MB);                                       /* the heap really ends in the top 16 MB */
    CHECK(mac_cpu_write(rt.cpu, hp + MB - 1, 1, 0xee) == 0 && mac_cpu_read(rt.cpu, hp + MB - 1, 1) == 0xee);
    CHECK(call(&rt, 0xa31e, 0, 6 * MB) == 1 && memerr(&rt) == 0);         /* NewPtr in the last of the room */
    uint32_t p = reg_a0(&rt);
    CHECK(p + 6 * MB <= appl && p > RAM256 - 16 * MB);
    CHECK(call(&rt, 0xa31e, 0, 8 * MB) == 1 && memerr(&rt) == -108 && reg_a0(&rt) == 0);   /* memFullErr, no crash */
    CHECK(call(&rt, 0xa31e, 0, 0xfffffff0u) == 1 && memerr(&rt) == -108 && reg_a0(&rt) == 0);
    CHECK(call(&rt, 0xa31e, 0, 0x80000000u) == 1 && memerr(&rt) == -108);
    CHECK(!mac_cpu_halted(rt.cpu));
    CHECK(call(&rt, 0xa01f, p, 0) == 1 && memerr(&rt) == 0);              /* DisposePtr near the top */
    CHECK(call(&rt, 0xa023, h, 0) == 1 && memerr(&rt) == 0);              /* DisposeHandle */
    CHECK(mac_cpu_read(rt.cpu, h, 4) == 0);
    CHECK(call(&rt, 0xa31e, 0, 6 * MB) == 1 && memerr(&rt) == 0);         /* the freed room is reusable */
    CHECK(call(&rt, 0xa01f, pbig, 0) == 1 && memerr(&rt) == 0);           /* and the big block frees cleanly */
    CHECK(call(&rt, 0xa01c, 0, 0) == 1 && reg_d0(&rt) > free_all - 8 * MB);
    /* Last valid byte of the whole machine, then one past it. */
    CHECK(mac_cpu_write(rt.cpu, RAM256 - 1, 1, 0x42) == 0 && mac_cpu_read(rt.cpu, RAM256 - 1, 1) == 0x42);
    CHECK(mac_cpu_read(rt.cpu, RAM256, 1) == 0 && mac_cpu_halted(rt.cpu));
    mac_runtime_destroy(&rt);
    clear_env();
}

int main(void) {
    config_rules();
    cpu_creation_rules(); cpu_helper_boundaries(); cpu_bus_boundaries(); addressing_fallback();
    host_allocation_failure();
    partition_layout();
    default_machine(); fallback_machine(); configured_sizes_and_clamps(); allocation_failure();
    top_of_ram_allocation();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("guest memory: all checks passed");
    return 0;
}
