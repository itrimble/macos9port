#include "mac_runtime.h"
#include "mac_codefmt.h"
#include "mac_rescomp.h"
#include "guest_memory.h"
#include "mac_sound.h"
#include "mac_time.h"
#include "mac_resource_chain.h"
#include "mac_system_volume.h"
#include "mac_drive_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_policy_heap_mb = 8, g_policy_guard = 0, g_policy_ram_mb = 0, g_policy_addr = 0;

void mac_runtime_set_memory_policy(int heap_mb, int guard_allocs) {
    g_policy_heap_mb = (heap_mb == 2 || heap_mb == 4 || heap_mb == 8 || heap_mb == 12 ||
                        (heap_mb >= 16 && heap_mb <= 1008)) ? heap_mb : 8;
    g_policy_guard = !!guard_allocs;
}

void mac_runtime_set_machine_policy(int ram_mb, int addr_bits) {
    g_policy_ram_mb = ram_mb > 0 ? ram_mb : 0;
    g_policy_addr = addr_bits == 24 || addr_bits == 32 ? addr_bits : 0;
}

/* Independently implemented from the CODE resource/jump-table format described
 * by W. E. Woody, comp.sys.mac.misc, 17 March 1991 ("CODE 0"). No ROM loaded.
 * This first loader supports classic near-model jump tables, not CFM or PPC.
 */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int fail(MacRuntime *rt, char *err, size_t n, const char *message) {
    if (err && n) snprintf(err, n, "%s", message);
    mac_runtime_destroy(rt);
    return -1;
}

uint32_t mac_runtime_alloc(MacRuntime *rt, uint32_t size) {
    if (size > UINT32_MAX - 32) return 0;
    uint32_t aligned = (size + 3) & ~UINT32_C(3);
    if (!aligned) aligned = 4;
    uint32_t guard = rt->guard_allocs ? 16u : 0u;
    uint32_t total = aligned + guard;
    uint32_t ceiling = rt->heap_limit;
    if (rt->heap_high && rt->heap_high < ceiling) ceiling = rt->heap_high;
    if (ceiling > mac_cpu_ram_size(rt->cpu)) ceiling = (uint32_t)mac_cpu_ram_size(rt->cpu);
    if (rt->heap_next > ceiling || total > ceiling - rt->heap_next) return 0;
    uint32_t result = rt->heap_next;
    rt->heap_next += total;
    memset(mac_cpu_ram(rt->cpu) + result, 0, aligned);
    if (guard) memset(mac_cpu_ram(rt->cpu) + result + aligned, 0xA5, guard);
    return result;
}

uint32_t mac_runtime_alloc_data(MacRuntime *rt, uint32_t size) {
    return rt->heap_bridge ? mac_heap_alloc(rt, size) : mac_runtime_alloc(rt, size);
}

void mac_runtime_free_data(MacRuntime *rt, uint32_t address) {
    if (mac_heap_owns(rt, address)) mac_heap_free(rt, address); /* bump storage is never reclaimed */
}

uint32_t mac_runtime_resource(MacRuntime *rt, uint32_t type, int16_t id) {
    return mac_runtime_resource_owned(rt, type, id, 0);
}

uint32_t mac_runtime_resource_owned(MacRuntime *rt, uint32_t type, int16_t id, int16_t owner) {
    const MacResourceFork *fork = mac_resource_owner_fork(rt, owner);
    if (!fork) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *h = &rt->handles[i];
        if (h->resource && h->resource_owner == owner && h->type == type && h->id == id) return h->handle;
    }
    MacResource r;
    if (mac_resource_find(fork, type, id, &r) != 0) return 0;
    mac_resedit_apply(rt, owner, &r); /* SetResInfo / WriteResource edits win over the fork's bytes */
    if (r.size > UINT32_MAX) return 0;
    size_t slot = 0;
    while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    if (slot == MAC_RUNTIME_HANDLES) return 0;
    uint32_t old_heap = rt->heap_next;
    uint32_t h = mac_runtime_alloc(rt, 4), p = mac_runtime_alloc_data(rt, (uint32_t)r.size);
    if (!h || !p) { rt->heap_next = old_heap; mac_runtime_free_data(rt, p); return 0; }
    memcpy(mac_cpu_ram(rt->cpu) + p, r.data, r.size);
    mac_cpu_write(rt->cpu, h, 4, p);
    if (slot == rt->handle_count) rt->handle_count++;
    rt->handles[slot] = (MacHandle){.handle = h, .pointer = p,
        .size = (uint32_t)r.size, .type = type, .id = id, .resource = 1, .resource_owner = owner};
    return h;
}

