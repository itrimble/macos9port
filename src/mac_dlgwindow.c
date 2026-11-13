#include "mac_dlgwindow.h"
#include "mac_cdef.h"
#include "mac_window_int.h"

static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
#define M(rt) ((Mgr *)(rt)->dlgmgr)

/* NewCWindow / DisposeWindow are called through their own services with a frame built in unused stack below the caller's A7. */
static uint32_t scratch_frame(MacRuntime *rt, uint32_t sp, unsigned bytes) {
    uint32_t at = (sp - 0x180) & ~1u;
    size_t z = mac_cpu_ram_size(rt->cpu);
    return sp >= 0x2000 + 0x180 && sp <= z && at + bytes <= sp ? at : 0;
}
uint32_t mac_dlgwin_create(MacRuntime *rt, int top, int left, int height, int width) {
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), f = scratch_frame(rt, sp, 0x80);
    if (!f) return fail(rt, "A dialog window has no stack room."), 0;
    mac_cpu_write(rt->cpu, f + 32, 2, (uint16_t)top); mac_cpu_write(rt->cpu, f + 34, 2, (uint16_t)left);
    mac_cpu_write(rt->cpu, f + 36, 2, (uint16_t)(top + height)); mac_cpu_write(rt->cpu, f + 38, 2, (uint16_t)(left + width));
    mac_cpu_write(rt->cpu, f + 48, 1, 0);                                                    /* an empty title */
    wr(rt, f, 4, 0); wr(rt, f + 4, 2, 0); wr(rt, f + 6, 4, 0xffffffffu); wr(rt, f + 10, 2, 1); wr(rt, f + 12, 2, 0);   /* refCon, goAway, in front, dBoxProc, hidden */
    wr(rt, f + 14, 4, f + 48); wr(rt, f + 18, 4, f + 32); wr(rt, f + 22, 4, 0); wr(rt, f + 26, 4, 0);
    mac_cpu_set(rt->cpu, MAC_A7, f);
    int rc = mac_window_service(rt, 0xaa45);
    uint32_t win = rc == 1 ? rd(rt, f + 26, 4) : 0;
    mac_cpu_set(rt->cpu, MAC_A7, sp);
    if (rc < 0 || mac_cpu_halted(rt->cpu)) return 0;
    if (!win || !mac_window_select(rt, win)) return 0;
    rt->window.port_only = 1;
    uint32_t vis = rd(rt, rt->window.handles[2], 4);                                         /* the visRgn: the whole content, so QuickDraw is not clipped away */
    size_t z = mac_cpu_ram_size(rt->cpu);
    if (vis < 0x400 || vis + 10 > z) { fail(rt, "A dialog window's visible region is invalid."); return 0; }
    wr(rt, vis, 2, 10); wr(rt, vis + 2, 2, 0); wr(rt, vis + 4, 2, 0); wr(rt, vis + 6, 2, (uint16_t)height); wr(rt, vis + 8, 2, (uint16_t)width);
    return win;
}
int mac_dlgwin_available(const MacRuntime *rt) { return rt->main_device && (rt->manager_flags & 2) && rt->qd_globals && rt->window_count < 7; }
uint32_t mac_dlgwin_ensure(MacRuntime *rt, Dlg *d) {
    if (!d->port_window) {
        d->port_window = mac_dlgwin_create(rt, d->top, d->left, d->height, d->width);
        if (d->port_window) mac_dlgwin_shown(rt, d->port_window, !d->hidden);
    }
    return d->port_window;
}
uint32_t mac_dlgwin_of_dialog(MacRuntime *rt, uint32_t dialog) {
    if (!rt->dlgmgr || !dialog) return 0;
    for (unsigned i = 0; i < MAX_OPEN; i++) if (M(rt)->open[i] && M(rt)->open[i]->ptr == dialog) return mac_dlgwin_available(rt) ? mac_dlgwin_ensure(rt, M(rt)->open[i]) : 0;
    return 0;
}
void mac_dlgwin_shown(MacRuntime *rt, uint32_t window, int shown) { MacWindowState *w = mac_window_find(rt, window); if (w && w->port_only) w->port_only = shown ? 2 : 1; }
void mac_dlgwin_destroy(MacRuntime *rt, uint32_t window) {
    if (!window || mac_cpu_halted(rt->cpu)) return;
    mac_controls_release(rt, window);
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), f = scratch_frame(rt, sp, 0x10);
    if (!f) { fail(rt, "A dialog window has no stack room to be disposed."); return; }
    wr(rt, f, 4, window); mac_cpu_set(rt->cpu, MAC_A7, f);
    mac_window_service(rt, 0xa914);                                                          /* DisposeWindow */
    mac_cpu_set(rt->cpu, MAC_A7, sp);
}

