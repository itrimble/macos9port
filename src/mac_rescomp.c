#include "mac_rescomp.h"
#include "mac_codefmt.h"
#include "mac_resource_chain.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What happens between "the bytes are in a handle" and "the program may have the handle".
 *
 * 1. Expansion of a Resource Manager compressed resource (extended header, signature A89F 6572). The decompressor is a 'dcmp' resource found through the
 *    resource chain with the ID the header names, and runs as guest 68K code through mac_guest_call, the way a custom menu definition does. A decompressor that
 *    is not in any open resource file is reported by name; none is supplied here. The stock decompressors live in the System file, which OS999 does not ship,
 *    and nothing here re-creates their algorithms.
 *      Calling convention used (from public descriptions of the interface; NOT checked against a real stock decompressor, none being available):
 *      FUNCTION dcmp(src: Ptr; dst: Ptr; header: Ptr; work: Ptr): OSErr, Pascal conventions. `src` is the first byte after the extended header, `dst` a zeroed
 *      buffer of the expanded size, `header` the extended header, `work` a zeroed scratch area. The test decompressor follows exactly this.
 * 2. A program's own load hook. An installer engine may patch OS trap $A0FC, which the Resource Manager calls after it has read a resource's bytes into memory,
 *    with A2 pointing at the resource's 12-byte map entry (the handle is the long at +8). The patch may rewrite the handle's contents in place, or release it and
 *    set ResErr. Observed on one such engine (the QuickTime 4.0.3 and WordPerfect 3.5e installers), not documented: it packs resources behind a first long of
 *    A89F 000C and unpacks them in this hook. The hook is run only for a resource that carries that marker, and only if the program changed that trap's address;
 *    every register is saved around it and ResErr is cleared before it runs. Resources loaded while a hook is running are not hooked (guest calls cannot nest).
 *    See the eager pass at the end for why most of this happens when the hook is installed. */
enum { RES_ERROR = 0xa60, E_RES_NOT_FOUND = -192, E_MEM_FULL = -108, E_BAD_DCMP = -199 };

typedef struct MacRescomp {
    uint32_t entry;               /* 12 bytes of guest memory: the map entry the hook is shown */
    uint32_t saved[16];           /* D0-D7, A0-A6 around a hook (A7 and PC belong to the guest call) */
    uint32_t handle, dst, work, expanded;
    MacRescompDone done; void *ctx;
    int hook_pending;
    char why[240];
    MacResPatch *patch; size_t patch_n, patch_cap;   /* expansions found by the eager pass, host copies */
    size_t scan; uint32_t pre_saved[16]; int prepass, pre_owned, pre_done_once;
    uint8_t *fork_bytes;          /* the rebuilt fork rt->fork reads after the eager pass */
} MacRescomp;

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }

void mac_rescomp_destroy(MacRuntime *rt) {
    MacRescomp *j = rt->rescomp;
    if (j) {
        for (size_t i = 0; i < j->patch_n; i++) free((void *)j->patch[i].bytes);
        free(j->patch); free(j->fork_bytes);
    }
    free(j); rt->rescomp = NULL;
}
const char *mac_rescomp_why(const MacRuntime *rt) { return rt->rescomp ? rt->rescomp->why : ""; }
int mac_rescomp_service(MacRuntime *rt, uint16_t opcode) {
    (void)rt;
    return opcode == 0xa0fc ? 1 : 0;
}

static MacHandle *find(MacRuntime *rt, uint32_t handle) {
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].handle == handle && rt->handles[i].resource) return &rt->handles[i];
    return NULL;
}
static void save_regs(MacRuntime *rt, uint32_t *to) { for (int i = 0; i < 15; i++) to[i] = mac_cpu_get(rt->cpu, (MacCpuReg)(MAC_D0 + i)); }
static void restore_regs(MacRuntime *rt, const uint32_t *from) { for (int i = 0; i < 15; i++) mac_cpu_set(rt->cpu, (MacCpuReg)(MAC_D0 + i), from[i]); }
static int marked(const MacRuntime *rt, const MacHandle *h) {
    return h->pointer && h->size >= 16 && h->pointer + h->size <= mac_cpu_ram_size(rt->cpu) && mac_cf_be32(mac_cpu_ram(rt->cpu) + h->pointer) == UINT32_C(0xa89f000c);
}
static void drop(MacRuntime *rt, MacHandle *h) {
    mac_cpu_write(rt->cpu, h->handle, 4, 0);
    mac_runtime_free_data(rt, h->pointer);
    h->resource = 0; h->pointer = h->size = 0;
}

