#include "mac_cfm.h"

#include <math.h>
#include <string.h>

/* Host-implemented imports that need no MacRuntime: pure functions over
 * registers and guest memory, enough to prove the call gate on its own.
 * Toolbox imports are served by the bridge (mac_ppc_bridge.c and the tables
 * beside it); mac_ppc_imports() returns these rows together with those. */

static int fault(PpcCpu *cpu, const char *name, uint32_t address) {
    ppc_cpu_halt(cpu, "PowerPC host import %s touched memory outside guest RAM at %08x.", name, address);
    return -1;
}

static int host_strlen(PpcCpu *cpu, void *user) {
    (void)user;
    uint32_t s = cpu->gpr[3];
    const uint8_t *p = ppc_mem_span(cpu, s, 0);
    const uint8_t *end = p ? memchr(p, 0, cpu->ram_size - s) : NULL;
    if (!end) return fault(cpu, "strlen", s);
    cpu->gpr[3] = (uint32_t)(end - p);
    return 0;
}

static int host_memcpy(PpcCpu *cpu, void *user) {
    (void)user;
    uint32_t n = cpu->gpr[5];
    uint8_t *d = ppc_mem_span(cpu, cpu->gpr[3], n), *s = ppc_mem_span(cpu, cpu->gpr[4], n);
    if (!d) return fault(cpu, "memcpy", cpu->gpr[3]);
    if (!s) return fault(cpu, "memcpy", cpu->gpr[4]);
    memmove(d, s, n); /* r3 already holds the destination, which is the result */
    return 0;
}

static int host_memset(PpcCpu *cpu, void *user) {
    (void)user;
    uint8_t *d = ppc_mem_span(cpu, cpu->gpr[3], cpu->gpr[5]);
    if (!d) return fault(cpu, "memset", cpu->gpr[3]);
    memset(d, (int)(cpu->gpr[4] & 0xffu), cpu->gpr[5]);
    return 0;
}

static int host_abs(PpcCpu *cpu, void *user) {
    (void)user;
    if ((int32_t)cpu->gpr[3] < 0) cpu->gpr[3] = 0u - cpu->gpr[3];
    return 0;
}

static int host_sqrt(PpcCpu *cpu, void *user) {
    (void)user;
    ppc_fpr_set(cpu, 1, sqrt(ppc_fpr_get(cpu, 1)));
    return 0;
}

static const MacCfmHostImport imports[] = {
    {"StdCLib", "strlen", host_strlen, NULL},
    {"StdCLib", "memcpy", host_memcpy, NULL},
    {"StdCLib", "memset", host_memset, NULL},
    {"StdCLib", "abs", host_abs, NULL},
    {"MathLib", "sqrt", host_sqrt, NULL},
};

const MacCfmHostImport *mac_ppc_host_imports(size_t *count) {
    if (count) *count = sizeof imports / sizeof imports[0];
    return imports;
}
