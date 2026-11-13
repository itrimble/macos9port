/* The Install Classic Apps dialog: a list of catalogue items with checkboxes, source and size, a button
 * to install from a folder or archive the user already has (the in-OS999 file dialog), and a worker thread
 * so the desktop stays alive while a download runs. Mouse and keyboard only; nothing here runs software. */
#include "classic_apps.h"
#include "file_dialog.h"
#include "session_int.h"
#include "volume_capacity.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { ROW_H = 34, BTN_H = 22, WIN_W = 540, WIN_H = 400, ST_NONE = 0, ST_DONE, ST_FAILED, ST_SKIPPED };
enum { RUN_IDLE = 0, RUN_BUSY = 1 };

static ClassicCatalog g_cat;
static int g_loaded, g_tick[CLASSIC_MAX_APPS], g_status[CLASSIC_MAX_APPS];
static char g_root[OSPI_PATH], g_item_msg[CLASSIC_MAX_APPS][140], g_stage[96], g_summary[300];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_run;
static atomic_bool g_cancel;
static pthread_t g_thread;
static int g_have_thread;
static FileDialog g_fd;
static int g_browse;

typedef struct { int idx[CLASSIC_MAX_APPS]; int n; char local[OSPI_PATH]; } Job;
static Job g_job;

static void set_stage(void *ctx, const char *stage) {
    (void)ctx;
    pthread_mutex_lock(&g_lock);
    snprintf(g_stage, sizeof g_stage, "%s", stage);
    pthread_mutex_unlock(&g_lock);
}

static void *worker(void *arg) {
    Job *job = arg;
    ClassicHooks hooks = {.progress = set_stage, .cancel = &g_cancel};
    char rel[OSPI_PATH], err[240];
    int ok = 0, bad = 0;
    for (int k = 0; k < (job->local[0] ? 1 : job->n); k++) {
        int i = job->local[0] ? -1 : job->idx[k];
        err[0] = 0;
        ClassicResult r = job->local[0] ? classic_install_local(g_root, job->local, &hooks, rel, sizeof rel, err, sizeof err)
                                        : classic_install(g_root, &g_cat.app[i], &hooks, rel, sizeof rel, err, sizeof err);
        pthread_mutex_lock(&g_lock);
        if (i >= 0) {
            g_status[i] = r == CA_OK || r == CA_ALREADY ? ST_DONE : ST_FAILED;
            snprintf(g_item_msg[i], sizeof g_item_msg[i], "%s", r == CA_OK ? "Installed." : err);
        }
        if (r == CA_OK) ok++; else if (r != CA_ALREADY) bad++;
        if (r == CA_OK && !job->local[0]) snprintf(g_stage, sizeof g_stage, "Installed into %.60s", rel);
        if (r != CA_OK) snprintf(g_summary, sizeof g_summary, "%s", err);
        pthread_mutex_unlock(&g_lock);
        if (r == CA_CANCELLED) break;
    }
    pthread_mutex_lock(&g_lock);
    if (!bad) snprintf(g_summary, sizeof g_summary, "%s", ok ? "Done. The files are in your volume and were not run." : "Nothing new to install.");
    g_stage[0] = 0;
    pthread_mutex_unlock(&g_lock);
    volume_space_invalidate();
    atomic_store(&g_run, RUN_IDLE);
    return NULL;
}

static void start_job(void) {
    if (g_have_thread) { pthread_join(g_thread, NULL); g_have_thread = 0; }
    atomic_store(&g_cancel, 0);
    atomic_store(&g_run, RUN_BUSY);
    pthread_mutex_lock(&g_lock);
    g_summary[0] = 0;
    snprintf(g_stage, sizeof g_stage, "Starting");
    pthread_mutex_unlock(&g_lock);
    if (pthread_create(&g_thread, NULL, worker, &g_job) != 0) {
        atomic_store(&g_run, RUN_IDLE);
        snprintf(g_summary, sizeof g_summary, "Could not start the installer.");
    } else {
        g_have_thread = 1;
    }
}

static void refresh_installed(void) {
    for (int i = 0; i < g_cat.n; i++)
        if (g_status[i] != ST_FAILED || classic_installed(g_root, &g_cat.app[i]))
            g_status[i] = classic_installed(g_root, &g_cat.app[i]) ? ST_DONE : ST_NONE;
}

int classic_apps_open(Session *s) {
    if (!s) return -1;
    if (atomic_load(&g_run) == RUN_IDLE) {
        snprintf(g_root, sizeof g_root, "%s", s->root);
        if (!g_loaded) {
            if (classic_catalog_default(&g_cat) <= 0) { dialog_info(s, "Install Classic Apps", "The built-in catalogue could not be read."); return -1; }
            g_loaded = 1;
        }
        memset(g_tick, 0, sizeof g_tick);
        for (int i = 0; i < g_cat.n; i++) { g_tick[i] = g_cat.app[i].default_on && g_cat.app[i].page[0]; g_item_msg[i][0] = 0; g_status[i] = ST_NONE; }
        refresh_installed();
        g_browse = 0;
        g_summary[0] = 0;
    }
    if (session_open_text(s, "Install Classic Apps", ":classicapps", "")) {
        dialog_info(s, "Install Classic Apps", "Close a window first.");
        return -1;
    }
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (w->kind == WIN_TEXT && !strcmp(w->rel, ":classicapps")) {
            w->w = WIN_W; w->h = WIN_H;
            w->x = (s->sw - w->w) / 2; w->y = (s->sh - w->h) / 2;
            w->shade_h = w->h;
        }
    }
    return 0;
}

