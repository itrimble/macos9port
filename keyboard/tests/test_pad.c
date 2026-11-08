/* Trackpad feel and haptics: headless, no SDL. */
#include "kbd.h"
#include "../src/kbd_int.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t T;
static void tick(Kbd *k, int ms) { for (int i = 0; i < ms; i += 4) { T += 4; kbd_tick(k, T); } }

typedef struct { float px, py, ppath, sx, sy; int down, up, ctrl, force, hap[KBD_HAPTIC_COUNT], haptics; } Out;

static Out drain(Kbd *k) {
    Out o;
    memset(&o, 0, sizeof o);
    KbdEvent e;
    while (kbd_poll_event(k, &e)) {
        if (e.kind == KBD_EV_POINTER) { o.px += e.dx; o.py += e.dy; o.ppath += fabsf(e.dx) + fabsf(e.dy); }
        if (e.kind == KBD_EV_SCROLL) { o.sx += e.dx; o.sy += e.dy; }
        if (e.kind == KBD_EV_BUTTON) { if (e.down) o.down++; else o.up++; if (e.mods & KBD_MOD_CTRL) o.ctrl++; }
        if (e.kind == KBD_EV_FORCE) o.force++;
        if (e.kind == KBD_EV_HAPTIC) { o.haptics++; if (e.haptic >= 0 && e.haptic < KBD_HAPTIC_COUNT) o.hap[e.haptic]++; }
    }
    return o;
}

static Kbd *flex(void) {
    Kbd *k = kbd_create(1);
    kbd_set_surface(k, 900, 760);
    kbd_set_posture(k, KBD_FLEX);
    kbd_show(k, 1);
    T = 1000;
    kbd_tick(k, T);
    tick(k, 1200);
    drain(k);
    return k;
}

static float cx(Kbd *k) { return k->lay.pad.x + k->lay.pad.w / 2; }
static float cy(Kbd *k) { return k->lay.pad.y + k->lay.pad.h / 2; }

/* Swipe dist px along x in ms, sampled at hz. Returns pointer travel. */
static float swipe(Kbd *k, float dist, int ms, int hz, int64_t f) {
    float x = cx(k) - dist / 2, y = cy(k);
    kbd_touch_down(k, f, x, y, T);
    int n = ms * hz / 1000;
    for (int i = 1; i <= n; i++) {
        T += (uint32_t)(1000 / hz);
        kbd_touch_move(k, f, x + dist * i / n, y, T);
    }
    T += 300;                             /* hold still, then lift: no tap */
    kbd_touch_up(k, f, x + dist, y, T);
    tick(k, 600);
    return drain(k).px;
}

static void test_pointer(void) {
    Kbd *k = flex();
    /* A resting finger with a pixel of noise barely moves the pointer. */
    float x = cx(k), y = cy(k), raw = 0;
    kbd_touch_down(k, 1, x, y, T);
    uint32_t seed = 7;
    float lx = x;
    for (int i = 0; i < 120; i++) {
        seed = seed * 1103515245u + 12345u;
        float nx = x + ((float)(seed >> 16 & 255) / 255.0f - 0.5f) * 2.0f;
        raw += fabsf(nx - lx);
        lx = nx;
        T += 8;
        kbd_touch_move(k, 1, nx, y, T);
    }
    Out o = drain(k);
    CHECK(o.ppath < raw * 0.35f);         /* filtered: well under the raw noise */
    kbd_touch_up(k, 1, x, y, T + 300);
    tick(k, 600);
    drain(k);
    /* Same gesture at 60 Hz and 240 Hz moves the pointer the same distance. */
    float a = swipe(k, 160, 150, 60, 2), b = swipe(k, 160, 150, 240, 3);
    CHECK(a > 160 && b > 160);
    CHECK(fabsf(a - b) < 0.12f * fmaxf(a, b));
    /* Slow is precise, fast throws: same finger travel, very different pointer travel. */
    float slow = swipe(k, 100, 1500, 120, 4), fast = swipe(k, 100, 90, 120, 5);
    CHECK(fast > slow * 1.8f);
    CHECK(slow > 60 && slow < 260);
    kbd_settings(k)->tracking = 2.0f;
    float quick = swipe(k, 100, 90, 120, 6);
    CHECK(quick > fast * 1.2f);           /* tracking speed setting */
    kbd_destroy(k);
}

static void press(Kbd *k, int64_t f, float p) { T += 8; kbd_touch_force(k, f, p, T); kbd_touch_force(k, f, p, T); kbd_touch_force(k, f, p, T); }

