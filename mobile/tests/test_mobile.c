/* Headless checks for the OS999 phone shell (no SDL). */
#include "mobile.h"
#include "mobile_touch.h"
#include "mobile_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int test_mobile_shell(int *checks_out, int *fails_out);   /* test_mobile_shell.c */

static int rect_inside(Rect in, Rect out) {
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

static void test_devices(void) {
    CHECK(mobile_device_count() >= 5);
    CHECK(mobile_device_find("pixel-10-pro-fold") != NULL);
    CHECK(mobile_device_find("nope") == NULL);
    for (int i = 0; i < mobile_device_count(); i++) {
        const MobileDeviceProfile *d = mobile_device_at(i);
        MobileScreen s;
        if (d->tablet) {   /* one screen whatever the posture asked for */
            mobile_device_screen(d, MP_LAPTOP, &s);
            CHECK(s.tablet && s.w > s.h && s.fold_axis == MF_AXIS_NONE);
            CHECK(mobile_posture_detect(&s) == MP_FLAT);
            continue;
        }
        mobile_device_screen(d, MP_COVER, &s);
        CHECK(mobile_posture_detect(&s) == MP_COVER);
        mobile_device_screen(d, MP_FLAT, &s);
        CHECK(mobile_posture_detect(&s) == MP_FLAT);
        mobile_device_screen(d, MP_LAPTOP, &s);
        CHECK(mobile_posture_detect(&s) == MP_LAPTOP);
        CHECK(s.fold_axis == MF_AXIS_HORIZONTAL);
        mobile_device_screen(d, MP_BOOK, &s);
        CHECK(mobile_posture_detect(&s) == (d->flip ? MP_FLAT : MP_BOOK));
    }
    /* No fold API (iOS today): fall back to the shape of the screen. */
    MobileScreen u = {.w = 1422, .h = 2088, .inner = -1};
    CHECK(mobile_posture_detect(&u) == MP_COVER);
    u.w = 2713; u.h = 1920;
    CHECK(mobile_posture_detect(&u) == MP_FLAT);
}

static void test_layouts(void) {
    for (int i = 0; i < mobile_device_count(); i++) {
        const MobileDeviceProfile *d = mobile_device_at(i);
        for (int p = MP_COVER; p < MP_POSTURE_COUNT; p++) {
            if (d->tablet && p != MP_FLAT) continue;
            MobileScreen s;
            MobileLayout l;
            mobile_device_screen(d, (MobilePosture)p, &s);
            mobile_layout(&s, MP_AUTO, 0, &l);
            Rect screen = {0, 0, s.w, s.h};
            CHECK(l.scale >= 1);
            CHECK(l.desk_w * l.scale == l.desk.w && l.desk_h * l.scale == l.desk.h);
            CHECK(rect_inside(l.desk, l.region));
            CHECK(rect_inside(l.region, screen));
            CHECK(l.desk_w >= MOBILE_MIN_W && l.desk_h >= MOBILE_MIN_H);
            /* a desktop pixel is between 0.1 mm and 0.35 mm on glass */
            float mm = l.scale * 25.4f / s.ppi;
            CHECK(mm >= 0.1f && mm <= 0.35f);
            int found = 0;
            for (int k = 0; k < l.nscales; k++) found |= l.scales[k] == l.scale;
            CHECK(found);
            if (l.posture == MP_LAPTOP) {
                CHECK(l.kbd_always && l.kbd_posture == 2);
                CHECK(l.desk.y + l.desk.h <= l.deck.y);
                CHECK(l.crease.h > 0);
            }
            if (l.posture == MP_COVER) CHECK(l.crease.w == 0 && l.kbd_posture == 0);
            if (d->tablet) CHECK(l.tablet && l.crease.w == 0 && l.deck.h == 0);
            for (int k = 0; k < l.nscales; k++) {
                MobileLayout o;
                mobile_layout(&s, MP_AUTO, l.scales[k], &o);
                CHECK(o.scale == l.scales[k]);
            }
        }
    }
    MobileScreen s;
    MobileLayout l;
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_FLAT, &s);
    mobile_layout(&s, MP_AUTO, 0, &l);
    CHECK(l.scale == 3 && l.desk_w == 692);
    mobile_device_screen(mobile_device_find("galaxy-z-fold8"), MP_COVER, &s);
    mobile_layout(&s, MP_AUTO, 0, &l);
    CHECK(l.scale == 2 && l.desk_w == 540);
    /* Tablets land on classic desktop sizes at 2x. */
    MobileScreen t;
    MobileLayout tl;
    mobile_device_screen(mobile_device_find("ipad-pro-13"), MP_AUTO, &t);
    mobile_layout(&t, MP_AUTO, 0, &tl);
    CHECK(tl.scale == 2 && tl.desk_w == 1376);
    mobile_device_screen(mobile_device_find("pixel-tablet"), MP_AUTO, &t);
    mobile_layout(&t, MP_AUTO, 0, &tl);
    CHECK(tl.scale == 2 && tl.desk_w == 1280);
    CHECK(mobile_layout_cycle_scale(&l, +1) != 0);
    /* Laptop forced on a screen with no hinge folds at the middle. */
    mobile_layout(&s, MP_LAPTOP, 0, &l);
    CHECK(l.deck.y == s.h / 2 && l.kbd_always);
}

