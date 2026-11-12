#include "mac_ppc.h"

#include <stdlib.h>
#include <string.h>

/* The generic import bridge: PowerPC arguments in, the 68K trap convention out, through the same mac_runtime_trap an
 * A-line instruction reaches. The signature grammar is in include/mac_ppc.h.
 *
 * A call normally completes inside mac_ppc_trap_begin: the service pops its arguments, leaves its result, and the 68K
 * PC is still just after the frame's trap word. When the PC ends up anywhere else the service has redirected execution
 * (it wants the trap run again later, it is calling guest code, or the trap is patched), and the caller parks the
 * frame for the scheduler in mac_ppc_runtime.c. The Pascal frames are checked on the way out: a signature row that
 * disagrees with what the service popped halts with the size of the disagreement instead of corrupting the stack. */

static int reg_of(const char *t) {
    if ((t[0] != 'D' && t[0] != 'A') || t[1] < '0' || t[1] > '7') return -1;
    return (t[0] == 'D' ? MAC_D0 : MAC_A0) + (t[1] - '0');
}

static int halted(MacRuntime *rt) {
    if (!mac_cpu_halted(rt->cpu)) return 0;
    ppc_cpu_halt(&rt->ppc->cpu, "%s", mac_cpu_error(rt->cpu));
    return 1;
}

static int bad_sig(MacRuntime *rt, const MacPpcFrame *f) {
    return mac_ppc_fail(rt, "The bridge signature \"%s\" for PowerPC import %s is malformed.", f->sig, f->name);
}

static int marshal_registers(MacRuntime *rt, MacPpcFrame *f) {
    PpcCpu *c = &rt->ppc->cpu;
    unsigned next = 0;
    const char *p = f->sig + 1;
    while (*p && *p != '>') {
        if (*p == ',') { p++; continue; }
        int load = *p == '&', store = *p == '*';
        if (*p == '-') { next++; p++; continue; }
        if (load || store) p++;
        int reg = reg_of(p);
        if (reg < 0) return bad_sig(rt, f);
        p += 2;
        if (*p == '=') { /* a constant: uses no parameter */
            char *end;
            mac_cpu_set(rt->cpu, (MacCpuReg)reg, (uint32_t)strtoul(p + 1, &end, 16));
            p = end;
            continue;
        }
        if (next >= MAC_PPC_ARGS) return bad_sig(rt, f);
        uint32_t value = f->args[next++];
        if (load && !ppc_mem_read(c, value, 4, &value)) return -1;
        if (!store) mac_cpu_set(rt->cpu, (MacCpuReg)reg, value);
        if (*p == 'w') p++;
    }
    if (*p != '>') return bad_sig(rt, f);
    const char *selector = strchr(p, '/'); /* a package selector word on the stack, popped by the service */
    if (selector && selector[1] == 'w') {
        mac_cpu_write(rt->cpu, f->a7 - 2, 2, (uint32_t)strtoul(selector + 2, NULL, 16) & 0xffffu);
        mac_cpu_set(rt->cpu, MAC_A7, f->a7 - 2);
    }
    return 0;
}

static int marshal_stack(MacRuntime *rt, MacPpcFrame *f) {
    MacCpu *m = rt->cpu;
    const char *p = f->sig;
    uint32_t sp = f->a7;
    unsigned next = 0;
    if (!strchr("-bwl", p[0]) || p[1] != ':' || (sp & 1) || sp < 0x1000) return bad_sig(rt, f);
    if (p[0] == 'l') { sp -= 4; mac_cpu_write(m, sp, 4, 0); }
    else if (p[0] != '-') { sp -= 2; mac_cpu_write(m, sp, 2, 0); }
    for (p += 2; *p && *p != '/'; p++) {
        if (next >= MAC_PPC_ARGS) return bad_sig(rt, f);
        uint32_t value = f->args[next++];
        if (*p == 'l') { sp -= 4; mac_cpu_write(m, sp, 4, value); }
        else if (*p == 'w') { sp -= 2; mac_cpu_write(m, sp, 2, value & 0xffffu); }
        else if (*p == 'b') { sp -= 2; mac_cpu_write(m, sp, 2, (value & 0xffu) << 8); }
        else return bad_sig(rt, f);
    }
    if (*p == '/') {
        uint32_t selector = (uint32_t)strtoul(p + 2, NULL, 16);
        if (p[1] == 'w') { sp -= 2; mac_cpu_write(m, sp, 2, selector & 0xffffu); }
        else if (p[1] == 'l') { sp -= 4; mac_cpu_write(m, sp, 4, selector); }
        else if (p[1] == 'd') mac_cpu_set(m, MAC_D0, selector);
        else return bad_sig(rt, f);
    }
    mac_cpu_set(m, MAC_A7, sp);
    return 0;
}

int mac_ppc_trap_begin(MacRuntime *rt, MacPpcFrame *f) {
    MacPpcState *st = rt->ppc;
    MacCpu *m = rt->cpu;
    if (st->depth >= MAC_PPC_FRAMES)
        return mac_ppc_fail(rt, "Too many nested switches between the PowerPC and 68K sides (at %s).", f->name);
    f->kind = MAC_PPC_FRAME_TRAP;
    f->stub = st->stubs + 4 * st->depth;
    f->a7 = mac_cpu_get(m, MAC_A7);
    if (f->sig[0] == '@' ? marshal_registers(rt, f) : marshal_stack(rt, f)) return -1;
    mac_cpu_write(m, f->stub, 2, f->trap);
    mac_cpu_set(m, MAC_PC, f->stub + 2); /* as after fetching the A-line word */
    int rc = mac_runtime_trap(m, f->trap, f->stub, rt);
    if (halted(rt) || st->cpu.halted) return -1;
    if (rc) return mac_ppc_fail(rt, "Toolbox trap $%04X behind PowerPC import %s is not implemented.", f->trap, f->name);
    return mac_cpu_get(m, MAC_PC) == f->stub + 2 ? 0 : 1;
}

