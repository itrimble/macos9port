/* Trackpads: the Flex pad (with its portable-style button), the thumb pad in
 * the split gutter, and space-bar tracking on any layout. They aim to feel
 * like a modern Mac trackpad:
 *
 *  - Pointer: jitter-filtered, timestamp-true velocity, Mac-shaped
 *    acceleration, sub-pixel deltas all the way to the host.
 *  - Clickpad: where the panel reports real pressure, press anywhere to
 *    click and press deeper to force click, with a click haptic going down
 *    and a lighter, sharper one coming up. The release threshold sits well
 *    below the click threshold, so a click never chatters. Light, medium
 *    and firm match the Mac setting.
 *  - Tap to click everywhere else; tap then touch-and-move drags; a
 *    two-finger tap is a Control-click (OS 8/9 contextual menus).
 *  - Two-finger scroll with axis lock and momentum from the real flick speed;
 *    resting a finger stops the glide, and a pause before lifting cancels it.
 *  - Optional three-finger drag.
 */
#include "kbd_int.h"
#include <math.h>

enum { TAP_MS = 200, TAP_DRAG_MS = 280, MULTI_CLICK_MS = 450, TWO_TAP_MS = 260,
       FLICK_PAUSE_MS = 70 };

static const float click_at[3] = { 0.28f, 0.40f, 0.55f };

static void button(Kbd *k, int down, int mods) {
    KbdEvent e = { .kind = KBD_EV_BUTTON, .button = 1, .down = down, .mods = mods };
    if (down) {
        k->pad.clicks = (k->now - k->pad.last_click < MULTI_CLICK_MS && k->pad.last_click)
                            ? k->pad.clicks + 1 : 1;
        k->pad.last_click = k->now ? k->now : 1;
    }
    e.clicks = k->pad.clicks;
    kbd_emit(k, &e);
}

static int pad_fingers(const Kbd *k) {
    int n = 0;
    for (int i = 0; i < KBD_MAX_TOUCH; i++)
        if (k->touch[i].active && k->touch[i].role == ROLE_PAD) n++;
    return n;
}

static void motion_start(Kbd *k, KbdTouch *t) {
    kbd_euro_reset(&t->ex, t->x);
    kbd_euro_reset(&t->ey, t->y);
    t->fx = t->x;
    t->fy = t->y;
    t->speed = 0;
    t->lt = k->now ? k->now : 1;
    t->stage = t->forced = 0;
    t->ps = 0;
}

int kbd_pad_down(Kbd *k, KbdTouch *t) {
    KbdPad *p = &k->pad;
    motion_start(k, t);
    if (k->lay.pad_button.w > 0 && krect_in(k->lay.pad_button, t->x, t->y)) {
        t->role = ROLE_PAD_BUTTON;
        p->button_down = 1;
        button(k, 1, kbd_mods(k));
        kbd_emit_sound(k, KBD_SND_PAD_CLICK, t->x);
        kbd_emit_haptic(k, KBD_HAPTIC_CLICK);
        return 1;
    }
    t->role = ROLE_PAD;
    int n = pad_fingers(k);
    p->fingers = n;
    p->vx = p->vy = 0;                   /* a resting finger stops the glide */
    if (n == 1) {
        p->first_down = k->now;
        p->travel = 0;
        p->two_finger = p->three_finger = 0;
        if (p->last_tap_up && k->now - p->last_tap_up < TAP_DRAG_MS && k->set.tap_to_click) {
            p->dragging = 1;             /* tap, then touch: drag */
            button(k, 1, kbd_mods(k));
        }
    } else if (n == 2) {
        p->two_finger = 1;
        p->axis = 0;
        p->ax = p->ay = 0;
    } else if (n >= 3) {
        p->two_finger = 0;
        p->three_finger = 1;
    }
    return 1;
}