static void test_clickpad(void) {
    Kbd *k = flex();
    float x = cx(k), y = cy(k);
    kbd_touch_down(k, 1, x, y, T);
    press(k, 1, 0.12f);
    Out o = drain(k);
    CHECK(o.down == 0 && k->pad.pressure_seen);
    press(k, 1, 0.5f);
    o = drain(k);
    CHECK(o.down == 1 && o.hap[KBD_HAPTIC_CLICK] == 1);
    press(k, 1, 0.3f);                    /* below click, above release: hysteresis holds */
    o = drain(k);
    CHECK(o.up == 0);
    press(k, 1, 0.95f);
    o = drain(k);
    CHECK(o.force == 1 && o.hap[KBD_HAPTIC_FORCE] == 1 && o.down == 0);
    press(k, 1, 0.97f);
    CHECK(drain(k).force == 0);           /* one force click per deep press */
    press(k, 1, 0.1f);
    o = drain(k);
    CHECK(o.up == 1 && o.hap[KBD_HAPTIC_RELEASE] == 1);
    kbd_touch_up(k, 1, x, y, T + 20);
    tick(k, 100);
    o = drain(k);
    CHECK(o.down == 0);                   /* no extra tap-click after a pressure click */
    /* Firmness setting. */
    kbd_settings(k)->click_pressure = 2;
    kbd_touch_down(k, 2, x, y, T);
    press(k, 2, 0.45f);
    CHECK(drain(k).down == 0);
    press(k, 2, 0.6f);
    CHECK(drain(k).down == 1);
    kbd_touch_up(k, 2, x, y, T + 400);
    o = drain(k);
    CHECK(o.up == 1 && o.hap[KBD_HAPTIC_RELEASE] == 1);   /* lifting while pressed releases */
    kbd_destroy(k);
    /* A panel that reports a constant 1.0 has no pressure: tap-to-click only. */
    k = flex();
    kbd_touch_down(k, 3, x, y, T);
    press(k, 3, 1.0f);
    CHECK(drain(k).down == 0 && !k->pad.pressure_seen);
    kbd_touch_up(k, 3, x, y, T + 60);
    o = drain(k);
    CHECK(o.down == 1 && o.up == 1 && o.hap[KBD_HAPTIC_TAP] == 1);
    kbd_destroy(k);
}

static void two(Kbd *k, float dx, float dy, int steps, int ms_per, int pause) {
    float x = cx(k) - 20, y = cy(k) - 30;
    kbd_touch_down(k, 10, x, y, T);
    kbd_touch_down(k, 11, x + 40, y, T);
    for (int i = 1; i <= steps; i++) {
        T += (uint32_t)ms_per;
        kbd_touch_move(k, 10, x + dx * i, y + dy * i, T);
        kbd_touch_move(k, 11, x + 40 + dx * i, y + dy * i, T);
    }
    T += (uint32_t)pause;
    kbd_touch_up(k, 10, x + dx * steps, y + dy * steps, T);
    kbd_touch_up(k, 11, x + 40 + dx * steps, y + dy * steps, T);
}

static void test_scroll(void) {
    Kbd *k = flex();
    two(k, 2, 8, 8, 12, 0);               /* mostly vertical: locks to vertical */
    Out o = drain(k);
    CHECK(fabsf(o.sx) < 0.05f * o.sy && o.sy > 3);   /* only the pre-lock wobble leaks */
    CHECK(k->pad.axis == 2);
    tick(k, 200);
    o = drain(k);
    CHECK(o.sy > 0.5f);                   /* flick glides */
    kbd_touch_down(k, 12, cx(k), cy(k), T);  /* a resting finger stops it */
    tick(k, 200);
    o = drain(k);
    CHECK(o.sy == 0 && k->pad.vy == 0);
    kbd_touch_up(k, 12, cx(k), cy(k), T + 400);
    tick(k, 500);
    drain(k);
    two(k, 0, 8, 8, 12, 150);             /* paused before lifting: no glide */
    drain(k);
    tick(k, 300);
    CHECK(drain(k).sy == 0);
    kbd_destroy(k);
}

static void test_three_finger_drag(void) {
    Kbd *k = flex();
    float x = cx(k), y = cy(k);
    for (int on = 0; on < 2; on++) {
        kbd_settings(k)->three_finger_drag = on;
        for (int f = 0; f < 3; f++) kbd_touch_down(k, 20 + f, x + f * 20, y, T);
        for (int i = 1; i <= 10; i++) {
            T += 8;
            for (int f = 0; f < 3; f++) kbd_touch_move(k, 20 + f, x + f * 20 + i * 6, y, T);
        }
        for (int f = 0; f < 3; f++) kbd_touch_up(k, 20 + f, x + f * 20 + 60, y, T + 10);
        tick(k, 400);
        Out o = drain(k);
        if (on) CHECK(o.down == 1 && o.up == 1 && o.px > 40);
        else CHECK(o.down == 0 && o.px == 0);
    }
    kbd_destroy(k);
}