/* Everything up to the point where application code is placed: guest RAM, the partition's heap split, the screen
 * defaults. `below` is the size of the A5 globals area. Shared by the 68K (CODE 0) and PowerPC (cfrg) start paths. */
static int machine_begin(MacRuntime *rt, const MacResourceFork *fork, uint32_t below, uint32_t *appl_limit, char *err, size_t n) {
    GuestMemoryConfig mem = guest_memory_from_env(g_policy_ram_mb, g_policy_addr);
    rt->cpu = mac_cpu_create_mode((size_t)mem.ram_mb << 20, MAC_CPU_68020, mem.addr32 ? MAC_ADDR_32 : MAC_ADDR_24);
    if (!rt->cpu) {
        char msg[256];
        snprintf(msg, sizeof msg, "Could not allocate %u MB of guest memory (%s) Set OS999_GUEST_RAM_MB lower.",
                 mem.ram_mb, mac_cpu_create_error());
        return fail(rt, err, n, msg);
    }
    rt->addr32 = mem.addr32;
    int heap_mb = g_policy_heap_mb;
    const char *env_mb = getenv("OS999_GUEST_HEAP_MB");
    if (env_mb && env_mb[0]) heap_mb = atoi(env_mb);
    const char *env_guard = getenv("OS999_GUEST_GUARD");
    rt->guard_allocs = (env_guard && env_guard[0]) ? (atoi(env_guard) != 0) : g_policy_guard;
    MacPartitionLayout lay;
    mac_partition_layout(heap_mb, mac_partition_preferred(fork), (uint32_t)mac_cpu_ram_size(rt->cpu), rt->addr32, below, &lay);
    rt->a5 = lay.a5; rt->heap_limit = lay.heap_limit; rt->partition_extended = lay.extended;
    /* What the guest is told the machine has. 32-bit: the RAM itself. 24-bit: the legacy value (the partition top), so
     * the fallback reproduces the earlier 16 MB runtime exactly. */
    rt->ram_reported = rt->addr32 ? (uint32_t)mac_cpu_ram_size(rt->cpu) : rt->a5;
    rt->heap_next = 0x100000;
    /* The upper half of the partition's heap span becomes the auto-growing protected
     * heap; the bump allocator keeps the lower half for runtime-internal records.
     * This holds for every supported partition (2/4/8/12 MiB), with or without
     * guard redzones (the bridge stamps and checks its own 16-byte redzones).
     * The split is half of the span rounded down to 64 KiB, which is 0x400000
     * for the default 8 MiB partition. */
    rt->stack_base = rt->a5 - below;
    *appl_limit = rt->heap_limit;
    if (rt->partition_extended) {
        /* Extended partition: the whole of [16 MB, stack) is the protected heap; the bump region below keeps its 3 MB for
         * runtime records. The RAM is untouched since it was reserved, so attaching does not commit the window. */
        uint32_t hi = (rt->stack_base - 0x100000u) & ~UINT32_C(7);
        if (mac_heap_attach_fresh(rt, 0x1000000u, hi)) return fail(rt, err, n, "Could not create the protected guest heap.");
        *appl_limit = hi;
    } else if (rt->heap_limit > rt->heap_next) {
        uint32_t span = rt->heap_limit - rt->heap_next;
        uint32_t lo = rt->heap_next + ((span / 2) & ~UINT32_C(0xffff));
        uint32_t ceil = rt->stack_base > 0x30000 ? rt->stack_base - 0x30000 : rt->heap_limit;
        if (rt->heap_limit > lo && rt->heap_limit - lo >= 0x10000) {
            if (mac_heap_attach(rt, lo, mac_partition_heap_hi(fork, lo, rt->heap_limit, ceil))) return fail(rt, err, n, "Could not create the protected guest heap.");
            rt->heap_limit = lo;
        }
    }
    rt->stack_base = rt->a5 - below;
    rt->screen_base = 0xe00000;
    rt->screen_width = 640;
    rt->screen_height = 480;
    rt->screen_row_bytes = 80;
    rt->screen_depth = 1;
    rt->palette[0] = 0xffffffff;
    rt->palette[1] = 0xff000000;
    rt->modifiers = 0x80; /* btnState: no mouse button held. */
    return 0;
}

/* Registers, process-owned low-memory globals, the authored System volume and the trap table. `pc` is where the 68K
 * CPU starts; `jt_offset` is CurJTOffset (0 when there is no jump table). */
