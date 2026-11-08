/* Where the desktop goes on a folding phone, and how big its pixels are.
 *
 * The desktop is resolution independent (session_paint takes any size), so
 * instead of shrinking a 1024x768 picture onto a 6-inch screen we give each
 * posture its own desktop resolution and draw every desktop pixel as an
 * integer block of device pixels. Pixel art stays crisp, and targets keep a
 * size a fingertip can hit.
 */
#include "mobile.h"

#include <math.h>
#include <string.h>

/* One desktop pixel should be about this many millimetres on glass. A classic
 * 72-dpi monitor was 0.35 mm; 0.19 mm keeps a 20 px menu bar near 4 mm while
 * still fitting a useful desktop on a cover screen. */
#define TARGET_MM 0.19f
#define LAPTOP_MM 0.15f   /* on a desk the screen sits further away, and you point with the pad */
#define FINEST_MM 0.10f

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static float screen_ppi(const MobileScreen *s) {
    if (s->ppi > 0) return s->ppi;
    int lo = imin(s->w, s->h);
    /* Unknown density: assume a phone-width cover (~400 dp) or inner (~700 dp). */
    return lo / (mobile_posture_detect(s) == MP_COVER ? 400.0f : 700.0f) * 160.0f;
}

static Rect empty_rect(void) { return (Rect){0, 0, 0, 0}; }

static void hinge(const MobileScreen *s, int *axis, int *at) {
    if (s->fold_axis == MF_AXIS_HORIZONTAL) { *axis = 1; *at = s->fold.y + s->fold.h / 2; }
    else if (s->fold_axis == MF_AXIS_VERTICAL) { *axis = 2; *at = s->fold.x + s->fold.w / 2; }
    else { *axis = 0; *at = 0; }
}

static void choose_scales(MobileLayout *l, float ppi, float target_mm) {
    int finest = imax(1, (int)ceilf(ppi * FINEST_MM / 25.4f));
    l->nscales = 0;
    for (int sc = finest; sc <= finest + 8 && l->nscales < MOBILE_MAX_SCALES; sc++) {
        if (l->region.w / sc >= MOBILE_MIN_W && l->region.h / sc >= MOBILE_MIN_H)
            l->scales[l->nscales++] = sc;
    }
    if (l->nscales == 0) l->scales[l->nscales++] = finest;   /* tiny screen: best effort */
    float target = ppi * target_mm / 25.4f;
    int best = 0;
    for (int i = 1; i < l->nscales; i++) {
        float d = fabsf(l->scales[i] - target), b = fabsf(l->scales[best] - target);
        if (d < b || (d == b && l->scales[i] > l->scales[best])) best = i;
    }
    l->default_scale = l->scales[best];
}

void mobile_layout(const MobileScreen *s, MobilePosture posture, int scale_pref,
                   MobileLayout *out) {
    memset(out, 0, sizeof *out);
    if (!s || s->w <= 0 || s->h <= 0) return;
    if (posture == MP_AUTO) posture = mobile_posture_detect(s);
    float ppi = screen_ppi(s);
    out->posture = posture;
    out->tablet = s->tablet;
    out->dp = ppi / 160.0f;
    Rect usable = {s->inset_l, s->inset_t, s->w - s->inset_l - s->inset_r,
                   s->h - s->inset_t - s->inset_b};
    out->region = usable;
    out->deck = out->crease = empty_rect();
    int axis, at;
    hinge(s, &axis, &at);
    int band = (int)lroundf(10 * out->dp);
    if (axis == 1 && posture != MP_COVER)
        out->crease = (Rect){0, at - band, s->w, 2 * band};
    else if (axis == 2 && posture != MP_COVER)
        out->crease = (Rect){at - band, 0, 2 * band, s->h};
    switch (posture) {
    case MP_LAPTOP: {
        /* The keyboard deck owns everything below the hinge. If the platform
         * gave no hinge (posture forced by hand), fold at the middle as the
         * keyboard does. */
        int fold_y = axis == 1 ? at : s->h / 2;
        out->region.h = imax(1, fold_y - band - usable.y);
        out->deck = (Rect){0, fold_y, s->w, s->h - fold_y};
        out->crease = (Rect){0, fold_y - band, s->w, 2 * band};
        out->kbd_posture = 2;   /* KBD_FLEX */
        out->kbd_always = 1;
        break;
    }
    case MP_BOOK:
    case MP_FLAT:
        out->kbd_posture = 1;   /* KBD_OPEN: thumb halves, nothing on the crease */
        out->kbd_split = 1;
        break;
    default:
        out->kbd_posture = 0;   /* KBD_COVER */
        break;
    }
    choose_scales(out, ppi, posture == MP_LAPTOP ? LAPTOP_MM : TARGET_MM);
    int scale = out->default_scale;
    for (int i = 0; scale_pref > 0 && i < out->nscales; i++)
        if (out->scales[i] == scale_pref) scale = scale_pref;
    out->scale = scale;
    for (int i = 0; i < out->nscales; i++)
        if (out->scales[i] == scale) out->scale_index = i;
    out->desk_w = imax(1, out->region.w / scale);
    out->desk_h = imax(1, out->region.h / scale);
    out->desk = (Rect){out->region.x + (out->region.w - out->desk_w * scale) / 2,
                       out->region.y + (out->region.h - out->desk_h * scale) / 2,
                       out->desk_w * scale, out->desk_h * scale};
}

int mobile_layout_cycle_scale(const MobileLayout *l, int direction) {
    if (!l || l->nscales <= 0) return 0;
    int i = l->scale_index + (direction < 0 ? -1 : 1);
    i = (i % l->nscales + l->nscales) % l->nscales;
    return l->scales[i];
}

/* ---- view ---- */

void mobile_view_reset(MobileView *v) { v->zoom = 1; v->ox = v->oy = 0; v->lift = 0; }

void mobile_view_clamp(MobileView *v, const MobileLayout *l) {
    if (v->zoom < 1.0f) v->zoom = 1.0f;
    if (v->zoom > MOBILE_MAX_ZOOM) v->zoom = MOBILE_MAX_ZOOM;
    float vw = l->desk_w / v->zoom, vh = l->desk_h / v->zoom;
    if (v->ox > l->desk_w - vw) v->ox = l->desk_w - vw;
    if (v->oy > l->desk_h - vh) v->oy = l->desk_h - vh;
    if (v->ox < 0) v->ox = 0;
    if (v->oy < 0) v->oy = 0;
}

static float px_per_desk(const MobileView *v, const MobileLayout *l) {
    return (float)l->scale * v->zoom;
}

void mobile_view_zoom_at(MobileView *v, const MobileLayout *l, float zoom, float sx, float sy) {
    float k = px_per_desk(v, l);
    float dx = v->ox + (sx - l->desk.x) / k, dy = v->oy + (sy - (l->desk.y - v->lift)) / k;
    v->zoom = zoom;
    mobile_view_clamp(v, l);
    k = px_per_desk(v, l);
    v->ox = dx - (sx - l->desk.x) / k;
    v->oy = dy - (sy - (l->desk.y - v->lift)) / k;
    mobile_view_clamp(v, l);
}

void mobile_view_pan(MobileView *v, const MobileLayout *l, float dx_px, float dy_px) {
    float k = px_per_desk(v, l);
    v->ox -= dx_px / k;
    v->oy -= dy_px / k;
    mobile_view_clamp(v, l);
}

int mobile_view_to_desk(const MobileView *v, const MobileLayout *l, float sx, float sy,
                        int *dx, int *dy) {
    float k = px_per_desk(v, l);
    float fx = v->ox + (sx - l->desk.x) / k, fy = v->oy + (sy - (l->desk.y - v->lift)) / k;
    int inside = fx >= 0 && fy >= 0 && fx < l->desk_w && fy < l->desk_h &&
                 sx >= l->desk.x && sx < l->desk.x + l->desk.w;
    int x = (int)floorf(fx), y = (int)floorf(fy);
    *dx = imax(0, imin(l->desk_w - 1, x));
    *dy = imax(0, imin(l->desk_h - 1, y));
    return inside;
}

void mobile_view_to_screen(const MobileView *v, const MobileLayout *l, float dx, float dy,
                           float *sx, float *sy) {
    float k = px_per_desk(v, l);
    *sx = l->desk.x + (dx - v->ox) * k;
    *sy = l->desk.y - v->lift + (dy - v->oy) * k;
}

void mobile_view_rects(const MobileView *v, const MobileLayout *l, Rect *src, Rect *dst) {
    int vw = (int)ceilf(l->desk_w / v->zoom), vh = (int)ceilf(l->desk_h / v->zoom);
    int ox = (int)floorf(v->ox), oy = (int)floorf(v->oy);
    if (ox + vw > l->desk_w) ox = l->desk_w - vw;
    if (oy + vh > l->desk_h) oy = l->desk_h - vh;
    *src = (Rect){imax(0, ox), imax(0, oy), vw, vh};
    *dst = (Rect){l->desk.x, l->desk.y - v->lift, l->desk.w, l->desk.h};
}

void mobile_view_lift(MobileView *v, const MobileLayout *l, int focus_y, int occluded,
                      int screen_h) {
    if (occluded <= 0) { v->lift = 0; return; }
    float k = px_per_desk(v, l);
    float fy = l->desk.y + (focus_y - v->oy) * k;
    int margin = (int)(48 * l->scale);
    int need = (int)ceilf(fy + margin - (screen_h - occluded));
    v->lift = imax(0, imin(need, occluded));
}
