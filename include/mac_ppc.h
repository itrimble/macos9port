#ifndef MAC_PPC_H
#define MAC_PPC_H

#include "mac_cfm.h"
#include "mac_runtime.h"

/* PowerPC applications on the 68K runtime's machine: one MacRuntime, one guest RAM, two CPUs.
 *
 * A PowerPC fragment is loaded into the MacRuntime's guest RAM above the A5 world. Its InterfaceLib imports are served
 * by the same trap services 68K applications use: an import is bound either to the generic bridge (a row of the
 * signature table: symbol, A-line trap word, parameter description) or to a small native function where the classic
 * call has no trap. The bridge lays the PowerPC arguments out as the 68K registers or Pascal stack frame the service
 * expects, enters the service through mac_runtime_trap exactly as an A-line instruction would, and copies the result
 * back to r3. docs/POWERPC-RUNTIME.md has the full description.
 *
 * Signature strings (MacPpcSig.sig):
 *
 *   Pascal stack-based Toolbox trap:   "<result>:<arguments>[/<selector>]"
 *     result     '-' none, 'b' Boolean (byte in the high half of a word slot), 'w' 16-bit (sign-extended into r3),
 *                'l' 32-bit
 *     arguments  one character per parameter in declaration order: 'b' Boolean, 'w' 16-bit, 'l' 32-bit (long, pointer,
 *                handle, or a 4-byte structure such as Point, which PowerPC passes by value in one register)
 *     selector   "/w<hex>" pushes a 16-bit selector after the arguments, "/l<hex>" a 32-bit one, "/d<hex>" loads it
 *                into D0 (dispatcher traps)
 *
 *   Register-based OS trap:            "@<in>[,<in>...]><out>"
 *     in         one token per PowerPC parameter in order: a register name (D0 D1 D2 A0 A1) receives the value;
 *                "*<reg>" marks a pointer parameter that receives the 32-bit register after the call ("*<reg>w" 16
 *                bits); "&<reg>" loads the register from the pointed-to long before the call and stores it back
 *                after; "-" ignores the parameter; "<reg>=<hex>" sets a register to a constant and uses no parameter
 *     out        empty (no result), "D0" (32-bit), "D0w" (16-bit, sign-extended), "D0b" (8-bit, sign-extended), "A0"
 */
typedef struct {
    const char *name;
    uint16_t trap;
    const char *sig;
} MacPpcSig;

#define MAC_PPC_FRAMES 16
#define MAC_PPC_ARGS 14 /* r3..r10 and six words of the caller's parameter area */

enum { MAC_PPC_FRAME_TRAP = 1, MAC_PPC_FRAME_CALL68K, MAC_PPC_FRAME_CALLBACK };

/* One pending switch between the two CPUs. TRAP and CALL68K: PowerPC code is waiting for the 68K side (a trap service
 * that blocks or calls guest code, or a 68K routine called through CallUniversalProc). CALLBACK: 68K-side code reached a
 * routine descriptor and is waiting for a PowerPC routine. */
typedef struct {
    uint8_t kind;
    const char *name, *sig;
    uint16_t trap;
    uint32_t stub, a7, proc_info;
    uint32_t args[MAC_PPC_ARGS];
    /* CALLBACK: PowerPC state to put back, and where the 68K side continues */
    uint32_t pc, npc, lr, ctr, r1, r2, ret, new_a7;
} MacPpcFrame;

typedef struct MacPpcState {
    PpcCpu cpu;
    MacCfmFragment fragment;
    uint8_t *pef;                 /* owned copy of the container; the loader keeps pointers into it */
    uint32_t arena_next, arena_limit; /* fragment sections, call gates and the PowerPC stack: above A5, below the screen */
    uint32_t stubs;               /* 68K scratch: MAC_PPC_FRAMES slots of [trap word][pad word] */
    MacPpcFrame frames[MAC_PPC_FRAMES];
    unsigned depth;
    int sync_depth;               /* > 0 while a PowerPC callback runs inside a 68K trap callback (see mac_ppc_mixed.c) */
    int sched_68k;                /* the scheduler itself is stepping the 68K CPU */
    int blocked;                  /* the pending trap asked to be run again later: give the host its time slice */
    int trace;                    /* OS999_PPC_TRACE: print every import call to stderr */
    uint64_t calls;               /* import calls so far */
    char last_import[176];
} MacPpcState;

