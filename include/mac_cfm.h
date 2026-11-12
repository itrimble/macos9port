#ifndef MAC_CFM_H
#define MAC_CFM_H

#include "mac_pef.h"
#include "ppc_cpu.h"

/* Fragment loader: places a PowerPC PEF fragment in guest RAM, relocates it,
 * binds its imports to host-implemented functions and runs its entry points
 * in the interpreter. Clean-room, written from the public PEF description.
 *
 * Host call gate. Every imported symbol gets a 16-byte record in guest RAM:
 *
 *     +0  transition vector: code address (= record + 8)
 *     +4  transition vector: TOC value (0, host code has no TOC)
 *     +8  sc
 *     +12 blr
 *
 * The import's address is the record itself, so compiler-generated cross-TOC
 * glue (load the vector, mtctr, bctr) lands on the `sc`. The interpreter hands
 * `sc` to the loader, which maps the address back to the import and calls the
 * bound C function; the `blr` then returns to the guest caller. Arguments
 * follow the PowerPC calling convention: integers and pointers in r3..r10,
 * floating point in f1..f13, results in r3 (and r4) or f1. A symbol nobody
 * implements is still bound to a gate; calling it halts the CPU with a
 * diagnostic naming the library and symbol. The exception is a weak import
 * (the symbol's weak bit or its library's weak option): with no host
 * implementation it resolves to address 0, the way the system's loader
 * resolves a weak import whose library is not installed, so the fragment's
 * own "is it there" test sees it as absent. */

/* `user` is the pointer given to mac_cfm_load (the MacRuntime once Toolbox
 * imports exist). Return 0 on success; any other value halts the CPU. */
typedef int (*MacCfmHostFn)(PpcCpu *cpu, void *user);

typedef struct {
    const char *library; /* NULL matches any library */
    const char *symbol;
    MacCfmHostFn fn;
    const void *data; /* passed through to MacCfmImport.data; lets one function serve many symbols */
} MacCfmHostImport;

/* Returns the guest address of a zero-filled block of at least `size` bytes, aligned to 4, or 0.
 * Same contract as mac_runtime_alloc, so a MacRuntime can back it directly. */
typedef uint32_t (*MacCfmAlloc)(void *ctx, uint32_t size);

typedef struct {
    char library[64], name[96];
    uint8_t symbol_class, weak, resolved;
    uint8_t absent;   /* a weak import nobody implements: its address is 0, as on a system without the library */
    uint32_t address; /* value added by import relocations */
    MacCfmHostFn fn;
    const void *data;
    uint64_t calls;
} MacCfmImport;

typedef struct {
    PpcCpu *cpu;
    MacPef pef;
    uint32_t section_address[MAC_PEF_MAX_SECTIONS]; /* 0 for sections that are not instantiated */
    MacCfmImport *imports;
    uint32_t import_count, gate_base;
    uint32_t main_vector, init_vector, term_vector; /* guest addresses of transition vectors, 0 when absent */
    uint32_t missing;         /* imports with no host implementation */
    char first_missing[176];  /* "Library::symbol" of the first one */
    uint32_t absent;          /* weak imports resolved to 0 */
    const MacCfmImport *calling; /* the import whose host function is running */
    void (*on_call)(const MacCfmImport *import, PpcCpu *cpu, void *user); /* optional: called before each host call */
    void *user;
    char error[200];
} MacCfmFragment;

#define MAC_CFM_GATE_BYTES 16u

/* 0: loaded, relocated and bound (check `missing`); -1: rejected, reason in fragment->error.
 * `pef` must stay alive until mac_cfm_unload. The fragment installs itself as the CPU's `sc` handler,
 * so one fragment per CPU for now. */
int mac_cfm_load(MacCfmFragment *fragment, PpcCpu *cpu, const void *pef, size_t size,
                 const MacCfmHostImport *table, size_t table_count, MacCfmAlloc alloc, void *alloc_ctx, void *user);
void mac_cfm_unload(MacCfmFragment *fragment);
/* Sets r1 to the top of [base, base + size) with a linkage area and a zero back chain. 0 on success. */
int mac_cfm_stack(MacCfmFragment *fragment, uint32_t base, uint32_t size);
/* Calls through a transition vector. 0: returned; 1: budget exhausted; -1: halted or invalid vector. */
int mac_cfm_call(MacCfmFragment *fragment, uint32_t vector, size_t budget);
/* Guest address of an exported symbol. 0: found; 1: not found; -1: malformed or not addressable. */
int mac_cfm_export(const MacCfmFragment *fragment, const char *name, uint32_t *address);

/* Relocation engine (mac_cfm_reloc.c). */
int mac_cfm_relocate(MacCfmFragment *fragment, const MacPefRelocations *relocations);

/* Host-implemented imports available to every fragment (ppc_host.c). */
const MacCfmHostImport *mac_ppc_host_imports(size_t *count);

#endif
