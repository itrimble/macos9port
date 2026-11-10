/* Display modes: the runtime desktop size, every mode painted between guard
 * regions, the narrow menu bar rule, re-layout on a live switch, the Monitors
 * panel path back from the smallest mode, persistence and host-window maths.
 * Headless: nothing here opens a window. */
#include "display_modes.h"
#include "session_int.h"
#include "control_panels.h"
#include "control_panels_int.h"
#include "control_panel_store.h"
#include "desktop_guest.h"
#include "app_switcher.h"
#include "draw.h"
#include "menu_navigation.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

enum { GUARD = 8192 };
static const uint32_t SENTINEL = 0x00C0FFEEu;   /* alpha 0: never a painted desktop pixel */

static void click_at(Session *s, int x, int y) {
    session_down(s, x, y, 1);
    session_up(s, x, y);
}

/* Paint one frame of exactly w x h between two guard regions. */
static void paint_guarded(Session *s, int w, int h, const char *what) {
    size_t pixels = (size_t)w * (size_t)h, total = pixels + 2 * GUARD;
    uint32_t *buffer = malloc(total * sizeof *buffer);
    CHECK(buffer);
    if (!buffer) return;
    for (size_t i = 0; i < total; i++) buffer[i] = SENTINEL;
    session_paint(s, buffer + GUARD, w, h);
    size_t damaged = 0, unpainted = 0;
    for (size_t i = 0; i < GUARD; i++) damaged += buffer[i] != SENTINEL || buffer[GUARD + pixels + i] != SENTINEL;
    for (size_t i = 0; i < pixels; i++) unpainted += buffer[GUARD + i] == SENTINEL;
    if (damaged || unpainted)
        fprintf(stderr, "  %dx%d %s: %zu guard words damaged, %zu pixels unpainted\n", w, h, what, damaged, unpainted);
    CHECK(damaged == 0);
    CHECK(unpainted == 0);
    CHECK(s->sw == w && s->sh == h && OSPI_W == w && OSPI_H == h);
    free(buffer);
}

static int inside_desktop(Rect r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= OSPI_W && r.y + r.h <= OSPI_H; }

static void test_table(void) {
    CHECK(OSPI_W == 1024 && OSPI_H == 768);   /* nothing selected: the classic desktop */
    CHECK(DISPLAY_MODE_COUNT == 30);
    int landscape = 0, portrait = 0, narrow = OSPI_MAX_W, wide = 0;
    for (int i = 0; i < DISPLAY_MODE_COUNT; i++) {
        int w = 0, h = 0;
        CHECK(display_mode_size(i, &w, &h) == 0);
        CHECK(w >= OSPI_MIN_W && w <= OSPI_MAX_W && h >= OSPI_MIN_H && h <= OSPI_MAX_H);
        CHECK(display_mode_find(w, h) == i);   /* also proves no size is listed twice */
        CHECK(display_mode_find(h, w) >= 0);   /* every mode has its turned counterpart */
        const char *aspect = display_mode_aspect(i);
        CHECK(aspect[0] && strchr(aspect, ':'));
        int a = atoi(aspect), b = atoi(strchr(aspect, ':') + 1);
        CHECK(a > 0 && b > 0 && abs(w * b - h * a) <= 2 * b);   /* 1366x768 and 854x480 are nominal 16:9 */
        if (w > h) landscape++; else portrait++;
        if (w < narrow) narrow = w;
        if (w > wide) wide = w;
    }
    CHECK(landscape == 15 && portrait == 15 && narrow == OSPI_MIN_W && wide == OSPI_MAX_W);
    CHECK(display_mode_size(-1, NULL, NULL) == -1 && display_mode_size(DISPLAY_MODE_COUNT, NULL, NULL) == -1);
    CHECK(display_mode_index(0, 2, 0) == display_mode_find(1024, 768));
    CHECK(display_mode_index(2, 0, 1) == display_mode_find(480, 854));
    CHECK(display_mode_index(3, 0, 0) == -1 && display_mode_index(0, 5, 0) == -1);
    CHECK(display_set_size(123, 456) == -1 && display_set_size(1024, 767) == -1 && display_set_size(0, 0) == -1);
    CHECK(OSPI_W == 1024 && OSPI_H == 768);
}

