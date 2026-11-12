#ifndef PPC_CPU_H
#define PPC_CPU_H

#include <stddef.h>
#include <stdint.h>

/* Clean-room 32-bit big-endian PowerPC user-mode interpreter. It is written
 * from the public instruction-set definition only; no emulator source was
 * consulted and nothing here is vendored. One execution thread per instance.
 *
 * Guest memory is a flat big-endian buffer borrowed from the caller, so the
 * same bytes can be the 68K runtime's guest RAM (mac_cpu_ram). Every guest
 * access is bounds-checked; a fault halts with a diagnostic, it never reads
 * or writes host memory outside the buffer. */

#define PPC_XER_SO UINT32_C(0x80000000)
#define PPC_XER_OV UINT32_C(0x40000000)
#define PPC_XER_CA UINT32_C(0x20000000)

#define PPC_CR_LT 8u
#define PPC_CR_GT 4u
#define PPC_CR_EQ 2u
#define PPC_CR_SO 1u /* "unordered" for floating-point compares */

#define PPC_FPSCR_FX     UINT32_C(0x80000000)
#define PPC_FPSCR_VX     UINT32_C(0x20000000)
#define PPC_FPSCR_ZX     UINT32_C(0x04000000)
#define PPC_FPSCR_VXSNAN UINT32_C(0x01000000)
#define PPC_FPSCR_VXVC   UINT32_C(0x00080000)
#define PPC_FPSCR_FPRF   UINT32_C(0x0001f000)
#define PPC_FPSCR_VXCVI  UINT32_C(0x00000100)

/* Returning to this address ends ppc_cpu_call. It is never inside guest RAM. */
#define PPC_STOP_PC UINT32_C(0xfffffffc)
/* Guest RAM may not reach this address; the loader parks unresolved data imports above it. */
#define PPC_UNMAPPED_BASE UINT32_C(0xff000000)

typedef struct PpcCpu PpcCpu;

/* Called for the `sc` instruction with the address of that instruction.
 * Return 0 when handled (execution continues at npc, which is pc + 4 unless
 * the handler changed it; registers may have been changed). Any other value
 * halts the CPU. Must not run the CPU. */
typedef int (*PpcSyscall)(PpcCpu *cpu, uint32_t pc, void *user);

struct PpcCpu {
    uint32_t gpr[32];
    uint64_t fpr[32]; /* IEEE double bit patterns, kept bit-exact */
    uint32_t pc, npc, lr, ctr, xer, cr, fpscr;
    uint8_t *ram;
    uint32_t ram_size;
    uint64_t instructions;
    PpcSyscall syscall;
    void *syscall_user;
    int halted;
    int stop; /* set by a syscall handler to end the current ppc_cpu_run after this instruction; cleared on entry */
    char error[200];
};

/* `ram` stays owned by the caller. Sizes above PPC_UNMAPPED_BASE are clipped. */
void ppc_cpu_init(PpcCpu *cpu, uint8_t *ram, size_t ram_size);
/* Runs up to `budget` instructions; stops early on halt or when pc reaches PPC_STOP_PC. Returns attempts. */
size_t ppc_cpu_run(PpcCpu *cpu, size_t budget);
/* Calls guest code as a function: pc = code, r2 = toc, lr = PPC_STOP_PC. Arguments (r3..r10, f1..) and r1
 * must already be set. 0: the routine returned; 1: budget exhausted; -1: halted. */
int ppc_cpu_call(PpcCpu *cpu, uint32_t code, uint32_t toc, size_t budget);
void ppc_cpu_halt(PpcCpu *cpu, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Checked big-endian access (width 1, 2 or 4). Returns 1 on success; a fault halts and returns 0. */
int ppc_mem_read(PpcCpu *cpu, uint32_t address, unsigned width, uint32_t *value);
int ppc_mem_write(PpcCpu *cpu, uint32_t address, unsigned width, uint32_t value);
int ppc_mem_read64(PpcCpu *cpu, uint32_t address, uint64_t *value);
int ppc_mem_write64(PpcCpu *cpu, uint32_t address, uint64_t value);
/* Host pointer to `length` guest bytes, or NULL when the span leaves guest RAM. Does not halt. */
uint8_t *ppc_mem_span(PpcCpu *cpu, uint32_t address, uint32_t length);

double ppc_fpr_get(const PpcCpu *cpu, unsigned index);
void ppc_fpr_set(PpcCpu *cpu, unsigned index, double value);

#endif
