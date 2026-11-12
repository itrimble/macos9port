#include "mac_ppc.h"
#include "mac_ppc_probe.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* PowerPC application start path and the two-CPU scheduler. See include/mac_ppc.h and docs/POWERPC-RUNTIME.md. */

#define PPC_STACK_BYTES UINT32_C(0x40000)
#define PPC_ARENA_GAP UINT32_C(0x10000) /* room above A5 for the (empty) jump table area */

int mac_ppc_fail(MacRuntime *rt, const char *format, ...) {
    char message[200];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    if (rt->ppc) ppc_cpu_halt(&rt->ppc->cpu, "%s", message);
    mac_cpu_halt(rt->cpu, message);
    return -1;
}

static uint32_t arena_alloc(void *ctx, uint32_t size) {
    MacPpcState *st = ctx;
    uint32_t aligned = (size + 3u) & ~UINT32_C(3);
    if (aligned < size || st->arena_next > st->arena_limit || aligned > st->arena_limit - st->arena_next) return 0;
    uint32_t at = st->arena_next;
    st->arena_next += aligned ? aligned : 4;
    return at; /* guest RAM starts zeroed and the arena is never reused */
}

static void on_call(const MacCfmImport *import, PpcCpu *cpu, void *user) {
    MacRuntime *rt = user;
    MacPpcState *st = rt->ppc;
    st->calls++;
    snprintf(st->last_import, sizeof st->last_import, "%s::%s", import->library, import->name);
    if (st->trace || rt->trace)
        fprintf(stderr, "ppc-import %llu %s r3=%08x r4=%08x r5=%08x r6=%08x lr=%08x\n", (unsigned long long)st->calls,
                st->last_import, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->lr);
}

static const MacCfmHostImport *g_table;
static size_t g_table_count;

static void add_imports(MacCfmHostImport *out, size_t *n, const MacCfmHostImport *in, size_t count) {
    for (size_t i = 0; i < count; i++) out[(*n)++] = in[i];
}

static void add_sigs(MacCfmHostImport *out, size_t *n, const MacPpcSig *sigs, size_t count) {
    for (size_t i = 0; i < count; i++)
        out[(*n)++] = (MacCfmHostImport){"InterfaceLib", sigs[i].name, mac_ppc_bridge_call, &sigs[i]};
}

const MacCfmHostImport *mac_ppc_imports(size_t *count) {
    if (!g_table) {
        size_t n[9], total = 0, used = 0;
        const MacPpcSig *sigs[4] = {mac_ppc_sigs_memory(&n[0]), mac_ppc_sigs_quickdraw(&n[1]), mac_ppc_sigs_toolbox(&n[2]),
                                    mac_ppc_sigs_managers(&n[3])};
        const MacCfmHostImport *direct[5] = {mac_ppc_native_imports(&n[4]), mac_ppc_mixed_imports(&n[5]),
                                             mac_ppc_file_imports(&n[6]), mac_ppc_math_imports(&n[7]),
                                             mac_ppc_host_imports(&n[8])};
        for (unsigned i = 0; i < 9; i++) total += n[i];
        MacCfmHostImport *table = calloc(total ? total : 1, sizeof *table); /* lives for the process */
        if (!table) { if (count) *count = 0; return NULL; }
        /* Native functions first: where both exist for a symbol, the hand-written one wins. */
        for (unsigned i = 0; i < 5; i++) add_imports(table, &used, direct[i], n[4 + i]);
        for (unsigned i = 0; i < 4; i++) add_sigs(table, &used, sigs[i], n[i]);
        g_table = table;
        g_table_count = used;
    }
    if (count) *count = g_table_count;
    return g_table;
}

int mac_ppc_runtime_init(MacRuntime *rt, const MacResourceFork *fork, const uint8_t *pef, size_t size, char *err, size_t n) {
    if (mac_runtime_init_machine(rt, fork, err, n)) return -1;
    MacPpcState *st = calloc(1, sizeof *st);
    size_t count;
    const MacCfmHostImport *table = mac_ppc_imports(&count);
    if (st) st->pef = malloc(size ? size : 1);
    if (!st || !st->pef || !table) {
        if (st) free(st->pef);
        free(st);
        snprintf(err, n, "Out of host memory for the PowerPC runtime.");
        mac_runtime_destroy(rt);
        return -1;
    }
    memcpy(st->pef, pef, size);
    rt->ppc = st;
    const char *trace = getenv("OS999_PPC_TRACE");
    st->trace = trace && trace[0] && trace[0] != '0';
    ppc_cpu_init(&st->cpu, mac_cpu_ram(rt->cpu), mac_cpu_ram_size(rt->cpu));
    st->arena_next = rt->a5 + PPC_ARENA_GAP;
    st->arena_limit = rt->screen_base;
    st->stubs = mac_runtime_alloc(rt, 4 * MAC_PPC_FRAMES);
    uint32_t stack = arena_alloc(st, PPC_STACK_BYTES);
    const char *why = NULL;
    if (!st->stubs || !stack) why = "Out of guest memory for the PowerPC stack.";
    else if (mac_cfm_load(&st->fragment, &st->cpu, st->pef, size, table, count, arena_alloc, st, rt)) why = st->fragment.error;
    else if (!st->fragment.main_vector) why = "The PowerPC fragment has no main entry point.";
    else if (st->fragment.init_vector) why = "The PowerPC fragment has an initialisation routine, which is not supported.";
    else if (mac_cfm_stack(&st->fragment, stack, PPC_STACK_BYTES)) why = "Could not set up the PowerPC stack.";
    if (why) {
        snprintf(err, n, "PowerPC fragment: %s", why);
        mac_runtime_destroy(rt);
        return -1;
    }
    st->fragment.on_call = on_call;
    /* Start state: pc at the main routine's code, r2 its TOC, returning to the stop address. Nothing has run yet. */
    uint32_t code = 0, toc = 0;
    ppc_mem_read(&st->cpu, st->fragment.main_vector, 4, &code);
    ppc_mem_read(&st->cpu, st->fragment.main_vector + 4, 4, &toc);
    st->cpu.pc = code;
    st->cpu.gpr[2] = toc;
    st->cpu.lr = PPC_STOP_PC;
    mac_cpu_set(rt->cpu, MAC_PC, st->stubs); /* the 68K CPU only ever runs on behalf of a bridged call */
    return 0;
}

