#include "mac_ppc.h"

/* Mixed mode, minimum viable. A universal procedure pointer is the address of a routine descriptor: 32 bytes that begin
 * with the A-line word $AAFE, so 68K-side code that jumps to it traps here.
 *
 *     +0  $AAFE   +2 version (7)   +3 flags   +4 reserved   +8 reserved   +9 selector info   +10 routine count - 1
 *     +12 procInfo   +16 reserved   +17 ISA (0 68K, 1 PowerPC)   +18 routine flags   +20 procedure   +24 reserved   +28 selector
 *
 * procInfo: bits 0-3 calling convention (0 Pascal stack, 1 C stack, 2 register), bits 4-5 result size, then for the
 * stack conventions one 2-bit size per parameter from bit 6; for the register convention a 5-bit result register from
 * bit 6 and, from bit 11, one 7-bit field per parameter (2-bit size, 5-bit register). Size codes: 1, 2, 3 = 1, 2, 4
 * bytes. Only the first routine record is used; selector-based descriptors are not supported.
 *
 * 68K to PowerPC. When the scheduler is stepping the 68K CPU (a trap service called the routine through
 * mac_guest_call) the switch is a CALLBACK frame and the PowerPC routine runs in the scheduler like any other code, so
 * it may itself make blocking Toolbox calls. When some other loop is running the 68K CPU (the VBL, Time Manager and
 * sound pollers) the PowerPC routine runs to completion here, inside the trap, and may only make calls that complete
 * at once. PowerPC to PowerPC through CallUniversalProc is a tail call. PowerPC to 68K code is a CALL68K frame. */

static const uint8_t reg_map[15] = {MAC_D0, MAC_D1, MAC_D2, MAC_D3, MAC_A0, MAC_A1, MAC_A2, MAC_A3,
                                    MAC_D4, MAC_D5, MAC_D6, MAC_D7, MAC_A4, MAC_A5, MAC_A6};
#define SYNC_BUDGET ((size_t)50000000)

static uint32_t sized(uint32_t value, unsigned code) {
    return code == 1 ? value & 0xffu : code == 2 ? (uint32_t)(int32_t)(int16_t)value : value;
}

/* Parameter size codes of a procInfo, in declaration order; returns the count, or -1 for an unsupported convention. */
static int param_codes(uint32_t info, unsigned codes[13], unsigned regs[13]) {
    unsigned conv = info & 15u, n = 0;
    if (conv == 0 || conv == 1) {
        while (n < 13 && (codes[n] = (info >> (6 + 2 * n)) & 3u)) n++;
    } else if (conv == 2) {
        for (; n < 4; n++) {
            unsigned field = (info >> (11 + 7 * n)) & 0x7fu;
            if (!(field & 3u)) break;
            if ((field >> 2) >= 15) return -1;
            codes[n] = field & 3u;
            regs[n] = reg_map[field >> 2];
        }
    } else return -1;
    return (int)n;
}

int mac_ppc_callback_finish(MacRuntime *rt, MacPpcFrame *f) {
    PpcCpu *c = &rt->ppc->cpu;
    MacCpu *m = rt->cpu;
    uint32_t result = c->gpr[3], info = f->proc_info, size = (info >> 4) & 3u;
    c->pc = f->pc; c->npc = f->npc; c->lr = f->lr; c->ctr = f->ctr; c->gpr[1] = f->r1; c->gpr[2] = f->r2;
    if ((info & 15u) == 0) {
        if (size == 1) mac_cpu_write(m, f->new_a7, 1, result & 0xffu);
        else if (size == 2) mac_cpu_write(m, f->new_a7, 2, result & 0xffffu);
        else if (size == 3) mac_cpu_write(m, f->new_a7, 4, result);
    } else if ((info & 15u) == 1) {
        if (size) mac_cpu_set(m, MAC_D0, sized(result, size));
    } else if (size && ((info >> 6) & 31u) < 15) {
        mac_cpu_set(m, (MacCpuReg)reg_map[(info >> 6) & 31u], sized(result, size));
    }
    mac_cpu_set(m, MAC_A7, f->new_a7);
    mac_cpu_set(m, MAC_PC, f->ret);
    return mac_cpu_halted(m) ? -1 : 0;
}

