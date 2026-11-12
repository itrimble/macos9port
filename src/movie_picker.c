#include "movie_picker.h"
#include "file_dialog.h"
#include "movie_window.h"
#include "session_int.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <copyfile.h>
#endif

#define PICK_MAX 32
#define ROW_H 18

static char g_rel[PICK_MAX][OSPI_PATH];
static int g_count;
static char g_root[OSPI_PATH];
static FileDialog g_fd;   /* the in-window chooser; static because it holds a 512-entry listing */
static int g_browse;

int movie_picker_count(void) { return g_count; }
const char *movie_picker_rel(int i) { return i >= 0 && i < g_count ? g_rel[i] : ""; }

static void scan_dir(const char *rel, int depth) {
    char full[OSPI_PATH];
    if (volume_join(full, sizeof full, g_root, rel) != 0) return;
    DIR *d = opendir(full);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_count < PICK_MAX) {
        if (e->d_name[0] == '.') continue;
        char child[OSPI_PATH], cfull[OSPI_PATH];
        if (snprintf(child, sizeof child, "%s/%s", rel, e->d_name) >= (int)sizeof child) continue;
        if (volume_join(cfull, sizeof cfull, g_root, child) != 0) continue;
        struct stat st;
        if (lstat(cfull, &st) != 0) continue;
        if (S_ISREG(st.st_mode) && movie_is_name(e->d_name)) snprintf(g_rel[g_count++], OSPI_PATH, "%s", child);
        else if (S_ISDIR(st.st_mode) && depth > 0) scan_dir(child, depth - 1);
    }
    closedir(d);
}

void movie_picker_scan(Session *s) {
    g_count = 0;
    if (!s) return;
    snprintf(g_root, sizeof g_root, "%s", s->root);
    static const char *const dirs[] = {"Movies", "Documents", "Downloads", "Desktop"};
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) scan_dir(dirs[i], 2);
}

static const char *base_of(const char *rel) { const char *s = strrchr(rel, '/'); return s ? s + 1 : rel; }

static Rect button_rect(const Layout *lay) { return (Rect){lay->content.x + 12, lay->content.y + 34, 140, 22}; }
static int row_count(const Layout *lay) {
    int n = (lay->content.h - 96) / ROW_H;
    return n < 0 ? 0 : n > g_count ? g_count : n;
}
static Rect row_rect(const Layout *lay, int i) {
    return (Rect){lay->content.x + 12, lay->content.y + 84 + i * ROW_H, lay->content.w - 24, ROW_H};
}

void movie_picker_paint(Fb *fb, const Layout *lay) {
    if (g_browse) { fd_paint(fb, &g_fd, lay->content); return; }
    fb_text(fb, lay->content.x + 12, lay->content.y + 10, C_WHITE, "Choose a movie to play.");
    Rect b = button_rect(lay);
    fb_fill(fb, b.x, b.y, b.w, b.h, C_MENU);
    fb_frame(fb, b.x, b.y, b.w, b.h, C_WHITE);
    fb_text(fb, b.x + 8, b.y + 4, C_BLACK, "Choose Movie...");
    fb_text(fb, lay->content.x + 12, lay->content.y + 66, C_WHITE, g_count ? "Movies on this disk:" : "No movies on this disk yet.");
    for (int i = 0; i < row_count(lay); i++) {
        Rect r = row_rect(lay, i);
        fb_text(fb, r.x + 4, r.y + 2, C_WHITE, base_of(g_rel[i]));
    }
    fb_text(fb, lay->content.x + 12, lay->content.y + lay->content.h - 18, C_DISABLED,
            "Plays MOV, QT, MP4 and M4V.");
}

static int copy_file(const char *src, const char *dst) {
#ifdef __APPLE__
    if (copyfile(src, dst, NULL, COPYFILE_DATA | COPYFILE_CLONE | COPYFILE_EXCL) == 0) return 0;
    return -1;
#else
    FILE *in = fopen(src, "rb"), *out = in ? fopen(dst, "wbx") : NULL;
    if (!in || !out) { if (in) fclose(in); return -1; }
    char buf[65536]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    fclose(in); if (fclose(out)) rc = -1;
    return rc;
#endif
}