/* Creates the machine (mac_runtime_init_machine) and loads the PowerPC fragment `pef` into it; the bytes are copied.
 * The application's resource fork is `fork`, opened as for a 68K application. 0 on success. */
int mac_ppc_runtime_init(MacRuntime *rt, const MacResourceFork *fork, const uint8_t *pef, size_t size, char *err, size_t n);
/* 1 when `fork` describes a PowerPC-only application (cfrg member, no CODE 0) and the machine was created from the
 * data fork beside `fork_path`; 0 when the 68K path should be used; -1 on failure with the reason in `err`. */
int mac_ppc_runtime_open(MacRuntime *rt, const MacResourceFork *fork, const char *fork_path, char *err, size_t n);
/* One line for diagnostics: PowerPC pc/lr, the last import called and the call count. */
void mac_ppc_report(const MacRuntime *rt, char *out, size_t n);

/* The complete import table: bridged traps, native functions and the C/Math library subset. */
const MacCfmHostImport *mac_ppc_imports(size_t *count);
/* Signature tables (mac_ppc_sigs_*.c) and native functions (mac_ppc_native*.c, mac_ppc_mixed.c, mac_ppc_files.c). */
const MacPpcSig *mac_ppc_sigs_memory(size_t *count);
const MacPpcSig *mac_ppc_sigs_quickdraw(size_t *count);
const MacPpcSig *mac_ppc_sigs_toolbox(size_t *count);
const MacPpcSig *mac_ppc_sigs_managers(size_t *count);
const MacCfmHostImport *mac_ppc_native_imports(size_t *count);
const MacCfmHostImport *mac_ppc_mixed_imports(size_t *count);
const MacCfmHostImport *mac_ppc_file_imports(size_t *count);
const MacCfmHostImport *mac_ppc_math_imports(size_t *count);

/* The generic bridge (MacCfmHostFn): serves the import whose MacCfmImport.data is a MacPpcSig. */
int mac_ppc_bridge_call(PpcCpu *cpu, void *user);
/* Enters a trap for native glue. `args` are the `count` values the signature consumes. 0: completed, *result set; -1: halted
 * (also when the trap would block or call guest code, which only the bridge itself can wait for). */
int mac_ppc_invoke(MacRuntime *rt, const char *name, uint16_t trap, const char *sig, const uint32_t *args, unsigned count,
                   uint32_t *result);
/* Scheduler internals shared by mac_ppc_bridge.c, mac_ppc_mixed.c and mac_ppc_runtime.c. */
int mac_ppc_trap_begin(MacRuntime *rt, MacPpcFrame *frame);  /* marshal and enter; 0 done, 1 pending, -1 halted */
int mac_ppc_trap_finish(MacRuntime *rt, MacPpcFrame *frame, uint32_t *result); /* read the result, restore A7 */
int mac_ppc_call68k_finish(MacRuntime *rt, MacPpcFrame *frame, uint32_t *result); /* CALL68K frames (mac_ppc_mixed.c) */
int mac_ppc_callback_finish(MacRuntime *rt, MacPpcFrame *frame);
int mac_ppc_park(MacRuntime *rt, const MacPpcFrame *frame);   /* leave a pending frame for the scheduler; 0 or -1 halted */
MacPpcFrame *mac_ppc_push(MacRuntime *rt, const char *name);  /* NULL after halting when the switch stack is full */
int mac_ppc_fail(MacRuntime *rt, const char *format, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif
