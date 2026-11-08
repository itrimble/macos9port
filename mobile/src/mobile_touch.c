/* Gesture recognizer: fingers in, classic mouse actions out (see mobile_touch.h). */
#include "mobile_touch.h"
#include "kbd.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { S_IDLE, S_PENDING, S_DRAG, S_AIM, S_TWO, S_DONE, S_TP_PENDING, S_TP_MOVE, S_TP_DRAG };

static void push(MobileTouch *t, MobileAction a) {
    int next = (t->qt + 1) % MT_QUEUE;
    if (next == t->qh) return;   /* full: drop, the next frame catches up */
    t->q[t->qt] = a;
    t->qt = next;
}

static void act(MobileTouch *t, MobileActionKind k, int x, int y, int clicks) {
    push(t, (MobileAction){k, x, y, clicks, 0, 0, 0});
}

static void buzz(MobileTouch *t, int type) {
    push(t, (MobileAction){MA_HAPTIC, 0, 0, 0, 0, 0, type});
}

static float dp(const MobileTouch *t) { return t->layout && t->layout->dp > 0 ? t->layout->dp : 1; }

static void desk_point(const MobileTouch *t, float sx, float sy, int *x, int *y) {
    mobile_view_to_desk(t->view, t->layout, sx, sy, x, y);
}

static MobileFinger *finger(MobileTouch *t, int64_t id) {
    for (int i = 0; i < MT_FINGERS; i++)
        if (t->f[i].active && t->f[i].id == id) return &t->f[i];
    return NULL;
}

static MobileFinger *nth_active(MobileTouch *t, int n) {
    for (int i = 0; i < MT_FINGERS; i++)
        if (t->f[i].active && n-- == 0) return &t->f[i];
    return NULL;
}

static float dist(float ax, float ay, float bx, float by) { return hypotf(ax - bx, ay - by); }

void mobile_touch_init(MobileTouch *t, const MobileLayout *l, MobileView *v) {
    MobileTouchMode mode = t->mode;
    memset(t, 0, sizeof *t);
    t->mode = mode;
    t->layout = l;
    t->view = v;
    t->slop_dp = 8;
    t->double_dp = 28;
    t->aim_ms = 320;
    t->context_ms = 1000;
    t->tap_ms = 260;
    t->double_ms = 380;
    t->tracking = 1.0f;
    if (l) { t->px = l->desk_w / 2.0f; t->py = l->desk_h / 2.0f; }
}

void mobile_touch_set_mode(MobileTouch *t, MobileTouchMode mode) {
    mobile_touch_cancel(t);
    t->mode = mode;
    push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
}

int mobile_touch_busy(const MobileTouch *t) { return t->nf > 0 || t->loupe; }

static void begin_two(MobileTouch *t, uint32_t ms) {
    MobileFinger *a = nth_active(t, 0), *b = nth_active(t, 1);
    if (!a || !b) return;
    t->state = S_TWO;
    t->loupe = 0;
    t->ring = 0;
    t->g_cx = (a->x + b->x) / 2;
    t->g_cy = (a->y + b->y) / 2;
    t->g_dist = fmaxf(1, dist(a->x, a->y, b->x, b->y));
    t->g_zoom0 = t->view->zoom;
    t->g_mode = 0;
    t->wheel_acc = 0;
    t->g_down_ms = ms;
    t->g_travel = 0;
    push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
}

void mobile_touch_down(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms) {
    if (finger(t, id)) return;
    MobileFinger *f = NULL;
    for (int i = 0; i < MT_FINGERS && !f; i++)
        if (!t->f[i].active) f = &t->f[i];
    if (!f) return;
    *f = (MobileFinger){id, sx, sy, sx, sy, 1};
    t->nf++;
    if (t->nf == 1) {
        t->down_ms = ms;
        t->dragging = t->context_sent = 0;
        t->ring = 0;
        t->state = t->mode == MT_TRACKPAD ? S_TP_PENDING : S_PENDING;
        return;
    }
    if (t->nf == 2 && (t->state == S_PENDING || t->state == S_AIM ||
                       t->state == S_TP_PENDING || t->state == S_TP_MOVE))
        begin_two(t, ms);
}