static int finish(MacRuntime *rt, int error) {
    MacRescomp *j = rt->rescomp;
    restore_regs(rt, j->saved);
    if (j->work) { mac_runtime_free_data(rt, j->work); j->work = 0; }
    MacRescompDone done = j->done; void *ctx = j->ctx;
    j->done = NULL;
    return done(rt, ctx, error);
}

/* Give up on a handle the program asked for: it is released the way a failed load leaves it, so the program gets NIL and a later request tries again. */
static int refuse(MacRuntime *rt, int error, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int refuse(MacRuntime *rt, int error, const char *fmt, ...) {
    MacRescomp *j = rt->rescomp;
    va_list ap; va_start(ap, fmt); vsnprintf(j->why, sizeof j->why, fmt, ap); va_end(ap);
    MacHandle *h = find(rt, j->handle);
    if (h && !h->loaded_code) drop(rt, h);
    if (rt->trace) fprintf(stderr, "compressed-resource: %s\n", j->why);
    return finish(rt, error);
}

static int run_hook(MacRuntime *rt);
static int hook_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; MacRescomp *j = rt->rescomp; (void)ctx; (void)result;
    if (!j) return -1;
    int16_t reserr = (int16_t)rd(rt, RES_ERROR, 2);
    MacHandle *h = find(rt, j->handle);
    uint32_t still = rd(rt, j->entry + 8, 4);
    if (!h || !still) {
        /* The hook released the resource (it also leaves its own error in ResErr). */
        if (h && !h->loaded_code) drop(rt, h);
        snprintf(j->why, sizeof j->why, "the program's load hook released the resource (ResErr %d)", reserr);
        return finish(rt, reserr ? reserr : E_MEM_FULL);
    }
    uint32_t master = rd(rt, h->handle, 4);
    if (master && master != h->pointer && master < mac_cpu_ram_size(rt->cpu)) h->pointer = master;   /* contents replaced behind the Memory Manager's back */
    return finish(rt, reserr < 0 ? reserr : 0);
}

static int run_hook(MacRuntime *rt) {
    MacRescomp *j = rt->rescomp;
    uint32_t patch = rt->trap_addresses[0xfc];
    if (patch == mac_native_stub(0xfc)) return finish(rt, 0);
    MacHandle *h = find(rt, j->handle);
    if (!h) return finish(rt, E_RES_NOT_FOUND);
    if (!j->entry && !(j->entry = mac_runtime_alloc(rt, 12))) return fail(rt, "No memory for the resource hook's map entry.");
    mac_cpu_write(rt->cpu, j->entry, 2, (uint16_t)h->id);
    mac_cpu_write(rt->cpu, j->entry + 2, 2, 0xffff);
    mac_cpu_write(rt->cpu, j->entry + 4, 4, 0);
    mac_cpu_write(rt->cpu, j->entry + 8, 4, h->handle);
    mac_cpu_write(rt->cpu, RES_ERROR, 2, 0);
    mac_cpu_set(rt->cpu, MAC_A2, j->entry);
    if (mac_guest_call(rt, patch, NULL, 0, 0, hook_done, NULL) < 0) return -1;
    return 1;
}

static int expand_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; MacRescomp *j = rt->rescomp; (void)ctx;
    if (!j) return -1;
    MacHandle *h = find(rt, j->handle);
    if (!h) return finish(rt, E_RES_NOT_FOUND);
    int16_t err = (int16_t)result;
    if (err) {
        mac_runtime_free_data(rt, j->dst); j->dst = 0;
        return refuse(rt, err, "the 'dcmp' decompressor returned error %d", err);
    }
    mac_runtime_free_data(rt, h->pointer);
    h->pointer = j->dst; h->size = j->expanded; j->dst = 0;
    mac_cpu_write(rt->cpu, h->handle, 4, h->pointer);
    if (j->hook_pending) { int rc = run_hook(rt); return rc < 0 ? -1 : 0; }
    return finish(rt, 0);
}

