/* End-to-end shell checks: a real desktop session and keyboard, every device
 * and posture, fingers through to desktop calls. Headless. */
#include "kbd_int.h"
#include "mobile_shell.h"
#include "os999_mobile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int haptics;
static void on_haptic(void *ctx, int type, float s, float sharp, int ms) {
    (void)ctx; (void)type; (void)sharp; (void)ms;
    if (s > 0) haptics++;
}

static void tap(MobileShell *m, float x, float y, uint32_t *ms) {
    mobile_shell_finger(m, MSH_DOWN, 77, x, y, *ms);
    mobile_shell_finger(m, MSH_UP, 77, x, y, *ms + 60);
    *ms += 500;
    mobile_shell_frame(m, *ms);
}

static void test_continuity(Session *s) {
    MobileContinuity c = {0};
    s->sw = 1024; s->sh = 768;
    s->nwin = 1;
    Window *w = &s->win[0];
    memset(w, 0, sizeof *w);
    snprintf(w->rel, sizeof w->rel, "Documents");
    w->x = 600; w->y = 300; w->w = 400; w->h = 300;
    mobile_continuity_resize(&c, s, 540, 1100);
    CHECK(w->w == 400 && w->x + w->w <= 540 && w->y + w->h <= 1100);
    s->sw = 540; s->sh = 1100;
    mobile_continuity_resize(&c, s, 400, 320);   /* tiny: shrinks */
    CHECK(w->w <= 384 && w->h <= 320 - 20);
    s->sw = 400; s->sh = 320;
    mobile_continuity_resize(&c, s, 1038, 1076); /* unfold: grows back */
    CHECK(w->w == 400 && w->h == 300);
    s->sw = 1038; s->sh = 1076;
    w->w = 500;                                  /* user resizes by hand */
    mobile_continuity_resize(&c, s, 1038, 1076);
    mobile_continuity_resize(&c, s, 540, 1100);
    CHECK(w->w == 500);
    s->nwin = 0;
}

/* Naming things on a phone: ⌘N raises the keyboard, typed text (with its
 * capitals and accents) lands in the name, Return files it, the keyboard
 * goes away again; a Force Click on a name starts the same edit. */
static void test_edit(MobileShell *m, Session *s, const char *root, uint32_t *ms) {
    MobileScreen sc;
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_COVER, &sc);
    mobile_shell_set_screen(m, &sc, *ms);
    while (s->nwin) session_key(s, 'w', MOD_CMD);
    kbd_show(m->kbd, 0);
    for (int i = 0; i < 40; i++) mobile_shell_frame(m, *ms += 16);
    CHECK(!kbd_visible(m->kbd));
    s->desk_sel = 0; session_command(s, CMD_OPEN); /* name a folder inside the disk window */
    mobile_shell_key(m, 'n', MOD_CMD);
    mobile_shell_frame(m, *ms += 16);
    CHECK(session_editing(s) && kbd_visible(m->kbd) && m->kbd_for_edit);
    Rect r;
    CHECK(session_edit_rect(s, &r) && m->focus_y == r.y + r.h + 4);
    const char *typed[] = {"R", "\xC3\xA9", "sum", "\xC3\xA9", "s & ", "Lette", "rs"};   /* key by key */
    for (int i = 0; i < 7; i++) kbd_emit_text(m->kbd, typed[i]);
    KbdEvent ret = {.kind = KBD_EV_KEY, .key = KBD_K_RETURN, .down = 1};
    kbd_emit(m->kbd, &ret);
    mobile_shell_frame(m, *ms += 16);
    char path[OSPI_PATH * 2];
    snprintf(path, sizeof path, "%s/R\xC3\xA9sum\xC3\xA9s & Letters", root);
    struct stat st;
    CHECK(!session_editing(s) && stat(path, &st) == 0);
    for (int i = 0; i < 40; i++) mobile_shell_frame(m, *ms += 16);
    CHECK(!kbd_visible(m->kbd) && !m->kbd_for_edit);
    /* Force Click on the name, at the pointer, as the trackpad sends it. */
    Window *w = &s->win[s->nwin - 1];
    int idx = -1, x = 0, y = 0;
    for (int i = 0; i < w->nitems; i++) if (strncmp(w->items[i].name, "R\xC3\xA9", 3) == 0) idx = i;
    CHECK(idx >= 0 && ospi_item_center(s, s->nwin - 1, idx, &x, &y) == 0);
    m->touch.px = (float)x;
    m->touch.py = (float)(y + 19);
    KbdEvent down = {.kind = KBD_EV_BUTTON, .down = 1, .clicks = 1}, force = {.kind = KBD_EV_FORCE};
    kbd_emit(m->kbd, &down);
    kbd_emit(m->kbd, &force);
    mobile_shell_frame(m, *ms += 16);
    CHECK(session_editing(s) && strncmp(s->edit_old, "R\xC3\xA9", 3) == 0 && !s->down);
    KbdEvent esc = {.kind = KBD_EV_KEY, .key = KBD_K_ESCAPE, .down = 1};
    kbd_emit(m->kbd, &esc);
    mobile_shell_frame(m, *ms += 16);
    CHECK(!session_editing(s));
    mobile_shell_finger_force(m, 4242, 0.7f, *ms);   /* unknown finger: ignored */
    /* Book <-> Laptop turns the screen; the visible keyboard rides along. */
    MobileScreen book, lap;
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_BOOK, &book);
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_LAPTOP, &lap);
    mobile_shell_set_screen(m, &book, *ms);
    kbd_show(m->kbd, 1);
    for (int i = 0; i < 40; i++) mobile_shell_frame(m, *ms += 16);
    mobile_shell_set_screen(m, &lap, *ms);
    CHECK(kbd_visible(m->kbd) && kbd_animating(m->kbd));
    for (int i = 0; i < 90; i++) mobile_shell_frame(m, *ms += 16);
    CHECK(kbd_visible(m->kbd) && !kbd_animating(m->kbd));
    mobile_shell_set_screen(m, &book, *ms);
    CHECK(mobile_shell_frame(m, *ms += 16) == 1);
}