static void test_view(void) {
    MobileScreen s;
    MobileLayout l;
    MobileView v;
    mobile_device_screen(mobile_device_find("galaxy-z-fold8"), MP_FLAT, &s);
    mobile_layout(&s, MP_AUTO, 0, &l);
    mobile_view_reset(&v);
    int x, y;
    float sx, sy;
    CHECK(mobile_view_to_desk(&v, &l, l.desk.x + 30.5f * l.scale, l.desk.y + 40.5f * l.scale, &x, &y));
    CHECK(x == 30 && y == 40);
    mobile_view_to_screen(&v, &l, 30, 40, &sx, &sy);
    CHECK(fabsf(sx - (l.desk.x + 30 * l.scale)) < 0.01f);
    /* Pinch: the desktop point under the fingers stays under them. */
    float fx = l.desk.x + 400, fy = l.desk.y + 600;
    int bx, by, ax, ay;
    mobile_view_to_desk(&v, &l, fx, fy, &bx, &by);
    mobile_view_zoom_at(&v, &l, 3.0f, fx, fy);
    mobile_view_to_desk(&v, &l, fx, fy, &ax, &ay);
    CHECK(abs(ax - bx) <= 1 && abs(ay - by) <= 1);
    CHECK(v.zoom == 3.0f);
    mobile_view_zoom_at(&v, &l, 99.0f, fx, fy);
    CHECK(v.zoom == MOBILE_MAX_ZOOM);
    Rect src, dst;
    mobile_view_rects(&v, &l, &src, &dst);
    CHECK(src.x >= 0 && src.y >= 0 && src.x + src.w <= l.desk_w && src.y + src.h <= l.desk_h);
    mobile_view_pan(&v, &l, -100000, -100000);
    CHECK(v.ox <= l.desk_w - l.desk_w / v.zoom + 0.01f);
    mobile_view_reset(&v);
    mobile_view_lift(&v, &l, l.desk_h - 10, 900, s.h);
    CHECK(v.lift > 0 && v.lift <= 900);
    mobile_view_lift(&v, &l, 10, 900, s.h);
    CHECK(v.lift == 0);
}

static int drain(MobileTouch *t, MobileAction *out, int max) {
    int n = 0;
    MobileAction a;
    while (mobile_touch_poll(t, &a))
        if (a.kind != MA_REDRAW && a.kind != MA_HAPTIC && n < max) out[n++] = a;
    return n;
}

static int count(const MobileAction *a, int n, MobileActionKind k) {
    int c = 0;
    for (int i = 0; i < n; i++) c += a[i].kind == k;
    return c;
}