static void test_every_mode_paints(Session *s) {
    for (int i = 0; i < DISPLAY_MODE_COUNT; i++) {
        int w, h;
        display_mode_size(i, &w, &h);
        control_panels_close();
        CHECK(display_mode_apply(s, w, h) == 0);
        paint_guarded(s, w, h, "desktop");
        for (int which = 0; which < 2; which++) {   /* the disk and the Trash follow the new edges */
            Rect icon, hit;
            session_desktop_icon_rect(s, which, &icon, &hit);
            CHECK(inside_desktop(icon) && icon.x >= w - 80);
            CHECK(which == 0 ? icon.y < 80 : icon.y >= h - 100);
        }
        /* every menu open in turn, including the right-hand application menu */
        menu_update(s);
        for (int m = 0; m < menu_count(); m++) {
            Rect title;
            menu_title_rect(m, &title);
            if (title.w <= 0) continue;
            s->menu = m; s->menu_hot = 0;
            paint_guarded(s, w, h, "menu");
            for (int row = 0; row < menu_nitems(m); row++) {
                Rect item;
                menu_item_rect(m, row, &item);
                CHECK(item.x >= 0 && item.x + item.w <= w);
            }
        }
        s->menu = s->menu_hot = -1;
        session_command(s, CMD_CP_MONITORS);
        CHECK(control_panels_active());
        paint_guarded(s, w, h, "Monitors & Display");
        Rect panel = cp_window_rect(s);
        CHECK(panel.y >= TITLE_H && panel.y + panel.h <= h);
        CHECK(w >= panel.w ? panel.x == (w - panel.w) / 2 : panel.x == 0);   /* centred, or at the left edge */
        control_panels_close();
        for (int kind = 1; kind <= 5; kind++) {   /* alerts and the wide dialogs stay on the desktop */
            Rect box, cancel, ok;
            s->dialog = kind;
            dialog_layout(s, &box, &cancel, &ok);
            CHECK(inside_desktop(box) && inside_desktop(ok));
            CHECK(abs((box.x + box.w / 2) - w / 2) <= 1 && abs((box.y + box.h / 2) - h / 2) <= 1);
        }
        s->dialog = 0;
        dialog_info(s, "Display", "A dialog in this mode.");
        paint_guarded(s, w, h, "dialog");
        s->dialog = 0;
        GuestWindow guest = {0};   /* a classic app surface is scaled and centred inside the mode */
        guest_window_layout(&guest, 640, 480, 1, 0, 0);
        CHECK(guest.x >= 0 && guest.y >= TITLE_H && guest.x + guest.w <= w && guest.y + guest.h <= h);
        CHECK(guest.image_w > 0 && guest.image_w <= 640 && guest.image_w * 480 / 640 - guest.image_h <= 1);
        guest_window_drag(&guest, w + 500, h + 500, 0, 0);
        CHECK(guest.x + guest.w <= w && guest.y + guest.h <= h);
    }
}

/* Every visible title hit-tests to itself, stays on the bar and overlaps no other. */
static int check_menu_bar(Session *s, const char *what) {
    menu_update(s);
    int visible = 0, count = menu_count();
    Rect r[16];
    for (int i = 0; i < count; i++) {
        menu_title_rect(i, &r[i]);
        if (r[i].w <= 0) continue;
        visible++;
        CHECK(r[i].x >= 0 && r[i].x + r[i].w <= OSPI_W && r[i].y == 0 && r[i].h == 20);
        CHECK(r[i].w >= fb_text_width(menu_title(i)));
        CHECK(menu_title_at(r[i].x, 10) == i);
        CHECK(menu_title_at(r[i].x + r[i].w / 2, 10) == i);
        CHECK(menu_title_at(r[i].x + r[i].w - 1, 10) == i);
        for (int j = 0; j < i; j++)
            if (r[j].w > 0) CHECK(r[i].x >= r[j].x + r[j].w || r[j].x >= r[i].x + r[i].w);
    }
    for (int x = 0; x < OSPI_W; x++) {
        int hit = menu_title_at(x, 10);
        if (hit >= 0) CHECK(r[hit].w > 0 && x >= r[hit].x && x < r[hit].x + r[hit].w);
    }
    Rect none;
    menu_title_rect(count, &none);
    CHECK(none.w == 0 && menu_title_at(OSPI_W, 10) == -1 && menu_title_at(-1, 10) == -1);
    fprintf(stderr, "  menu bar at %dx%d (%s): %d of %d titles\n", OSPI_W, OSPI_H, what, visible, count);
    return visible;
}

