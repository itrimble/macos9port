#ifndef MAC_INIT_H
#define MAC_INIT_H

#include "mac_runtime.h"

/* Extension (INIT) execution inside ospi-runtime.
 *
 * Contract (OS999, not a claim of full Macintosh INIT compatibility):
 *  - Only INIT resources with IDs 0 through 31 are startup code; they run once
 *    each, in ascending ID order, as a plain subroutine. Higher IDs are patch
 *    code that a startup INIT installs; running them would corrupt the stack.
 *    A plain subroutine: PC at byte 0, return address on the stack, D0 = 0.
 *  - D0 on return is ignored. Real INITs do not define a result: of 268 real
 *    INIT forks, 53 left a nonzero D0 and still ran to completion. An INIT
 *    fails only by patching a protected trap, halting the CPU on an
 *    unsupported call, or exceeding its instruction budget.
 *  - Trap patches (SetTrapAddress) are allowed, except the protected traps:
 *    GetTrapAddress, SetTrapAddress, LoadSeg and ExitToShell. Attempting one
 *    fails the extension.
 *  - NewGestalt/ReplaceGestalt record the procedure but never call it; the
 *    selector reports "present" with response 0. Hosts can register real
 *    values with mac_gestalt_set().
 *  - A failed extension is rolled back: trap table, Gestalt table and the
 *    heap it used. CPU registers are restored after every INIT. */

#define MAC_INIT_TYPE UINT32_C(0x494e4954)
#define MAC_INIT_BUDGET 2000000u

typedef struct {
    int count;       /* INIT resources executed */
    char error[96];  /* empty on success */
} MacInitReport;

/* Bare guest used only to host extensions (no application). 0 ok, -1 error. */
int mac_init_host_create(MacRuntime *rt, char *err, size_t n);

/* 0: ran (or no INIT resources; report->count == 0)
 * -1: failed and rolled back, runtime still usable
 * -2: failed and the CPU halted (an unsupported trap); runtime is unusable */
int mac_init_run(MacRuntime *rt, const MacResourceFork *fork, size_t budget, MacInitReport *report);

#endif
