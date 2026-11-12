#include "mac_cpu.h"
#include "m68k.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

struct MacCpu {
    uint8_t *ram;
    size_t ram_size;
    void *context;
    MacCpuTrap trap;
    void *trap_user;
    uint64_t cycles;
    size_t instructions_in_slice;
    int count_instructions;
    int halted;
    int yielded;
    MacCpuModel model;
    uint32_t amask;
    char error[192];
};

static MacCpu *active;

static void instruction_hook(unsigned int pc) {
    if (!active) return;
    if (active->count_instructions) active->instructions_in_slice++;
    uint32_t address = pc & active->amask;
    if ((size_t)address + 2 > active->ram_size) {
        mac_cpu_halt(active, "Instruction fetch exceeds guest RAM.");
        return;
    }
    uint16_t opcode = (uint16_t)((active->ram[address] << 8) | active->ram[address + 1]);
    if ((opcode & 0xf000) == 0xf000 || opcode == 0x4e72) {
        char error[128];
        snprintf(error, sizeof error, "unsupported instruction %04x at %08x", opcode, address);
        mac_cpu_halt(active, error);
    }
}

void mac_cpu_halt(MacCpu *cpu, const char *reason) {
    if (!cpu || cpu->halted) {
        return;
    }
    cpu->halted = 1;
    snprintf(cpu->error, sizeof cpu->error, "%s", reason ? reason : "halted");
    if (active == cpu) {
        m68k_end_timeslice();
    }
}

void mac_cpu_cancel_yield(MacCpu *cpu) { if (cpu) cpu->yielded = 0; }
void mac_cpu_yield(MacCpu *cpu) {
    if (!cpu || cpu->halted) return;
    cpu->yielded = 1;
    if (active == cpu) m68k_end_timeslice();
}

/* A flag kept in a pointer's high byte works in 24-bit mode only. In 32-bit mode that address is far outside RAM; say
 * what it probably is instead of leaving a bare fault. */
static void halt_bad_access(MacCpu *cpu, const char *what, unsigned width, uint32_t address) {
    char error[256];
    snprintf(error, sizeof error, "invalid %u-byte memory %s at %08x (PC %08x)",
             width, what, address, mac_cpu_get(cpu, MAC_PC));
    if (cpu->amask == 0xffffffffu && (size_t)address >= cpu->ram_size && (address >> 24) &&
        (address & 0xffffff) < cpu->ram_size)
        snprintf(error + strlen(error), sizeof error - strlen(error),
                 "; looks like a 24-bit-dirty address, try --addressing 24");
    mac_cpu_halt(cpu, error);
}

static int valid_access(MacCpu *cpu, uint32_t address, unsigned width) {
    if (!cpu || cpu->halted) {
        return 0;
    }
    if ((width != 1 && width != 2 && width != 4) || address > cpu->ram_size ||
        width > cpu->ram_size - address || (width != 1 && (address & 1))) {
        halt_bad_access(cpu, "access", width, address);
        return 0;
    }
    return 1;
}

uint32_t mac_cpu_read(MacCpu *cpu, uint32_t address, unsigned width) {
    if (!valid_access(cpu, address, width)) {
        return 0;
    }
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++) {
        value = (value << 8) | cpu->ram[address + i];
    }
    return value;
}

int mac_cpu_write(MacCpu *cpu, uint32_t address, unsigned width, uint32_t value) {
    if (!valid_access(cpu, address, width)) {
        return -1;
    }
    for (unsigned i = 0; i < width; i++) {
        cpu->ram[address + width - i - 1] = (uint8_t)(value >> (8 * i));
    }
    return 0;
}

