#include "mac_cdef.h"
#include "mac_window_int.h"
#include <stdlib.h>
#include <string.h>

/* Guest control definition procedures: see mac_cdef.h. The CDEF's own code is never relocated (its handle is marked loaded_code) and is validated
 * against the runtime's handle table on every call. The variant (procID & 15) travels in the high byte of contrlDefProc, as it does on a real Mac. The
 * owner window's port is the current port while a CDEF runs and is restored afterwards. A CDEF's answer to testCntl is clamped to 0..254. */
#define CDEF_TYPE UINT32_C(0x43444546)
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }

int mac_cdef_custom_id(unsigned proc) { return (proc >> 4) >= 2 ? (int)(proc >> 4) : 0; }
int mac_cdef_busy(const MacRuntime *rt) { return rt->cdef && rt->cdef->active; }
void mac_cdef_destroy(MacRuntime *rt) { free(rt->cdef); rt->cdef = NULL; }

uint32_t mac_cdef_load(MacRuntime *rt, unsigned id) {
    uint32_t h = mac_runtime_resource(rt, CDEF_TYPE, (int16_t)id);
    if (!h) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        MacHandle *m = &rt->handles[i];
        if (m->handle != h) continue;
        if (!m->resource || m->type != CDEF_TYPE || m->size < 16 || m->size > 0x100000) return 0;
        m->loaded_code = 1;
        return h;
    }
    return 0;
}

int mac_cdef_job_new(MacRuntime *rt, uint32_t sp, uint32_t args, MacCdefFinish finish) {
    if (!rt->cdef) rt->cdef = calloc(1, sizeof *rt->cdef);
    if (!rt->cdef) return fail(rt, "Control definition job could not be allocated.");
    if (rt->cdef->active) return fail(rt, "A Control Manager call was made from inside a control definition (unsupported).");
    MacCdefJob *j = rt->cdef;
    j->count = j->next = 0; j->part = 0; j->found = 0; j->cur_message = 0; j->port_changed = 0; memset(j->user, 0, sizeof j->user);
    rt->cdef->sp = sp; rt->cdef->args = args; rt->cdef->finish = finish;
    return 0;
}
int mac_cdef_add(MacRuntime *rt, uint32_t handle, int message, uint32_t param) {
    MacCdefJob *j = rt->cdef;
    if (!j || j->count >= CDEF_MAX_STEPS) return fail(rt, "Too many control definition messages in one call.");
    j->step[j->count].handle = handle; j->step[j->count].message = message; j->step[j->count].param = param; j->count++;
    return 0;
}

static int complete(MacRuntime *rt) {
    MacCdefJob *j = rt->cdef;
    j->active = 0;
    if (j->port_changed) {
        rt->current_port = j->saved_port;
        if (rt->qd_globals) wr(rt, rt->qd_globals, 4, j->saved_port);
        j->port_changed = 0;
    }
    if (j->finish && j->finish(rt, j) < 0) { if (!mac_cpu_halted(rt->cpu)) mac_cpu_halt(rt->cpu, "Control definition completion failed."); return -1; }
    mac_cpu_set(rt->cpu, MAC_A7, j->sp + j->args);
    return 0;
}

static int step_done(void *vrt, void *ctx, uint32_t result);
/* Runs steps until a guest call is under way (returns 0 with the call set up) or the job is complete (returns 0 after finishing). */
static int advance(MacRuntime *rt) {
    MacCdefJob *j = rt->cdef;
    while (j->next < j->count) {
        uint32_t h = j->step[j->next].handle, p = mac_control_record(rt, h); int message = j->step[j->next].message; uint32_t param = j->step[j->next].param;
        j->next++;
        if (!p) return -1;
        MacWindowState *w = mac_window_find(rt, rd(rt, p + CTL_OWNER, 4));
        if (!w) return fail(rt, "A control's owner window is not a registered window.");
        uint32_t def = rd(rt, p + CTL_DEFPROC, 4);
        if (message == CDEF_TEST && (!rd(rt, p + CTL_VIS, 1) || rd(rt, p + CTL_HILITE, 1) == 255)) continue;   /* hidden and inactive controls are not hit */
        if (message == CDEF_DRAW && (!w->visible || !rd(rt, p + CTL_VIS, 1))) continue;   /* only visible controls in visible windows are drawn */
        if (message == CDEF_DISP && !def) continue;                         /* standard controls have nothing to dispose */
        if (!def) {                                                         /* a standard control: drawn and tested natively */
            if (message == CDEF_DRAW) mac_control_draw(rt, w, p, 0);
            if (message == CDEF_TEST) {
                int part = mac_control_part(rt, p, (int16_t)(param >> 16), (int16_t)(param & 0xffff));
                if (part) { j->part = part; j->found = h; j->next = j->count; }
            }
            continue;
        }
        uint32_t dh = def & 0xffffff, code = 0;
        for (size_t i = 0; i < rt->handle_count; i++) {
            const MacHandle *m = &rt->handles[i];
            if (m->handle == dh && m->resource && m->type == CDEF_TYPE && m->loaded_code && m->size >= 16) { code = rd(rt, dh, 4); if (code != m->pointer) code = 0; break; }
        }
        if (!code) return fail(rt, "A control's definition procedure handle is not a live CDEF resource.");
        if (!j->port_changed) { j->saved_port = rt->current_port; j->port_changed = 1; }
        rt->current_port = w->address;
        if (rt->qd_globals) wr(rt, rt->qd_globals, 4, w->address);
        uint8_t a[12];                                                      /* lowest address first: param, message, control, varCode */
        uint32_t var = def >> 24;
        a[0] = (uint8_t)(param >> 24); a[1] = (uint8_t)(param >> 16); a[2] = (uint8_t)(param >> 8); a[3] = (uint8_t)param;
        a[4] = (uint8_t)(message >> 8); a[5] = (uint8_t)message;
        a[6] = (uint8_t)(h >> 24); a[7] = (uint8_t)(h >> 16); a[8] = (uint8_t)(h >> 8); a[9] = (uint8_t)h;
        a[10] = (uint8_t)(var >> 8); a[11] = (uint8_t)var; 
        j->cur_message = message;
        return mac_guest_call(rt, code, a, 12, 4, step_done, NULL);
    }
    return complete(rt);
}

static int step_done(void *vrt, void *ctx, uint32_t result) {
    MacRuntime *rt = vrt; (void)ctx;
    MacCdefJob *j = rt->cdef;
    if (!j || !j->active) return -1;
    if (j->cur_message == CDEF_TEST) {
        int part = (int16_t)(result & 0xffff);
        if (part > 0 && part < 255) { j->part = part; j->found = j->step[j->next - 1].handle; j->next = j->count; }
    }
    return advance(rt);
}

int mac_cdef_start(MacRuntime *rt) {
    MacCdefJob *j = rt->cdef;
    if (!j) return fail(rt, "No control definition job to start.");
    j->active = 1; j->next = 0; j->part = 0; j->found = 0;
    return advance(rt) < 0 ? -1 : 1;
}
