#include "mac_controls.h"
#include "mac_cdef.h"
#include "mac_window_int.h"
#include "mac_resource.h"
#include <stdio.h>
#include <string.h>

/* Control Manager for guest windows. Handles and records live in guest memory; every call re-validates the handle against the runtime's own table (a forged or stale
 * handle, or one whose master pointer was changed, halts) and the owner window against the registry, and reads the record's fields afresh (a program may change
 * them). Signatures from the MPW interface listing. Supported: NewControl, DisposeControl, KillControls, Show/HideControl, Move/SizeControl, Get/SetCTitle,
 * Get/SetCtlValue, Get/SetMinCtl, Get/SetMaxCtl, Get/SetCRefCon, Get/SetCtlAction (stored, never called), HiliteControl, DrawControls, UpdateControls, TestControl
 * and FindControl (part codes inButton 10, inCheckBox 11, inUpButton 20, inDownButton 21, inPageUp 22, inPageDown 23, inThumb 129). procIDs 0, 1, 2 and 16 (+8 for
 * useWFont) only; custom definition procedures halt by name. TrackControl, DragControl and GetNewControl are not implemented and halt by name.
 * Not like a real Mac: the control's procID is kept in the record's contrlData long (contrlDefProc is nil); redrawing erases to the lightest palette entry, not
 * to the window's own background; drawing ignores windows in front of the owner. */
#define CTL_TYPE UINT32_C(0x43544c48)
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static void wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, a, n, v); }
static int ram_ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }

/* The record address of a control handle, or 0 (halting) when the handle is not one of ours. */
static uint32_t record(MacRuntime *rt, uint32_t h) {
    if (!h || (h & 1) || !ram_ok(rt, h, 4)) { mac_cpu_halt(rt->cpu, "A control handle is nil or invalid."); return 0; }
    for (size_t i = 0; i < rt->handle_count; i++) {
        const MacHandle *m = &rt->handles[i];
        if (m->handle != h || m->type != CTL_TYPE) continue;
        uint32_t p = rd(rt, h, 4);
        if (p != m->pointer || !ram_ok(rt, p, CTL_BYTES)) break;
        return p;
    }
    mac_cpu_halt(rt->cpu, "A control handle is not a live control (forged, disposed or its master pointer changed).");
    return 0;
}
static MacWindowState *owner_of(MacRuntime *rt, uint32_t p) {
    MacWindowState *w = mac_window_find(rt, rd(rt, p + CTL_OWNER, 4));
    if (!w) mac_cpu_halt(rt->cpu, "A control's owner window is not a registered window.");
    return w;
}
uint32_t mac_control_record(MacRuntime *rt, uint32_t h) { return record(rt, h); }
/* Every operation builds its guest-visible effects as a job (mac_cdef.c): standard controls are drawn and tested natively when their step comes up, custom
 * ones by running their CDEF. A call with no steps completes at once. */