static void test_keyboard_haptics(void) {
    Kbd *k = kbd_create(1);
    kbd_set_surface(k, 390, 860);
    kbd_show(k, 1);
    T = 1000;
    kbd_tick(k, T);
    tick(k, 1000);
    drain(k);
    int e = -1, opt = -1;
    for (int i = 0; i < k->lay.n; i++) {
        if (k->lay.keys[i].id == KK_E) e = i;
        if (k->lay.keys[i].id == KK_OPT) opt = i;
    }
    KRect r = k->lay.keys[e].r, ro = k->lay.keys[opt].r;
    kbd_touch_down(k, 1, r.x + r.w / 2, r.y + r.h / 2, T);
    kbd_touch_up(k, 1, r.x + r.w / 2, r.y + r.h / 2, T + 40);
    Out o = drain(k);
    CHECK(o.hap[KBD_HAPTIC_KEY] == 1 && o.haptics == 1);
    kbd_touch_down(k, 2, ro.x + ro.w / 2, ro.y + ro.h / 2, T + 100);
    kbd_touch_up(k, 2, ro.x + ro.w / 2, ro.y + ro.h / 2, T + 140);
    CHECK(drain(k).hap[KBD_HAPTIC_MODIFIER] == 1);           /* latch */
    kbd_touch_down(k, 2, ro.x + ro.w / 2, ro.y + ro.h / 2, T + 600);
    kbd_touch_up(k, 2, ro.x + ro.w / 2, ro.y + ro.h / 2, T + 640);
    drain(k);
    T += 700;
    kbd_touch_down(k, 3, r.x + r.w / 2, r.y + r.h / 2, T);
    tick(k, 520);
    CHECK(k->alt_open);
    float cw = k->alt_rect.w / k->alt_n;
    for (int i = 0; i < k->alt_n; i++) { kbd_touch_move(k, 3, k->alt_rect.x + cw * (i + 0.5f), r.y, T); tick(k, 20); }
    kbd_touch_up(k, 3, k->alt_rect.x + cw * 0.5f, r.y, T);
    o = drain(k);
    CHECK(o.hap[KBD_HAPTIC_SELECTION] == k->alt_n);   /* open + one detent per new choice */
    kbd_settings(k)->haptic_strength = 0.5f;
    kbd_touch_down(k, 4, r.x + r.w / 2, r.y + r.h / 2, T);
    KbdEvent ev;
    int seen = 0;
    while (kbd_poll_event(k, &ev))
        if (ev.kind == KBD_EV_HAPTIC) { seen = 1; CHECK(fabsf(ev.strength - 0.225f) < 0.001f && ev.duration_ms > 0); }
    CHECK(seen);
    kbd_touch_up(k, 4, r.x + r.w / 2, r.y + r.h / 2, T + 30);
    drain(k);
    kbd_settings(k)->haptics = 0;
    kbd_touch_down(k, 5, r.x + r.w / 2, r.y + r.h / 2, T + 100);
    kbd_touch_up(k, 5, r.x + r.w / 2, r.y + r.h / 2, T + 140);
    CHECK(drain(k).haptics == 0);
    kbd_hw_key(k, 226, 1, T + 200);       /* Option alone on a Bluetooth keyboard */
    kbd_hw_key(k, 226, 0, T + 260);
    CHECK(!kbd_hw_active(k));             /* is not typing: keyboard stays */
    kbd_destroy(k);
    for (int i = 0; i < KBD_HAPTIC_COUNT; i++) {
        KbdHapticSpec s;
        kbd_haptic_spec(i, &s);
        CHECK(s.intensity > 0 && s.intensity <= 1 && s.sharpness > 0 && s.sharpness <= 1);
        CHECK(s.duration_ms >= 4 && s.duration_ms <= 30);
    }
    KbdHapticSpec c, rel;
    kbd_haptic_spec(KBD_HAPTIC_CLICK, &c);
    kbd_haptic_spec(KBD_HAPTIC_RELEASE, &rel);
    CHECK(rel.intensity < c.intensity && rel.sharpness > c.sharpness);   /* the Mac press/release pair */
}

static void test_rotate(void) {
    Kbd *k = kbd_create(1);
    kbd_set_surface(k, 980, 740);              /* unfolded flat, landscape */
    kbd_set_posture(k, KBD_OPEN);
    kbd_show(k, 1);
    T = 1000;
    kbd_tick(k, T);
    tick(k, 1200);
    int q = -1;
    for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == KK_Q) q = i;
    KRect before = k->lay.keys[q].r;
    kbd_rotate(k, 740, 980, 1, KBD_FLEX);       /* turned on its side for Flex */
    int nq = -1;
    for (int i = 0; i < k->lay.n; i++) if (k->lay.keys[i].id == KK_Q) nq = i;
    KRect start = kbd_anim_rect(k, nq);
    CHECK(fabsf(start.x - (740 - before.y - before.h)) < 0.5f && fabsf(start.y - before.x) < 0.5f);
    CHECK(kbd_animating(k) && k->posture == KBD_FLEX);
    tick(k, 3000);
    KRect end = kbd_anim_rect(k, nq), want = k->lay.keys[nq].r;
    CHECK(!kbd_animating(k) && fabsf(end.x - want.x) < 0.01f && fabsf(end.y - want.y) < 0.01f);
    CHECK(want.y > 490);                       /* below the fold */
    kbd_destroy(k);
}

int main(void) {
    test_rotate();
    test_pointer();
    test_clickpad();
    test_scroll();
    test_three_finger_drag();
    test_keyboard_haptics();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