static void two_move(MobileTouch *t) {
    MobileFinger *a = nth_active(t, 0), *b = nth_active(t, 1);
    if (!a || !b) return;
    float cx = (a->x + b->x) / 2, cy = (a->y + b->y) / 2;
    float d = fmaxf(1, dist(a->x, a->y, b->x, b->y));
    float ddx = cx - t->g_cx, ddy = cy - t->g_cy;
    t->g_travel += hypotf(ddx, ddy);
    if (t->g_mode == 0) {
        if (fabsf(d - t->g_dist) > 14 * dp(t)) t->g_mode = 2;
        else if (t->g_travel > t->slop_dp * dp(t)) t->g_mode = t->view->zoom > 1.01f ? 2 : 1;
    }
    if (t->g_mode == 1) {
        float line = 16.0f * t->layout->scale * t->view->zoom;
        t->wheel_acc += ddy / line;
        int lines = (int)t->wheel_acc;
        if (lines) {
            t->wheel_acc -= lines;
            push(t, (MobileAction){MA_WHEEL, 0, 0, 0, lines, 0, 0});
        }
    } else if (t->g_mode == 2) {
        float z = t->g_zoom0 * d / t->g_dist;
        if (z < 1.04f) z = 1.0f;   /* settle cleanly back to fit */
        mobile_view_zoom_at(t->view, t->layout, z, cx, cy);
        mobile_view_pan(t->view, t->layout, ddx, ddy);
        push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
    }
    t->g_cx = cx;
    t->g_cy = cy;
}

static void pointer_by(MobileTouch *t, float dx, float dy) {
    float k = (float)t->layout->scale * t->view->zoom;
    float speed = hypotf(dx, dy) / dp(t);
    float gain = t->tracking * (1.0f + 0.8f * fminf(1.0f, speed / 10.0f));
    t->px = fmaxf(0, fminf(t->layout->desk_w - 1, t->px + dx * gain / k * 1.6f));
    t->py = fmaxf(0, fminf(t->layout->desk_h - 1, t->py + dy * gain / k * 1.6f));
    act(t, MA_MOVE, (int)t->px, (int)t->py, 0);
}

void mobile_touch_move(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms) {
    (void)ms;
    MobileFinger *f = finger(t, id);
    if (!f) return;
    float lx = f->x, ly = f->y;
    f->x = sx;
    f->y = sy;
    float slop = t->slop_dp * dp(t);
    int x, y;
    switch (t->state) {
    case S_PENDING:
        if (dist(sx, sy, f->x0, f->y0) <= slop) break;
        desk_point(t, f->x0, f->y0, &x, &y);
        act(t, MA_MOVE, x, y, 0);
        act(t, MA_DOWN, x, y, 1);
        t->state = S_DRAG;
        t->dragging = 1;
        /* then follow the finger, as a drag does */
        /* fall through */
    case S_DRAG:
        desk_point(t, sx, sy, &x, &y);
        act(t, MA_MOVE, x, y, 0);
        break;
    case S_AIM:
        desk_point(t, sx, sy, &x, &y);
        act(t, MA_MOVE, x, y, 0);
        t->loupe_sx = sx;
        t->loupe_sy = sy;
        if (dist(sx, sy, t->aim_x0, t->aim_y0) > slop) t->ring = -1;   /* aiming, no menu */
        push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
        break;
    case S_TWO:
        two_move(t);
        break;
    case S_TP_PENDING:
        pointer_by(t, sx - lx, sy - ly);
        if (dist(sx, sy, f->x0, f->y0) > slop) {
            int tapdrag = t->tap_count > 0 && t->down_ms - t->last_tap_ms <= t->double_ms;
            if (tapdrag) {
                act(t, MA_DOWN, (int)t->px, (int)t->py, 1);
                t->state = S_TP_DRAG;
                t->dragging = 1;
            } else {
                t->state = S_TP_MOVE;
            }
        }
        break;
    case S_TP_MOVE:
    case S_TP_DRAG:
        pointer_by(t, sx - lx, sy - ly);
        break;
    default:
        break;
    }
}

static void click_at(MobileTouch *t, int x, int y, uint32_t ms) {
    int near = abs(x - t->last_tap_x) * t->layout->scale <= t->double_dp * dp(t) &&
               abs(y - t->last_tap_y) * t->layout->scale <= t->double_dp * dp(t);
    int clicks = (t->tap_count > 0 && ms - t->last_tap_ms <= t->double_ms && near) ? 2 : 1;
    act(t, MA_MOVE, x, y, 0);
    act(t, MA_DOWN, x, y, clicks);
    act(t, MA_UP, x, y, 0);
    t->tap_count = clicks == 2 ? 0 : 1;   /* a third tap starts a new pair */
    t->last_tap_ms = ms;
    t->last_tap_x = x;
    t->last_tap_y = y;
}