static void test_menu_bar(Session *s) {
    Rect r;
    CHECK(display_mode_apply(s, 1024, 768) == 0);
    CHECK(check_menu_bar(s, "default") == 9);
    menu_title_rect(1, &r);   /* the default layout is the one from before runtime sizes */
    CHECK(r.x == 6 + 34 && r.w == fb_text_width("File") + 18);
    menu_title_rect(7, &r);
    CHECK(r.x + r.w == 1024 - 8 && r.w == fb_text_width(menu_title(7)) + 18);

    CHECK(display_mode_apply(s, 1920, 1080) == 0);
    CHECK(check_menu_bar(s, "widest") == 9);

    static const int narrow[][2] = {{480, 640}, {480, 854}};
    for (int n = 0; n < 2; n++) {
        CHECK(display_mode_apply(s, narrow[n][0], narrow[n][1]) == 0);
        int visible = check_menu_bar(s, "narrowest");
        CHECK(visible >= 3 && visible <= 9);
        for (int keep = 0; keep < 3; keep++) {   /* the fruit menu, File and the application menu never leave */
            menu_title_rect(keep == 2 ? 7 : keep, &r);
            CHECK(r.w > 0);
        }
        /* a long application name takes more of the bar; still no overlap */
        s->guest_active = s->guest_focused = 1;
        snprintf(s->guest_title, sizeof s->guest_title, "A Very Long Application Name Indeed");
        int with_guest = check_menu_bar(s, "narrowest, long application name");
        CHECK(with_guest >= 3 && with_guest <= visible);
        menu_title_rect(1, &r);
        CHECK(r.w > 0);
        paint_guarded(s, narrow[n][0], narrow[n][1], "narrow menu bar");
        s->guest_active = s->guest_focused = 0;
        s->guest_title[0] = 0;
        menu_update(s);
        /* Left and Right walk only the titles that are on the bar. */
        s->menu = 1; s->menu_hot = 0;
        for (int step = 0; step < 2 * menu_count(); step++) {
            CHECK(menu_navigation_key(s, step < menu_count() ? MENU_KEY_RIGHT : MENU_KEY_LEFT, 0));
            menu_title_rect(s->menu, &r);
            CHECK(r.w > 0);
        }
        CHECK(s->menu == 1);
        s->menu = s->menu_hot = -1;
    }
}