int mac_ppc_mixed_trap(MacRuntime *rt, uint32_t pc) {
    MacPpcState *st = rt->ppc;
    PpcCpu *c = &st->cpu;
    MacCpu *m = rt->cpu;
    uint32_t info = mac_cpu_read(m, pc + 12, 4), proc = mac_cpu_read(m, pc + 20, 4), sp = mac_cpu_get(m, MAC_A7);
    unsigned isa = mac_cpu_read(m, pc + 17, 1), codes[13], regs[13];
    if (mac_cpu_read(m, pc + 2, 1) != 7 || isa > 1)
        return mac_ppc_fail(rt, "Routine descriptor at %08x has an unsupported version or instruction set.", pc);
    if (isa == 0) { mac_cpu_set(m, MAC_PC, proc); return 0; } /* a descriptor for 68K code: simply go there */
    int n = param_codes(info, codes, regs);
    if (n < 0 || n > 8)
        return mac_ppc_fail(rt, "Routine descriptor at %08x uses an unsupported calling convention (procInfo %08x).", pc, info);
    MacPpcFrame f = {.kind = MAC_PPC_FRAME_CALLBACK, .name = "routine descriptor", .proc_info = info};
    uint32_t args[8] = {0}, at = sp + 4, code = 0, toc = 0;
    f.ret = mac_cpu_read(m, sp, 4);
    f.new_a7 = sp + 4;
    if ((info & 15u) == 0) { /* Pascal: the last parameter is nearest the return address; the callee pops them */
        for (int i = n; i-- > 0;) {
            args[i] = codes[i] == 3 ? mac_cpu_read(m, at, 4) : codes[i] == 2 ? sized(mac_cpu_read(m, at, 2), 2) : mac_cpu_read(m, at, 1);
            at += codes[i] == 3 ? 4 : 2;
        }
        f.new_a7 = at;
    } else if ((info & 15u) == 1) { /* C: first parameter nearest the return address, every one a long, caller pops */
        for (int i = 0; i < n; i++) args[i] = sized(mac_cpu_read(m, at + 4 * (uint32_t)i, 4), codes[i]);
    } else {
        for (int i = 0; i < n; i++) args[i] = sized(mac_cpu_get(m, (MacCpuReg)regs[i]), codes[i]);
    }
    if (mac_cpu_halted(m) || !ppc_mem_read(c, proc, 4, &code) || !ppc_mem_read(c, proc + 4, 4, &toc)) return mac_ppc_fail(rt, "Routine descriptor at %08x does not name a PowerPC transition vector.", pc);
    f.pc = c->pc; f.npc = c->npc; f.lr = c->lr; f.ctr = c->ctr; f.r1 = c->gpr[1]; f.r2 = c->gpr[2];
    uint32_t r1 = (c->gpr[1] - 224 - 64) & ~UINT32_C(15); /* below the interrupted code's red zone */
    if (!ppc_mem_write(c, r1, 4, f.r1)) return -1;
    c->gpr[1] = r1;
    for (int i = 0; i < n; i++) c->gpr[3 + i] = args[i];
    c->gpr[2] = toc;
    c->lr = PPC_STOP_PC;
    c->pc = code;
    if (st->sched_68k) {
        MacPpcFrame *slot = mac_ppc_push(rt, f.name);
        if (slot) *slot = f;
        return 0;
    }
    int stop = c->stop;
    st->sync_depth++;
    ppc_cpu_run(c, SYNC_BUDGET);
    st->sync_depth--;
    c->stop = stop;
    if (c->halted) { mac_cpu_halt(m, c->error); return 0; }
    if (c->pc != PPC_STOP_PC) return mac_ppc_fail(rt, "A PowerPC callback did not return within its instruction budget.");
    mac_ppc_callback_finish(rt, &f);
    return 0;
}

int mac_ppc_call68k_finish(MacRuntime *rt, MacPpcFrame *f, uint32_t *result) {
    MacCpu *m = rt->cpu;
    uint32_t info = f->proc_info, size = (info >> 4) & 3u, sp = mac_cpu_get(m, MAC_A7);
    if ((info & 15u) == 0) {
        uint32_t bytes = size == 3 ? 4 : size ? 2 : 0;
        if (sp != f->a7 - bytes)
            return mac_ppc_fail(rt, "A 68K routine called through CallUniversalProc returned with an unbalanced stack.");
        if (size) *result = size == 3 ? mac_cpu_read(m, sp, 4) : size == 2 ? sized(mac_cpu_read(m, sp, 2), 2) : mac_cpu_read(m, sp, 1);
    } else if ((info & 15u) == 1) {
        if (size) *result = sized(mac_cpu_get(m, MAC_D0), size);
    } else if (size && ((info >> 6) & 31u) < 15) {
        *result = sized(mac_cpu_get(m, (MacCpuReg)reg_map[(info >> 6) & 31u]), size);
    }
    mac_cpu_set(m, MAC_A7, f->a7);
    return mac_cpu_halted(m) ? -1 : 0;
}