/* Trackpad pinch: zoom around the pointer, clamped; the platform queue
 * multiplies factors and adds scroll lines until the host takes them. */
static void test_magnify(MobileShell *m, float sx, float sy) {
    mobile_shell_mouse(m, MSH_MOVE, sx, sy, 0, 0, m->now);
    float px = m->touch.px, py = m->touch.py, ax, ay;
    mobile_shell_magnify(m, 2.0f);
    CHECK(m->view.zoom > 1.99f && m->view.zoom < 2.01f);
    mobile_view_to_screen(&m->view, &m->layout, px, py, &ax, &ay);
    CHECK(ax > sx - 2 && ax < sx + 2 && ay > sy - 2 && ay < sy + 2);   /* the point under the pointer stays */
    mobile_shell_magnify(m, 10.0f);
    CHECK(m->view.zoom <= MOBILE_MAX_ZOOM);
    mobile_shell_magnify(m, 0.01f);
    CHECK(m->view.zoom == 1.0f);
    mobile_shell_magnify(m, -1.0f);   /* ignored */
    CHECK(m->view.zoom == 1.0f);
    float f, lines;
    os999_mobile_report_magnify(1.5f);
    os999_mobile_report_magnify(2.0f);
    os999_mobile_report_magnify(0.0f);   /* nonsense: dropped */
    os999_mobile_report_scroll(1.0f);
    os999_mobile_report_scroll(-0.25f);
    CHECK(os999_mobile_take_gestures(&f, &lines) == 1 && f > 2.99f && f < 3.01f && lines == 0.75f);
    CHECK(os999_mobile_take_gestures(&f, &lines) == 0 && f == 1.0f && lines == 0);
    m->wheel_acc = 0;
    mobile_shell_wheel(m, 0.5f);   /* fine trackpad scrolls build up to whole lines */
    CHECK(m->wheel_acc == 0.5f);
    mobile_shell_wheel(m, 0.75f);
    CHECK(m->wheel_acc == 0.25f);
}