/* Returns 1 when the guest decompressor was started, 0 if `finish` already ran (a refusal), -1 on halt. */
static int run_expand(MacRuntime *rt, MacHandle *h, const MacComp *c) {
    MacRescomp *j = rt->rescomp;
    MacResource r; int16_t owner = 0;
    if (mac_resource_chain_find(rt, 0x64636d70u /* 'dcmp' */, c->dcmp_id, 0, &r, &owner) != 0) {
        refuse(rt, E_RES_NOT_FOUND, "%s resource %d is compressed (header version %u, expands to %u bytes) and needs decompressor 'dcmp' %d, which is in none of the "
               "open resource files. The stock decompressors belong to the System file; OS999 does not ship it.",
               h->type == MAC_CODE_TYPE ? "CODE" : "a", h->id, c->version, c->expanded, c->dcmp_id);
        return 0;
    }
    uint32_t dh = mac_runtime_resource_owned(rt, 0x64636d70u, r.id, owner);
    MacHandle *dm = dh ? find(rt, dh) : NULL;
    if (!dm || dm->size < 4 || mac_cpu_read(rt->cpu, dh, 4) != dm->pointer || (dm->pointer & 1)) {
        refuse(rt, E_BAD_DCMP, "'dcmp' %d is unusable as code", c->dcmp_id);
        return 0;
    }
    dm->loaded_code = 1; dm->hooked = 1;
    uint32_t dst_bytes = c->expanded ? c->expanded : 2;
    unsigned work_bytes = 0x2000 + (unsigned)((uint64_t)c->expanded * c->work_fraction / 256) + (unsigned)c->expand_extra * 0x400;
    j->dst = mac_runtime_alloc_data(rt, dst_bytes);
    j->work = mac_runtime_alloc_data(rt, work_bytes);
    if (!j->dst || !j->work) {
        refuse(rt, E_MEM_FULL, "no memory to expand a resource to %u bytes", c->expanded);
        return 0;
    }
    memset(mac_cpu_ram(rt->cpu) + j->dst, 0, dst_bytes);
    memset(mac_cpu_ram(rt->cpu) + j->work, 0, work_bytes);
    j->expanded = c->expanded;
    uint32_t src = h->pointer + c->header_len, hdr = h->pointer;
    uint8_t a[16];   /* lowest address first: the last argument first */
    uint32_t v[4] = {j->work, hdr, j->dst, src};
    for (int i = 0; i < 4; i++) for (int b = 0; b < 4; b++) a[i * 4 + b] = (uint8_t)(v[i] >> (24 - 8 * b));
    if (mac_guest_call(rt, dm->pointer, a, 16, 2, expand_done, NULL) < 0) return -1;
    return 1;
}

int mac_rescomp_begin(MacRuntime *rt, uint32_t handle, MacRescompDone done, void *ctx) {
    MacHandle *h = find(rt, handle);
    if (!h || h->hooked || rt->guest.active || !h->pointer) return 0;
    MacComp c;
    int kind = h->size >= 4 && h->pointer + h->size <= mac_cpu_ram_size(rt->cpu) ? mac_comp_parse(mac_cpu_ram(rt->cpu) + h->pointer, h->size, &c) : 0;
    int use_hook = kind == 1 && c.kind == MAC_COMP_MARKED && rt->trap_addresses[0xfc] != mac_native_stub(0xfc);
    h->hooked = 1;
    if (kind == 0 || (kind == 1 && c.kind == MAC_COMP_MARKED && !use_hook)) return 0;
    if (!rt->rescomp && !(rt->rescomp = calloc(1, sizeof *rt->rescomp))) return fail(rt, "No memory for resource hooks.");
    MacRescomp *j = rt->rescomp;
    j->handle = handle; j->done = done; j->ctx = ctx; j->why[0] = 0; j->hook_pending = 0;
    save_regs(rt, j->saved);
    int rc;
    if (kind < 0) { refuse(rt, E_BAD_DCMP, "a resource with a compressed-resource signature has a damaged or unsupported header"); rc = 0; }
    else if (c.kind == MAC_COMP_SYSTEM) rc = run_expand(rt, h, &c);
    else rc = run_hook(rt);
    return rc < 0 ? -1 : 1;
}

/* ---- The eager pass ---------------------------------------------------------------------------------------------------------------------------------------
 * The Resource Manager calls the hook for every load, whoever asked: a dialog read through the Dialog Manager reaches it as well as a GetResource. Here only
 * GetResource, LoadResource and LoadSeg can wait for guest code; the many native readers (dialog items, menus, pictures, cursors) take their bytes straight from
 * rt->fork. So when the program installs its hook, every marked resource of its own fork is run through the hook once, up front, and rt->fork is then replaced
 * by a rebuilt copy that holds the expanded bytes: every reader, native or guest, sees them. Differences from a real machine: the work happens once and early
 * instead of per load, a SizeRsrc patch that asks "is it still unloaded" meets handles that exist, and the expanded bytes stay resident on the host. */
