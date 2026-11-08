#include "finder_state.h"
#include "finder_recent.h"
#include "session_int.h"
#include "finder_popup.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int path_ok(const char *path) {
    if (!path || strnlen(path, OSPI_PATH) >= OSPI_PATH || path[0] == '/' ||
        strchr(path, '\n') || strchr(path, '\r') || strchr(path, '\t')) return 0;
    const char *p = path;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        if (!n || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) return 0;
        if (!slash) break;
        p = slash + 1;
        if (!*p) return 0;
    }
    return 1;
}
int finder_directory_exists(const char *root, const char *relative) {
    if (!path_ok(relative)) return 0;
    int dir = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) return 0;
    char path[OSPI_PATH]; strcpy(path, relative);
    char *part = path;
    while (*part) {
        char *slash = strchr(part, '/'); if (slash) *slash = 0;
        int next = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir); if (next < 0) return 0;
        dir = next; if (!slash) break; part = slash + 1;
    }
    close(dir); return 1;
}
static int index_for(Session *s, const char *rel) {
    for (int i = 0; i < s->nfolder_prefs; i++) if (!strcmp(s->folder_prefs[i].rel, rel)) return i;
    return -1;
}
void finder_state_remember(Session *s, const Window *w, int opened) {
    if (!s || !w || w->kind != WIN_FOLDER || w->search_results || !path_ok(w->rel)) return;
    int i = index_for(s, w->rel);
    if (i < 0) {
        if (s->nfolder_prefs >= OSPI_FOLDER_PREFS) {
            /* Evict the oldest closed folder, preserving all open records. */
            for (i = 0; i < s->nfolder_prefs && s->folder_prefs[i].open; i++) {}
            if (i == s->nfolder_prefs) return;
            memmove(&s->folder_prefs[i], &s->folder_prefs[i + 1],
                    (size_t)(s->nfolder_prefs - i - 1) * sizeof s->folder_prefs[0]);
            s->nfolder_prefs--;
        }
        i = s->nfolder_prefs++;
    }
    FolderPreference *p = &s->folder_prefs[i];
    memset(p, 0, sizeof *p); strcpy(p->rel, w->rel);
    p->x = w->zoomed ? w->zoom_x : w->x; p->y = w->zoomed ? w->zoom_y : w->y;
    p->w = w->zoomed ? w->zoom_w : w->w;
    p->h = w->zoomed ? w->zoom_h : w->collapsed ? w->shade_h : w->h;
    if (w->popup) { p->x = w->popup_x; p->y = w->popup_y; p->w = w->popup_w; p->h = w->popup_h; }
    p->popup = w->popup; p->sort_reverse = w->sort_reverse;
    memcpy(p->list_columns, w->list_columns, sizeof p->list_columns);
    p->view = w->view_mode; p->sort = w->sort_mode; p->scroll = w->scroll; p->open = !!opened;
}
void finder_state_apply(Session *s, Window *w) {
    int i = index_for(s, w->rel); if (i < 0) return;
    const FolderPreference *p = &s->folder_prefs[i];
    w->x = p->x; w->y = p->y; w->w = p->w; w->h = w->shade_h = p->h;
    w->view_mode = p->view; w->sort_mode = p->sort; w->scroll = p->scroll;
    w->sort_reverse = p->sort_reverse; memcpy(w->list_columns, p->list_columns, sizeof w->list_columns);
    if (p->popup) finder_popup_set(s, w, 1);
}
static int valid(const FolderPreference *p) {
    int columns = (!p->list_columns[0] && !p->list_columns[1] && !p->list_columns[2]) ||
        (p->list_columns[0] >= 200 && p->list_columns[0] <= 700 &&
         p->list_columns[1] >= 300 && p->list_columns[1] <= 800 &&
         p->list_columns[2] >= 400 && p->list_columns[2] <= 900 &&
         p->list_columns[1] - p->list_columns[0] >= 100 && p->list_columns[2] - p->list_columns[1] >= 100);
    return columns && (p->sort_reverse == 0 || p->sort_reverse == 1) && path_ok(p->rel) && p->x >= 0 && p->x <= 16384 && p->y >= 20 && p->y <= 16384 &&
           p->w >= 260 && p->w <= 16384 && p->h >= 140 && p->h <= 16384 &&
           p->view >= VIEW_ICONS && p->view <= VIEW_BUTTONS && p->sort >= SORT_NAME &&
           p->sort <= SORT_LABEL && p->scroll >= 0 && p->scroll <= 1000000 && (p->open >= 0 && p->open <= OSPI_MAX_WIN) && (p->popup == 0 || p->popup == 1);
}
int finder_state_load(Session *s) {
    int dir = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) return -1;
    int fd = openat(dir, ".os999-finder-state", O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 65536) { close(fd); return -1; }
    FILE *f = fdopen(fd, "r"); if (!f) { close(fd); return -1; }
    char line[OSPI_PATH + 180];
    if (!fgets(line, sizeof line, f)) { fclose(f); return -1; }
    int version = !strcmp(line, "OS999-FINDER-3\n") ? 3 : !strcmp(line, "OS999-FINDER-2\n") ? 2 : 0;
    if (!version) { fclose(f); return -1; }
    s->nfolder_prefs = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (!n || line[n - 1] != '\n') { s->nfolder_prefs = 0; fclose(f); return -1; }
        line[n - 1] = 0;
        FolderPreference p = {0};
        char *path = strchr(line, '\t'); if (!path) continue;
        *path++ = 0; if (strlen(path) >= OSPI_PATH) continue;
        int *fields[] = {&p.x, &p.y, &p.w, &p.h, &p.view, &p.sort, &p.scroll, &p.open, &p.popup,
                         &p.sort_reverse, &p.list_columns[0], &p.list_columns[1], &p.list_columns[2]};
        int count = version == 3 ? 13 : 9;
        char *cursor = line; int parsed = 1;
        for (int i = 0; i < count; i++) {
            char *end; errno = 0; long value = strtol(cursor, &end, 10);
            if (errno || end == cursor || value < INT_MIN || value > INT_MAX ||
                (i < count - 1 ? *end != ' ' : *end != 0)) { parsed = 0; break; }
            *fields[i] = (int)value; cursor = i < count - 1 ? end + 1 : end;
        }
        if (!parsed) continue;
        strcpy(p.rel, path);
        if (!valid(&p) || !finder_directory_exists(s->root, p.rel)) continue;
        if (index_for(s, p.rel) >= 0) continue;
        if (s->nfolder_prefs == OSPI_FOLDER_PREFS) break;
        s->folder_prefs[s->nfolder_prefs++] = p;
    }
    int error = ferror(f); fclose(f); if (error) return -1;
    for (int rank = 1; rank <= OSPI_MAX_WIN; rank++)
        for (int i = 0; i < s->nfolder_prefs; i++) if (s->folder_prefs[i].open == rank) open_folder(s, s->folder_prefs[i].rel);
    session_clamp(s); return 1;
}
int finder_state_save(Session *s) {
    if (!s) return -1;
    for (int i = 0; i < s->nfolder_prefs; i++) s->folder_prefs[i].open = 0;
    for (int i = 0; i < s->nwin; i++) {
        finder_state_remember(s, &s->win[i], 1);
        int index = index_for(s, s->win[i].rel);
        if (s->win[i].kind == WIN_FOLDER && !s->win[i].search_results && index >= 0)
            s->folder_prefs[index].open = i + 1;
    }
    int dir = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) return -1;
    char temporary[80]; snprintf(temporary, sizeof temporary, ".os999-finder-state.%ld.tmp", (long)getpid());
    int fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return -1; }
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlinkat(dir, temporary, 0); close(dir); return -1; }
    int result = fputs("OS999-FINDER-3\n", f) < 0 ? -1 : 0;
    for (int i = 0; !result && i < s->nfolder_prefs; i++) {
        const FolderPreference *p = &s->folder_prefs[i]; if (!valid(p)) continue;
        if (fprintf(f, "%d %d %d %d %d %d %d %d %d %d %d %d %d\t%s\n", p->x, p->y, p->w, p->h,
                    p->view, p->sort, p->scroll, p->open, p->popup, p->sort_reverse,
                    p->list_columns[0], p->list_columns[1], p->list_columns[2], p->rel) < 0) result = -1;
    }
    if (fflush(f) || fsync(fd)) result = -1;
    if (fclose(f)) result = -1;
    if (!result && renameat(dir, temporary, dir, ".os999-finder-state")) result = -1;
    if (result) unlinkat(dir, temporary, 0);
    close(dir); return result;
}

