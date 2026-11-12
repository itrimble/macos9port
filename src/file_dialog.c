#include "file_dialog.h"
#include "finder_edit.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define NAME_MAX_CHARS 63

static void say(FileDialog *fd, const char *fmt, const char *a) {
    snprintf(fd->msg, sizeof fd->msg, fmt, a ? a : "");
}

/* Longest matching root, component-aware; -1 when outside every root. */
static int contained_in(const FileDialog *fd, const char *p) {
    int best = -1; size_t best_len = 0;
    if (!fd || !p || p[0] != '/') return -1;
    for (int i = 0; i < fd->nroots; i++) {
        size_t rl = strlen(fd->root_path[i]);
        if (strncmp(p, fd->root_path[i], rl) != 0) continue;
        if (p[rl] != 0 && p[rl] != '/') continue;
        if (best < 0 || rl > best_len) { best = i; best_len = rl; }
    }
    return best;
}

int fd_contains(const FileDialog *fd, const char *real_path) { return contained_in(fd, real_path); }

static int ent_cmp(const void *a, const void *b) {
    const FdEntry *x = a, *y = b;
    if (x->dir != y->dir) return (int)y->dir - (int)x->dir;
    return strcasecmp(x->name, y->name);
}

static int join(char *out, size_t n, const char *dir, const char *name) {
    int k = snprintf(out, n, "%s/%s", dir, name);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

/* Reads `dir` into fd->ent. Leaves fd untouched when the folder cannot be opened. */
static int load(FileDialog *fd, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    fd->nent = 0; fd->sel = -1; fd->top = 0; fd->truncated = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= OSPI_NAME) continue;
        char full[OSPI_PATH], real[PATH_MAX];
        struct stat st, ls;
        if (join(full, sizeof full, dir, e->d_name) != 0 || lstat(full, &ls) != 0) continue;
        if (S_ISLNK(ls.st_mode)) {   /* an alias is listed only when its target is inside the roots */
            if (!realpath(full, real) || contained_in(fd, real) < 0) continue;
        }
        if (stat(full, &st) != 0) continue;
        int isdir = S_ISDIR(st.st_mode);
        if (!isdir && !S_ISREG(st.st_mode)) continue;
        if (!isdir && fd->filter && fd->filter_on && !fd->filter(e->d_name)) continue;
        if (fd->nent >= FD_ENT_MAX) { fd->truncated = 1; break; }
        snprintf(fd->ent[fd->nent].name, OSPI_NAME, "%s", e->d_name);
        fd->ent[fd->nent++].dir = (unsigned char)isdir;
    }
    closedir(d);
    qsort(fd->ent, (size_t)fd->nent, sizeof *fd->ent, ent_cmp);
    snprintf(fd->cwd, sizeof fd->cwd, "%s", dir);
    fd->confirm = 0;
    return 0;
}

int fd_goto(FileDialog *fd, const char *path) {
    char cand[OSPI_PATH], real[PATH_MAX];
    struct stat st;
    if (!fd || !path || !*path) return -1;
    if (path[0] == '/') snprintf(cand, sizeof cand, "%s", path);
    else if (join(cand, sizeof cand, fd->cwd, path) != 0) { say(fd, "That path is too long.", NULL); return -1; }
    if (!realpath(cand, real)) { say(fd, "That folder is not available.", NULL); return -1; }
    int root = contained_in(fd, real);
    if (root < 0) { say(fd, "That is outside the folders this window may browse.", NULL); return -1; }
    if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode)) { say(fd, "That is not a folder.", NULL); return -1; }
    if (strlen(real) >= OSPI_PATH || load(fd, real) != 0) {
        say(fd, "OS999 could not open that folder (no permission).", NULL);
        return -1;
    }
    fd->root = root; fd->msg[0] = 0;
    return 0;
}

int fd_select_root(FileDialog *fd, int root) {
    if (!fd || root < 0 || root >= fd->nroots) return -1;
    return fd_goto(fd, fd->root_path[root]);
}

int fd_up(FileDialog *fd) {
    if (!fd) return -1;
    if (strcmp(fd->cwd, fd->root_path[fd->root]) == 0) { say(fd, "This is the top of %s.", fd->root_label[fd->root]); return -1; }
    char parent[OSPI_PATH]; snprintf(parent, sizeof parent, "%s", fd->cwd);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent) return -1;
    *slash = 0;
    return fd_goto(fd, parent);
}

void fd_set_filter(FileDialog *fd, FdFilter filter, const char *label) {
    if (!fd) return;
    fd->filter = filter; fd->filter_on = filter != NULL;
    snprintf(fd->filter_label, sizeof fd->filter_label, "%s", label ? label : "");
    if (fd->cwd[0]) { char keep[OSPI_PATH]; snprintf(keep, sizeof keep, "%s", fd->cwd); (void)load(fd, keep); }
}