static void scroll(Kbd *k, float dx, float dy, float dt) {
    KbdPad *p = &k->pad;
    if (p->axis == 0) {
        p->ax += dx;
        p->ay += dy;
        if (fabsf(p->ax) + fabsf(p->ay) > 6 * k->dp) {
            p->axis = fabsf(p->ay) > 1.8f * fabsf(p->ax) ? 2 : fabsf(p->ax) > 1.8f * fabsf(p->ay) ? 3 : 1;
        }
    }
    if (p->axis == 2) dx = 0;
    if (p->axis == 3) dy = 0;
    float line = 14 * k->dp;
    float sx = -dx * 0.5f / line, sy = dy * 0.5f / line;   /* each finger carries half */
    if (sx == 0 && sy == 0) return;
    KbdEvent e = { .kind = KBD_EV_SCROLL, .dx = sx, .dy = sy };
    kbd_emit(k, &e);
    float a = 1 - expf(-dt / 0.05f);
    p->vx += (2 * sx / dt - p->vx) * a;
    p->vy += (2 * sy / dt - p->vy) * a;
    p->last_scroll = k->now ? k->now : 1;
}

void kbd_pad_move(Kbd *k, KbdTouch *t) {
    KbdPad *p = &k->pad;
    float dt = kbd_motion_dt(k->now, t->lt);
    t->lt = k->now ? k->now : 1;
    float nx = kbd_euro(&t->ex, t->x, dt, k->dp), ny = kbd_euro(&t->ey, t->y, dt, k->dp);
    float dx = nx - t->fx, dy = ny - t->fy;
    t->fx = nx;
    t->fy = ny;
    float d = sqrtf(dx * dx + dy * dy);
    t->speed += (d / dt - t->speed) * (1 - expf(-dt / 0.035f));
    p->travel += d;
    if (t->role == ROLE_PAD && p->two_finger && !p->three_finger) { scroll(k, dx, dy, dt); return; }
    float share = 1;
    if (t->role == ROLE_PAD && p->three_finger) {
        if (!k->set.three_finger_drag) return;
        share = 1.0f / 3.0f;
        if (!p->tfd && p->travel > 4 * k->dp) {
            p->tfd = 1;
            button(k, 1, kbd_mods(k));
            kbd_emit_haptic(k, KBD_HAPTIC_SELECTION);
        }
        if (!p->tfd) return;
    }
    float w = t->role == ROLE_CURSOR ? k->lay.deck.w : k->lay.pad.w;
    float g = kbd_pointer_gain(k, t->speed, w) * share;
    if (dx == 0 && dy == 0) return;
    KbdEvent e = { .kind = KBD_EV_POINTER, .dx = dx * g, .dy = dy * g };
    kbd_emit(k, &e);
}

/* Two-stage clickpad from contact pressure. */
void kbd_pad_force(Kbd *k, KbdTouch *t) {
    KbdPad *p = &k->pad;
    if (!p->pressure_seen) return;
    if (t->role != ROLE_CURSOR && !(t->role == ROLE_PAD && p->fingers == 1)) return;
    int firm = k->set.click_pressure < 0 ? 0 : k->set.click_pressure > 2 ? 2 : k->set.click_pressure;
    float on = click_at[firm], off = on * 0.62f, force = fminf(0.92f, on * 1.85f);
    int mods = kbd_mods(k);
    if (t->stage == 0 && t->ps >= on) {
        t->stage = 1;
        t->forced = 1;
        button(k, 1, mods);
        kbd_emit_haptic(k, KBD_HAPTIC_CLICK);
        kbd_emit_sound(k, KBD_SND_PAD_CLICK, t->x);
    } else if (t->stage > 0 && t->ps <= off) {
        t->stage = 0;
        button(k, 0, mods);
        kbd_emit_haptic(k, KBD_HAPTIC_RELEASE);
        kbd_emit_sound(k, KBD_SND_PAD_RELEASE, t->x);
    } else if (t->stage == 1 && t->ps >= force) {
        t->stage = 2;
        kbd_emit_haptic(k, KBD_HAPTIC_FORCE);
        if (k->set.force_click) {
            KbdEvent e = { .kind = KBD_EV_FORCE, .mods = mods };
            kbd_emit(k, &e);
        }
    } else if (t->stage == 2 && t->ps < force * 0.8f) {
        t->stage = 1;                    /* ready for another force click */
    }
}