int test_mobile_shell(int *checks_out, int *fails_out) {
    char root[] = "/tmp/os999-mobile-test-XXXXXX";
    if (!mkdtemp(root)) { *fails_out = 1; return 1; }
    CHECK(volume_seed(root) == 0);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) { *checks_out = checks; *fails_out = fails; return 1; }
    test_continuity(s);
    for (int i = 0; i < s->nwin; i++) s->win[i].zoomed = 0;
    MobileShellHost host = {.haptic = on_haptic};
    MobileShell m;
    CHECK(mobile_shell_init(&m, s, host, root) == 0);
    CHECK(m.coach.visible == 1);
    uint32_t ms = 1000;
    for (int i = 0; i < mobile_device_count(); i++) {
        for (int p = MP_COVER; p < MP_POSTURE_COUNT; p++) {
            MobileScreen sc;
            mobile_device_screen(mobile_device_at(i), (MobilePosture)p, &sc);
            mobile_shell_set_screen(&m, &sc, ms);
            CHECK(m.canvas.w == sc.w && m.canvas.h == sc.h);
            CHECK(mobile_shell_frame(&m, ms += 16) == 1);
            CHECK(s->sw == m.layout.desk_w && s->sh == m.layout.desk_h);
            for (int k = 0; k < s->nwin; k++) {
                Window *w = &s->win[k];
                CHECK(w->x >= 0 && w->x + w->w <= s->sw);
            }
        }
    }
    /* Back to a Pixel cover screen and walk through a session. */
    MobileScreen sc;
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_COVER, &sc);
    mobile_shell_set_screen(&m, &sc, ms);
    mobile_shell_frame(&m, ms += 16);
    /* the guide eats touches until dismissed */
    Rect b = m.coach.button;
    tap(&m, b.x + b.w / 2.0f, b.y + b.h / 2.0f, &ms);
    CHECK(m.coach.visible == 0);
    struct stat st;
    CHECK(stat(m.guide_path, &st) == 0);
    /* strip: tab expands, keyboard tile shows the keyboard */
    tap(&m, m.strip.tab.x + 5.0f, m.strip.tab.y + 10.0f, &ms);
    CHECK(m.strip.expanded == 1);
    mobile_shell_frame(&m, ms += 16);
    Rect kt = m.strip.tiles[MS_KEYBOARD];
    tap(&m, kt.x + kt.w / 2.0f, kt.y + kt.h / 2.0f, &ms);
    CHECK(kbd_visible(m.kbd));
    for (int i = 0; i < 60; i++) mobile_shell_frame(&m, ms += 16);
    CHECK(kbd_occluded_height(m.kbd) > 0);
    /* posture tile cycles Auto -> Cover (locked) */
    mobile_shell_frame(&m, ms += 16);
    Rect pt = m.strip.tiles[MS_POSTURE];
    tap(&m, pt.x + pt.w / 2.0f, pt.y + pt.h / 2.0f, &ms);
    CHECK(m.locked == MP_COVER);
    /* display tile changes the desktop size and announces it */
    mobile_shell_frame(&m, ms += 16);
    int before = m.layout.desk_w;
    Rect dt = m.strip.tiles[MS_DISPLAY];
    tap(&m, dt.x + dt.w / 2.0f, dt.y + dt.h / 2.0f, &ms);
    CHECK(m.layout.nscales == 1 || m.layout.desk_w != before);
    /* a tap on the Trash reaches the desktop */
    mobile_shell_lock_posture(&m, MP_AUTO, ms);
    kbd_show(m.kbd, 0);
    m.strip.expanded = 0;
    for (int i = 0; i < 40; i++) mobile_shell_frame(&m, ms += 16);
    Rect icon, hit;
    desktop_icon_rect(s->sw, s->sh, 1, &icon, &hit);
    float sx, sy;
    mobile_view_to_screen(&m.view, &m.layout, hit.x + hit.w / 2.0f, hit.y + hit.h / 2.0f, &sx, &sy);
    tap(&m, sx, sy, &ms);
    CHECK(s->desk_sel == 1);
    /* Tablet with a keyboard case: the hardware strip instead of keys, and a
     * trackpad that drives the pointer exactly. */
    mobile_device_screen(mobile_device_find("ipad-pro-13"), MP_AUTO, &sc);
    sc.hw_keyboard = sc.pointer = 1;
    mobile_shell_set_screen(&m, &sc, ms);
    CHECK(m.layout.tablet && m.pointer_visible);
    kbd_hw_key(m.kbd, 4, 1, ms);   /* the user types: 'a' */
    kbd_hw_key(m.kbd, 4, 0, ms + 30);
    for (int i = 0; i < 40; i++) mobile_shell_frame(&m, ms += 16);
    CHECK(kbd_hw_active(m.kbd) && !kbd_visible(m.kbd));
    /* the strip keeps its own band: the desktop (and its Trash) ends above it */
    CHECK(m.hw_reserve > 0 && kbd_occluded_height(m.kbd) > 0);
    CHECK(m.layout.desk.y + m.layout.desk.h <= sc.h - kbd_occluded_height(m.kbd));
    CHECK(s->sw == m.layout.desk_w && s->sh == m.layout.desk_h);
    desktop_icon_rect(s->sw, s->sh, 1, &icon, &hit);
    mobile_view_to_screen(&m.view, &m.layout, hit.x + hit.w / 2.0f, hit.y + hit.h / 2.0f, &sx, &sy);
    CHECK(sy < sc.h - kbd_occluded_height(m.kbd));
    s->desk_sel = 0;
    mobile_shell_mouse(&m, MSH_MOVE, sx, sy, 0, 0, ms);
    CHECK((int)m.touch.px == hit.x + hit.w / 2);
    mobile_shell_mouse(&m, MSH_DOWN, sx, sy, MSH_BUTTON_PRIMARY, 1, ms += 16);
    mobile_shell_mouse(&m, MSH_UP, sx, sy, MSH_BUTTON_PRIMARY, 1, ms += 60);
    CHECK(s->desk_sel == 1);
    mobile_shell_frame(&m, ms += 16);
    Rect tt = m.strip.tab;   /* the strip works with the pointer too */
    mobile_shell_mouse(&m, MSH_DOWN, tt.x + 4.0f, tt.y + 10.0f, MSH_BUTTON_PRIMARY, 1, ms);
    mobile_shell_mouse(&m, MSH_UP, tt.x + 4.0f, tt.y + 10.0f, MSH_BUTTON_PRIMARY, 1, ms += 50);
    CHECK(m.strip.expanded == 1);
    mobile_shell_mouse(&m, MSH_DOWN, sx, sy - 200, MSH_BUTTON_SECONDARY, 1, ms += 400);
    mobile_shell_mouse(&m, MSH_UP, sx, sy - 200, MSH_BUTTON_SECONDARY, 1, ms += 50);
    CHECK(session_overlay_active(s));   /* contextual menu is up */
    mobile_shell_key(&m, 27, 0);
    m.strip.expanded = 0;
    test_magnify(&m, sx, sy);
    /* A platform that never reports the keyboard (the Android emulator, some
     * Bluetooth stacks): the first key brings the slim strip, not the full
     * keys, and it survives a rotation. */
    mobile_device_screen(mobile_device_find("pixel-tablet"), MP_AUTO, &sc);
    sc.pointer = 1;
    mobile_shell_set_screen(&m, &sc, ms);
    kbd_show(m.kbd, 0);
    for (int i = 0; i < 40; i++) mobile_shell_frame(&m, ms += 16);
    m.hw_seen = 0;
    kbd_hw_key(m.kbd, 21, 1, ms);   /* 'r' */
    kbd_hw_key(m.kbd, 21, 0, ms + 30);
    for (int i = 0; i < 40; i++) mobile_shell_frame(&m, ms += 16);
    CHECK(kbd_hw_active(m.kbd) && !kbd_visible(m.kbd) && m.hw_reserve > 0);
    MobileScreen rot = sc;
    rot.w = sc.h; rot.h = sc.w;
    mobile_shell_set_screen(&m, &rot, ms);
    for (int i = 0; i < 40; i++) mobile_shell_frame(&m, ms += 16);
    CHECK(kbd_hw_active(m.kbd) && !kbd_visible(m.kbd) && m.hw_reserve > 0);
    /* laptop: the deck is the keyboard */
    mobile_device_screen(mobile_device_find("pixel-10-pro-fold"), MP_LAPTOP, &sc);
    haptics = 0;
    mobile_shell_set_screen(&m, &sc, ms);
    CHECK(kbd_visible(m.kbd) && m.layout.kbd_always);
    CHECK(m.toast.active && haptics == 1);
    CHECK(mobile_shell_frame(&m, ms += 16) == 1);
    test_edit(&m, s, root, &ms);
    mobile_shell_free(&m);
    session_destroy(s);
    *checks_out = checks;
    *fails_out = fails;
    return fails;
}