int mac_dlgwin_items(MacRuntime *rt, Dlg *d) {
    for (unsigned k = 0; k < d->ditl.count; k++) {
        const MacDitlItem *it = &d->ditl.items[k];
        if (it->kind != MAC_ITEM_CONTROL) continue;
        if (it->length != 2) return fail(rt, "A dialog control item does not name a CNTL resource.");
        uint8_t rect[8] = { (uint8_t)((uint16_t)it->top >> 8), (uint8_t)it->top, (uint8_t)((uint16_t)it->left >> 8), (uint8_t)it->left,
                            (uint8_t)((uint16_t)it->bottom >> 8), (uint8_t)it->bottom, (uint8_t)((uint16_t)it->right >> 8), (uint8_t)it->right };
        int64_t h = mac_control_from_cntl(rt, d->port_window, (int16_t)(it->data[0] << 8 | it->data[1]), rect);
        if (h < 0) return -1;
        if (!h) return fail(rt, "A dialog control item's CNTL resource is missing or could not be created.");
        d->handle[k] = (uint32_t)h;
    }
    return 0;
}

static int fin_yield(MacRuntime *rt, MacCdefJob *j) { (void)j; mac_cpu_yield(rt->cpu); return 0; }
int mac_dlgwin_flush(MacRuntime *rt) {
    if (!rt->dlgmgr || rt->guest.active || mac_cpu_halted(rt->cpu)) return 1;
    for (unsigned i = 0; i < 4; i++) {
        Dlg *d = M(rt)->open[i];
        if (!d || !d->ctl_pending || !d->port_window) continue;
        d->ctl_pending = 0;
        if (d->hidden) continue;
        MacWindowState *w = mac_window_find(rt, d->port_window);
        if (!w) return fail(rt, "A dialog's port window has disappeared."), -1;
        if (mac_cdef_job_new(rt, mac_cpu_get(rt->cpu, MAC_A7), 0, NULL) < 0) return -1;
        uint32_t cur = rd(rt, w->address + 140, 4);
        for (unsigned guard = 0; cur && guard < 512; guard++) {
            uint32_t p = mac_control_record(rt, cur);
            if (!p) return -1;
            if (mac_cdef_add(rt, cur, CDEF_DRAW, 0) < 0) return -1;
            cur = rd(rt, p + CTL_NEXT, 4);
        }
        rt->cdef->finish = fin_yield; mac_cpu_cancel_yield(rt->cpu);                         /* the controls are drawn before the host sees the screen */
        return mac_cdef_start(rt);
    }
    return 1;
}

/* BeginUpdate / EndUpdate on a dialog's port window. The window's visible region is the whole dialog (mac_dlgwin_create), so nothing is intersected or restored: the
 * pair only records that an update is open (the window service refuses a nested one and a visibility change inside one). */
int mac_dlgwin_update(MacRuntime *rt, uint32_t window, int begin) {
    MacWindowState *w = mac_window_find(rt, window);
    if (!w || !w->port_only) return fail(rt, "A dialog's port window has disappeared."), -1;
    if (begin && w->updating) return fail(rt, "Nested BeginUpdate is unsupported."), -1;
    if (!begin && !w->updating) return fail(rt, "EndUpdate requires a matching BeginUpdate."), -1;
    w->updating = begin ? 1 : 0;
    return 1;
}

/* A control of a dialog's port window needs drawing while a guest call is running (nested guest calls are unsupported): note that the dialog owes its controls a
 * redraw, which mac_dlgwin_flush does once the call has returned. 1 when `window` is a dialog's port window (the draw is deferred), 0 otherwise. */
int mac_dlgwin_owe_redraw(MacRuntime *rt, uint32_t window) {
    if (!rt->dlgmgr || !window) return 0;
    for (unsigned i = 0; i < 4; i++) {
        Dlg *d = M(rt)->open[i];
        if (d && d->port_window == window) { d->ctl_pending = 1; return 1; }
    }
    return 0;
}

/* The dialog was resized: its port window follows (SizeWindow through the window service) and its visible region is the whole new content again. 1, or 0 after halting. */
int mac_dlgwin_size(MacRuntime *rt, uint32_t window, int width, int height) {
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7), f = scratch_frame(rt, sp, 0x10);
    if (!f) return fail(rt, "A dialog window has no stack room to be resized."), 0;
    wr(rt, f, 2, 0); wr(rt, f + 2, 2, (uint16_t)height); wr(rt, f + 4, 2, (uint16_t)width); wr(rt, f + 6, 4, window);
    mac_cpu_set(rt->cpu, MAC_A7, f);
    int rc = mac_window_service(rt, 0xa91d);
    mac_cpu_set(rt->cpu, MAC_A7, sp);
    if (rc < 0 || mac_cpu_halted(rt->cpu) || !mac_window_select(rt, window)) return 0;
    uint32_t vis = rd(rt, rt->window.handles[2], 4);
    size_t z = mac_cpu_ram_size(rt->cpu);
    if (vis < 0x400 || vis + 10 > z) { fail(rt, "A dialog window's visible region is invalid."); return 0; }
    wr(rt, vis, 2, 10); wr(rt, vis + 2, 2, 0); wr(rt, vis + 4, 2, 0); wr(rt, vis + 6, 2, (uint16_t)height); wr(rt, vis + 8, 2, (uint16_t)width);
    return 1;
}