int classic_apps_first_run(Session *s) {
    char dir[OSPI_PATH + 32], flag[OSPI_PATH + 64];
    if (!s) return 0;
    os999_private_set_root(s->root);
    if (os999_private_dir(NULL, dir, sizeof dir) || snprintf(flag, sizeof flag, "%s/classic-apps-offered", dir) >= (int)sizeof flag) return 0;
    struct stat st;
    if (!lstat(flag, &st)) return 0;
    FILE *f = fopen(flag, "w");   /* offer once, whatever the answer */
    if (!f) return 0;
    fputs("offered\n", f);
    fclose(f);
    return classic_apps_open(s) == 0;
}

/* ---- drawing ---- */

static int is_panel(const Window *w) { return w && w->kind == WIN_TEXT && !strcmp(w->rel, ":classicapps"); }
static Rect rrect(const Layout *l, int x, int y, int w) { return (Rect){l->content.x + x, l->content.y + y, w, BTN_H}; }
static Rect install_btn(const Layout *l) { return rrect(l, 12, l->content.h - 32, 130); }
static Rect local_btn(const Layout *l) { return rrect(l, 150, l->content.h - 32, 190); }
static Rect close_btn(const Layout *l) { return rrect(l, l->content.w - 92, l->content.h - 32, 80); }
static Rect folder_btn(const Layout *l) { return rrect(l, 12, l->content.h - 28, 150); }
static Rect browse_cancel_btn(const Layout *l) { return rrect(l, 170, l->content.h - 28, 80); }
static Rect fd_area(const Layout *l) { return (Rect){l->content.x, l->content.y, l->content.w, l->content.h - 34}; }
static Rect row_box(const Layout *l, int i) { return (Rect){l->content.x + 10, l->content.y + 74 + i * ROW_H, l->content.w - 20, ROW_H - 2}; }

static void button(Fb *fb, Rect r, const char *label, int enabled) {
    fb_fill(fb, r.x, r.y, r.w, r.h, enabled ? C_MENU : 0xFFD8D8D8u);
    fb_frame(fb, r.x, r.y, r.w, r.h, enabled ? C_BLACK : C_GRAY);
    fb_text(fb, r.x + (r.w - fb_text_width(label)) / 2, r.y + 4, enabled ? C_BLACK : 0xFF707070u, label);
}

static void text_fit(Fb *fb, int x, int y, int max_w, uint32_t c, const char *s) {
    int n = (int)strlen(s);
    while (n > 0 && fb_text_width_n(s, n) > max_w) n--;
    fb_text_n(fb, x, y, c, s, n);
}

int classic_apps_panel_paint(Fb *fb, const Window *w, const Layout *lay) {
    if (!is_panel(w)) return 0;
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, C_WHITE);
    if (g_browse) {
        fd_paint(fb, &g_fd, fd_area(lay));
        button(fb, folder_btn(lay), "Use This Folder", 1);
        button(fb, browse_cancel_btn(lay), "Cancel", 1);
        return 1;
    }
    int x = lay->content.x + 12, y = lay->content.y + 8, tw = lay->content.w - 24;
    static const char *intro[] = {
        "You are fetching third-party software into your own volume.",
        "Some of it is Apple's. OS999 does not include it: each item is",
        "downloaded from Macintosh Garden (or taken from a folder or",
        "archive you choose), checked, unpacked, and never run."};
    for (int i = 0; i < 4; i++) fb_text(fb, x, y + i * 13, C_BLACK, intro[i]);
    fb_fill(fb, lay->content.x + 10, lay->content.y + 66, lay->content.w - 20, 1, C_GRAY);
    pthread_mutex_lock(&g_lock);
    int busy = atomic_load(&g_run) == RUN_BUSY;
    for (int i = 0; i < g_cat.n; i++) {
        const ClassicApp *a = &g_cat.app[i];
        Rect r = row_box(lay, i);
        if (r.y + r.h > lay->content.y + lay->content.h - 64) break;
        int can = a->page[0] && g_status[i] != ST_DONE;
        fb_frame(fb, r.x, r.y + 2, 12, 12, can ? C_BLACK : C_GRAY);
        if (g_tick[i] && can) { fb_fill(fb, r.x + 3, r.y + 5, 6, 6, C_BLACK); }
        char head[160];
        if (a->size_mb[0]) snprintf(head, sizeof head, "%s   %s MB", a->name, a->size_mb);
        else snprintf(head, sizeof head, "%s%s", a->name, a->page[0] ? "" : "   (from a folder or archive)");
        text_fit(fb, r.x + 20, r.y + 2, r.w - 20, can ? C_BLACK : 0xFF606060u, head);
        const char *line = g_item_msg[i][0] ? g_item_msg[i] : g_status[i] == ST_DONE ? "Installed." : a->note;
        uint32_t col = g_status[i] == ST_FAILED ? 0xFF9C1C1Cu : 0xFF505050u;
        text_fit(fb, r.x + 20, r.y + 16, r.w - 20, col, line);
    }
    int by = lay->content.h - 52;
    if (busy) text_fit(fb, x, lay->content.y + by, tw, C_BLACK, g_stage);
    else if (g_summary[0]) text_fit(fb, x, lay->content.y + by, tw, C_BLACK, g_summary);
    pthread_mutex_unlock(&g_lock);
    button(fb, install_btn(lay), busy ? "Stop" : "Install Selected", 1);
    button(fb, local_btn(lay), "From Folder or Archive...", !busy);
    button(fb, close_btn(lay), "Close", !busy);
    return 1;
}