void kbd_pad_up(Kbd *k, KbdTouch *t) {
    KbdPad *p = &k->pad;
    int mods = kbd_mods(k);
    if (t->stage > 0) {                  /* lifted while pressed: release */
        t->stage = 0;
        button(k, 0, mods);
        kbd_emit_haptic(k, KBD_HAPTIC_RELEASE);
        kbd_emit_sound(k, KBD_SND_PAD_RELEASE, t->x);
    }
    if (t->role == ROLE_PAD_BUTTON) {
        if (k->cursor_mode) {
            KbdEvent e = { .kind = KBD_EV_BUTTON, .button = 1, .down = 0, .clicks = 1, .mods = mods };
            kbd_emit(k, &e);
        } else {
            p->button_down = 0;
            button(k, 0, mods);
        }
        kbd_emit_haptic(k, KBD_HAPTIC_RELEASE);
        kbd_emit_sound(k, KBD_SND_PAD_RELEASE, t->x);
        return;
    }
    if (t->role == ROLE_CURSOR) {
        k->cursor_mode = 0;
        return;
    }
    t->active = 0;
    int left = pad_fingers(k);
    uint32_t held = k->now - p->first_down;
    if (p->three_finger) {
        if (left == 0) {
            if (p->tfd) button(k, 0, mods);
            p->tfd = p->three_finger = 0;
        }
        p->fingers = left;
        return;
    }
    if (p->two_finger) {
        if (left == 0) {
            if (held < TWO_TAP_MS && p->travel < 10 * k->dp && k->set.tap_to_click) {
                button(k, 1, mods | KBD_MOD_CTRL);   /* contextual menu */
                button(k, 0, mods | KBD_MOD_CTRL);
                kbd_emit_haptic(k, KBD_HAPTIC_TAP);
                p->vx = p->vy = 0;
            } else if (k->now - p->last_scroll > FLICK_PAUSE_MS) {
                p->vx = p->vy = 0;       /* fingers paused before lifting: no glide */
            }
            p->two_finger = 0;
        }
        p->fingers = left;
        return;
    }
    if (p->dragging) {
        p->dragging = 0;
        button(k, 0, mods);
        p->last_tap_up = 0;
    } else if (!t->forced && held < TAP_MS && p->travel < 8 * k->dp && k->set.tap_to_click &&
               !p->button_down) {
        button(k, 1, mods);
        button(k, 0, mods);
        kbd_emit_haptic(k, KBD_HAPTIC_TAP);
        p->last_tap_up = k->now ? k->now : 1;
    }
    p->fingers = left;
}

void kbd_pad_tick(Kbd *k, float dt) {
    KbdPad *p = &k->pad;
    if (p->fingers || dt <= 0) return;
    if (fabsf(p->vx) < 0.5f && fabsf(p->vy) < 0.5f) { p->vx = p->vy = 0; return; }
    KbdEvent e = { .kind = KBD_EV_SCROLL, .dx = p->vx * dt, .dy = p->vy * dt };
    kbd_emit(k, &e);
    float decay = expf(-dt / 0.45f);     /* about the glide of a Mac trackpad flick */
    p->vx *= decay;
    p->vy *= decay;
}

void kbd_cursor_begin(Kbd *k, KbdTouch *t) {
    t->role = ROLE_CURSOR;
    t->committed = 1;
    k->cursor_mode = 1;
    k->preview = -1;
    k->pad.first_down = k->now;
    motion_start(k, t);
    kbd_emit_haptic(k, KBD_HAPTIC_MODE);
}