static void replace_prefix(char path[OSPI_PATH], const char *old, const char *next) {
    size_t length = strlen(old);
    if (strncmp(path, old, length) || (path[length] && path[length] != '/')) return;
    char result[OSPI_PATH];
    int n = snprintf(result, sizeof result, "%s%s", next, path + length);
    if (n >= 0 && (size_t)n < sizeof result) strcpy(path, result);
}
void finder_state_retarget(Session *s, const char *parent, const char *name,
                           const char *destination, const char *next_name) {
    char old[OSPI_PATH], next[OSPI_PATH];
    int a = snprintf(old, sizeof old, "%s%s%s", parent, parent[0] ? "/" : "", name);
    int b = snprintf(next, sizeof next, "%s%s%s", destination, destination[0] ? "/" : "", next_name);
    if (a < 0 || b < 0 || a >= OSPI_PATH || b >= OSPI_PATH) return;
    finder_recent_retarget(s, old, next);
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || w->search_results) continue;
        replace_prefix(w->rel, old, next);
        if (w->rel[0]) {
            const char *leaf = strrchr(w->rel, '/');
            snprintf(w->title, sizeof w->title, "%s", leaf ? leaf + 1 : w->rel);
        }
    }
    for (int i = 0; i < s->nfolder_prefs; i++)
        replace_prefix(s->folder_prefs[i].rel, old, next);
}