static void patch_store(MacRuntime *rt, const MacHandle *h) {
    MacRescomp *j = rt->rescomp;
    if (!h->pointer || h->pointer + h->size > mac_cpu_ram_size(rt->cpu) || h->size < 4 || h->resource_owner != 0 || marked(rt, h)) return;
    if (j->patch_n == j->patch_cap) {
        size_t cap = j->patch_cap ? j->patch_cap * 2 : 256;
        MacResPatch *grown = realloc(j->patch, cap * sizeof *grown);
        if (!grown) return;
        j->patch = grown; j->patch_cap = cap;
    }
    uint8_t *copy = malloc(h->size);
    if (!copy) return;
    memcpy(copy, mac_cpu_ram(rt->cpu) + h->pointer, h->size);
    j->patch[j->patch_n++] = (MacResPatch){.type = h->type, .id = h->id, .bytes = copy, .size = h->size};
}

static int pre_next(MacRuntime *rt);
static int pre_done(MacRuntime *rt, void *ctx, int error) {
    (void)ctx;
    MacRescomp *j = rt->rescomp;
    MacHandle *h = find(rt, j->handle);
    if (h) {
        if (!error) patch_store(rt, h);
        if (j->pre_owned && !h->loaded_code) drop(rt, h);   /* the bytes are kept on the host; a block the pass made itself goes back */
    }
    return pre_next(rt);
}

static void pre_swap_fork(MacRuntime *rt) {
    MacRescomp *j = rt->rescomp;
    uint8_t *bytes = NULL; size_t n = 0;
    if (!j->patch_n) return;
    if (mac_rescomp_build_fork(&rt->fork, j->patch, j->patch_n, &bytes, &n)) { snprintf(j->why, sizeof j->why, "the expanded resources could not be gathered into a fork"); return; }
    MacResourceFork nf; char err[160];
    if (mac_resource_open(&nf, bytes, n, err, sizeof err)) { free(bytes); snprintf(j->why, sizeof j->why, "rebuilt fork rejected: %s", err); return; }
    free(j->fork_bytes);
    j->fork_bytes = bytes;
    rt->fork = nf;
}

/* Next marked resource of the application's fork; when there is none the pass is over, the fork is swapped and the registers are back as the trap found them. */
static int pre_next(MacRuntime *rt) {
    MacRescomp *j = rt->rescomp;
    while (j->scan < mac_resource_count(&rt->fork)) {
        MacResource r;
        if (mac_resource_at(&rt->fork, j->scan++, &r) != 0) continue;
        MacComp c;
        if (mac_comp_parse(r.data, r.size, &c) != 1 || c.kind != MAC_COMP_MARKED) continue;
        MacHandle *h = NULL;
        for (size_t i = 0; i < rt->handle_count; i++)
            if (rt->handles[i].resource && rt->handles[i].resource_owner == 0 && rt->handles[i].type == r.type && rt->handles[i].id == r.id) h = &rt->handles[i];
        j->pre_owned = h == NULL;
        if (!h) {
            uint32_t handle = mac_runtime_resource_owned(rt, r.type, r.id, 0);
            h = handle ? find(rt, handle) : NULL;
        }
        if (!h) continue;
        h->hooked = 1;
        j->handle = h->handle; j->done = pre_done; j->ctx = NULL; j->why[0] = 0;
        if (!marked(rt, h)) {   /* already unpacked in place by the program itself */
            patch_store(rt, h);
            continue;
        }
        int rc = run_hook(rt);
        return rc < 0 ? -1 : 0;   /* started: its completion continues the pass; or finished inline, and pre_done already moved on */
    }
    pre_swap_fork(rt);
    restore_regs(rt, j->pre_saved);
    j->prepass = 0; j->pre_done_once = 1;
    return 0;
}

int mac_rescomp_prepass(MacRuntime *rt) {
    if (rt->guest.active || mac_cpu_halted(rt->cpu) || !rt->fork.bytes) return 0;
    if (!rt->rescomp && !(rt->rescomp = calloc(1, sizeof *rt->rescomp))) return fail(rt, "No memory for resource hooks.");
    MacRescomp *j = rt->rescomp;
    if (j->prepass || j->pre_done_once) return 0;
    size_t count = mac_resource_count(&rt->fork), any = 0;
    for (size_t i = 0; i < count && !any; i++) {
        MacResource r; MacComp c;
        if (mac_resource_at(&rt->fork, i, &r) == 0 && mac_comp_parse(r.data, r.size, &c) == 1 && c.kind == MAC_COMP_MARKED) any = 1;
    }
    if (!any) return 0;
    j->prepass = 1; j->scan = 0;
    save_regs(rt, j->pre_saved);
    save_regs(rt, j->saved);
    int rc = pre_next(rt);
    if (rc < 0) return -1;
    return rt->guest.active ? 1 : 0;
}
