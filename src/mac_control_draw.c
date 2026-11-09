#include "mac_controls.h"
#include "mac_dialog_draw.h"
#include "mac_window_int.h"

/* Native drawing and hit testing of the standard controls. The look is this project's own (not Apple's CDEFs): a push button is the dialog button, a check
 * box or radio button a 12-pixel box (marked when its value is non-zero) with the title beside it, a scroll bar a 16-pixel-wide strip with an arrow cell at
 * each end, a track and a 16-pixel thumb. The control's procID is not in the record (contrlDefProc is a handle, here nil), so it lives in contrlData's slot:
 * the runtime keeps the procID in the long at CTL_DATA, and a program that reads contrlData sees that small number instead of a handle (stated difference).
 * All coordinates are window-local; global = local minus the port bitmap's bounds origin, as elsewhere. Clipped to the window's content rectangle. */
static uint32_t rd(MacRuntime *rt, uint32_t a, unsigned n) { return mac_cpu_read(rt->cpu, a, n); }
static int ram_ok(const MacRuntime *rt, uint32_t p, size_t n) { size_t z = mac_cpu_ram_size(rt->cpu); return p >= 0x400 && p <= z && n <= z - p; }

static int box(MacRuntime *rt, uint32_t p, int out[4]) {
    if (!ram_ok(rt, p, CTL_BYTES)) return 0;
    out[0] = (int16_t)rd(rt, p + CTL_RECT, 2); out[1] = (int16_t)rd(rt, p + CTL_RECT + 2, 2); out[2] = (int16_t)rd(rt, p + CTL_RECT + 4, 2); out[3] = (int16_t)rd(rt, p + CTL_RECT + 6, 2);
    return out[2] > out[0] && out[3] > out[1];
}
static unsigned proc_of(MacRuntime *rt, uint32_t p) { return rd(rt, p + CTL_DATA, 4) & ~8u; }
static int scroll_geometry(MacRuntime *rt, uint32_t p, int *horizontal, int *length, int *thumb) {
    int b[4]; if (!box(rt, p, b)) return 0;
    *horizontal = (b[3] - b[1]) > (b[2] - b[0]);
    *length = *horizontal ? b[3] - b[1] : b[2] - b[0];
    int lo = (int16_t)rd(rt, p + CTL_MIN, 2), hi = (int16_t)rd(rt, p + CTL_MAX, 2), v = (int16_t)rd(rt, p + CTL_VALUE, 2), track = *length - 32;
    if (track < 16 || hi <= lo) { *thumb = -1; return 1; }
    if (v < lo) v = lo; if (v > hi) v = hi;
    *thumb = 16 + (int)(((long)(v - lo) * (track - 16)) / (hi - lo));
    return 1;
}

int mac_control_part(MacRuntime *rt, uint32_t p, int v, int h) {
    if (rd(rt, p + CTL_DEFPROC, 4)) return 0;                  /* a custom control's parts come from its own CDEF */
    int b[4]; if (!box(rt, p, b) || !rd(rt, p + CTL_VIS, 1) || rd(rt, p + CTL_HILITE, 1) == 255) return 0;
    if (v < b[0] || v >= b[2] || h < b[1] || h >= b[3]) return 0;
    unsigned proc = proc_of(rt, p);
    if (proc == 0) return 10;
    if (proc == 1 || proc == 2) return 11;
    int horizontal, length, thumb; if (!scroll_geometry(rt, p, &horizontal, &length, &thumb)) return 0;
    int at = horizontal ? h - b[1] : v - b[0];
    if (at < 16) return 20;
    if (at >= length - 16) return 21;
    if (thumb < 0) return 0;                                  /* no range: only the arrows respond */
    if (at < thumb) return 22;
    if (at < thumb + 16) return 129;
    return 23;
}

void mac_control_draw(MacRuntime *rt, const MacWindowState *w, uint32_t p, int erase) {
    int b[4]; if (!box(rt, p, b)) return;
    if (!erase && rd(rt, p + CTL_DEFPROC, 4)) return;          /* a custom control draws itself */
    uint32_t pm = rd(rt, w->handles[0], 4);
    int oy = ram_ok(rt, pm, 50) ? (int16_t)rd(rt, pm + 6, 2) : 0, ox = ram_ok(rt, pm, 50) ? (int16_t)rd(rt, pm + 8, 2) : 0;
    int t = b[0] - oy, l = b[1] - ox, bt = b[2] - oy, r = b[3] - ox;
    MacPen pen; mac_pen_init(&pen, rt, w->bounds[1], w->bounds[0], w->bounds[3] - w->bounds[1], w->bounds[2] - w->bounds[0]);
    if (erase) { mac_pen_fill(&pen, l, t, r, bt, pen.light); return; }
    int active = rd(rt, p + CTL_HILITE, 1) != 255;
    unsigned proc = proc_of(rt, p), n = rd(rt, p + CTL_TITLE, 1);
    char title[256]; for (unsigned i = 0; i < n; i++) title[i] = (char)rd(rt, p + CTL_TITLE + 1 + i, 1);
    if (proc == 0) {
        mac_pen_button(&pen, l, t, r, bt, (const uint8_t *)title, n, active, rd(rt, p + CTL_HILITE, 1) == 10, 0);
    } else if (proc == 1 || proc == 2) {
        mac_pen_fill(&pen, l, t, r, bt, pen.light);
        int my = t + (bt - t - 12) / 2; if (my < t) my = t;
        mac_pen_box(&pen, l, my, l + 12, my + 12, pen.dark);
        if ((int16_t)rd(rt, p + CTL_VALUE, 2) != 0) { mac_pen_fill(&pen, l + 3, my + 3, l + 9, my + 9, pen.dark); }
        mac_pen_text(&pen, l + 16, my + 2, title, n, pen.dark, !active);
    } else {
        int horizontal, length, thumb; if (!scroll_geometry(rt, p, &horizontal, &length, &thumb)) return;
        mac_pen_fill(&pen, l, t, r, bt, pen.light);
        mac_pen_box(&pen, l, t, r, bt, pen.dark);
        for (int end = 0; end < 2; end++) {                    /* the two arrow cells: a box with a small triangle */
            int a0 = end ? length - 16 : 0;
            int cl = horizontal ? l + a0 : l, ct = horizontal ? t : t + a0, cr = horizontal ? cl + 16 : r, cb = horizontal ? bt : ct + 16;
            mac_pen_box(&pen, cl, ct, cr, cb, pen.dark);
            if (!active) continue;
            int cx = (cl + cr) / 2, cy = (ct + cb) / 2;
            for (int k = 0; k < 4; k++) {
                int sign = end ? -1 : 1;
                if (horizontal) mac_pen_fill(&pen, cx + sign * (k - 2), cy - (3 - k), cx + sign * (k - 2) + 1, cy + (3 - k) + 1, pen.dark);
                else mac_pen_fill(&pen, cx - (3 - k), cy + sign * (k - 2), cx + (3 - k) + 1, cy + sign * (k - 2) + 1, pen.dark);
            }
        }
        if (active && thumb >= 0) {
            int tl = horizontal ? l + thumb : l, tt = horizontal ? t : t + thumb;
            mac_pen_fill(&pen, tl, tt, horizontal ? tl + 16 : r, horizontal ? bt : tt + 16, pen.light);
            mac_pen_box(&pen, tl, tt, horizontal ? tl + 16 : r, horizontal ? bt : tt + 16, pen.dark);
        }
    }
}