static uint32_t bus_read(uint32_t address, unsigned width) {
    if (!active) return 0;
    address &= active->amask; /* 24-bit: the original Macintosh mode; 32-bit: the full 68020 bus */
    if (active->model == MAC_CPU_68000 && width > 1 && (address & 1))
        return mac_cpu_read(active, address, width); /* Checked alignment fault. */
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++)
        value = (value << 8) | mac_cpu_read(active, (address + i) & active->amask, 1);
    return value;
}
static void bus_write(uint32_t address, unsigned width, uint32_t value) {
    if (!active) return;
    uint32_t raw = address;
    address &= active->amask;
    if (active->model == MAC_CPU_68000 && width > 1 && (address & 1)) {
        mac_cpu_write(active, address, width, value); return;
    }
    /* Check every translated byte before writing, including address-bus wrap.
     * A failed instruction must not leave a partially written guest word. */
    for (unsigned i = 0; i < width; i++) {
        if (((address + i) & active->amask) >= active->ram_size) {
            halt_bad_access(active, "write", width, raw); return;
        }
    }
    for (unsigned i = 0; i < width; i++)
        mac_cpu_write(active, (address + i) & active->amask, 1, value >> (8 * (width - i - 1)));
}
unsigned int m68k_read_memory_8(unsigned int address) { return bus_read(address, 1); }
unsigned int m68k_read_memory_16(unsigned int address) { return bus_read(address, 2); }
unsigned int m68k_read_memory_32(unsigned int address) { return bus_read(address, 4); }
void m68k_write_memory_8(unsigned int address, unsigned int value) {
    bus_write(address, 1, value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
    bus_write(address, 2, value);
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
    bus_write(address, 4, value);
}

static int illegal_instruction(int opcode) {
    char error[128];
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PPC) & (active ? active->amask : 0xffffffu);
    if ((opcode & 0xf000) == 0xa000) {
        if (active && active->trap && active->trap(active, (uint16_t)opcode, pc,
                                                   active->trap_user) == 0)
            return 1;
        snprintf(error, sizeof error, "unhandled A-line trap %04x at %08x (D0 %08x)", opcode & 0xffff, pc, (unsigned)m68k_get_reg(NULL, M68K_REG_D0)); /* D0 names the selector of the dispatching traps */
        mac_cpu_halt(active, error);
        return 1;
    }
    if ((opcode & 0xf000) == 0xf000) {
        snprintf(error, sizeof error, "unsupported instruction %04x at %08x", opcode & 0xffff, pc);
        mac_cpu_halt(active, error);
        return 1;
    }
    snprintf(error, sizeof error, "illegal instruction %04x at %08x", opcode & 0xffff,
             pc);
    mac_cpu_halt(active, error);
    return 1;
}

MacCpu *mac_cpu_create(size_t ram_size) {
    return mac_cpu_create_model(ram_size, MAC_CPU_68000);
}

static char create_error[160];
static int fail_next_alloc;
const char *mac_cpu_create_error(void) { return create_error; }
void mac_cpu_test_fail_next_alloc(int fail) { fail_next_alloc = fail; }
MacAddrMode mac_cpu_addr_mode(const MacCpu *cpu) { return cpu && cpu->amask == 0xffffffffu ? MAC_ADDR_32 : MAC_ADDR_24; }
uint32_t mac_cpu_addr_mask(const MacCpu *cpu) { return cpu ? cpu->amask : 0xffffffu; }

/* Reserve zeroed guest RAM. Anonymous mappings are committed page by page on first touch, so 256 MB of guest RAM
 * costs address space until the guest uses it. mmap, unlike a calloc of this size, reports failure cleanly. */
static uint8_t *ram_reserve(size_t bytes) {
    if (fail_next_alloc) { fail_next_alloc = 0; return NULL; }
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON
#ifdef MAP_NORESERVE
                   | MAP_NORESERVE
#endif
                   , -1, 0);
    return p == MAP_FAILED ? NULL : (uint8_t *)p;
}

MacCpu *mac_cpu_create_model(size_t ram_size, MacCpuModel model) {
    return mac_cpu_create_mode(ram_size, model, ram_size > MAC_CPU_RAM_24BIT_MAX ? MAC_ADDR_32 : MAC_ADDR_24);
}

MacCpu *mac_cpu_create_mode(size_t ram_size, MacCpuModel model, MacAddrMode mode) {
    create_error[0] = 0;
    if (active || ram_size < MAC_CPU_RAM_MIN || ram_size > MAC_CPU_RAM_MAX || (ram_size & 1) ||
        (model != MAC_CPU_68000 && model != MAC_CPU_68020) ||
        (mode != MAC_ADDR_24 && mode != MAC_ADDR_32) ||
        (mode == MAC_ADDR_32 && model == MAC_CPU_68000) ||
        (mode == MAC_ADDR_24 && ram_size > MAC_CPU_RAM_24BIT_MAX)) {
        snprintf(create_error, sizeof create_error,
                 "Unsupported guest memory configuration (%zu bytes, %s addressing).",
                 ram_size, mode == MAC_ADDR_32 ? "32-bit" : "24-bit");
        return NULL;
    }
    MacCpu *cpu = calloc(1, sizeof *cpu);
    if (!cpu) {
        snprintf(create_error, sizeof create_error, "Out of host memory creating the CPU.");
        return NULL;
    }
    cpu->ram = ram_reserve(ram_size);
    cpu->context = calloc(1, m68k_context_size());
    cpu->ram_size = ram_size;
    cpu->model = model;
    cpu->amask = mode == MAC_ADDR_32 ? 0xffffffffu : 0xffffffu;
    if (!cpu->ram || !cpu->context) {
        snprintf(create_error, sizeof create_error,
                 "Could not reserve %zu MB of guest memory from the host.", ram_size >> 20);
        mac_cpu_destroy(cpu);
        return NULL;
    }
    active = cpu;
    m68k_init();
    m68k_set_cpu_type(model == MAC_CPU_68020 ? M68K_CPU_TYPE_68020 : M68K_CPU_TYPE_68000);
    m68k_set_instr_hook_callback(instruction_hook);
    m68k_pulse_reset();
    /* Consume reset latency without executing an instruction. */
    m68k_execute(1);
    for (int i = MAC_D0; i <= MAC_A7; i++) {
        m68k_set_reg((m68k_register_t)i, 0);
    }
    m68k_set_reg(M68K_REG_SR, 0);
    m68k_set_reg(M68K_REG_SP, (unsigned int)ram_size);
    m68k_set_reg(M68K_REG_PC, 0);
    m68k_set_illg_instr_callback(illegal_instruction);
    m68k_get_context(cpu->context);
    active = NULL;
    return cpu;
}

