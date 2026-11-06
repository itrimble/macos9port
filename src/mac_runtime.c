#include "mac_runtime.h"
#include "mac_sound.h"
#include "mac_time.h"
#include "mac_resource_chain.h"
#include "mac_system_volume.h"
#include "mac_drive_queue.h"

#include <stdio.h>
#include <string.h>

/* Independently implemented from the CODE resource/jump-table format described
 * by W. E. Woody, comp.sys.mac.misc, 17 March 1991 ("CODE 0"). No ROM loaded.
 * This first loader supports classic near-model jump tables, not CFM or PPC.
 */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

static int fail(MacRuntime *rt, char *err, size_t n, const char *message) {
    if (err && n) snprintf(err, n, "%s", message);
    mac_runtime_destroy(rt);
    return -1;
}

uint32_t mac_runtime_alloc(MacRuntime *rt, uint32_t size) {
    if (size > UINT32_MAX - 3) return 0;
    uint32_t aligned = (size + 3) & ~UINT32_C(3);
    if (!aligned) aligned = 4;
    uint32_t ceiling = rt->heap_limit;
    if (rt->heap_high && rt->heap_high < ceiling) ceiling = rt->heap_high;
    if (ceiling > mac_cpu_ram_size(rt->cpu)) ceiling = (uint32_t)mac_cpu_ram_size(rt->cpu);
    if (rt->heap_next > ceiling || aligned > ceiling - rt->heap_next) return 0;
    uint32_t result = rt->heap_next;
    rt->heap_next += aligned;
    memset(mac_cpu_ram(rt->cpu) + result, 0, aligned);
    return result;
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
    if (mac_resource_find(fork, type, id, &r) != 0 || r.size > UINT32_MAX) return 0;
    size_t slot = 0;
    while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    if (slot == MAC_RUNTIME_HANDLES) return 0;
    uint32_t old_heap = rt->heap_next;
    uint32_t h = mac_runtime_alloc(rt, 4), p = mac_runtime_alloc(rt, (uint32_t)r.size);
    if (!h || !p) { rt->heap_next = old_heap; return 0; }
    memcpy(mac_cpu_ram(rt->cpu) + p, r.data, r.size);
    mac_cpu_write(rt->cpu, h, 4, p);
    if (slot == rt->handle_count) rt->handle_count++;
    rt->handles[slot] = (MacHandle){.handle = h, .pointer = p,
        .size = (uint32_t)r.size, .type = type, .id = id, .resource = 1, .resource_owner = owner};
    return h;
}