static int machine_finish(MacRuntime *rt, uint32_t below, uint32_t pc, uint32_t jt_offset, uint32_t appl_limit, char *err, size_t n) {
    /* Classic Mac OS runs application code in supervisor mode, and ordinary programs use privileged instructions: heap code
     * brackets critical sections with MOVE SR,-(A7) and ORI #$0200,SR. In user mode those raise a privilege violation (an empty
     * vector sent Castle of Dr. Brain to address 0 with A7 = $FFFFFFF8). The test checks that A7 is intact after this, in either order
     * of the two stores (I expected the stack pointer to be swapped by the mode change; it is not, with this CPU wrapper). */
    mac_cpu_set(rt->cpu, MAC_SR, 0x2000);
    mac_cpu_set(rt->cpu, MAC_A5, rt->a5);
    mac_cpu_set(rt->cpu, MAC_A7, rt->a5 - below);
    mac_cpu_set(rt->cpu, MAC_PC, pc);
    /* Process-owned low-memory globals needed before the first Toolbox call. */
    mac_cpu_write(rt->cpu, 0x904, 4, rt->a5); /* CurrentA5 */
    mac_cpu_write(rt->cpu, 0x908, 4, rt->a5 - below); /* CurStackBase: below A5 globals */
    mac_appfork_publish(rt); /* CurApRefNum, CurMap and TopMapHndl for code that reads its own resource file directly */
    mac_cpu_write(rt->cpu, 0x934, 2, jt_offset); /* CurJTOffset */
    mac_cpu_write(rt->cpu, 0x15a, 2, MAC_PROFILE_SYSTEM_VERSION); /* Guest compatibility profile, not host OS. */
    mac_cpu_write(rt->cpu, 0x12f, 1, 2); /* 68020 */
    /* $6F4: the vector start-up code of MPW-built programs calls (on anything above a 68000) to flush the instruction cache after moving code. It is
     * compared with the unimplemented-trap address, and equal means "nothing to flush"; zero here sent such a program to address 0. */
    mac_cpu_write(rt->cpu, 0x6f4, 4, mac_native_stub(0x89f));
    mac_cpu_write(rt->cpu, 0x108, 4, rt->ram_reported); /* MemTop: top of RAM (32-bit: the machine's RAM; 24-bit: the legacy partition top) */
    mac_cpu_write(rt->cpu, 0x130, 4, appl_limit); /* ApplLimit: the partition's heap top, before the bump/protected split */
    rt->system_volume = mac_system_volume_create(rt);
    if (!rt->system_volume) return fail(rt, err, n, "Could not create authored System volume.");
    if (mac_drive_mount(rt, 2, 1)) return fail(rt, err, n, "Could not mount authored System drive.");
    mac_traps_init(rt);
    mac_cpu_set_trap(rt->cpu, mac_runtime_trap, rt);
    return 0;
}

int mac_runtime_init(MacRuntime *rt, const MacResourceFork *fork, char *err, size_t n) {
    if (!rt || !fork) return -1;
    memset(rt, 0, sizeof *rt);
    if (err && n) err[0] = 0;
    rt->fork = *fork;
    MacResource zero;
    if (mac_resource_find(fork, MAC_CODE_TYPE, 0, &zero) != 0 || zero.size < 24)
        return fail(rt, err, n, "Missing or truncated CODE 0 jump table.");
    MacCode0 z0;
    if (mac_code0_parse(zero.data, zero.size, &z0)) return fail(rt, err, n, "Unsupported or invalid CODE 0 layout.");
    uint32_t below = z0.below, length = z0.length, offset = z0.offset, above = z0.above;
    (void)above;
    uint32_t appl_limit;
    if (machine_begin(rt, fork, below, &appl_limit, err, n)) return -1;
    rt->jump_table = rt->a5 + offset;
    rt->jump_size = length;
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    memcpy(ram + rt->jump_table, zero.data + 16, z0.present); /* a table stored shorter than declared is zero-filled beyond what is stored */
    /* Validate every referenced segment, but leave non-startup entries lazy:
     * applications may patch LoadSeg to relocate their own code resources. */
    for (uint32_t i = 0; i < z0.present; i += 8) {
        const uint8_t *entry = zero.data + 16 + i;
        if (i && be16(entry) == 0xa89f) break; /* the rest of the table is an installer engine's packed payload; its startup stub expands it */
        if (be16(entry + 2) != 0x3f3c || be16(entry + 6) != 0xa9f0)
            return fail(rt, err, n, "Unsupported jump-table entry (not near-model LoadSeg).");
        int16_t id = (int16_t)be16(entry + 4);
        MacResource segment;
        if (id <= 0 || mac_resource_find(fork, MAC_CODE_TYPE, id, &segment) != 0 || segment.size < 4)
            return fail(rt, err, n, "Jump table refers to a missing CODE segment.");
        uint32_t target_offset = be16(entry);
        size_t seg_size = mac_comp_size(segment.data, segment.size); /* a compressed segment is checked at the size it will have once expanded */
        if (seg_size < 6 || target_offset > seg_size - 6 || (target_offset & 1))
            return fail(rt, err, n, "CODE entry point is outside its segment.");
    }
    /* A startup segment stored compressed cannot be expanded before the CPU runs: its jump-table entry stays an unloaded LoadSeg stub, and the first call
     * through it loads (and expands) the segment like any other. */
    MacResource startup; MacComp packed;
    int deferred = mac_resource_find(fork, MAC_CODE_TYPE, (int16_t)be16(zero.data + 20), &startup) == 0 && mac_comp_parse(startup.data, startup.size, &packed) != 0;
    if (!deferred && mac_runtime_load_segment(rt, (int16_t)be16(zero.data + 20)))
        return fail(rt, err, n, "Could not load startup CODE segment.");
    return machine_finish(rt, below, rt->jump_table + 2, offset, appl_limit, err, n);
}