void mac_cpu_destroy(MacCpu *cpu) {
    if (cpu && active != cpu) {
        free(cpu->context);
        if (cpu->ram) munmap(cpu->ram, cpu->ram_size);
        free(cpu);
    }
}

uint8_t *mac_cpu_ram(MacCpu *cpu) { return cpu ? cpu->ram : NULL; }
size_t mac_cpu_ram_size(const MacCpu *cpu) { return cpu ? cpu->ram_size : 0; }

uint32_t mac_cpu_get(const MacCpu *cpu, MacCpuReg reg) {
    if (!cpu || reg < MAC_D0 || reg > MAC_SR) {
        return 0;
    }
    return m68k_get_reg(active == cpu ? NULL : cpu->context, (m68k_register_t)reg);
}

void mac_cpu_set(MacCpu *cpu, MacCpuReg reg, uint32_t value) {
    if (!cpu || reg < MAC_D0 || reg > MAC_SR || (active && active != cpu)) {
        return;
    }
    if (!active) {
        m68k_set_context(cpu->context);
    }
    m68k_set_reg((m68k_register_t)reg, value);
    if (!active) {
        m68k_get_context(cpu->context);
    }
}

void mac_cpu_set_trap(MacCpu *cpu, MacCpuTrap trap, void *user) {
    if (cpu) {
        cpu->trap = trap;
        cpu->trap_user = user;
    }
}

size_t mac_cpu_run(MacCpu *cpu, size_t instruction_budget) {
    if (!cpu || cpu->halted || active || !instruction_budget) {
        return 0;
    }
    active = cpu;
    m68k_set_context(cpu->context);
    size_t attempted = 0;
    cpu->yielded = 0;
    while (attempted < instruction_budget && !cpu->halted && !cpu->yielded) {
        uint32_t pc = mac_cpu_get(cpu, MAC_PC) & cpu->amask;
        mac_cpu_set(cpu, MAC_PC, pc);
        uint16_t opcode = (uint16_t)mac_cpu_read(cpu, pc, 2);
        if (cpu->halted) {
            break;
        }
        attempted++;
        if ((opcode & 0xf000) == 0xa000) {
            mac_cpu_set(cpu, MAC_PC, (pc + 2) & cpu->amask);
            if (!cpu->trap || cpu->trap(cpu, opcode, pc, cpu->trap_user) != 0) {
                char error[128];
                snprintf(error, sizeof error, "unhandled A-line trap %04x at %08x (D0 %08x)", opcode, pc, mac_cpu_get(cpu, MAC_D0));
                mac_cpu_halt(cpu, error);
            }
            cpu->cycles += 4; /* Adapter dispatch work, not a Macintosh timing model. */
        } else if ((opcode & 0xf000) == 0xf000 || opcode == 0x4e72) {
            char error[128];
            snprintf(error, sizeof error, "unsupported instruction %04x at %08x", opcode, pc);
            mac_cpu_halt(cpu, error);
        } else {
            int cycles = m68k_execute(1);
            if (cycles > 0) {
                cpu->cycles += (unsigned int)cycles;
            }
        }
    }
    mac_cpu_set(cpu, MAC_PC, mac_cpu_get(cpu, MAC_PC) & cpu->amask);
    m68k_get_context(cpu->context);
    active = NULL;
    return attempted;
}

size_t mac_cpu_run_cycles(MacCpu *cpu, uint64_t cycle_budget) {
    if (!cpu || cpu->halted || active || !cycle_budget) return 0;
    active = cpu;
    m68k_set_context(cpu->context);
    cpu->instructions_in_slice = 0;
    cpu->count_instructions = 1;
    cpu->yielded = 0;
    while (cycle_budget && !cpu->halted && !cpu->yielded) {
        int request = cycle_budget > INT_MAX ? INT_MAX : (int)cycle_budget;
        int used = m68k_execute(request);
        if (used <= 0) break;
        cpu->cycles += (unsigned)used;
        if ((uint64_t)used >= cycle_budget) break;
        cycle_budget -= (unsigned)used;
    }
    cpu->count_instructions = 0;
    m68k_get_context(cpu->context);
    active = NULL;
    return cpu->instructions_in_slice;
}

int mac_cpu_halted(const MacCpu *cpu) { return !cpu || cpu->halted; }
const char *mac_cpu_error(const MacCpu *cpu) { return cpu ? cpu->error : "no CPU"; }
uint64_t mac_cpu_cycles(const MacCpu *cpu) { return cpu ? cpu->cycles : 0; }