int mac_runtime_init(MacRuntime *rt, const MacResourceFork *fork, char *err, size_t n) {
    if (!rt || !fork) return -1;
    memset(rt, 0, sizeof *rt);
    if (err && n) err[0] = 0;
    rt->fork = *fork;
    MacResource zero;
    if (mac_resource_find(fork, MAC_CODE_TYPE, 0, &zero) != 0 || zero.size < 24)
        return fail(rt, err, n, "Missing or truncated CODE 0 jump table.");
    uint32_t above = be32(zero.data), below = be32(zero.data + 4);
    uint32_t length = be32(zero.data + 8), offset = be32(zero.data + 12);
    if (below > 0x100000 || above > 0x100000 || length < 8 || length % 8 || (offset & 1) ||
        length > zero.size - 16 || offset > above || length > above - offset)
        return fail(rt, err, n, "Unsupported or invalid CODE 0 layout.");
    rt->cpu = mac_cpu_create_model(MAC_RUNTIME_RAM, MAC_CPU_68020);
    if (!rt->cpu) return fail(rt, err, n, "Could not allocate guest memory.");
    rt->a5 = 0x800000;
    rt->jump_table = rt->a5 + offset;
    rt->jump_size = length;
    rt->heap_next = 0x100000;
    rt->heap_limit = 0x700000;
    rt->stack_base = rt->a5 - below;
    rt->screen_base = 0xe00000;
    rt->screen_width = 640;
    rt->screen_height = 480;
    rt->screen_row_bytes = 80;
    rt->screen_depth = 1;
    rt->palette[0] = 0xffffffff;
    rt->palette[1] = 0xff000000;
    rt->modifiers = 0x80; /* btnState: no mouse button held. */
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    memcpy(ram + rt->jump_table, zero.data + 16, length);
    /* Validate every referenced segment, but leave non-startup entries lazy:
     * applications may patch LoadSeg to relocate their own code resources. */
    for (uint32_t i = 0; i < length; i += 8) {
        const uint8_t *entry = zero.data + 16 + i;
        if (be16(entry + 2) != 0x3f3c || be16(entry + 6) != 0xa9f0)
            return fail(rt, err, n, "Unsupported jump-table entry (not near-model LoadSeg).");
        int16_t id = (int16_t)be16(entry + 4);
        MacResource segment;
        if (id <= 0 || mac_resource_find(fork, MAC_CODE_TYPE, id, &segment) != 0 || segment.size < 4)
            return fail(rt, err, n, "Jump table refers to a missing CODE segment.");
        uint32_t target_offset = be16(entry);
        if (segment.size < 6 || target_offset > segment.size - 6 || (target_offset & 1))
            return fail(rt, err, n, "CODE entry point is outside its segment.");
    }
    if (mac_runtime_load_segment(rt, (int16_t)be16(zero.data + 20)))
        return fail(rt, err, n, "Could not load startup CODE segment.");
    mac_cpu_set(rt->cpu, MAC_A5, rt->a5);
    mac_cpu_set(rt->cpu, MAC_A7, rt->a5 - below);
    mac_cpu_set(rt->cpu, MAC_PC, rt->jump_table + 2);
    mac_cpu_set(rt->cpu, MAC_SR, 0);
    /* Process-owned low-memory globals needed before the first Toolbox call. */
    mac_cpu_write(rt->cpu, 0x904, 4, rt->a5); /* CurrentA5 */
    mac_cpu_write(rt->cpu, 0x908, 4, rt->a5 - below); /* CurStackBase: below A5 globals */
    mac_cpu_write(rt->cpu, 0x934, 2, offset); /* CurJTOffset */
    mac_cpu_write(rt->cpu, 0x15a, 2, 0x0607); /* Guest compatibility profile, not host OS. */
    mac_cpu_write(rt->cpu, 0x12f, 1, 2); /* 68020 */
    mac_cpu_write(rt->cpu, 0x108, 4, 0x800000); /* Guest MemTop */
    mac_cpu_write(rt->cpu, 0x130, 4, rt->heap_limit); /* ApplLimit */
    rt->system_volume = mac_system_volume_create(rt);
    if (!rt->system_volume) return fail(rt, err, n, "Could not create authored System volume.");
    if (mac_drive_mount(rt, 2, 1)) return fail(rt, err, n, "Could not mount authored System drive.");
    mac_traps_init(rt);
    mac_cpu_set_trap(rt->cpu, mac_runtime_trap, rt);
    return 0;
}

int mac_runtime_load_segment(MacRuntime *rt, int16_t id) {
    uint32_t handle = mac_runtime_resource(rt, MAC_CODE_TYPE, id);
    if (!handle) return -1;
    uint32_t pointer = mac_cpu_read(rt->cpu, handle, 4);
    MacResource zero, segment;
    if (mac_resource_find(&rt->fork, MAC_CODE_TYPE, 0, &zero) ||
        mac_resource_find(&rt->fork, MAC_CODE_TYPE, id, &segment)) return -1;
    for (uint32_t i = 0; i < rt->jump_size; i += 8) {
        const uint8_t *entry = zero.data + 16 + i;
        if ((int16_t)be16(entry + 4) != id) continue;
        uint32_t offset = be16(entry);
        if (segment.size < 6 || offset > segment.size - 6 || (offset & 1)) return -1;
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
    mac_time_destroy(rt);
    mac_sound_destroy(rt);
    mac_resource_chain_destroy(rt);
    mac_files_destroy(rt);
    mac_drive_destroy(rt);
    mac_system_volume_destroy(rt->system_volume);
    rt->system_volume = NULL;
    mac_cpu_destroy(rt->cpu);
    rt->cpu = NULL;
}

size_t mac_runtime_run(MacRuntime *rt, size_t instructions) {
    if (!rt || !rt->cpu) return 0;
    size_t ran = mac_time_poll(rt, instructions);
    if (ran < instructions && !mac_cpu_halted(rt->cpu))
        ran += mac_vbl_poll(rt, instructions - ran);
    if (ran < instructions && !mac_cpu_halted(rt->cpu))
        ran += mac_cpu_run(rt->cpu, instructions - ran);
    rt->instructions += ran;
    return ran;
}

size_t mac_runtime_run_cycles(MacRuntime *rt, uint64_t cycles) {
    if (!rt || !rt->cpu || !cycles || mac_cpu_halted(rt->cpu)) return 0;
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
    if (!mac_cpu_halted(rt->cpu) && used < cycles)
        ran += mac_cpu_run_cycles(rt->cpu, cycles - used);
    rt->instructions += ran;
    return ran;
}
