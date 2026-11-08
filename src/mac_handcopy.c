#include "mac_runtime.h"
#include <string.h>

/* HandToHand ($A9E1) and PtrToHand ($A9E3): copy a block into a new relocatable block. Register interfaces (from my memory of the Memory
 * Manager, not from a document I opened this session): HandToHand takes the handle in A0 and returns the copy's handle in A0; PtrToHand takes
 * the pointer in A0 and the byte count in D0 and returns the new handle in A0; both return an OSErr in D0 (and low memory $220, with the condition
 * codes set like the other Memory Manager calls). On an error A0 is nil. Only blocks this runtime owns can be copied: a handle must be in the
 * handle table with its master pointer unchanged (a purged or forged handle is refused), a pointer must lie inside guest RAM. The copy is
 * unlocked and unpurgeable, and is not a resource handle even when the original is one. */
static void result(MacCpu *cpu, int16_t err) {
    mac_cpu_write(cpu, 0x220, 2, (uint16_t)err);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)err);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (err < 0 ? 8u : err == 0 ? 4u : 0u));
}
static int ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }

int mac_handcopy_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa9e1 && opcode != 0xa9e3) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), size = 0, source = 0;
    if (opcode == 0xa9e1) {
        const MacHandle *found = NULL;
        for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle && rt->handles[i].handle == a0) { found = &rt->handles[i]; break; }
        if (!found || !ok(rt, a0, 4) || (a0 & 1)) { mac_cpu_set(cpu, MAC_A0, 0); result(cpu, -109); return 1; }                    /* nilHandleErr-style refusal */
        uint32_t master = mac_cpu_read(cpu, a0, 4);
        if (!master) { mac_cpu_set(cpu, MAC_A0, 0); result(cpu, -112); return 1; }                                                   /* purged: memPurErr */
        if (master != found->pointer || !ok(rt, master, found->size)) { mac_cpu_set(cpu, MAC_A0, 0); result(cpu, -111); return 1; }  /* memWZErr: not a live block */
        size = found->size; source = master;
    } else {
        size = mac_cpu_get(cpu, MAC_D0); source = a0;
        if (size > 0x1000000u || (size && !ok(rt, source, size))) { mac_cpu_set(cpu, MAC_A0, 0); result(cpu, -111); return 1; }
    }
    MacHandle *slot = NULL;
    for (size_t i = 0; i < rt->handle_count; i++) if (!rt->handles[i].handle && !rt->handles[i].pointer) { slot = &rt->handles[i]; break; }
    if (!slot && rt->handle_count < MAC_RUNTIME_HANDLES) slot = &rt->handles[rt->handle_count++];
    uint32_t saved = rt->heap_next, handle = slot ? mac_runtime_alloc(rt, 4) : 0, pointer = handle ? mac_runtime_alloc(rt, size ? size : 1) : 0;
    if (!slot || !handle || !pointer) {                                                       /* out of handles or heap: memFullErr, nothing kept */
        rt->heap_next = saved;
        if (slot && slot == &rt->handles[rt->handle_count - 1] && !slot->handle && !slot->pointer) rt->handle_count--;
        mac_cpu_set(cpu, MAC_A0, 0); result(cpu, -108); return 1;
    }
    if (size) memmove(mac_cpu_ram(cpu) + pointer, mac_cpu_ram(cpu) + source, size);
    *slot = (MacHandle){.handle = handle, .pointer = pointer, .size = size};
    mac_cpu_write(cpu, handle, 4, pointer);
    mac_cpu_set(cpu, MAC_A0, handle);
    result(cpu, 0);
    return 1;
}