static void test_touch(void) {
    MobileScreen s;
    MobileLayout l;
    MobileView v;
    MobileTouch t = {0};
    MobileAction a[64];
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_FLAT, &s);
    mobile_layout(&s, MP_AUTO, 0, &l);
    mobile_view_reset(&v);
    mobile_touch_init(&t, &l, &v);
    float x = l.desk.x + 300, y = l.desk.y + 300;
    uint32_t ms = 1000;
    /* tap */
    mobile_touch_down(&t, 1, x, y, ms);
    mobile_touch_up(&t, 1, x + 2, y, ms + 80);
    int n = drain(&t, a, 64);
    CHECK(count(a, n, MA_DOWN) == 1 && count(a, n, MA_UP) == 1);
    CHECK(a[1].kind == MA_DOWN && a[1].clicks == 1 && a[1].x == 100 && a[1].y == 100);
    /* second tap soon after: double-click */
    mobile_touch_down(&t, 2, x + 4, y, ms + 200);
    mobile_touch_up(&t, 2, x + 4, y, ms + 260);
    n = drain(&t, a, 64);
    CHECK(n >= 2 && a[1].kind == MA_DOWN && a[1].clicks == 2);
    /* drag */
    ms = 5000;
    mobile_touch_down(&t, 3, x, y, ms);
    mobile_touch_move(&t, 3, x + 90, y + 30, ms + 40);
    mobile_touch_move(&t, 3, x + 150, y + 60, ms + 80);
    mobile_touch_up(&t, 3, x + 150, y + 60, ms + 120);
    n = drain(&t, a, 64);
    CHECK(count(a, n, MA_DOWN) == 1 && count(a, n, MA_UP) == 1 && count(a, n, MA_MOVE) >= 3);
    CHECK(a[n - 1].kind == MA_UP && a[n - 1].x == 150 && a[n - 1].y == 120);
    /* hold: loupe, slide to aim, lift clicks where aimed */
    ms = 9000;
    mobile_touch_down(&t, 4, x, y, ms);
    mobile_touch_tick(&t, ms + 400);
    CHECK(t.loupe == 1);
    mobile_touch_move(&t, 4, x + 60, y, ms + 500);
    mobile_touch_tick(&t, ms + 1200);
    CHECK(t.loupe == 1);   /* moved, so no contextual menu */
    mobile_touch_up(&t, 4, x + 60, y, ms + 1300);
    n = drain(&t, a, 64);
    CHECK(count(a, n, MA_CONTEXT) == 0 && count(a, n, MA_DOWN) == 1);
    CHECK(t.loupe == 0);
    /* hold still: contextual menu, no click */
    ms = 20000;
    mobile_touch_down(&t, 5, x, y, ms);
    mobile_touch_tick(&t, ms + 400);
    mobile_touch_tick(&t, ms + 700);
    CHECK(t.ring > 0.3f && t.ring < 1);
    mobile_touch_tick(&t, ms + 1050);
    mobile_touch_up(&t, 5, x, y, ms + 1200);
    n = drain(&t, a, 64);
    CHECK(count(a, n, MA_CONTEXT) == 1 && count(a, n, MA_DOWN) == 0);
    /* two-finger tap: contextual menu */
    ms = 30000;
    mobile_touch_down(&t, 6, x, y, ms);
    mobile_touch_down(&t, 7, x + 120, y, ms + 20);
    mobile_touch_up(&t, 6, x, y, ms + 120);
    mobile_touch_up(&t, 7, x + 120, y, ms + 130);
    n = drain(&t, a, 64);
    CHECK(count(a, n, MA_CONTEXT) == 1 && count(a, n, MA_DOWN) == 0);
    /* two-finger drag down: scroll */
    ms = 40000;
    mobile_touch_down(&t, 8, x, y, ms);
    mobile_touch_down(&t, 9, x + 120, y, ms + 10);
    for (int i = 1; i <= 10; i++) {
        mobile_touch_move(&t, 8, x, y + i * 30, ms + 20 + i * 16);
        mobile_touch_move(&t, 9, x + 120, y + i * 30, ms + 20 + i * 16);
    }
    mobile_touch_up(&t, 8, x, y + 300, ms + 300);
    mobile_touch_up(&t, 9, x + 120, y + 300, ms + 300);
    n = drain(&t, a, 64);
    int lines = 0;
    for (int i = 0; i < n; i++) if (a[i].kind == MA_WHEEL) lines += a[i].lines;
    CHECK(lines >= 5 && count(a, n, MA_DOWN) == 0);
    /* pinch out: zoom in */
    ms = 50000;
    mobile_touch_down(&t, 10, x, y, ms);
    mobile_touch_down(&t, 11, x + 100, y, ms + 10);
    mobile_touch_move(&t, 10, x - 100, y, ms + 50);
    mobile_touch_move(&t, 11, x + 200, y, ms + 50);
    mobile_touch_up(&t, 10, x - 100, y, ms + 90);
    mobile_touch_up(&t, 11, x + 200, y, ms + 90);
    drain(&t, a, 64);
    CHECK(v.zoom > 2.5f);
    mobile_view_reset(&v);
    /* trackpad: slide moves the pointer, tap clicks at the pointer */
    mobile_touch_set_mode(&t, MT_TRACKPAD);
    float px0 = t.px;
    ms = 60000;
    mobile_touch_down(&t, 12, x, y, ms);
    mobile_touch_move(&t, 12, x + 120, y, ms + 50);
    mobile_touch_up(&t, 12, x + 120, y, ms + 400);
    mobile_touch_down(&t, 13, x, y, ms + 1000);
    mobile_touch_up(&t, 13, x, y, ms + 1080);
    n = drain(&t, a, 64);
    CHECK(t.px > px0 + 10);
    CHECK(count(a, n, MA_DOWN) == 1);
    for (int i = 0; i < n; i++)
        if (a[i].kind == MA_DOWN) CHECK(a[i].x == (int)t.px);
}

static void test_strip(void) {
    MobileStrip st = {0};
    Rect area = {0, 0, 1080, 2000};
    mobile_strip_place(&st, area, 2.6f);
    CHECK(mobile_strip_hit(&st, st.tab.x + 4, st.tab.y + 4) == MS_TAB);
    CHECK(mobile_strip_hit(&st, 500, 500) == MS_NONE);
    st.expanded = 1;
    mobile_strip_place(&st, area, 2.6f);
    for (int i = 0; i < MS_TILE_COUNT; i++) {
        Rect r = st.tiles[i];
        CHECK(r.w > 0 && r.x + r.w <= area.w);
        CHECK(mobile_strip_hit(&st, r.x + r.w / 2, r.y + r.h / 2) == i);
    }
    MobileStripState s = {0};
    char buf[32];
    s.desk_w = 540; s.desk_h = 1100;
    CHECK(strcmp(mobile_strip_label(MS_DISPLAY, &s, buf, sizeof buf), "540x1100") == 0);
}

int main(void) {
    test_devices();
    test_layouts();
    test_view();
    test_touch();
    test_strip();
    int c = 0, f = 0;
    test_mobile_shell(&c, &f);
    checks += c;
    fails += f;
    printf("test_mobile: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