static void redraw(MacRuntime *rt, uint32_t h) { mac_cdef_add(rt, h, CDEF_DRAW, 0); }
static int fin_result(MacRuntime *rt, MacCdefJob *j) { if (j->user[2]) wr(rt, j->user[0], j->user[2], j->user[1]); return 0; }
static int fin_find(MacRuntime *rt, MacCdefJob *j) { wr(rt, j->user[0], 4, j->found); wr(rt, j->user[1], 2, (uint16_t)j->part); return 0; }
static int fin_test(MacRuntime *rt, MacCdefJob *j) { wr(rt, j->user[1], 2, (uint16_t)j->part); return 0; }
static int dispose(MacRuntime *rt, uint32_t h);
static int fin_dispose(MacRuntime *rt, MacCdefJob *j) {
    for (unsigned i = 0; i < j->count; i++) if (j->step[i].message == CDEF_DISP && dispose(rt, j->step[i].handle) < 0) return -1;
    return 0;
}
static int pascal_ok(MacRuntime *rt, uint32_t s) { return s >= 0x400 && ram_ok(rt, s, 1) && ram_ok(rt, s, 1 + (size_t)mac_cpu_ram(rt->cpu)[s]); }
static void unlink_control(MacRuntime *rt, MacWindowState *w, uint32_t h) {
    uint32_t cur = rd(rt, w->address + 140, 4), prev = 0;
    for (unsigned guard = 0; cur && guard < 512; guard++) {
        uint32_t next = ram_ok(rt, cur, 4) ? rd(rt, rd(rt, cur, 4) + CTL_NEXT, 4) : 0;
        if (cur == h) { if (prev) wr(rt, rd(rt, prev, 4) + CTL_NEXT, 4, next); else wr(rt, w->address + 140, 4, next); return; }
        prev = cur; cur = next;
    }
}
static int dispose(MacRuntime *rt, uint32_t h) {
    uint32_t p = record(rt, h); if (!p) return -1;
    MacWindowState *w = owner_of(rt, p); if (!w) return -1;
    if (w->visible && rd(rt, p + CTL_VIS, 1)) mac_control_draw(rt, w, p, 1);
    unlink_control(rt, w, h);
    for (size_t i = 0; i < rt->handle_count; i++) if (rt->handles[i].handle == h && rt->handles[i].type == CTL_TYPE) {
        mac_runtime_free_data(rt, rt->handles[i].pointer); wr(rt, h, 4, 0); memset(&rt->handles[i], 0, sizeof rt->handles[i]); break;
    }
    return 0;
}
/* Shared by NewControl and GetNewControl: validates, allocates the handle and record, links it into the window's list and draws it. Returns the handle, 0 for
 * a memory failure (ResError -108, nil handle) or -1 after halting. `rect` is 8 bytes of big-endian top/left/bottom/right; `title` a Pascal string already checked. */
static int64_t make_control(MacRuntime *rt, uint32_t win, const uint8_t *rect, const uint8_t *title, unsigned visible, int16_t value, int16_t lo, int16_t hi, unsigned proc, uint32_t refcon) {
    MacWindowState *w = mac_window_find(rt, win);
    if (!w) return fail(rt, "NewControl needs a registered window (dialogs are not supported).");
    unsigned base = proc & ~8u;
    int cid = mac_cdef_custom_id(proc);
    uint32_t cdef = 0;
    if (cid) {
        cdef = mac_cdef_load(rt, (unsigned)cid);
        if (!cdef) { char why[160]; snprintf(why, sizeof why, "NewControl: procID %u needs CDEF %d, which the application does not have.", proc, cid); return fail(rt, why); }
    } else if (base != 0 && base != 1 && base != 2 && base != 16) return fail(rt, "NewControl: only push button, check box, radio button and scroll bar control definitions are supported.");
    if (hi < lo) return fail(rt, "NewControl: the maximum is below the minimum.");
    unsigned slot = 0; while (slot < rt->handle_count && (rt->handles[slot].handle || rt->handles[slot].pointer)) slot++;
    if (slot == rt->handle_count && rt->handle_count == MAC_RUNTIME_HANDLES) return fail(rt, "NewControl: no free handle slot.");
    uint32_t saved = rt->heap_next, h = mac_runtime_alloc(rt, 4), p = h ? mac_runtime_alloc(rt, CTL_BYTES) : 0;
    if (!p) { rt->heap_next = saved; wr(rt, 0xa60, 2, (uint16_t)-108); return 0; }
    if (slot == rt->handle_count) rt->handle_count++;
    rt->handles[slot] = (MacHandle){ .handle = h, .pointer = p, .size = CTL_BYTES, .type = CTL_TYPE };
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    memset(ram + p, 0, CTL_BYTES); wr(rt, h, 4, p);
    wr(rt, p + CTL_NEXT, 4, rd(rt, w->address + 140, 4)); wr(rt, w->address + 140, 4, h);
    wr(rt, p + CTL_OWNER, 4, win);
    memcpy(ram + p + CTL_RECT, rect, 8);
    wr(rt, p + CTL_VIS, 1, visible);
    if (value < lo) value = lo; if (value > hi) value = hi;
    wr(rt, p + CTL_VALUE, 2, (uint16_t)value); wr(rt, p + CTL_MIN, 2, (uint16_t)lo); wr(rt, p + CTL_MAX, 2, (uint16_t)hi);
    if (cdef) wr(rt, p + CTL_DEFPROC, 4, ((proc & 15) << 24) | cdef); else wr(rt, p + CTL_DATA, 4, proc);   /* a custom control's contrlData belongs to its CDEF */
    wr(rt, p + CTL_REFCON, 4, refcon);
    memcpy(ram + p + CTL_TITLE, title, 1 + (size_t)title[0]);
    wr(rt, 0xa60, 2, 0);
    if (cdef) mac_cdef_add(rt, h, CDEF_INIT, 0);
    redraw(rt, h);
    return h;
}
static int new_control(MacRuntime *rt, uint32_t sp) {
    uint32_t win = rd(rt, sp + 22, 4), bounds = rd(rt, sp + 18, 4), title = rd(rt, sp + 14, 4), refcon = rd(rt, sp, 4);
    if (!ram_ok(rt, bounds, 8) || (bounds & 1)) return fail(rt, "NewControl bounds rectangle is outside guest RAM.");
    if (!pascal_ok(rt, title)) return fail(rt, "NewControl title is outside guest RAM.");
    uint8_t rect[8], ttl[256]; memcpy(rect, mac_cpu_ram(rt->cpu) + bounds, 8); memcpy(ttl, mac_cpu_ram(rt->cpu) + title, 1 + (size_t)mac_cpu_ram(rt->cpu)[title]);
    int64_t h = make_control(rt, win, rect, ttl, rd(rt, sp + 12, 1) != 0, (int16_t)rd(rt, sp + 10, 2), (int16_t)rd(rt, sp + 8, 2), (int16_t)rd(rt, sp + 6, 2), rd(rt, sp + 4, 2), refcon);
    if (h < 0) return -1;
    rt->cdef->user[0] = sp + 26; rt->cdef->user[1] = (uint32_t)h; rt->cdef->user[2] = 4;
    return mac_cdef_start(rt);
}
/* GetNewControl(controlID, owner): the CNTL resource is bounds (8), value (2), visible (1) + filler (1), max (2), min (2), procID (2), refCon (4), title (Pascal);
 * this layout is from memory of Inside Macintosh. The resource is read from the application's own fork. */