void fd_toggle_filter(FileDialog *fd) {
    if (!fd || !fd->filter) return;
    fd->filter_on = !fd->filter_on;
    char keep[OSPI_PATH]; snprintf(keep, sizeof keep, "%s", fd->cwd);
    (void)load(fd, keep);
}

int fd_open(FileDialog *fd, FdMode mode, const char *title, const FdRoot *roots, int nroots,
            const char *start, const char *default_name) {
    if (!fd) return -1;
    memset(fd, 0, sizeof *fd);
    fd->mode = mode; fd->sel = -1; fd->rows = 8;
    snprintf(fd->title, sizeof fd->title, "%s", title ? title : (mode == FD_SAVE ? "Save" : "Open"));
    for (int i = 0; roots && i < nroots && fd->nroots < FD_ROOTS; i++) {
        char real[PATH_MAX]; struct stat st;
        if (!roots[i].path || !realpath(roots[i].path, real) || strcmp(real, "/") == 0) continue;
        if (stat(real, &st) != 0 || !S_ISDIR(st.st_mode) || strlen(real) >= OSPI_PATH) continue;
        snprintf(fd->root_path[fd->nroots], OSPI_PATH, "%s", real);
        snprintf(fd->root_label[fd->nroots], FD_LABEL, "%s", roots[i].label ? roots[i].label : "Folder");
        fd->nroots++;
    }
    if (!fd->nroots) { snprintf(fd->msg, sizeof fd->msg, "No folders are available to browse."); fd->state = FD_CANCELLED; return -1; }
    if (default_name && mode == FD_SAVE) snprintf(fd->name, sizeof fd->name, "%.*s", NAME_MAX_CHARS, default_name);
    if (!start || fd_goto(fd, start) != 0) {
        for (int i = 0; i < fd->nroots; i++) if (fd_select_root(fd, i) == 0) return 0;
        fd->state = FD_CANCELLED;
        return -1;
    }
    return 0;
}

void fd_select(FileDialog *fd, int index) {
    if (!fd || fd->nent == 0) return;
    if (index < 0) index = 0;
    if (index >= fd->nent) index = fd->nent - 1;
    fd->sel = index;
    int rows = fd->rows > 0 ? fd->rows : 8;
    if (index < fd->top) fd->top = index;
    if (index >= fd->top + rows) fd->top = index - rows + 1;
    if (fd->mode == FD_SAVE && !fd->ent[index].dir) { snprintf(fd->name, sizeof fd->name, "%s", fd->ent[index].name); fd->confirm = 0; }
}

void fd_move(FileDialog *fd, int delta) {
    if (!fd || fd->nent == 0) return;
    fd_select(fd, fd->sel < 0 ? (delta > 0 ? 0 : fd->nent - 1) : fd->sel + delta);
}

int fd_name_check(const char *name, const char **why) {
    const char *w = NULL;
    size_t n = name ? strlen(name) : 0;
    if (n == 0) w = "Type a name first.";
    else if (n > NAME_MAX_CHARS) w = "That name is too long (63 characters at most).";
    else if (!strcmp(name, ".") || !strcmp(name, "..") || name[0] == '.') w = "A name cannot start with a period.";
    else for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127) { w = "That name has a character that is not allowed."; break; }
        if (c == '/' || c == ':') { w = "A name cannot contain a slash or a colon."; break; }
    }
    if (why) *why = w;
    return w ? -1 : 0;
}

void fd_type(FileDialog *fd, const char *utf8) {
    if (!fd || fd->mode != FD_SAVE || !utf8) return;
    size_t n = strlen(fd->name);
    for (; *utf8 && n < NAME_MAX_CHARS + 1 && n + 1 < sizeof fd->name; utf8++) {
        unsigned char c = (unsigned char)*utf8;
        if (c < 32 || c >= 127) continue;
        fd->name[n++] = (char)c; fd->name[n] = 0;
    }
    fd->confirm = 0; fd->msg[0] = 0;
}

void fd_backspace(FileDialog *fd) {
    if (!fd || fd->mode != FD_SAVE) return;
    size_t n = strlen(fd->name);
    if (n) fd->name[n - 1] = 0;
    fd->confirm = 0; fd->msg[0] = 0;
}

static int accept_open(FileDialog *fd, const FdEntry *e) {
    char full[OSPI_PATH], real[PATH_MAX]; struct stat st;
    if (join(full, sizeof full, fd->cwd, e->name) != 0 || !realpath(full, real)) { say(fd, "%s is not available any more.", e->name); return 0; }
    if (contained_in(fd, real) < 0) { say(fd, "%s leads outside the folders this window may browse.", e->name); return 0; }
    if (stat(real, &st) != 0 || !S_ISREG(st.st_mode)) { say(fd, "%s is not a file.", e->name); return 0; }
    if (fd->filter && fd->filter_on && !fd->filter(e->name)) { say(fd, "%s is not a type this window can use.", e->name); return 0; }
    if (strlen(real) >= sizeof fd->result) { say(fd, "That path is too long.", NULL); return 0; }
    snprintf(fd->result, sizeof fd->result, "%s", real);
    fd->state = FD_ACCEPTED;
    return 1;
}

