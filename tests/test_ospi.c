#include "ospi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

static uint32_t at(const uint32_t *px, int x, int y) { return px[(size_t)y * OSPI_W + x] & 0xFFFFFFu; }

static int write_ppm(const uint32_t *px, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", OSPI_W, OSPI_H);
    for (int i = 0; i < OSPI_W * OSPI_H; i++) {
        unsigned char rgb[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8),
                                (unsigned char)px[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static int is_desk(uint32_t c) { return c == 0x5B71A2u || c == 0x56709Cu; }

static void click(Session *s, int x, int y, int clicks) {
    session_down(s, x, y, clicks);
    session_up(s, x, y);
}

static int find_win(const Session *s, const char *title) {
    for (int i = 0; i < s->nwin; i++) {
        if (strcmp(s->win[i].title, title) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_item(const Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) {
        if (strcmp(w->items[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int main(void) {
    char tmpl[] = "/tmp/ospi-test-XXXXXX";
    char *dir = mkdtemp(tmpl);
    CHECK(dir != NULL);
    if (!dir) {
        return 1;
    }
    CHECK(volume_seed(dir) == 0);
    Session *s = session_create(dir);
    CHECK(s != NULL);
    if (!s) {
        return 1;
    }
    CHECK(s->nwin == 1);
    CHECK(strcmp(s->win[0].title, "OS π HD") == 0);
    CHECK(s->win[0].nitems == 4);
    CHECK(strcmp(s->win[0].items[0].name, "Applications") == 0);
    CHECK(strcmp(s->win[0].items[1].name, "Documents") == 0);
    CHECK(strcmp(s->win[0].items[2].name, "Read Me") == 0);
    CHECK(strcmp(s->win[0].items[3].name, "System Folder") == 0);
    CHECK(s->win[0].items[3].kind == ITEM_SYSTEM);
    CHECK(s->free_bytes > 0);
    CHECK(s->ram > 0);
    CHECK(strcmp(s->arch, "arm64") == 0 || strcmp(s->arch, "x86_64") == 0);

    uint32_t *px = calloc((size_t)OSPI_W * OSPI_H, 4);
    CHECK(px != NULL);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(at(px, 2, 4) == 0xEEEEEEu);
    CHECK(at(px, 2, 19) == 0);
    CHECK(is_desk(at(px, 8, 400)));
    CHECK(at(px, s->win[0].x, s->win[0].y) == 0);
    /* File menu: F is the first glyph. font8x8 stores the left pixel in bit 0. */
    CHECK(at(px, 40, 6) == 0);
    CHECK(at(px, 47, 6) == 0xEEEEEEu);
    write_ppm(px, "/tmp/ospi-boot.ppm");

    int ax, ay;
    CHECK(ospi_item_center(s, 0, 0, &ax, &ay) == 0);
    click(s, ax, ay, 2);
    CHECK(s->nwin == 2);
    CHECK(strcmp(s->win[1].rel, "Applications") == 0);
    CHECK(s->win[1].nitems == 1);

    int cx, cy;
    CHECK(ospi_control_center(&s->win[1], 0, &cx, &cy) == 0);
    click(s, cx, cy, 1);
    CHECK(s->nwin == 1);

    session_key(s, 'n', MOD_CMD);
    CHECK(s->win[0].nitems == 5);
    int created = find_item(&s->win[0], "untitled folder");
    CHECK(created >= 0);
    char folder[OSPI_PATH];
    snprintf(folder, sizeof folder, "%s/untitled folder", dir);
    struct stat st;
    CHECK(stat(folder, &st) == 0 && S_ISDIR(st.st_mode));

    int ix, iy, tx, ty;
    CHECK(ospi_item_center(s, 0, created, &ix, &iy) == 0);
    CHECK(ospi_desktop_center(s, 1, &tx, &ty) == 0);
    session_down(s, ix, iy, 1);
    session_move(s, ix + 12, iy + 8);
    session_move(s, tx, ty);
    session_up(s, tx, ty);
    CHECK(stat(folder, &st) != 0);
    char trashed[OSPI_PATH];
    snprintf(trashed, sizeof trashed, "%s/Trash/untitled folder", dir);
    CHECK(stat(trashed, &st) == 0);

    int mx, my;
    CHECK(ospi_menu_center(4, -1, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->menu == 4);
    CHECK(ospi_menu_center(4, 0, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->dialog == 2);
    CHECK(ospi_dialog_center(s, 0, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->dialog == 0);
    CHECK(stat(trashed, &st) != 0);

    CHECK(ospi_menu_center(5, -1, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(ospi_menu_center(5, 0, &mx, &my) == 0);
    click(s, mx, my, 1);
    CHECK(s->nwin == 2);
    CHECK(strcmp(s->win[1].title, "How to Use OS Pi") == 0);
    CHECK(strstr(s->win[1].text, "Cmd-Q") != NULL);
    CHECK(strstr(s->win[1].text, dir) != NULL);

    int root = find_win(s, "OS π HD");
    CHECK(root >= 0);
    CHECK(ospi_control_center(&s->win[root], 2, &cx, &cy) == 0);
    click(s, cx, cy, 1);
    root = find_win(s, "OS π HD");
    CHECK(root >= 0 && s->win[root].zoomed == 1);
    CHECK(ospi_control_center(&s->win[root], 2, &cx, &cy) == 0);
    click(s, cx, cy, 1);
    root = find_win(s, "OS π HD");
    CHECK(root >= 0 && s->win[root].zoomed == 0);

    CHECK(ospi_control_center(&s->win[root], 3, &cx, &cy) == 0);
    session_down(s, cx, cy, 2);
    session_up(s, cx, cy);
    root = find_win(s, "OS π HD");
    CHECK(root >= 0 && s->win[root].collapsed == 1);
    CHECK(ospi_control_center(&s->win[root], 3, &cx, &cy) == 0);
    session_down(s, cx, cy, 2);
    root = find_win(s, "OS π HD");
    CHECK(root >= 0 && s->win[root].collapsed == 0);

    session_key(s, 'w', MOD_CMD);
    session_key(s, 'w', MOD_CMD);
    CHECK(s->nwin == 0);
    CHECK(ospi_desktop_center(s, 0, &tx, &ty) == 0);
    click(s, tx, ty, 2);
    CHECK(s->nwin == 1);
    CHECK(strcmp(s->win[0].title, "OS π HD") == 0);

    session_paint(s, px, OSPI_W, OSPI_H);
    write_ppm(px, "/tmp/ospi-after.ppm");
    session_key(s, 'q', MOD_CMD);
    CHECK(session_quit(s) == 1);

    session_destroy(s);
    free(px);
    char cmd[OSPI_PATH + 16];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    if (fails == 0) {
        int rm = system(cmd);
        (void)rm;
    } else {
        fprintf(stderr, "left test volume at %s\n", dir);
    }
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
