#include "movie_fixture.h"
#include "movie_window.h"
#include "ospi.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(c)                                                                                         \
    do {                                                                                                 \
        if (!(c)) {                                                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c);                                              \
            failures++;                                                                                  \
        }                                                                                                \
    } while (0)

static void click_at(Session *s, int x, int y, int clicks) {
    session_down(s, x, y, clicks);
    session_up(s, x, y);
}

static int item_named(const Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) {
        if (strcmp(w->items[i].name, name) == 0) return i;
    }
    return -1;
}

static int open_named(Session *s, int win, const char *name) {
    int idx = item_named(&s->win[win], name);
    int x, y;
    if (idx < 0 || ospi_item_center(s, win, idx, &x, &y) != 0) return -1;
    click_at(s, x, y, 2);
    return 0;
}

static int write_bytes(const char *path, const void *bytes, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t got = fwrite(bytes, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

static void dismiss(Session *s) { session_key(s, 27, 0); }

static uint32_t picture_at(Session *s, const uint32_t *px, Rect *pic) {
    memset(pic, 0, sizeof *pic);
    Window *w = &s->win[s->nwin - 1];
    if (movie_window_picture_rect(w, pic) != 0) return 0;
    int x = pic->x + pic->w / 2;
    int y = pic->y + pic->h / 2;
    return px[(size_t)y * OSPI_W + x];
}

static void remove_tree(const char *root) {
    const char *files[] = {
        "/Applications/Nothing Installed Yet",
        "/Documents/Notes",
        "/Documents/clip.mov",
        "/Documents/bad.mov",
        "/Documents/codec.mov",
        "/Documents/huge.mov",
        "/Documents/movie.mov.txt",
        "/Documents/link.mov",
        "/Read Me",
        "/System Folder/About This Folder",
        "/System Folder/Fonts/About Fonts",
        "/Trash/.keep",
    };
    char path[OSPI_PATH];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s%s", root, files[i]);
        unlink(path);
    }
    const char *dirs[] = {"/System Folder/Fonts", "/System Folder", "/Applications", "/Documents", "/Trash", ""};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(path, sizeof path, "%s%s", root, dirs[i]);
        rmdir(path);
    }
}