static void test_live_switch(Session *s) {
    CHECK(display_mode_apply(s, 1920, 1200) == 0);
    CHECK(open_folder(s, "") == 0 || s->nwin > 0);
    CHECK(s->nwin > 0);
    if (!s->nwin) return;
    Window *w = &s->win[s->nwin - 1];
    w->w = 700; w->h = 500;
    w->x = 1920 - w->w; w->y = 1200 - w->h;   /* far corner of the largest mode */
    session_clamp(s);
    CHECK(w->x == 1220 && w->y == 700);
    s->switcher.open = 1; s->switcher.width = 190; s->switcher.x = 1700; s->switcher.y = 1100;
    s->menu = 1;
    CHECK(display_mode_apply(s, 480, 640) == 0);
    CHECK(s->sw == 480 && s->sh == 640 && s->menu == -1);
    Layout lay;
    window_layout(w, &lay);
    CHECK(inside_desktop(lay.outer) && lay.title.y >= TITLE_H);
    CHECK(inside_desktop(lay.title) && inside_desktop(lay.closeb));   /* title bar and close box reachable */
    CHECK(w->w <= 480 && w->h <= 640 - TITLE_H && w->w >= 260 && w->h >= 140);
    Rect sw;
    CHECK(app_switcher_bounds(s, &sw) && inside_desktop(sw) && s->switcher.x == sw.x && s->switcher.y == sw.y);
    paint_guarded(s, 480, 640, "window from the far corner");
    CHECK(display_mode_apply(s, 123, 456) == -1 && OSPI_W == 480 && s->sw == 480);   /* refused: nothing changes */
    s->switcher.open = 0;

    /* The way back from the smallest mode uses only what is on screen: the
     * panel opens at the left edge and its Desktop Size rows fit in 480. */
    session_command(s, CMD_CP_MONITORS);
    Rect c = cp_content_rect(cp_window_rect(s));
    CHECK(cp_window_rect(s).x == 0 && cp_window_rect(s).w == CP_WIN_W);
    int x = c.x + 250 + 46, y = c.y + 121;   /* Landscape */
    CHECK(x < OSPI_W && y < OSPI_H);
    click_at(s, x, y);
    CHECK(OSPI_W == 640 && OSPI_H == 480 && s->sw == 640 && s->sh == 480);
    paint_guarded(s, 640, 480, "panel after rotating");
    c = cp_content_rect(cp_window_rect(s));
    x = c.x + 70 + 2 * 56 + 26;   /* 16:9 lists its sizes; no switch yet */
    click_at(s, x, c.y + 121);
    CHECK(OSPI_W == 640 && OSPI_H == 480);
    x = c.x + 16 + 4 * 88 + 42;   /* fifth size: 1920x1080 */
    CHECK(x < OSPI_W);
    click_at(s, x, c.y + 147);
    CHECK(OSPI_W == 1920 && OSPI_H == 1080 && s->sw == 1920);
    Rect panel = cp_window_rect(s);
    CHECK(panel.x == (1920 - panel.w) / 2 && panel.y == (1080 - panel.h) / 2);   /* re-centred */
    c = cp_content_rect(panel);
    click_at(s, c.x + 250 + 96 + 46, c.y + 121);   /* Portrait */
    CHECK(OSPI_W == 1080 && OSPI_H == 1920);
    paint_guarded(s, 1080, 1920, "portrait panel");

    /* Wider than the desktop, the panel pans by its title bar and stops at both ends. */
    CHECK(display_mode_apply(s, 480, 854) == 0);
    panel = cp_window_rect(s);
    CHECK(panel.x == 0);
    session_down(s, 300, panel.y + 8, 1);
    session_move(s, 100, panel.y + 8);
    CHECK(cp_window_rect(s).x == -140);   /* 480 - 620: the right edge is now on screen */
    paint_guarded(s, 480, 854, "panned panel");
    session_move(s, -400, panel.y + 8);
    CHECK(cp_window_rect(s).x == -140);
    session_move(s, 900, panel.y + 300);
    session_up(s, 900, panel.y + 300);
    CHECK(cp_window_rect(s).x == 0 && cp_window_rect(s).y + CP_WIN_H <= 854);
    control_panels_close();
}

static void test_host_window_maths(void) {
    static const int scales[] = {50, 75, 100, 125, 150, 200};
    for (int i = 0; i < DISPLAY_MODE_COUNT; i++) {
        int w, h;
        display_mode_size(i, &w, &h);
        CHECK(display_set_size(w, h) == 0);
        for (int k = 0; k < 6; k++) {
            int ww = 0, wh = 0, x = -1, y = -1;
            display_window_fit(w, h, scales[k], 0, 0, &ww, &wh);
            CHECK(ww == w * scales[k] / 100 && wh == h * scales[k] / 100);
            display_window_fit(w, h, scales[k], 1512, 944, &ww, &wh);   /* a 14 inch laptop's usable area */
            CHECK(ww >= 1 && wh >= 1 && ww <= 1512 && wh <= 944);
            CHECK(ww == w * scales[k] / 100 || ww == 1512 || wh == 944);
            CHECK(abs(ww * h - wh * w) <= (w > h ? w : h) * 2);   /* shape kept to within two window pixels */
            /* the pointer: corners, centre and every column map inside the desktop, in order */
            display_point(0, 0, ww, wh, &x, &y);
            CHECK(x == 0 && y == 0);
            display_point(ww - 1, wh - 1, ww, wh, &x, &y);
            CHECK(x < w && y < h && x >= w - 1 - (w + ww - 1) / ww && y >= h - 1 - (h + wh - 1) / wh);
            display_point(ww / 2, wh / 2, ww, wh, &x, &y);
            CHECK(abs(x - w / 2) <= (w + ww - 1) / ww && abs(y - h / 2) <= (h + wh - 1) / wh);
            int last = -1;
            for (int px = 0; px < ww; px++) {
                display_point(px, 0, ww, wh, &x, &y);
                if (x < last || x >= w || x - last > (w + ww - 1) / ww) { CHECK(!"pointer mapping"); break; }
                last = x;
            }
        }
    }
    int ww, wh;
    display_window_fit(1024, 768, 999, 0, 0, &ww, &wh);   /* a bad percent means 100 */
    CHECK(ww == 1024 && wh == 768);
    CHECK(display_set_size(OSPI_DEFAULT_W, OSPI_DEFAULT_H) == 0);
}