/* A machine with no 68K application code: the same guest RAM, heap, low-memory globals, resource chain and trap table
 * as mac_runtime_init, with an A5 world of MAC_NATIVE_A5_BELOW bytes for the QuickDraw globals pointer and nothing
 * above A5. The PowerPC start path (mac_ppc_runtime.c) loads its fragment into this machine. The 68K CPU is parked on
 * an ExitToShell so that running it by mistake ends cleanly instead of executing zeros. */
int mac_runtime_init_machine(MacRuntime *rt, const MacResourceFork *fork, char *err, size_t n) {
    if (!rt || !fork) return -1;
    memset(rt, 0, sizeof *rt);
    if (err && n) err[0] = 0;
    rt->fork = *fork;
    uint32_t appl_limit;
    if (machine_begin(rt, fork, MAC_NATIVE_A5_BELOW, &appl_limit, err, n)) return -1;
    rt->jump_table = rt->a5 + 32;
    rt->jump_size = 0;
    mac_cpu_write(rt->cpu, rt->jump_table, 2, 0xa9f4);
    return machine_finish(rt, MAC_NATIVE_A5_BELOW, rt->jump_table, 32, appl_limit, err, n);
}

int mac_runtime_load_segment(MacRuntime *rt, int16_t id) {
    if (rt->trace) fprintf(stderr, "load-segment id=%d\n", id);
    uint32_t handle = mac_runtime_resource(rt, MAC_CODE_TYPE, id);
    if (!handle) return -1;
    uint32_t pointer = mac_cpu_read(rt->cpu, handle, 4);
    MacResource zero, segment;
    if (mac_resource_find(&rt->fork, MAC_CODE_TYPE, 0, &zero) ||
        mac_resource_find(&rt->fork, MAC_CODE_TYPE, id, &segment)) return -1;
    /* The jump table was validated against the application's CODE 0; never trust
     * whichever fork is current to still be that one. */
    if (zero.size < 16) return -1;
    uint32_t stored = (uint32_t)((zero.size - 16) & ~(size_t)7); /* entries CODE 0 actually holds; a table may be declared longer than it is stored */
    /* The segment as it is in memory now: a self-decompressing stub (the first CODE of an installer engine) replaces the contents of its own handle and
     * then re-enters its jump-table entry with a new offset word, so the size comes from the live handle and the offset from the entry as the program left it. */
    uint32_t live_size = (uint32_t)mac_comp_size(segment.data, segment.size);
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].handle == handle && rt->handles[i].size >= 6) live_size = rt->handles[i].size;
    for (uint32_t i = 0; i < rt->jump_size; i += 8) {
        const uint8_t *entry = i < stored ? zero.data + 16 + i : NULL;
        uint32_t at = rt->jump_table + i, offset = entry ? be16(entry) : 0;
        /* An entry the program left unloaded (offset, MOVE.W #id,-(SP), _LoadSeg) names its own segment and offset, whatever CODE 0 said: that is what the
         * Segment Loader reads. Entries still in CODE 0's form are matched by CODE 0's id as before. */
        int unloaded = mac_cpu_read(rt->cpu, at + 2, 2) == 0x3f3c && mac_cpu_read(rt->cpu, at + 6, 2) == 0xa9f0;
        if (unloaded) {
            if ((int16_t)mac_cpu_read(rt->cpu, at + 4, 2) != id) continue;
            offset = mac_cpu_read(rt->cpu, at, 2);
        } else if (!entry || (int16_t)be16(entry + 4) != id) continue;
        if (live_size < 6 || offset > live_size - 6 || (offset & 1)) return -1;
        mac_cpu_write(rt->cpu, rt->jump_table + i, 2, (uint16_t)id);
        mac_cpu_write(rt->cpu, rt->jump_table + i + 2, 2, 0x4ef9);
        mac_cpu_write(rt->cpu, rt->jump_table + i + 4, 4, pointer + 4 + offset);
    }
    if (mac_cpu_halted(rt->cpu)) return -1;
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].handle == handle) rt->handles[i].loaded_code = 1;
    return 0;
}

