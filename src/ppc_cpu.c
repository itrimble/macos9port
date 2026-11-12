#include "ppc_int.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ppc_cpu_init(PpcCpu *cpu, uint8_t *ram, size_t ram_size) {
    memset(cpu, 0, sizeof *cpu);
    cpu->ram = ram;
    cpu->ram_size = !ram ? 0 : ram_size > PPC_UNMAPPED_BASE ? PPC_UNMAPPED_BASE : (uint32_t)ram_size;
    cpu->pc = PPC_STOP_PC;
}

void ppc_cpu_halt(PpcCpu *cpu, const char *format, ...) {
    if (cpu->halted) return; /* the first diagnostic is the cause */
    cpu->halted = 1;
    va_list args;
    va_start(args, format);
    vsnprintf(cpu->error, sizeof cpu->error, format, args);
    va_end(args);
}

uint8_t *ppc_mem_span(PpcCpu *cpu, uint32_t address, uint32_t length) {
    if (!cpu->ram || address > cpu->ram_size || length > cpu->ram_size - address) return NULL;
    return cpu->ram + address;
}

int ppc_mem_read(PpcCpu *cpu, uint32_t address, unsigned width, uint32_t *value) {
    const uint8_t *p = ppc_mem_span(cpu, address, width);
    *value = 0;
    if (!p || cpu->halted) {
        ppc_cpu_halt(cpu, "PowerPC load of %u byte(s) outside guest RAM at %08x (pc %08x).", width, address, cpu->pc);
        return 0;
    }
    for (unsigned i = 0; i < width; i++) *value = (*value << 8) | p[i];
    return 1;
}

int ppc_mem_write(PpcCpu *cpu, uint32_t address, unsigned width, uint32_t value) {
    uint8_t *p = ppc_mem_span(cpu, address, width);
    if (!p || cpu->halted) {
        ppc_cpu_halt(cpu, "PowerPC store of %u byte(s) outside guest RAM at %08x (pc %08x).", width, address, cpu->pc);
        return 0;
    }
    for (unsigned i = 0; i < width; i++) p[i] = (uint8_t)(value >> (8u * (width - 1u - i)));
    return 1;
}

int ppc_mem_read64(PpcCpu *cpu, uint32_t address, uint64_t *value) {
    const uint8_t *p = ppc_mem_span(cpu, address, 8);
    *value = 0;
    if (!p || cpu->halted) {
        ppc_cpu_halt(cpu, "PowerPC load of 8 byte(s) outside guest RAM at %08x (pc %08x).", address, cpu->pc);
        return 0;
    }
    for (unsigned i = 0; i < 8; i++) *value = (*value << 8) | p[i];
    return 1;
}

int ppc_mem_write64(PpcCpu *cpu, uint32_t address, uint64_t value) {
    uint8_t *p = ppc_mem_span(cpu, address, 8);
    if (!p || cpu->halted) {
        ppc_cpu_halt(cpu, "PowerPC store of 8 byte(s) outside guest RAM at %08x (pc %08x).", address, cpu->pc);
        return 0;
    }
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (8u * (7u - i)));
    return 1;
}

double ppc_fpr_get(const PpcCpu *cpu, unsigned index) {
    double d;
    memcpy(&d, &cpu->fpr[index & 31u], sizeof d);
    return d;
}

void ppc_fpr_set(PpcCpu *cpu, unsigned index, double value) {
    memcpy(&cpu->fpr[index & 31u], &value, sizeof value);
}

static int execute(PpcCpu *c, uint32_t w) {
    switch (PPC_OP(w)) {
    case 7: case 8: case 10: case 11: case 12: case 13: case 14: case 15:
    case 20: case 21: case 23: case 24: case 25: case 26: case 27: case 28: case 29:
        return ppc_exec_int(c, w);
    case 16: case 17: case 18: case 19:
        return ppc_exec_branch(c, w);
    case 31:
        return ppc_exec_int(c, w) || ppc_exec_logic(c, w) || ppc_exec_mem(c, w) || ppc_exec_fp_mem(c, w);
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
        return ppc_exec_mem(c, w);
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55:
        return ppc_exec_fp_mem(c, w);
    case 59: case 63:
        return ppc_exec_fp(c, w);
    default:
        return 0;
    }
}

size_t ppc_cpu_run(PpcCpu *cpu, size_t budget) {
    size_t done = 0;
    cpu->stop = 0;
    while (done < budget && !cpu->halted && !cpu->stop && cpu->pc != PPC_STOP_PC) {
        uint32_t pc = cpu->pc;
        const uint8_t *p = (pc & 3u) ? NULL : ppc_mem_span(cpu, pc, 4);
        done++;
        if (!p) {
            ppc_cpu_halt(cpu, "PowerPC instruction fetch outside guest RAM or misaligned at pc %08x.", pc);
            break;
        }
        uint32_t w = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
        cpu->npc = pc + 4;
        if (!execute(cpu, w))
            ppc_cpu_halt(cpu, "Unimplemented PowerPC instruction %08x at pc %08x (primary opcode %u, extended %u).",
                         w, pc, PPC_OP(w), PPC_XO(w));
        if (cpu->halted) break; /* pc stays on the instruction that faulted */
        cpu->pc = cpu->npc;
        cpu->instructions++;
    }
    return done;
}

int ppc_cpu_call(PpcCpu *cpu, uint32_t code, uint32_t toc, size_t budget) {
    if (cpu->halted) return -1;
    cpu->pc = code;
    cpu->gpr[2] = toc;
    cpu->lr = PPC_STOP_PC;
    ppc_cpu_run(cpu, budget);
    if (cpu->halted) return -1;
    return cpu->pc == PPC_STOP_PC ? 0 : 1;
}