static void test_persistence(const char *root) {
    int w = 0, h = 0, scale = 0;
    display_mode_saved(root, &w, &h, &scale);   /* nothing saved yet */
    CHECK(w == 1024 && h == 768 && scale == 100);
    display_mode_saved(NULL, &w, &h, &scale);
    CHECK(w == 1024 && h == 768 && scale == 100);
    CHECK(display_mode_startup(root) == 100 && OSPI_W == 1024 && OSPI_H == 768);

    Session *s = session_create(root);
    CHECK(s);
    if (!s) return;
    CHECK(s->sw == 1024 && s->sh == 768);
    session_command(s, CMD_CP_MONITORS);
    Rect c = cp_content_rect(cp_window_rect(s));
    click_at(s, c.x + 16 + 1 * 88 + 42, c.y + 147);   /* 4:3 landscape, second size */
    CHECK(OSPI_W == 800 && OSPI_H == 600);
    c = cp_content_rect(cp_window_rect(s));
    click_at(s, c.x + 112 + 3 * 70 + 30, c.y + 336);   /* Screen size 150% keeps working */
    CHECK(s->display_window_scale == 150);
    control_panels_close();
    session_destroy(s);

    display_mode_saved(root, &w, &h, &scale);
    CHECK(w == 800 && h == 600 && scale == 150);
    CHECK(display_set_size(1024, 768) == 0);   /* as a fresh process starts */
    CHECK(display_mode_startup(root) == 150 && OSPI_W == 800 && OSPI_H == 600);
    s = session_create(root);
    CHECK(s);
    if (!s) return;
    CHECK(s->sw == 800 && s->sh == 600 && s->display_window_scale == 150);
    paint_guarded(s, 800, 600, "restored mode");
    session_command(s, CMD_CP_MONITORS);
    c = cp_content_rect(cp_window_rect(s));
    click_at(s, c.x + 250 + 96 + 46, c.y + 121);   /* Portrait is saved too */
    control_panels_close();
    session_destroy(s);
    display_mode_saved(root, &w, &h, &scale);
    CHECK(w == 600 && h == 800);

    os999_cp_store_t store;   /* a size that is not a mode falls back to the default */
    CHECK(os999_cp_store_init(&store, root) == 0 && os999_cp_store_load(&store) == 0);
    CHECK(os999_cp_store_set_int(&store, "monitors", "desktop_width", 777) == 0);
    CHECK(os999_cp_store_set_int(&store, "monitors", "window_scale", 5000) == 0);
    CHECK(os999_cp_store_save(&store) == 0);
    os999_cp_store_destroy(&store);
    display_mode_saved(root, &w, &h, &scale);
    CHECK(w == 1024 && h == 768 && scale == 100);
    CHECK(display_mode_startup(root) == 100 && OSPI_W == 1024 && OSPI_H == 768);
}

int main(void) {
    char root[] = "/tmp/os999-display-modes-XXXXXX";
    CHECK(mkdtemp(root));
    CHECK(!volume_seed(root));
    test_table();
    test_persistence(root);
    test_host_window_maths();
    Session *s = session_create(root);
    CHECK(s);
    if (!s) return 1;
    test_every_mode_paints(s);
    test_menu_bar(s);
    test_live_switch(s);
    control_panels_close();
    session_destroy(s);
    CHECK(!archive_remove_tree(root));
    printf("Display modes: %d failures\n", failures);
    return !!failures;
}
