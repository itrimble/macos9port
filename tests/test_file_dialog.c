/* Headless checks for the shared in-window file browser (include/file_dialog.h). */
#include "file_dialog.h"
#include "finder_edit.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void touch(const char *dir, const char *name, const char *data) {
    char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb"); if (f) { fputs(data, f); fclose(f); }
}
static void mk(const char *dir, const char *name) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", dir, name); mkdir(p, 0755); }
static void ln(const char *target, const char *dir, const char *name) { char p[PATH_MAX]; snprintf(p, sizeof p, "%s/%s", dir, name); unlink(p); symlink(target, p); }
static int movie_only(const char *n) { size_t l = strlen(n); return l > 4 && (!strcasecmp(n + l - 4, ".mov") || !strcasecmp(n + l - 4, ".mp4")); }
static int has(const FileDialog *fd, const char *name) { for (int i = 0; i < fd->nent; i++) if (!strcmp(fd->ent[i].name, name)) return 1; return 0; }
static int index_of(const FileDialog *fd, const char *name) { for (int i = 0; i < fd->nent; i++) if (!strcmp(fd->ent[i].name, name)) return i; return -1; }
static void type(FileDialog *fd, const char *s) { fd_type(fd, s); }

int main(void) {
    char tmpl[] = "/tmp/os999-fd-XXXXXX";
    CHECK(mkdtemp(tmpl));
    char top[PATH_MAX]; CHECK(realpath(tmpl, top));
    char vol[PATH_MAX], vol2[PATH_MAX], outside[PATH_MAX], docs[PATH_MAX], p[PATH_MAX];
    snprintf(vol, sizeof vol, "%s/vol", top); snprintf(vol2, sizeof vol2, "%s/vol2", top); snprintf(outside, sizeof outside, "%s/outside", top);
    mkdir(vol, 0755); mkdir(vol2, 0755); mkdir(outside, 0755);
    snprintf(docs, sizeof docs, "%s/Docs", vol);
    mk(vol, "Docs"); mk(vol, "alpha"); mk(vol, "Empty"); mk(vol, "locked"); mk(docs, "deep");
    touch(vol, "zeta.mov", "z"); touch(vol, "notes.txt", "n"); touch(vol, ".hidden.mov", "h");
    touch(docs, "a.mov", "a"); touch(docs, "b.txt", "b");
    snprintf(p, sizeof p, "%s/deep", docs); touch(p, "c.mp4", "c");
    touch(outside, "secret.mov", "s"); touch(vol2, "x.mov", "x");
    ln(outside, vol, "escape");                    /* folder alias leading out */
    snprintf(p, sizeof p, "%s/secret.mov", outside); ln(p, vol, "esc.mov");   /* file alias leading out */
    ln(vol2, vol, "sibling");                      /* prefix trap: /…/vol2 is not inside /…/vol */
    ln("Docs", vol, "inner");                      /* alias that stays inside */
    ln("nowhere", vol, "broken");
    snprintf(p, sizeof p, "%s/locked", vol); chmod(p, 0);
    int root_user = geteuid() == 0;

    FdRoot roots[] = {{"OS999 Disk", vol}, {"Gone", "/nonexistent/os999-fd"}, {"Slash", "/"}};
    FileDialog *fd = calloc(1, sizeof *fd);
    CHECK(fd_open(fd, FD_OPEN, "Open", roots, 3, NULL, NULL) == 0);
    CHECK(fd->nroots == 1);                        /* missing root and "/" are skipped */
    CHECK(!strcmp(fd->cwd, vol) && fd->state == FD_RUNNING);
    CHECK(fd->ent[0].dir && fd->ent[fd->nent - 1].dir == 0);   /* folders first */
    CHECK(has(fd, "Docs") && has(fd, "alpha") && has(fd, "zeta.mov") && has(fd, "notes.txt"));
    CHECK(!has(fd, ".hidden.mov"));
    CHECK(index_of(fd, "alpha") < index_of(fd, "Docs"));        /* case-insensitive sort */
    CHECK(has(fd, "inner"));                                    /* in-root alias listed */
    CHECK(!has(fd, "escape") && !has(fd, "esc.mov") && !has(fd, "sibling") && !has(fd, "broken"));
    {   /* a dialog with no usable root refuses to open */
        FdRoot bad[] = {{"Gone", "/nonexistent/os999-fd"}};
        FileDialog *z = calloc(1, sizeof *z);
        CHECK(fd_open(z, FD_OPEN, "Open", bad, 1, NULL, NULL) == -1 && z->state == FD_CANCELLED);
        CHECK(fd_open(z, FD_OPEN, "Open", NULL, 0, NULL, NULL) == -1);
        free(z);
    }

    /* navigation down and up */
    CHECK(fd_goto(fd, "Docs") == 0 && !strcmp(fd->cwd, docs));
    CHECK(has(fd, "deep") && has(fd, "a.mov"));
    CHECK(fd_up(fd) == 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_up(fd) != 0 && !strcmp(fd->cwd, vol) && strstr(fd->msg, "top of"));   /* stops at the root */
    /* traversal and escape refusals; the dialog stays put */
    CHECK(fd_goto(fd, "..") != 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_goto(fd, "../outside") != 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_goto(fd, "Docs/../../outside") != 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_goto(fd, outside) != 0 && fd_goto(fd, "/") != 0 && fd_goto(fd, "/etc") != 0);
    CHECK(fd_goto(fd, vol2) != 0 && !strcmp(fd->cwd, vol));     /* shares the string prefix only */
    CHECK(fd_goto(fd, "escape") != 0 && fd_goto(fd, "sibling") != 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_goto(fd, "notes.txt") != 0 && fd_goto(fd, "missing") != 0);
    CHECK(fd_goto(fd, "inner") == 0 && !strcmp(fd->cwd, docs)); /* resolved path, not the alias name */
    CHECK(fd_up(fd) == 0);
    CHECK(fd_contains(fd, vol) == 0 && fd_contains(fd, vol2) == -1 && fd_contains(fd, "/") == -1 && fd_contains(fd, "rel") == -1);
    /* empty and unreadable folders */
    CHECK(fd_goto(fd, "Empty") == 0 && fd->nent == 0 && fd->sel == -1);
    CHECK(!fd_activate(fd) && fd->msg[0]);                      /* nothing to open */
    fd_move(fd, 1); fd_key(fd, FINDER_KEY_DOWN, 0); CHECK(fd->sel == -1);
    CHECK(fd_up(fd) == 0);
    if (!root_user) {
        CHECK(fd_goto(fd, "locked") != 0 && !strcmp(fd->cwd, vol) && strstr(fd->msg, "could not open"));
    }

    /* selection, keys, Return, filter */
    fd_set_filter(fd, movie_only, "Movies");
    CHECK(!has(fd, "notes.txt") && has(fd, "zeta.mov") && has(fd, "Docs"));
    fd_toggle_filter(fd); CHECK(has(fd, "notes.txt")); fd_toggle_filter(fd); CHECK(!has(fd, "notes.txt"));
    CHECK(fd_key(fd, FINDER_KEY_DOWN, 0) && fd->sel == 0);
    CHECK(fd_key(fd, FINDER_KEY_DOWN, 0) && fd->sel == 1);
    CHECK(fd_key(fd, FINDER_KEY_UP, 0) && fd->sel == 0);
    CHECK(fd_key(fd, FINDER_KEY_END, 0) && fd->sel == fd->nent - 1);
    CHECK(fd_key(fd, FINDER_KEY_HOME, 0) && fd->sel == 0);
    CHECK(fd_key(fd, 'd', 0) && !strcmp(fd->ent[fd->sel].name, "Docs"));   /* type-ahead */
    CHECK(fd_key(fd, '\r', 0) && !strcmp(fd->cwd, docs) && fd->state == FD_RUNNING);   /* Return enters a folder */
    CHECK(fd_key(fd, FINDER_KEY_LEFT, 0) && !strcmp(fd->cwd, vol));        /* Left goes up */
    CHECK(fd_key(fd, 'd', 0) && fd_key(fd, FINDER_KEY_RIGHT, 0) && !strcmp(fd->cwd, docs));
    CHECK(fd_key(fd, 8, 0) && !strcmp(fd->cwd, vol));                      /* Backspace goes up in Open */
    CHECK(fd_key(fd, 'd', 0) && fd_key(fd, FINDER_KEY_DOWN, MOD_CMD) && !strcmp(fd->cwd, docs));
    CHECK(fd_key(fd, FINDER_KEY_UP, MOD_CMD) && !strcmp(fd->cwd, vol));
    CHECK(!fd_key(fd, 'q', MOD_CMD));                                      /* Cmd chords are not ours */
    fd_select(fd, index_of(fd, "zeta.mov"));
    CHECK(fd_key(fd, '\r', 0) && fd->state == FD_ACCEPTED);
    snprintf(p, sizeof p, "%s/zeta.mov", vol);
    CHECK(!strcmp(fd->result, p));
    CHECK(!fd_key(fd, '\r', 0));                                           /* finished dialogs ignore input */

    /* cancel */
    CHECK(fd_open(fd, FD_OPEN, "Open", roots, 1, docs, NULL) == 0 && !strcmp(fd->cwd, docs));
    CHECK(fd_key(fd, 27, 0) && fd->state == FD_CANCELLED && fd->result[0] == 0);
    /* Open with nothing chosen does not close */
    CHECK(fd_open(fd, FD_OPEN, "Open", roots, 1, docs, NULL) == 0);
    CHECK(!fd_activate(fd) && fd->state == FD_RUNNING && strstr(fd->msg, "Choose"));
    /* alias retargeted after listing is caught at accept time */
    CHECK(fd_open(fd, FD_OPEN, "Open", roots, 1, vol, NULL) == 0);
    ln("Docs/a.mov", vol, "late.mov");
    CHECK(fd_goto(fd, vol) == 0 && has(fd, "late.mov"));
    snprintf(p, sizeof p, "%s/secret.mov", outside); ln(p, vol, "late.mov");   /* now points out */
    fd_select(fd, index_of(fd, "late.mov"));
    CHECK(!fd_activate(fd) && fd->state == FD_RUNNING && strstr(fd->msg, "outside"));
    CHECK(fd_goto(fd, vol) == 0 && !has(fd, "late.mov"));

    /* several roots, switching, and root tracking */
    FdRoot two[] = {{"OS999 Disk", vol}, {"Outside", outside}};
    CHECK(fd_open(fd, FD_OPEN, "Open", two, 2, outside, NULL) == 0 && fd->root == 1 && has(fd, "secret.mov"));
    CHECK(fd_select_root(fd, 0) == 0 && fd->root == 0 && !strcmp(fd->cwd, vol));
    CHECK(fd_select_root(fd, 5) != 0 && fd_goto(fd, vol2) != 0);

    /* Save mode */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, docs, "Report.txt") == 0 && !strcmp(fd->name, "Report.txt"));
    const char *why = NULL;
    CHECK(fd_name_check("", &why) != 0 && why);
    CHECK(fd_name_check("a/b", &why) != 0 && fd_name_check("a:b", &why) != 0 && fd_name_check(".", &why) != 0);
    CHECK(fd_name_check("..", &why) != 0 && fd_name_check(".hidden", &why) != 0 && fd_name_check("a\nb", &why) != 0);
    CHECK(fd_name_check("0123456789012345678901234567890123456789012345678901234567890123", &why) != 0);   /* 64 */
    CHECK(fd_name_check("ok name.txt", &why) == 0 && why == NULL);
    fd->name[0] = 0; CHECK(!fd_activate(fd) && strstr(fd->msg, "name") && fd->state == FD_RUNNING);
    type(fd, "bad/name"); CHECK(!fd_activate(fd) && strstr(fd->msg, "slash"));
    for (int i = 0; i < 8; i++) fd_backspace(fd);
    CHECK(fd->name[0] == 0);
    type(fd, "New Note.txt");
    CHECK(fd_activate(fd) && fd->state == FD_ACCEPTED);
    snprintf(p, sizeof p, "%s/New Note.txt", docs); CHECK(!strcmp(fd->result, p));
    CHECK(access(p, F_OK) != 0);                                           /* the dialog never writes the file */
    /* overwrite confirmation: choosing an existing file asks first */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, docs, NULL) == 0);
    fd_select(fd, index_of(fd, "a.mov"));
    CHECK(!strcmp(fd->name, "a.mov"));                                     /* picking a file fills the name */
    CHECK(fd_activate(fd) && fd->confirm && fd->state == FD_RUNNING && strstr(fd->msg, "Replace"));
    CHECK(fd_key(fd, 27, 0) && !fd->confirm && fd->state == FD_RUNNING);   /* Escape answers "no" first */
    CHECK(fd_activate(fd) && fd->confirm);
    type(fd, "2"); CHECK(!fd->confirm);                                    /* editing the name drops the question */
    CHECK(fd_activate(fd) && fd->state == FD_ACCEPTED);                    /* a2.mov is new */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, docs, "a.mov") == 0);
    CHECK(fd_key(fd, '\r', 0) && fd->confirm);
    CHECK(fd_key(fd, FINDER_KEY_DOWN, 0) && fd->confirm);                  /* other keys do nothing while asking */
    CHECK(fd_key(fd, '\r', 0) && fd->state == FD_ACCEPTED);
    snprintf(p, sizeof p, "%s/a.mov", docs); CHECK(!strcmp(fd->result, p));
    /* a folder, or an alias, with that name is refused outright */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, vol, "Docs") == 0);
    CHECK(!fd_activate(fd) && !fd->confirm && fd->state == FD_RUNNING && strstr(fd->msg, "folder"));
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, vol, "esc.mov") == 0);
    CHECK(!fd_activate(fd) && fd->state == FD_RUNNING && strstr(fd->msg, "alias"));
    /* Return with an empty name and a folder selected enters it */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, vol, NULL) == 0);
    fd_select(fd, index_of(fd, "Docs")); CHECK(fd_activate(fd) && !strcmp(fd->cwd, docs));
    /* read-only folder */
    mk(vol, "ro"); snprintf(p, sizeof p, "%s/ro", vol); chmod(p, 0555);
    if (!root_user) {
        CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, p, "x.txt") == 0);
        CHECK(!fd_activate(fd) && fd->state == FD_RUNNING && strstr(fd->msg, "read-only"));
    }
    chmod(p, 0755);
    /* Save cancel */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, docs, "q.txt") == 0);
    fd_cancel(fd); CHECK(fd->state == FD_CANCELLED && fd->result[0] == 0);

    /* painting and pointer: headless framebuffer */
    enum { W = 400, H = 300 };
    static uint32_t px[W * H];
    Fb fb = {.px = px, .w = W, .h = H};
    Rect area = {10, 10, 360, 220};
    CHECK(fd_open(fd, FD_OPEN, "Open", roots, 1, vol, NULL) == 0);
    fd_set_filter(fd, movie_only, "Movies");
    fd_paint(&fb, fd, area);
    int ink = 0; for (int i = 0; i < W * H; i++) if (px[i] != 0 && px[i] != C_MENU) ink++;
    CHECK(ink > 500 && fd->rows >= 1);
    int ok_x = area.x + area.w - 8 - 76 + 5, btn_y = area.y + area.h - 28 + 5;
    CHECK(!fd_click(fd, area, 500, 500));                                  /* outside the dialog */
    CHECK(fd_click(fd, area, ok_x, btn_y) && fd->state == FD_RUNNING);     /* Open with nothing chosen */
    CHECK(fd_click(fd, area, area.x + 30, area.y + 48 + 5) && fd->sel == 0);   /* first row selects */
    CHECK(fd_click(fd, area, area.x + 30, area.y + 48 + 5));                   /* second click enters the folder */
    CHECK(strcmp(fd->cwd, vol) != 0);
    CHECK(fd_click(fd, area, area.x + 8 + 5, area.y + 6 + 5) && !strcmp(fd->cwd, vol));   /* Up button */
    CHECK(fd_click(fd, area, area.x + 8 + 5, area.y + 6 + 5) == 1 && !strcmp(fd->cwd, vol));
    fd_wheel(fd, 3); fd_wheel(fd, -9); CHECK(fd->top >= 0);
    fd_paint(&fb, fd, area);
    CHECK(fd_click(fd, area, ok_x - 76, btn_y) && fd->state == FD_CANCELLED);  /* Cancel */
    CHECK(!fd_click(fd, area, area.x + 30, area.y + 60));                      /* finished: ignores presses */
    /* Save-mode and confirm overlay paint without trouble */
    CHECK(fd_open(fd, FD_SAVE, "Save", roots, 1, docs, "a.mov") == 0);
    fd_paint(&fb, fd, area); CHECK(fd_activate(fd) && fd->confirm); fd_paint(&fb, fd, area);
    CHECK(fd_click(fd, area, area.x + 5, area.y + 5) && fd->confirm);          /* only the two buttons answer */
    free(fd);

    snprintf(p, sizeof p, "%s/locked", vol); chmod(p, 0755);
    snprintf(p, sizeof p, "rm -rf '%s'", top); if (system(p) != 0) fprintf(stderr, "cleanup failed\n");
    if (!failures) printf("file dialog: all checks passed\n");
    return failures ? 1 : 0;
}