static int finish_registers(MacRuntime *rt, MacPpcFrame *f, uint32_t *result) {
    PpcCpu *c = &rt->ppc->cpu;
    unsigned next = 0;
    const char *p = f->sig + 1;
    while (*p && *p != '>') {
        if (*p == ',') { p++; continue; }
        if (*p == '-') { next++; p++; continue; }
        int out = *p == '&' || *p == '*';
        if (out) p++;
        int reg = reg_of(p);
        p += 2;
        if (*p == '=') { while (*p && *p != ',' && *p != '>') p++; continue; }
        uint32_t pointer = f->args[next++];
        int word = *p == 'w';
        if (word) p++;
        if (out && !ppc_mem_write(c, pointer, word ? 2 : 4, mac_cpu_get(rt->cpu, (MacCpuReg)reg) & (word ? 0xffffu : UINT32_MAX)))
            return -1;
    }
    p++;
    int reg = *p ? reg_of(p) : -1;
    if (reg >= 0) {
        uint32_t value = mac_cpu_get(rt->cpu, (MacCpuReg)reg);
        *result = p[2] == 'w' ? (uint32_t)(int32_t)(int16_t)value : p[2] == 'b' ? (uint32_t)(int32_t)(int8_t)value : value;
    }
    return 0;
}

int mac_ppc_trap_finish(MacRuntime *rt, MacPpcFrame *f, uint32_t *result) {
    MacCpu *m = rt->cpu;
    if (f->kind == MAC_PPC_FRAME_CALL68K) return mac_ppc_call68k_finish(rt, f, result);
    if (f->sig[0] == '@') {
        if (finish_registers(rt, f, result)) return -1;
    } else {
        uint32_t sp = mac_cpu_get(m, MAC_A7), bytes = f->sig[0] == 'l' ? 4 : f->sig[0] == '-' ? 0 : 2;
        if (sp != f->a7 - bytes)
            return mac_ppc_fail(rt, "Stack imbalance of %d bytes after PowerPC import %s (trap $%04X): its signature "
                                    "\"%s\" disagrees with the trap service.",
                                (int)(sp - (f->a7 - bytes)), f->name, f->trap, f->sig);
        if (f->sig[0] == 'b') *result = mac_cpu_read(m, sp, 1);
        else if (f->sig[0] == 'w') *result = (uint32_t)(int32_t)(int16_t)mac_cpu_read(m, sp, 2);
        else if (f->sig[0] == 'l') *result = mac_cpu_read(m, sp, 4);
    }
    mac_cpu_set(m, MAC_A7, f->a7);
    return halted(rt) ? -1 : 0;
}

static void collect(PpcCpu *c, uint32_t *args) {
    for (unsigned i = 0; i < MAC_PPC_ARGS; i++) {
        const uint8_t *p = i < 8 ? NULL : ppc_mem_span(c, c->gpr[1] + 24 + 4 * i, 4); /* the caller's parameter area */
        args[i] = i < 8 ? c->gpr[3 + i] : p ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3] : 0;
    }
}

int mac_ppc_park(MacRuntime *rt, const MacPpcFrame *f) {
    MacPpcState *st = rt->ppc;
    if (st->sync_depth)
        return mac_ppc_fail(rt, "PowerPC import %s has to wait for the 68K side inside a callback that cannot be suspended.", f->name);
    MacPpcFrame *slot = mac_ppc_push(rt, f->name);
    if (!slot) return -1;
    *slot = *f;
    st->cpu.stop = 1;
    if (f->kind == MAC_PPC_FRAME_TRAP && mac_cpu_get(rt->cpu, MAC_PC) == f->stub) st->blocked = 1;
    return 0;
}

int mac_ppc_bridge_call(PpcCpu *c, void *user) {
    MacRuntime *rt = user;
    const MacPpcSig *sig = rt->ppc->fragment.calling->data;
    MacPpcFrame f = {.name = sig->name, .sig = sig->sig, .trap = sig->trap};
    collect(c, f.args);
    int rc = mac_ppc_trap_begin(rt, &f);
    uint32_t result = c->gpr[3];
    if (rc == 0 && !mac_ppc_trap_finish(rt, &f, &result)) c->gpr[3] = result;
    if (rc == 1) mac_ppc_park(rt, &f);
    return 0; /* a failure has already halted both CPUs with its own diagnostic */
}

int mac_ppc_invoke(MacRuntime *rt, const char *name, uint16_t trap, const char *sig, const uint32_t *args, unsigned count,
                   uint32_t *result) {
    MacPpcFrame f = {.name = name, .sig = sig, .trap = trap};
    uint32_t value = 0;
    if (args) memcpy(f.args, args, sizeof(uint32_t) * (count < MAC_PPC_ARGS ? count : MAC_PPC_ARGS));
    int rc = mac_ppc_trap_begin(rt, &f);
    if (rc == 1) return mac_ppc_fail(rt, "Trap $%04X inside PowerPC import %s would block or call guest code.", trap, name);
    if (rc || mac_ppc_trap_finish(rt, &f, &value)) return -1;
    if (result) *result = value;
    return 0;
}
