#ifndef MAC_CPU_H
#define MAC_CPU_H

#include <stddef.h>
#include <stdint.h>

/* ROM-free CPU adapter. One execution thread; instances may run sequentially.
 * Guest exception vectors and Macintosh services belong to the runtime.
 * The CPU dependency is Musashi (MIT-style license), not an Apple component.
 */
typedef struct MacCpu MacCpu;
typedef enum { MAC_CPU_68000, MAC_CPU_68020 } MacCpuModel;
typedef enum {
    MAC_D0, MAC_D1, MAC_D2, MAC_D3, MAC_D4, MAC_D5, MAC_D6, MAC_D7,
    MAC_A0, MAC_A1, MAC_A2, MAC_A3, MAC_A4, MAC_A5, MAC_A6, MAC_A7,
    MAC_PC, MAC_SR
} MacCpuReg;

/* PC is advanced past the A-line word before this callback. Return zero when
 * handled. The callback may change registers/PC or halt, but must not run a CPU.
 */
typedef int (*MacCpuTrap)(MacCpu *cpu, uint16_t opcode, uint32_t pc, void *user);

MacCpu *mac_cpu_create(size_t ram_size);
MacCpu *mac_cpu_create_model(size_t ram_size, MacCpuModel model);
void mac_cpu_destroy(MacCpu *cpu);
uint8_t *mac_cpu_ram(MacCpu *cpu);
size_t mac_cpu_ram_size(const MacCpu *cpu);
uint32_t mac_cpu_get(const MacCpu *cpu, MacCpuReg reg);
void mac_cpu_set(MacCpu *cpu, MacCpuReg reg, uint32_t value);
void mac_cpu_set_trap(MacCpu *cpu, MacCpuTrap trap, void *user);
/* One complete instruction per step; zero budget does nothing. Returns attempts.
 * A budget is a host work bound, not an emulated frame or wall-clock guarantee.
 */
size_t mac_cpu_run(MacCpu *cpu, size_t instruction_budget);
/* Batch execution for the interactive cycle-paced runtime. */
size_t mac_cpu_run_cycles(MacCpu *cpu, uint64_t cycle_budget);
/* Ends the current mac_cpu_run/mac_cpu_run_cycles slice early (from a trap callback). Registers are untouched. */
void mac_cpu_yield(MacCpu *cpu);
void mac_cpu_cancel_yield(MacCpu *cpu);   /* withdraws a yield requested earlier in the same trap (a guest call is about to run and will yield itself) */
void mac_cpu_halt(MacCpu *cpu, const char *reason);
int mac_cpu_halted(const MacCpu *cpu);
const char *mac_cpu_error(const MacCpu *cpu);
uint64_t mac_cpu_cycles(const MacCpu *cpu);
/* Checked big-endian memory helpers. Invalid or odd word/long access halts. */
uint32_t mac_cpu_read(MacCpu *cpu, uint32_t address, unsigned width);
int mac_cpu_write(MacCpu *cpu, uint32_t address, unsigned width, uint32_t value);

#endif