int main(void) {
    CHECK(movie_is_name("clip.mov") && movie_is_name("Clip.MOV"));
    CHECK(movie_is_name("a.qt") && movie_is_name("a.mp4") && movie_is_name("a.m4v"));
    CHECK(!movie_is_name("movie.mov.txt") && !movie_is_name("notes"));
    char root[] = "/tmp/os999-movie-XXXXXX";
    if (!mkdtemp(root)) return 1;
    if (volume_seed(root) != 0) {
        remove_tree(root);
        return 1;
    }
    MovieFixture clip, codec;
    char path[OSPI_PATH];
    CHECK(movie_fixture_build(&clip, 0, MAC_MOVIE_CODEC_RAW, 32) == 0);
    CHECK(movie_fixture_build(&codec, 0, 0x72707A61u, 16) == 0);
    snprintf(path, sizeof path, "%s/Documents/clip.mov", root);
    CHECK(write_bytes(path, clip.data, clip.len) == 0);
    snprintf(path, sizeof path, "%s/Documents/codec.mov", root);
    CHECK(write_bytes(path, codec.data, codec.len) == 0);
    snprintf(path, sizeof path, "%s/Documents/bad.mov", root);
    CHECK(write_bytes(path, "not ", 4) == 0);
    snprintf(path, sizeof path, "%s/Documents/movie.mov.txt", root);
    CHECK(write_bytes(path, "hello from text\n", 16) == 0);
    snprintf(path, sizeof path, "%s/Documents/huge.mov", root);
    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0 && ftruncate(fd, (off_t)MAC_MOVIE_MAX_BYTES + 1) == 0);
    if (fd >= 0) close(fd);
    snprintf(path, sizeof path, "%s/Documents/link.mov", root);
    CHECK(symlink("clip.mov", path) == 0);

    Session *s = session_create(root);
    uint32_t *px = calloc((size_t)OSPI_W * OSPI_H, sizeof *px);
    CHECK(s != NULL && px != NULL);
    if (!s || !px) {
        session_destroy(s);
        free(px);
        remove_tree(root);
        return 1;
    }
    CHECK(open_named(s, 0, "Read Me") == 0);
    CHECK(s->win[s->nwin - 1].kind == WIN_TEXT);
    session_key(s, 'w', MOD_CMD);
    CHECK(s->nwin == 1 && s->win[0].kind == WIN_FOLDER);

    CHECK(open_named(s, 0, "Documents") == 0);
    int docs = s->nwin - 1;
    CHECK(s->win[docs].kind == WIN_FOLDER);
    CHECK(open_named(s, docs, "movie.mov.txt") == 0);
    CHECK(s->win[s->nwin - 1].kind == WIN_TEXT);
    CHECK(strstr(s->win[s->nwin - 1].text, "hello from text") != NULL);
    session_key(s, 'w', MOD_CMD);
    CHECK(s->nwin == 2);

    CHECK(open_named(s, docs, "clip.mov") == 0);
    Window *movie = &s->win[s->nwin - 1];
    CHECK(movie->kind == WIN_MOVIE);
    CHECK(movie_window_state(movie) == MAC_MOVIE_PLAYING);
    CHECK(movie_window_time(movie) == 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    Rect pic;
    uint32_t color = picture_at(s, px, &pic);
    if (color != 0xFFFF0000u) fprintf(stderr, "frame %08x rect %d,%d %dx%d\n", color, pic.x, pic.y, pic.w, pic.h);
    CHECK(pic.w > 10 && pic.h > 10);
    CHECK(color == 0xFFFF0000u);

    session_tick(s, 5000);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 0);
    session_tick(s, 6000);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 600);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(picture_at(s, px, &pic) == 0xFF00FF00u);

    Layout lay;
    window_layout(&s->win[s->nwin - 1], &lay);
    click_at(s, lay.content.x + lay.content.w / 2, lay.content.y + lay.content.h / 2, 1);
    CHECK(movie_window_state(&s->win[s->nwin - 1]) == MAC_MOVIE_PAUSED);
    session_tick(s, 9000);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 600);

    session_key(s, ' ', 0);
    CHECK(movie_window_state(&s->win[s->nwin - 1]) == MAC_MOVIE_PLAYING);
    session_tick(s, 10000);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 600);
    session_tick(s, 12000);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 1800);
    CHECK(movie_window_state(&s->win[s->nwin - 1]) == MAC_MOVIE_ENDED);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(picture_at(s, px, &pic) == 0xFF0000FFu);

    click_at(s, lay.content.x + lay.content.w / 2, lay.content.y + lay.content.h / 2, 1);
    CHECK(movie_window_state(&s->win[s->nwin - 1]) == MAC_MOVIE_PLAYING);
    CHECK(movie_window_time(&s->win[s->nwin - 1]) == 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(picture_at(s, px, &pic) == 0xFFFF0000u);

    void *kept = s->win[s->nwin - 1].movie;
    int nwin = s->nwin;
    CHECK(movie_window_open(s, "clip.mov", "Documents/clip.mov") == 0);
    CHECK(s->nwin == nwin);
    CHECK(s->win[s->nwin - 1].movie == kept);

    int bx, by;
    CHECK(ospi_control_center(&s->win[s->nwin - 1], 0, &bx, &by) == 0);
    click_at(s, bx, by, 1);
    CHECK(s->nwin == nwin - 1);
    CHECK(s->win[s->nwin - 1].kind == WIN_FOLDER);
    CHECK(strcmp(s->win[s->nwin - 1].title, "Documents") == 0);

    nwin = s->nwin;
    CHECK(open_named(s, docs, "bad.mov") == 0);
    CHECK(s->dialog && strstr(s->dialog_body, "cut off"));
    CHECK(s->nwin == nwin);
    dismiss(s);
    CHECK(!s->dialog);

    CHECK(open_named(s, docs, "codec.mov") == 0);
    CHECK(s->dialog && strstr(s->dialog_body, "rpza"));
    CHECK(s->nwin == nwin);
    dismiss(s);

    CHECK(open_named(s, docs, "huge.mov") == 0);
    CHECK(s->dialog && strstr(s->dialog_body, "larger than 8 MB"));
    CHECK(s->nwin == nwin);
    dismiss(s);

    CHECK(open_named(s, docs, "link.mov") == 0);
    CHECK(s->dialog && strstr(s->dialog_body, "not a file"));
    CHECK(s->nwin == nwin);
    dismiss(s);

    CHECK(movie_window_open(s, "x", "../x.mov") == 0);
    CHECK(s->dialog && strstr(s->dialog_body, "outside this disk"));
    CHECK(s->nwin == nwin);
    dismiss(s);

    CHECK(open_named(s, docs, "clip.mov") == 0);
    CHECK(s->win[s->nwin - 1].kind == WIN_MOVIE);
    session_destroy(s);
    free(px);
    remove_tree(root);
    if (failures) fprintf(stderr, "%d movie window checks failed\n", failures);
    else printf("ok\n");
    return failures ? 1 : 0;
}