static int accept_save(FileDialog *fd) {
    const char *why = NULL; char full[OSPI_PATH]; struct stat st;
    if (fd_name_check(fd->name, &why) != 0) { say(fd, "%s", why); return 0; }
    if (join(full, sizeof full, fd->cwd, fd->name) != 0) { say(fd, "That path is too long.", NULL); return 0; }
    if (access(fd->cwd, W_OK) != 0) { say(fd, "This folder is read-only; choose another.", NULL); return 0; }
    if (lstat(full, &st) != 0) {
        if (errno != ENOENT) { say(fd, "OS999 cannot check that name (%s).", strerror(errno)); return 0; }
    } else if (S_ISLNK(st.st_mode)) { say(fd, "%s is an alias; pick a different name.", fd->name); return 0; }
    else if (S_ISDIR(st.st_mode)) { say(fd, "A folder named %s already exists.", fd->name); return 0; }
    else if (!S_ISREG(st.st_mode)) { say(fd, "%s cannot be replaced.", fd->name); return 0; }
    else if (!fd->confirm) { fd->confirm = 1; say(fd, "%s already exists. Replace it?", fd->name); return 1; }
    snprintf(fd->result, sizeof fd->result, "%s", full);
    fd->state = FD_ACCEPTED; fd->msg[0] = 0;
    return 1;
}

int fd_activate(FileDialog *fd) {
    if (!fd || fd->state != FD_RUNNING) return 0;
    if (fd->confirm) return accept_save(fd);
    fd->msg[0] = 0;
    const FdEntry *e = fd->sel >= 0 && fd->sel < fd->nent ? &fd->ent[fd->sel] : NULL;
    char full[OSPI_PATH];
    if (fd->mode == FD_OPEN) {
        if (!e) { say(fd, "Choose a file or folder first.", NULL); return 0; }
        if (e->dir) return join(full, sizeof full, fd->cwd, e->name) == 0 && fd_goto(fd, full) == 0;
        return accept_open(fd, e);
    }
    if (!fd->name[0] && e && e->dir) return join(full, sizeof full, fd->cwd, e->name) == 0 && fd_goto(fd, full) == 0;
    return accept_save(fd);
}

void fd_cancel(FileDialog *fd) {
    if (!fd || fd->state != FD_RUNNING) return;
    if (fd->confirm) { fd->confirm = 0; fd->msg[0] = 0; return; }
    fd->state = FD_CANCELLED; fd->result[0] = 0;
}

static void type_ahead(FileDialog *fd, int ch) {
    for (int k = 1; k <= fd->nent; k++) {
        int i = ((fd->sel < 0 ? -1 : fd->sel) + k) % fd->nent;
        if (tolower((unsigned char)fd->ent[i].name[0]) == tolower(ch)) { fd_select(fd, i); return; }
    }
}

int fd_key(FileDialog *fd, int key, int mods) {
    if (!fd || fd->state != FD_RUNNING) return 0;
    if (key == 27) { fd_cancel(fd); return 1; }
    if (key == '\r' || key == '\n') { fd_activate(fd); return 1; }
    if (fd->confirm) return 1;   /* only Return or Escape answer the question */
    if (key == FINDER_KEY_DOWN && (mods & MOD_CMD)) { if (fd->sel >= 0 && fd->ent[fd->sel].dir) fd_activate(fd); return 1; }
    if (key == FINDER_KEY_UP && (mods & MOD_CMD)) { (void)fd_up(fd); return 1; }
    if (key == FINDER_KEY_DOWN) { fd_move(fd, 1); return 1; }
    if (key == FINDER_KEY_UP) { fd_move(fd, -1); return 1; }
    if (key == FINDER_KEY_HOME) { fd_select(fd, 0); return 1; }
    if (key == FINDER_KEY_END) { fd_select(fd, fd->nent - 1); return 1; }
    if (key == FINDER_KEY_RIGHT) { if (fd->sel >= 0 && fd->ent[fd->sel].dir) fd_activate(fd); return 1; }
    if (key == FINDER_KEY_LEFT) { (void)fd_up(fd); return 1; }
    if (key == 8 || key == 127) {
        if (fd->mode == FD_SAVE) fd_backspace(fd); else (void)fd_up(fd);
        return 1;
    }
    if (fd->mode == FD_OPEN && key > 32 && key < 127 && !(mods & MOD_CMD)) { type_ahead(fd, key); return 1; }
    return 0;
}
