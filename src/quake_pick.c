/* The Quake III Arena "Choose Folder..." window: the shared in-OS999 file dialog plus Use This Folder and Cancel.
 * Nothing leaves the verified path: only a complete retail set or the 1.11 demo pak0.pk3 whose size and SHA-256
 * match is copied, and Quake starts only after that. */
#include "quake_pick.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIN_W = 540, WIN_H = 380, BTN_H = 22 };
#define PANEL_REL ":quakepick"

static FileDialog g_fd;   /* static: it holds a 512-entry listing */
static char g_paths[4][OSPI_PATH];

int quake_pick_roots(const char *volume, const char *host_home, FdRoot *roots, char paths[][OSPI_PATH]) {
    int n = 0;
    if (volume && *volume) roots[n++] = (FdRoot){"OS999 Disk", volume};
    static const char *const sub[] = {"Downloads", "Desktop", "Documents"};
    for (int i = 0; host_home && *host_home && i < 3; i++) {
        snprintf(paths[i], OSPI_PATH, "%s/%s", host_home, sub[i]);
        roots[n++] = (FdRoot){sub[i], paths[i]};
    }
    return n;
}

int quake_pick_commit(FileDialog *fd, const char *home, const QuakePak *table, int n,
                      off_t demo_size, const char *demo_sha) {
    if (!fd || !fd->cwd[0] || !home || !home[0]) return -1;
    int rc = table ? quake_data_import_folder(home, fd->cwd, table, n, demo_size, demo_sha)
                   : quake_data_import_folder_real(home, fd->cwd);   /* NULL table: the built-in size and hash tables */
    if (rc < 0)
        snprintf(fd->msg, sizeof fd->msg, "No unmodified Quake III files here. Need pak0 to pak8.pk3, or the demo pak0.pk3.");
    else
        fd->msg[0] = 0;
    return rc;
}

static int is_panel(const Window *w) { return w && w->kind == WIN_TEXT && !strcmp(w->rel, PANEL_REL); }
static Rect folder_btn(const Layout *l) { return (Rect){l->content.x + 12, l->content.y + l->content.h - 28, 150, BTN_H}; }
static Rect cancel_btn(const Layout *l) { return (Rect){l->content.x + 170, l->content.y + l->content.h - 28, 80, BTN_H}; }
static Rect fd_area(const Layout *l) { return (Rect){l->content.x, l->content.y, l->content.w, l->content.h - 34}; }

int quake_pick_open(Session *s) {
    static const char *const title = "Choose Quake III Arena Folder";
    if (!s) return -1;
    FdRoot roots[4];
    const char *host = getenv("HOME");
    int n = quake_pick_roots(s->root, host, roots, g_paths);
    if (fd_open(&g_fd, FD_OPEN, title, roots, n, n > 1 ? roots[1].path : NULL, NULL) != 0) {
        dialog_info(s, "Quake III Arena", "There is no folder OS999 can browse.");
        return -1;
    }
    if (session_open_text(s, title, PANEL_REL, "")) {
        dialog_info(s, "Quake III Arena", "Close a window first.");
        return -1;
    }
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (is_panel(w)) {
            w->w = WIN_W; w->h = WIN_H;
            w->x = (s->sw - w->w) / 2; w->y = (s->sh - w->h) / 2;
            w->shade_h = w->h;
        }
    }
    return 0;
}

static void button(Fb *fb, Rect r, const char *label) {
    fb_fill(fb, r.x, r.y, r.w, r.h, C_MENU);
    fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
    fb_text(fb, r.x + (r.w - fb_text_width(label)) / 2, r.y + 4, C_BLACK, label);
}

int quake_pick_panel_paint(Fb *fb, const Window *w, const Layout *lay) {
    if (!is_panel(w)) return 0;
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, C_WHITE);
    fd_paint(fb, &g_fd, fd_area(lay));
    button(fb, folder_btn(lay), "Use This Folder");
    button(fb, cancel_btn(lay), "Cancel");
    return 1;
}

static void close_panel(Session *s) { session_command(s, CMD_CLOSE); }

/* Use This Folder: verify what the folder holds, install it, and start Quake. A refusal keeps the window open. */
static void use_folder(Session *s) {
    char own[OSPI_PATH + 32];
    if (os999_private_dir(NULL, own, sizeof own)) { snprintf(g_fd.msg, sizeof g_fd.msg, "OS999 cannot reach its private folder."); return; }
    if (quake_pick_commit(&g_fd, own, NULL, 0, 0, NULL) < 0) return;   /* refused: the message shows, the window stays */
    close_panel(s);
    session_open_quake(s);   /* the full game when its set is installed, else the verified demo */
}

int quake_pick_panel_click(Session *s, Window *w, int x, int y) {
    if (!is_panel(w) || !s) return 0;
    Layout lay; window_layout(w, &lay);
    if (rect_has(folder_btn(&lay), x, y)) { use_folder(s); return 1; }
    if (rect_has(cancel_btn(&lay), x, y)) { close_panel(s); return 1; }
    (void)fd_click(&g_fd, fd_area(&lay), x, y);
    if (g_fd.state != FD_RUNNING) close_panel(s);   /* Cancel inside the dialog, or a file was opened: nothing to use */
    return 1;
}

int quake_pick_panel_key(Session *s, Window *w, int key, int mods) {
    if (!is_panel(w)) return 0;
    int used = fd_key(&g_fd, key, mods);
    if (g_fd.state != FD_RUNNING) close_panel(s);
    return used;
}

int quake_pick_panel_wheel(Window *w, int delta) {
    if (!is_panel(w)) return 0;
    fd_wheel(&g_fd, delta);
    return 1;
}