void mobile_touch_up(MobileTouch *t, int64_t id, float sx, float sy, uint32_t ms) {
    MobileFinger *f = finger(t, id);
    if (!f) return;
    f->x = sx;
    f->y = sy;
    int x, y;
    switch (t->state) {
    case S_PENDING:
        desk_point(t, f->x0, f->y0, &x, &y);   /* where the finger landed */
        click_at(t, x, y, ms);
        t->state = S_DONE;
        break;
    case S_DRAG:
        desk_point(t, sx, sy, &x, &y);
        act(t, MA_UP, x, y, 0);
        t->state = S_DONE;
        break;
    case S_AIM:
        desk_point(t, sx, sy, &x, &y);
        t->loupe = 0;
        t->ring = 0;
        click_at(t, x, y, ms);
        t->state = S_DONE;
        push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
        break;
    case S_TWO:
        if (t->g_mode == 0 && ms - t->g_down_ms <= 350 && t->g_travel <= 2 * t->slop_dp * dp(t)) {
            if (t->mode == MT_TRACKPAD) { x = (int)t->px; y = (int)t->py; }
            else desk_point(t, t->g_cx, t->g_cy, &x, &y);
            act(t, MA_MOVE, x, y, 0);
            act(t, MA_CONTEXT, x, y, 0);
            buzz(t, KBD_HAPTIC_TAP);
        }
        t->state = S_DONE;
        break;
    case S_TP_PENDING:
        if (ms - t->down_ms <= t->tap_ms) click_at(t, (int)t->px, (int)t->py, ms);
        t->state = S_DONE;
        break;
    case S_TP_DRAG:
        act(t, MA_UP, (int)t->px, (int)t->py, 0);
        t->state = S_DONE;
        break;
    default:
        break;
    }
    f->active = 0;
    t->nf--;
    if (t->nf <= 0) {
        t->nf = 0;
        t->state = S_IDLE;
        t->loupe = 0;
    }
}

void mobile_touch_cancel(MobileTouch *t) {
    if (t->dragging && t->state != S_DONE)
        act(t, MA_UP, t->mode == MT_TRACKPAD ? (int)t->px : t->last_tap_x,
            t->mode == MT_TRACKPAD ? (int)t->py : t->last_tap_y, 0);
    for (int i = 0; i < MT_FINGERS; i++) t->f[i].active = 0;
    t->nf = 0;
    t->state = S_IDLE;
    t->loupe = 0;
    t->ring = 0;
    t->dragging = 0;
}

void mobile_touch_tick(MobileTouch *t, uint32_t ms) {
    MobileFinger *f = nth_active(t, 0);
    if (t->state == S_PENDING && f && ms - t->down_ms >= t->aim_ms) {
        t->state = S_AIM;
        t->loupe = 1;
        t->ring = 0;
        t->aim_start_ms = ms;
        t->aim_x0 = t->loupe_sx = f->x;
        t->aim_y0 = t->loupe_sy = f->y;
        int x, y;
        desk_point(t, f->x, f->y, &x, &y);
        act(t, MA_MOVE, x, y, 0);
        buzz(t, KBD_HAPTIC_SELECTION);
        push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
        return;
    }
    if (t->state == S_AIM && f && t->ring >= 0 && !t->context_sent) {
        float span = (float)(t->context_ms - t->aim_ms);
        t->ring = fminf(1.0f, (float)(ms - t->down_ms - t->aim_ms) / span);
        push(t, (MobileAction){MA_REDRAW, 0, 0, 0, 0, 0, 0});
        if (ms - t->down_ms >= t->context_ms) {
            int x, y;
            desk_point(t, f->x, f->y, &x, &y);
            act(t, MA_CONTEXT, x, y, 0);
            buzz(t, KBD_HAPTIC_FORCE);
            t->context_sent = 1;
            t->loupe = 0;
            t->ring = 0;
            t->state = S_DONE;
        }
    }
}

int mobile_touch_poll(MobileTouch *t, MobileAction *out) {
    if (t->qh == t->qt) return 0;
    *out = t->q[t->qh];
    t->qh = (t->qh + 1) % MT_QUEUE;
    return 1;
}
