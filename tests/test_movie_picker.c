#include "about_panel.h"
#include "movie_picker.h"
#include "movie_window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void touch(const char *p, const char *data) { FILE *f = fopen(p, "wb"); if (f) { fputs(data, f); fclose(f); } }

int main(void) {
    char root[] = "/tmp/os999-picker-XXXXXX";
    CHECK(mkdtemp(root));
    char p[600];
    snprintf(p, sizeof p, "%s/Movies", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Movies/clip.mov", root); touch(p, "x");
    snprintf(p, sizeof p, "%s/Movies/notes.txt", root); touch(p, "x");
    snprintf(p, sizeof p, "%s/Downloads", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Downloads/trailer.MP4", root); touch(p, "x");
    static Session s; memset(&s, 0, sizeof s);
    snprintf(s.root, sizeof s.root, "%s", root);
    movie_picker_scan(&s);
    CHECK(movie_picker_count() == 2);
    int saw_mov = 0, saw_mp4 = 0;
    for (int i = 0; i < movie_picker_count(); i++) {
        if (!strcmp(movie_picker_rel(i), "Movies/clip.mov")) saw_mov = 1;
        if (!strcmp(movie_picker_rel(i), "Downloads/trailer.MP4")) saw_mp4 = 1;
    }
    CHECK(saw_mov && saw_mp4);
    /* Import copies a host file without overwriting. */
    char host[] = "/tmp/os999-host-XXXXXX"; CHECK(mkdtemp(host));
    snprintf(p, sizeof p, "%s/home.mov", host); touch(p, "data");
    char rel[OSPI_PATH];
    CHECK(movie_picker_import(root, p, rel, sizeof rel) == 0 && !strcmp(rel, "Movies/home.mov"));
    CHECK(movie_picker_import(root, p, rel, sizeof rel) == 0 && !strcmp(rel, "Movies/home 2.mov"));
    snprintf(p, sizeof p, "%s/readme.txt", host); touch(p, "no");
    CHECK(movie_picker_import(root, p, rel, sizeof rel) != 0);
    /* The empty player paints the chooser, not the old "drop here" text. */
    enum { W = 400, H = 300 };
    static uint32_t px[W * H];
    Fb fb = {.px = px, .w = W, .h = H};
    Window win; memset(&win, 0, sizeof win); win.kind = WIN_MOVIE; win.x = 10; win.y = 10; win.w = 360; win.h = 240;
    movie_window_paint(&fb, &win);
    int nonblack = 0; for (int i = 0; i < W * H; i++) if (px[i] != 0 && px[i] != C_BLACK) nonblack++;
    CHECK(nonblack > 200);
    /* About paints banner and bars for the About window only. */
    memset(&win, 0, sizeof win); win.kind = WIN_TEXT; snprintf(win.rel, sizeof win.rel, ":about");
    win.x = 5; win.y = 5; win.w = 380; win.h = 280;
    s.ram = 8ull << 30; snprintf(s.arch, sizeof s.arch, "arm64"); s.guest_ram_mb = 8;
    Layout lay; window_layout(&win, &lay);
    memset(px, 0, sizeof px);
    CHECK(about_panel_paint(&fb, &s, &win, &lay) == 1);
    int blue = 0; for (int i = 0; i < W * H; i++) if (px[i] == 0xff2f7fd8u) blue++;
    CHECK(blue > 100);
    snprintf(win.rel, sizeof win.rel, ":other");
    CHECK(about_panel_paint(&fb, &s, &win, &lay) == 0);
    CHECK(about_panel_resident_bytes() > 0);
    return failures ? 1 : 0;
}