int movie_picker_import(const char *root, const char *host_path, char *rel, size_t n) {
    if (!root || !host_path || !*host_path || !movie_is_name(host_path)) return -1;
    char dir[OSPI_PATH];
    if (volume_join(dir, sizeof dir, root, "Movies") != 0) return -1;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    const char *base = base_of(host_path);
    char stem[OSPI_NAME], name[OSPI_NAME], full[OSPI_PATH];
    const char *dot = strrchr(base, '.');
    snprintf(stem, sizeof stem, "%.*s", (int)(dot - base), base);
    for (int i = 0; i < 100; i++) {
        if (i) snprintf(name, sizeof name, "%s %d%s", stem, i + 1, dot); else snprintf(name, sizeof name, "%s", base);
        if (snprintf(full, sizeof full, "%s/%s", dir, name) >= (int)sizeof full) return -1;
        if (copy_file(host_path, full) == 0) { snprintf(rel, n, "Movies/%s", name); return 0; }
        if (errno != EEXIST) return -1;
    }
    return -1;
}

static int movie_name_filter(const char *name) { return movie_is_name(name); }

/* Roots the Movie chooser may browse: the OS999 disk, plus the three home media
 * folders (the movie to play usually lives there). Not the whole home folder. */
static int browse_open(Session *s) {
    static char movies[OSPI_PATH], downloads[OSPI_PATH], desktop[OSPI_PATH];
    const char *home = getenv("HOME");
    FdRoot roots[4]; int n = 0;
    roots[n++] = (FdRoot){"OS999 Disk", s->root};
    if (home && *home) {
        snprintf(movies, sizeof movies, "%s/Movies", home);
        snprintf(downloads, sizeof downloads, "%s/Downloads", home);
        snprintf(desktop, sizeof desktop, "%s/Desktop", home);
        roots[n++] = (FdRoot){"Movies", movies};
        roots[n++] = (FdRoot){"Downloads", downloads};
        roots[n++] = (FdRoot){"Desktop", desktop};
    }
    if (fd_open(&g_fd, FD_OPEN, "Choose a Movie", roots, n, n > 1 ? movies : NULL, NULL) != 0) return -1;
    fd_set_filter(&g_fd, movie_name_filter, "Movies");
    g_browse = 1;
    return 0;
}

/* The pick is outside the volume (or already on it); copy it into <volume>/Movies and play it. */
static void browse_finish(Session *s) {
    char rel[OSPI_PATH];
    g_browse = 0;
    if (g_fd.state != FD_ACCEPTED) return;
    if (movie_picker_import(s->root, g_fd.result, rel, sizeof rel) != 0) {
        dialog_info(s, "Movie Player", "OS999 could not copy that movie into its Movies folder.");
        return;
    }
    movie_picker_scan(s);
    movie_window_open(s, base_of(rel), rel);
}

int movie_picker_key(Session *s, Window *w, int key, int mods) {
    if (!g_browse || !s || !w || w->kind != WIN_MOVIE || w->movie) return 0;
    int used = fd_key(&g_fd, key, mods);
    if (g_fd.state != FD_RUNNING) browse_finish(s);
    return used;
}

int movie_picker_wheel(Window *w, int delta) {
    if (!g_browse || !w || w->kind != WIN_MOVIE || w->movie) return 0;
    fd_wheel(&g_fd, delta);
    return 1;
}

int movie_picker_browsing(void) { return g_browse; }

int movie_picker_click(Session *s, Window *w, int x, int y) {
    if (!s || !w || w->kind != WIN_MOVIE || w->movie) return 0;
    Layout lay; window_layout(w, &lay);
    if (g_browse) {
        (void)fd_click(&g_fd, lay.content, x, y);
        if (g_fd.state != FD_RUNNING) browse_finish(s);
        return 1;
    }
    if (rect_has(button_rect(&lay), x, y)) {
        if (browse_open(s) != 0) dialog_info(s, "Movie Player", "There is no folder OS999 can browse for movies.");
        return 1;
    }
    for (int i = 0; i < row_count(&lay); i++)
        if (rect_has(row_rect(&lay, i), x, y)) { movie_window_open(s, base_of(g_rel[i]), g_rel[i]); return 1; }
    return 1;
}