void mac_runtime_destroy(MacRuntime *rt) {
    if (!rt) return;
    mac_heap_detach(rt);
    mac_resedit_destroy(rt);
    mac_time_destroy(rt);
    mac_sound_destroy(rt);
    mac_resource_chain_destroy(rt);
    mac_stdfile_destroy(rt);
    mac_stdput_destroy(rt);
    mac_dlgmgr_destroy(rt);
    mac_alert_destroy(rt);
    mac_cdef_destroy(rt);
    mac_mdef_destroy(rt);
    mac_rescomp_destroy(rt);
    mac_gworld_destroy(rt);
    mac_scrap_destroy(rt);
    mac_rgn_record_destroy(rt);
    mac_menuselect_destroy(rt);
    mac_files_destroy(rt);
    mac_drive_destroy(rt);
    mac_system_volume_destroy(rt->system_volume);
    rt->system_volume = NULL;
    mac_ppc_destroy(rt);
    mac_cpu_destroy(rt->cpu);
    rt->cpu = NULL;
}

/* Many classic programs read the low-memory clock globals directly instead of calling TickCount or GetDateTime: Ticks ($16A,
 * a long, 60 per second since startup) and Time ($20C, a long, seconds since 1 Jan 1904). Sierra's SCI interpreter reads Ticks
 * for every tick-based wait and Time for its seconds, and sat forever in a room while both stayed zero. They follow rt->ticks,
 * which the host advances once per 1/60 s; Time is a base plus whole elapsed seconds. */
void mac_clock_publish(MacRuntime *rt) {
    if (!rt || !rt->cpu) return;
    if (!rt->clock_base) rt->clock_base = (uint32_t)((uint64_t)time(NULL) + 2082844800ull);
    mac_cpu_write(rt->cpu, 0x16a, 4, rt->ticks);
    mac_cpu_write(rt->cpu, 0x20c, 4, rt->clock_base + rt->ticks / 60);
}

size_t mac_runtime_run(MacRuntime *rt, size_t instructions) {
    if (!rt || !rt->cpu) return 0;
    mac_clock_publish(rt);
    size_t ran = mac_time_poll(rt, instructions);
    if (ran < instructions && !mac_cpu_halted(rt->cpu))
        ran += mac_vbl_poll(rt, instructions - ran);
    if (ran < instructions && !mac_cpu_halted(rt->cpu))
        ran += rt->ppc ? mac_ppc_run(rt, instructions - ran) : mac_cpu_run(rt->cpu, instructions - ran);
    rt->instructions += ran;
    return ran;
}

size_t mac_runtime_run_cycles(MacRuntime *rt, uint64_t cycles) {
    if (!rt || !rt->cpu || !cycles || mac_cpu_halted(rt->cpu)) return 0;
    mac_clock_publish(rt);
    uint64_t cycle_start = mac_cpu_cycles(rt->cpu);
    /* VBL and Time Manager polls refuse budgets under 10,000 instructions so a
     * callback can always complete. Real-time slices are often 1-2 ms (16-32k
     * cycles), so a budget tied only to the slice starved every periodic task
     * and the guest ran at a fraction of its speed. Keep the slice share, but
     * never below the polls' minimum; the CPU slice absorbs any overrun. */
    size_t service_budget = cycles / 4 > 100000 ? 100000 : (size_t)(cycles / 4);
    if (service_budget < MAC_RUNTIME_SERVICE_MIN) service_budget = MAC_RUNTIME_SERVICE_MIN;
    size_t ran = mac_time_poll(rt, service_budget);
    if (!mac_cpu_halted(rt->cpu) && ran < service_budget)
        ran += mac_vbl_poll(rt, service_budget - ran);
    uint64_t used = mac_cpu_cycles(rt->cpu) - cycle_start;
    if (!mac_cpu_halted(rt->cpu) && used < cycles) /* a PowerPC application: one instruction is costed as 4 cycles */
        ran += rt->ppc ? mac_ppc_run(rt, (size_t)((cycles - used) / 4) + 1) : mac_cpu_run_cycles(rt->cpu, cycles - used);
    rt->instructions += ran;
    return ran;
}