/* CallUniversalProc(upp, procInfo, ...). */
static int call_universal_proc(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    MacPpcState *st = rt->ppc;
    MacCpu *m = rt->cpu;
    uint32_t upp = c->gpr[3], info = c->gpr[4], head = 0, code = 0, toc = 0;
    unsigned codes[13], regs[13];
    int n = param_codes(info, codes, regs);
    if (n < 0 || n > 6 || (upp & 1) || !ppc_mem_read(c, upp, 2, &head)) {
        if (!c->halted) mac_ppc_fail(rt, "CallUniversalProc with an unsupported procInfo %08x or target %08x.", info, upp);
        return 0;
    }
    if (head == 0xaafe) {
        uint32_t proc = mac_cpu_read(m, upp + 20, 4);
        if (mac_cpu_read(m, upp + 17, 1) == 1) { /* PowerPC to PowerPC: a tail call; the caller's glue restores its own TOC */
            if (!ppc_mem_read(c, proc, 4, &code) || !ppc_mem_read(c, proc + 4, 4, &toc)) return 0;
            for (int i = 0; i < n; i++) c->gpr[3 + i] = c->gpr[5 + i];
            c->gpr[2] = toc;
            c->npc = code;
            return 0;
        }
        upp = proc;
    }
    if (st->depth >= MAC_PPC_FRAMES) { mac_ppc_fail(rt, "Too many nested switches between the PowerPC and 68K sides."); return 0; }
    MacPpcFrame f = {.kind = MAC_PPC_FRAME_CALL68K, .name = "CallUniversalProc", .proc_info = info};
    f.stub = st->stubs + 4 * st->depth;
    f.a7 = mac_cpu_get(m, MAC_A7);
    uint32_t sp = f.a7, size = (info >> 4) & 3u;
    if ((info & 15u) == 0) {
        if (size) { sp -= size == 3 ? 4 : 2; mac_cpu_write(m, sp, size == 3 ? 4 : 2, 0); }
        for (int i = 0; i < n; i++) {
            uint32_t v = c->gpr[5 + i];
            if (codes[i] == 3) { sp -= 4; mac_cpu_write(m, sp, 4, v); }
            else { sp -= 2; mac_cpu_write(m, sp, 2, codes[i] == 2 ? v & 0xffffu : (v & 0xffu) << 8); }
        }
    } else if ((info & 15u) == 1) {
        for (int i = n; i-- > 0;) { sp -= 4; mac_cpu_write(m, sp, 4, c->gpr[5 + i]); }
    } else {
        for (int i = 0; i < n; i++) mac_cpu_set(m, (MacCpuReg)regs[i], c->gpr[5 + i]);
    }
    sp -= 4;
    mac_cpu_write(m, sp, 4, f.stub + 2); /* the routine's RTS lands on the frame's return address */
    mac_cpu_set(m, MAC_A7, sp);
    mac_cpu_set(m, MAC_PC, upp);
    if (!mac_cpu_halted(m)) mac_ppc_park(rt, &f);
    return 0;
}

static int new_routine_descriptor(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    MacCpu *m = rt->cpu;
    uint32_t d = mac_runtime_alloc_data(rt, 32);
    if (d) {
        mac_cpu_write(m, d, 2, 0xaafe);
        mac_cpu_write(m, d + 2, 1, 7);
        mac_cpu_write(m, d + 12, 4, c->gpr[4]);
        mac_cpu_write(m, d + 17, 1, c->gpr[5] & 0xffu);
        mac_cpu_write(m, d + 20, 4, c->gpr[3]);
    }
    c->gpr[3] = d; /* 0 when the heap is exhausted, as the real call reports failure */
    return 0;
}

static int dispose_routine_descriptor(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    uint32_t head = 0;
    if (c->gpr[3] && ppc_mem_read(c, c->gpr[3], 2, &head) && head == 0xaafe) mac_runtime_free_data(rt, c->gpr[3]);
    return 0;
}

static const MacCfmHostImport imports[] = {
    {"InterfaceLib", "CallUniversalProc", call_universal_proc, NULL},
    {"InterfaceLib", "NewRoutineDescriptor", new_routine_descriptor, NULL},
    {"InterfaceLib", "DisposeRoutineDescriptor", dispose_routine_descriptor, NULL},
};

const MacCfmHostImport *mac_ppc_mixed_imports(size_t *count) {
    *count = sizeof imports / sizeof imports[0];
    return imports;
}