/* The control a CNTL resource describes, owned by `win` (steps are added to the current job); `rect` (4 big-endian int16 values, or NULL) replaces the resource's own
 * rectangle, as the Dialog Manager does for a control item. Returns the handle, 0 for a missing CNTL (ResError -192) or no memory, -1 after halting. */
int64_t mac_control_from_cntl(MacRuntime *rt, uint32_t win, int16_t id, const uint8_t *rect) {
    MacResource res;
    if (mac_resource_find(&rt->fork, 0x434e544c, id, &res)) { wr(rt, 0xa60, 2, (uint16_t)-192); return 0; }
    if (res.size < 23 || res.size < 23u + res.data[22]) return fail(rt, "GetNewControl: the CNTL resource is too short.");
    const uint8_t *d = res.data;
    return make_control(rt, win, rect ? rect : d, d + 22, d[10] != 0, (int16_t)(d[8] << 8 | d[9]), (int16_t)(d[14] << 8 | d[15]), (int16_t)(d[12] << 8 | d[13]),
                        (unsigned)(d[16] << 8 | d[17]), (uint32_t)d[18] << 24 | d[19] << 16 | d[20] << 8 | d[21]);
}
/* Frees every control a window owns without sending dispCntl (the caller is tearing the window down; a CDEF's own storage is not released). */
void mac_controls_release(MacRuntime *rt, uint32_t win) {
    MacWindowState *w = mac_window_find(rt, win);
    for (unsigned guard = 0; w && guard < 512; guard++) {
        uint32_t h = rd(rt, w->address + 140, 4);
        if (!h || !record(rt, h)) break;
        if (dispose(rt, h) < 0) break;
    }
}
static int get_new_control(MacRuntime *rt, uint32_t sp) {
    int16_t id = (int16_t)rd(rt, sp + 4, 2);
    int64_t h = mac_control_from_cntl(rt, rd(rt, sp, 4), id, NULL);
    if (h == 0 && !mac_cpu_halted(rt->cpu) && (int16_t)rd(rt, 0xa60, 2) == -192) { wr(rt, sp + 6, 4, 0); mac_cpu_set(rt->cpu, MAC_A7, sp + 6); return 1; }
    if (h < 0) return -1;
    rt->cdef->user[0] = sp + 6; rt->cdef->user[1] = (uint32_t)h; rt->cdef->user[2] = 4;
    return mac_cdef_start(rt);
}

