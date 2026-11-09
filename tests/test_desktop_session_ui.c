#include "draw.h"
#include "ospi.h"
#include "../src/session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

static int sound_checked(Session *s) {
    menu_update(s);
    for (int i = 0; i < menu_nitems(4); i++) {
        const MenuItem *item = menu_at(4, i);
        if (item->cmd == CMD_SOUND_TOGGLE) return !!(item->flags & MI_CHECKED);
    }
    CHECK(0); /* The sound command must remain available. */
    return -1;
}

static void click(Session *s, int x, int y, int clicks) {
    session_down(s, x, y, clicks);
    session_up(s, x, y);
}

static void test_about_and_menu_toggles(Session *s, const char *dir) {
    int mx = 0, my = 0;
    CHECK(ospi_menu_center(0, -1, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->menu == 0);
    CHECK(ospi_menu_center(0, 0, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->menu == -1);
    CHECK(s->nwin == 2);

    const Window *about = &s->win[s->nwin - 1];
    CHECK(strcmp(about->title, "About This Mac") == 0);
    CHECK(about->w == 460 && about->h == 440);
    CHECK(about->x == (OSPI_W - 460) / 2 && about->y == (OSPI_H - 440) / 2);
    CHECK(strstr(about->text, "Ian Trimble") != NULL);
    CHECK(strstr(about->text, "Explore Ian's apps using the link below.") != NULL);
    /* S11: Never expose temporary host paths or private volume location in About. */
    CHECK(strstr(about->text, dir) == NULL);

    /* Re-invoking About brings existing window to front without duplicating. */
    bring_front(s, 0);
    session_command(s, CMD_ABOUT);
    CHECK(s->nwin == 2);
    CHECK(strcmp(s->win[s->nwin - 1].title, "About This Mac") == 0);
    session_command(s, CMD_CLOSE);
    CHECK(s->nwin == 1);

    /* Startup toggle, preview, and sound toggle menu commands. */
    CHECK(session_take_startup_toggle(s) == 0);
    session_command(s, CMD_STARTUP_TOGGLE);
    CHECK(session_take_startup_toggle(s) == 1);
    CHECK(session_take_startup_toggle(s) == 0);

    CHECK(session_take_startup_preview(s) == 0);
    session_command(s, CMD_STARTUP_PREVIEW);
    CHECK(session_take_startup_preview(s) == 1);
    CHECK(session_take_startup_preview(s) == 0);

    menu_set_sound_enabled(1);
    CHECK(sound_checked(s) == 1);
    menu_set_sound_enabled(0);
    CHECK(sound_checked(s) == 0);
    menu_set_sound_enabled(1);
    session_command(s, CMD_SOUND_TOGGLE);
    CHECK(s->sound_toggle_request == 1);
    s->sound_toggle_request = 0;
}

static void test_window_drag_resize_clamp_and_cap(Session *s) {
    Window *w = &s->win[0];
    int orig_x = w->x, orig_y = w->y;
    int tx = 0, ty = 0;
    CHECK(ospi_control_center(w, 3, &tx, &ty) == 0);
    session_down(s, tx, ty, 1);
    session_move(s, tx + 40, ty + 30);
    session_up(s, tx + 40, ty + 30);
    CHECK(w->x == orig_x + 40 && w->y == orig_y + 30);

    /* Resize via grow box. */
    Layout lay;
    window_layout(w, &lay);
    int gx = lay.grow.x + lay.grow.w / 2;
    int gy = lay.grow.y + lay.grow.h / 2;
    int orig_w = w->w, orig_h = w->h;
    session_down(s, gx, gy, 1);
    session_move(s, gx + 50, gy + 60);
    session_up(s, gx + 50, gy + 60);
    CHECK(w->w == orig_w + 50 && w->h == orig_h + 60);

    /* Clamping enforces minimum dimensions and keeps title bar below menu bar. */
    w->x = -200;
    w->y = -50;
    w->w = 80;
    w->h = 40;
    session_clamp(s);
    CHECK(w->x == 0 && w->y == 20);
    CHECK(w->w == 260 && w->h == 140);

    /* Scroll wheel on multi-line Help window. */
    session_command(s, CMD_HELP);
    CHECK(s->nwin == 2);
    Window *help = &s->win[1];
    help->h = 150;
    session_clamp(s);
    int max_s = window_max_scroll(help);
    CHECK(max_s > 0);
    session_wheel(s, -2);
    CHECK(help->scroll > 0 && help->scroll <= max_s);
    session_wheel(s, 100);
    CHECK(help->scroll == 0);
    session_command(s, CMD_CLOSE);
    CHECK(s->nwin == 1);

    /* Reaching OSPI_MAX_WIN (8) surfaces "Close a window first." dialog. */
    for (int i = 0; i < 10; i++) {
        char created[OSPI_NAME];
        CHECK(volume_mkdir_unique(s->root, "Documents", "Sub", created, sizeof created) == 0);
        char rel[OSPI_PATH];
        snprintf(rel, sizeof rel, "Documents/%s", created);
        (void)open_folder(s, rel);
    }
    CHECK(s->nwin == OSPI_MAX_WIN);
    session_command(s, CMD_ABOUT);
    CHECK(s->dialog == 1);
    CHECK(strstr(s->dialog_body, "Close a window first.") != NULL);
    session_key(s, 27, 0); /* Escape dismisses dialog. */
    CHECK(s->dialog == 0);
    while (s->nwin > 1) {
        session_command(s, CMD_CLOSE);
    }
}

static void test_draw_clipping_and_text_wrap(void) {
    uint32_t px[64 * 64];
    memset(px, 0, sizeof px);
    Fb fb;
    fb_init(&fb, px, 64, 64);
    fb_clip(&fb, 10, 10, 20, 20);
    fb_fill(&fb, 0, 0, 64, 64, 0xFF123456u);
    CHECK(px[0] == 0);
    CHECK(px[9 * 64 + 15] == 0);
    CHECK(px[10 * 64 + 10] == 0xFF123456u);
    CHECK(px[29 * 64 + 29] == 0xFF123456u);
    CHECK(px[30 * 64 + 30] == 0);

    fb_invert(&fb, 0, 0, 64, 64);
    CHECK(px[0] == 0);
    CHECK(px[10 * 64 + 10] == 0xFFEDCBA9u);
    fb_unclip(&fb);

    int lh = typeface_line_height(1);
    CHECK(lh > 0);
    CHECK(text_height("", 120) == 0);
    CHECK(text_height("Short", 200) == lh);
    CHECK(text_height("One two three four five six seven eight", 60) >= lh * 2);
    CHECK(text_height("Supercalifragilisticexpialidocious", 32) >= lh * 2);
    text_draw(&fb, 4, 4, 48, C_WHITE, "Wrap test line\nSecond line");
}

int main(void) {
    char tmpl[] = "/tmp/ospi-ui-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (!dir) return 1;
    CHECK(volume_seed(dir) == 0);

    Session *s = session_create(dir);
    CHECK(s != NULL);
    if (s) {
        test_about_and_menu_toggles(s, dir);
        test_window_drag_resize_clamp_and_cap(s);
        session_destroy(s);
    }
    test_draw_clipping_and_text_wrap();

    char cmd[OSPI_PATH + 32];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (fails == 0) {
        (void)system(cmd);
    } else {
        fprintf(stderr, "left fixture at %s\n", dir);
    }
    if (fails) {
        fprintf(stderr, "desktop session UI checks: %d failed\n", fails);
        return 1;
    }
    puts("desktop session UI checks: ok");
    return 0;
}