int mac_ppc_runtime_open(MacRuntime *rt, const MacResourceFork *fork, const char *fork_path, char *err, size_t n) {
    MacResource code;
    MacCfrgMember member;
    char reason[200];
    size_t size = 0;
    if (!fork || mac_resource_find(fork, MAC_CODE_TYPE, 0, &code) == 0 || !mac_ppc_fork_member(fork, &member)) return 0;
    uint8_t *bytes = mac_ppc_read_fragment(fork_path, &member, &size, reason, sizeof reason);
    if (!bytes) {
        snprintf(err, n, "PowerPC application: %s.", reason);
        return -1;
    }
    int rc = mac_ppc_runtime_init(rt, fork, bytes, size, err, n);
    free(bytes);
    return rc ? -1 : 1;
}

void mac_ppc_destroy(MacRuntime *rt) {
    if (!rt || !rt->ppc) return;
    mac_cfm_unload(&rt->ppc->fragment);
    free(rt->ppc->pef);
    free(rt->ppc);
    rt->ppc = NULL;
}

void mac_ppc_report(const MacRuntime *rt, char *out, size_t n) {
    const MacPpcState *st = rt->ppc;
    if (!st) { if (n) out[0] = 0; return; }
    snprintf(out, n, "powerpc pc=%08x lr=%08x instructions=%llu import-calls=%llu last-import=%s switch-depth=%u", st->cpu.pc,
             st->cpu.lr, (unsigned long long)st->cpu.instructions, (unsigned long long)st->calls,
             st->last_import[0] ? st->last_import : "none", st->depth);
}

MacPpcFrame *mac_ppc_push(MacRuntime *rt, const char *name) {
    MacPpcState *st = rt->ppc;
    if (st->depth >= MAC_PPC_FRAMES) {
        mac_ppc_fail(rt, "Too many nested switches between the PowerPC and 68K sides (at %s).", name);
        return NULL;
    }
    return &st->frames[st->depth++];
}

/* The scheduler. Exactly one side runs at a time and neither runs inside the other's callback: the top switch frame
 * says which. With no frame, or a CALLBACK frame on top, PowerPC code runs. With a TRAP or CALL68K frame on top the
 * 68K CPU is stepped until it comes back to the frame's return address; a trap that asked to be run again (a blocking
 * call such as a modal dialog) ends the slice so the host can advance time and deliver events. */
size_t mac_ppc_run(MacRuntime *rt, size_t budget) {
    MacPpcState *st = rt->ppc;
    PpcCpu *c = &st->cpu;
    MacCpu *m = rt->cpu;
    size_t ran = 0;
    st->blocked = 0;
    while (ran < budget && !c->halted && !mac_cpu_halted(m) && !st->blocked) {
        MacPpcFrame *top = st->depth ? &st->frames[st->depth - 1] : NULL;
        if (top && top->kind != MAC_PPC_FRAME_CALLBACK) {
            if (mac_cpu_get(m, MAC_PC) == top->stub + 2) { /* the 68K side is back: hand the result to the caller */
                uint32_t result = c->gpr[3];
                MacPpcFrame done = *top;
                st->depth--;
                if (!mac_ppc_trap_finish(rt, &done, &result)) c->gpr[3] = result;
                continue;
            }
            unsigned depth = st->depth;
            st->sched_68k = 1;
            size_t n = mac_cpu_run(m, 1);
            st->sched_68k = 0;
            ran += n ? n : 1;
            if (st->depth == depth && top->kind == MAC_PPC_FRAME_TRAP && mac_cpu_get(m, MAC_PC) == top->stub)
                st->blocked = 1;
        } else {
            ran += ppc_cpu_run(c, budget - ran);
            if (c->halted || c->pc != PPC_STOP_PC) continue;
            if (top) {
                MacPpcFrame done = *top;
                st->depth--;
                mac_ppc_callback_finish(rt, &done);
            } else {
                rt->exited = 1;
                mac_ppc_fail(rt, "The PowerPC application returned from its main entry point.");
            }
        }
    }
    if (c->halted && !mac_cpu_halted(m)) mac_cpu_halt(m, c->error);
    if (mac_cpu_halted(m) && !c->halted) ppc_cpu_halt(c, "%s", mac_cpu_error(m));
    return ran;
}