/* Argument bytes and result bytes for each trap: from the MPW listing (Pascal order: the last argument is at SP). */
static int shape(uint16_t op, unsigned *args, unsigned *result) {
    *result = 0;
    switch (op) {
    case 0xa904: *args = 4; return 1;                                                /* DrawGrowIcon */
    case 0xa954: *args = 26; *result = 4; return 1;                                  /* NewControl */
    case 0xa955: case 0xa956: case 0xa957: case 0xa958: case 0xa969: *args = 4; return 1;       /* Dispose, Kill, Show, Hide, DrawControls */
    case 0xa953: *args = 8; return 1;                                                /* UpdateControls(window, rgn) */
    case 0xa959: case 0xa95c: *args = 8; return 1;                                   /* MoveControl, SizeControl */
    case 0xa95a: *args = 4; *result = 4; return 1;                                   /* GetCRefCon */
    case 0xa95b: *args = 8; return 1;                                                /* SetCRefCon */
    case 0xa95d: case 0xa963: case 0xa964: case 0xa965: *args = 6; return 1;        /* HiliteControl, SetCtlValue, SetMinCtl, SetMaxCtl */
    case 0xa95e: case 0xa95f: *args = 8; return 1;                                   /* GetCTitle, SetCTitle */
    case 0xa960: case 0xa961: case 0xa962: *args = 4; *result = 2; return 1;         /* GetCtlValue, GetMinCtl, GetMaxCtl */
    case 0xa96a: *args = 4; *result = 4; return 1;                                   /* GetCtlAction */
    case 0xa96b: *args = 8; return 1;                                                /* SetCtlAction */
    case 0xa966: *args = 8; *result = 2; return 1;                                   /* TestControl(ctl, pt) */
    case 0xa96c: *args = 12; *result = 2; return 1;                                  /* FindControl(pt, window, VAR ctl) */
    case 0xa968: *args = 12; *result = 2; return 1;                                  /* TrackControl */
    case 0xa967: *args = 20; return 1;                                               /* DragControl (named, not implemented) */
    case 0xa9be: *args = 6; *result = 4; return 1;                                   /* GetNewControl */
    default: return 0;
    }
}
int mac_controls_service(MacRuntime *rt, uint16_t op) {
    unsigned args, result;
    if (!shape(op, &args, &result)) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    if (op == 0xa967) return fail(rt, "DragControl is not implemented.");
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if ((sp & 1) || !ram_ok(rt, sp, args + result)) return fail(rt, "Control Manager call has an invalid stack.");
    MacWindowState *w;
    if (mac_cdef_job_new(rt, sp, args, fin_result) < 0) return -1;
    if (op == 0xa954) return new_control(rt, sp);
    if (op == 0xa9be) return get_new_control(rt, sp);
    if (op == 0xa904) {                                                              /* DrawGrowIcon(window): validated, draws nothing */
        w = mac_window_find(rt, rd(rt, sp, 4));
        if (!w) return fail(rt, "DrawGrowIcon's window is not a registered window.");
        /* No window definition this runtime creates (procIDs 1, 2, 4, 12 and the approximated custom ones) has a grow box, so there is nothing to draw. */
        return mac_cdef_start(rt);
    }
    uint32_t h = 0, p = 0;
    switch (op) {
    case 0xa956: case 0xa969: case 0xa953: {                                         /* KillControls, DrawControls, UpdateControls: take a window */
        w = mac_window_find(rt, rd(rt, sp + (op == 0xa953 ? 4 : 0), 4));
        if (!w) return fail(rt, "The window is not a registered window.");
        uint32_t cur = rd(rt, w->address + 140, 4);
        for (unsigned guard = 0; cur && guard < 512; guard++) {
            uint32_t pr = record(rt, cur); if (!pr) return -1;
            uint32_t next = rd(rt, pr + CTL_NEXT, 4);
            if (mac_cdef_add(rt, cur, op == 0xa956 ? CDEF_DISP : CDEF_DRAW, 0) < 0) return -1;
            cur = next;
        }
        if (cur) return fail(rt, "A window's control list is longer than 512 entries or loops.");
        if (op == 0xa956) rt->cdef->finish = fin_dispose;
        return mac_cdef_start(rt); }
    case 0xa96c: {                                                                   /* FindControl(thePoint, theWindow, VAR theControl): part */
        uint32_t out = rd(rt, sp, 4), pt = rd(rt, sp + 8, 4);
        w = mac_window_find(rt, rd(rt, sp + 4, 4));
        if (!w) return fail(rt, "FindControl's window is not a registered window.");
        if (!ram_ok(rt, out, 4) || (out & 1)) return fail(rt, "FindControl's result pointer is invalid.");
        int v = (int16_t)(pt >> 16), hh = (int16_t)(pt & 0xffff); uint32_t cur = rd(rt, w->address + 140, 4);
        for (unsigned guard = 0; cur && guard < 512; guard++) {                      /* front to back: the visible, active controls that hold the point */
            uint32_t pr = record(rt, cur); if (!pr) return -1;
            int b0 = (int16_t)rd(rt, pr + CTL_RECT, 2), b1 = (int16_t)rd(rt, pr + CTL_RECT + 2, 2), b2 = (int16_t)rd(rt, pr + CTL_RECT + 4, 2), b3 = (int16_t)rd(rt, pr + CTL_RECT + 6, 2);
            if (rd(rt, pr + CTL_VIS, 1) && rd(rt, pr + CTL_HILITE, 1) != 255 && v >= b0 && v < b2 && hh >= b1 && hh < b3 && mac_cdef_add(rt, cur, CDEF_TEST, pt) < 0) return -1;
            cur = rd(rt, pr + CTL_NEXT, 4);
        }
        if (cur) return fail(rt, "A window's control list is longer than 512 entries or loops.");
        rt->cdef->finish = fin_find; rt->cdef->user[0] = out; rt->cdef->user[1] = sp + args;
        return mac_cdef_start(rt); }
    default: break;
    }
    h = rd(rt, sp + args - 4, 4);                                                    /* the control handle is the first argument (deepest) */
    { int item = mac_dlgmgr_item_control(rt, op, h, sp, args); if (item) return item; }
    p = record(rt, h); if (!p) return -1;
    switch (op) {
    case 0xa955: rt->cdef->finish = fin_dispose; mac_cdef_add(rt, h, CDEF_DISP, 0); break;
    case 0xa957: case 0xa958: {
        w = owner_of(rt, p); if (!w) return -1;
        int on = op == 0xa957;
        if (on && !rd(rt, p + CTL_VIS, 1)) { wr(rt, p + CTL_VIS, 1, 1); redraw(rt, h); }
        if (!on && rd(rt, p + CTL_VIS, 1)) { if (w->visible) mac_control_draw(rt, w, p, 1); wr(rt, p + CTL_VIS, 1, 0); }
        break; }
    case 0xa959: case 0xa95c: {                                                      /* MoveControl(ctl, h, v) / SizeControl(ctl, w, h) */
        w = owner_of(rt, p); if (!w) return -1;
        int x = (int16_t)rd(rt, sp + 2, 2), y = (int16_t)rd(rt, sp, 2), t = (int16_t)rd(rt, p + CTL_RECT, 2), l = (int16_t)rd(rt, p + CTL_RECT + 2, 2);
        int b = (int16_t)rd(rt, p + CTL_RECT + 4, 2), r = (int16_t)rd(rt, p + CTL_RECT + 6, 2);
        if (op == 0xa959) { b = y + (b - t); r = x + (r - l); t = y; l = x; }
        else { b = t + y; r = l + x; }                                                    /* the Control Manager does not validate: a zero or negative size leaves an empty rectangle, which draws and hit-tests as nothing (a pane still 0 wide asks for width - 15) */
        if (w->visible && rd(rt, p + CTL_VIS, 1)) mac_control_draw(rt, w, p, 1);
        wr(rt, p + CTL_RECT, 2, (uint16_t)t); wr(rt, p + CTL_RECT + 2, 2, (uint16_t)l); wr(rt, p + CTL_RECT + 4, 2, (uint16_t)b); wr(rt, p + CTL_RECT + 6, 2, (uint16_t)r);
        redraw(rt, h);
        break; }
    case 0xa95a: wr(rt, sp + args, 4, rd(rt, p + CTL_REFCON, 4)); break;
    case 0xa95b: wr(rt, p + CTL_REFCON, 4, rd(rt, sp, 4)); break;
    case 0xa96a: wr(rt, sp + args, 4, rd(rt, p + CTL_ACTION, 4)); break;
    case 0xa96b: wr(rt, p + CTL_ACTION, 4, rd(rt, sp, 4)); break;
    case 0xa95d: wr(rt, p + CTL_HILITE, 1, rd(rt, sp, 2) & 0xff); redraw(rt, h); break;
    case 0xa960: wr(rt, sp + args, 2, rd(rt, p + CTL_VALUE, 2)); break;
    case 0xa961: wr(rt, sp + args, 2, rd(rt, p + CTL_MIN, 2)); break;
    case 0xa962: wr(rt, sp + args, 2, rd(rt, p + CTL_MAX, 2)); break;
    case 0xa963: case 0xa964: case 0xa965: {
        int16_t v = (int16_t)rd(rt, sp, 2), lo = (int16_t)rd(rt, p + CTL_MIN, 2), hi = (int16_t)rd(rt, p + CTL_MAX, 2), cur = (int16_t)rd(rt, p + CTL_VALUE, 2);
        if (op == 0xa964) lo = v; else if (op == 0xa965) hi = v;
        if (hi < lo) return fail(rt, "A control's maximum cannot be set below its minimum.");
        if (op == 0xa963) cur = v;
        if (cur < lo) cur = lo; if (cur > hi) cur = hi;
        wr(rt, p + CTL_MIN, 2, (uint16_t)lo); wr(rt, p + CTL_MAX, 2, (uint16_t)hi); wr(rt, p + CTL_VALUE, 2, (uint16_t)cur);
        redraw(rt, h);
        break; }
    case 0xa95e: {                                                                   /* GetCTitle(ctl, VAR title) */
        uint32_t out = rd(rt, sp, 4); unsigned n = rd(rt, p + CTL_TITLE, 1);
        if (!ram_ok(rt, out, 1 + (size_t)n)) return fail(rt, "GetCTitle's destination is outside guest RAM.");
        memmove(mac_cpu_ram(rt->cpu) + out, mac_cpu_ram(rt->cpu) + p + CTL_TITLE, 1 + (size_t)n);
        break; }
    case 0xa95f: {                                                                   /* SetCTitle(ctl, title) */
        uint32_t src = rd(rt, sp, 4);
        if (!pascal_ok(rt, src)) return fail(rt, "SetCTitle's title is outside guest RAM.");
        memmove(mac_cpu_ram(rt->cpu) + p + CTL_TITLE, mac_cpu_ram(rt->cpu) + src, 1 + (size_t)mac_cpu_ram(rt->cpu)[src]);
        redraw(rt, h);
        break; }
    case 0xa968: {                                                                   /* TrackControl(ctl, startPt, actionProc): custom CDEFs only, with the pop-up convention actionProc = -1 */
        uint32_t pt = rd(rt, sp + 4, 4);
        if (!rd(rt, p + CTL_DEFPROC, 4)) return fail(rt, "TrackControl on a standard control is not implemented.");
        if (rd(rt, sp, 4) != 0xffffffffu) return fail(rt, "TrackControl with an action procedure (or none) is not implemented: only -1, which sends autoTrack to the CDEF.");
        rt->cdef->finish = fin_test; rt->cdef->user[1] = sp + args; rt->cdef->keep_going = 1;
        mac_cdef_add(rt, h, CDEF_TEST, pt); mac_cdef_add(rt, h, CDEF_AUTOTRACK, 0);        /* a result of 0 when testCntl finds nothing; otherwise the part */
        break; }
    case 0xa966: {                                                                   /* TestControl(ctl, pt): part */
        uint32_t pt = rd(rt, sp, 4);
        rt->cdef->finish = fin_test; rt->cdef->user[1] = sp + args;
        mac_cdef_add(rt, h, CDEF_TEST, pt);
        break; }
    default: return fail(rt, "Unhandled control trap.");
    }
    if (mac_cpu_halted(rt->cpu)) return -1;
    return mac_cdef_start(rt);
}