/* ---- input ---- */

static int ends_with_archive(const char *name) { return install_archive_supported(name); }

static void browse_open(Session *s) {
    static char down[OSPI_PATH], desk[OSPI_PATH], docs[OSPI_PATH];
    const char *home = getenv("HOME");
    FdRoot roots[4];
    int n = 0;
    roots[n++] = (FdRoot){"OS999 Disk", s->root};
    if (home && *home) {
        snprintf(down, sizeof down, "%s/Downloads", home);
        snprintf(desk, sizeof desk, "%s/Desktop", home);
        snprintf(docs, sizeof docs, "%s/Documents", home);
        roots[n++] = (FdRoot){"Downloads", down};
        roots[n++] = (FdRoot){"Desktop", desk};
        roots[n++] = (FdRoot){"Documents", docs};
    }
    if (fd_open(&g_fd, FD_OPEN, "Choose an archive, or open a folder and press Use This Folder", roots, n, n > 1 ? down : NULL, NULL) != 0) {
        dialog_info(s, "Install Classic Apps", "There is no folder OS999 can browse.");
        return;
    }
    fd_set_filter(&g_fd, ends_with_archive, "Archives");
    g_browse = 1;
}

static void browse_finish(int accepted) {
    g_browse = 0;
    if (!accepted || !g_fd.result[0] || atomic_load(&g_run) == RUN_BUSY) return;
    memset(&g_job, 0, sizeof g_job);
    snprintf(g_job.local, sizeof g_job.local, "%s", g_fd.result);
    start_job();
}

int classic_apps_panel_click(Session *s, Window *w, int x, int y) {
    if (!is_panel(w) || !s) return 0;
    Layout lay;
    window_layout(w, &lay);
    if (g_browse) {
        if (rect_has(folder_btn(&lay), x, y)) { snprintf(g_fd.result, sizeof g_fd.result, "%s", g_fd.cwd); browse_finish(1); return 1; }
        if (rect_has(browse_cancel_btn(&lay), x, y)) { browse_finish(0); return 1; }
        (void)fd_click(&g_fd, fd_area(&lay), x, y);
        if (g_fd.state != FD_RUNNING) browse_finish(g_fd.state == FD_ACCEPTED);
        return 1;
    }
    int busy = atomic_load(&g_run) == RUN_BUSY;
    if (rect_has(install_btn(&lay), x, y)) {
        if (busy) { atomic_store(&g_cancel, 1); return 1; }
        memset(&g_job, 0, sizeof g_job);
        for (int i = 0; i < g_cat.n; i++)
            if (g_tick[i] && g_cat.app[i].page[0] && g_status[i] != ST_DONE) g_job.idx[g_job.n++] = i;
        if (!g_job.n) { snprintf(g_summary, sizeof g_summary, "Tick at least one item first."); return 1; }
        start_job();
        return 1;
    }
    if (busy) return 1;
    if (rect_has(local_btn(&lay), x, y)) { browse_open(s); return 1; }
    if (rect_has(close_btn(&lay), x, y)) { session_command(s, CMD_CLOSE); return 1; }
    for (int i = 0; i < g_cat.n; i++)
        if (rect_has(row_box(&lay, i), x, y) && g_cat.app[i].page[0] && g_status[i] != ST_DONE) { g_tick[i] = !g_tick[i]; return 1; }
    return 1;
}

int classic_apps_panel_key(Session *s, Window *w, int key, int mods) {
    (void)s;
    if (!is_panel(w) || !g_browse) return 0;
    int used = fd_key(&g_fd, key, mods);
    if (g_fd.state != FD_RUNNING) browse_finish(g_fd.state == FD_ACCEPTED);
    return used;
}

int classic_apps_panel_wheel(Window *w, int delta) {
    if (!is_panel(w) || !g_browse) return 0;
    fd_wheel(&g_fd, delta);
    return 1;
}
