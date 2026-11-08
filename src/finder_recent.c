#include "finder_recent.h"
#include "finder_state.h"
#include "session_int.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int split(const char *path, char parent[OSPI_PATH], char name[OSPI_NAME]) {
    if (!path || !path[0] || path[0] == '/' || strlen(path) >= OSPI_PATH || strpbrk(path, "\n\r\t")) return -1;
    strcpy(parent, path);
    char *leaf = strrchr(parent, '/');
    const char *base = leaf ? leaf + 1 : path;
    if (!base[0] || base[0] == '.' || strlen(base) >= OSPI_NAME) return -1;
    strcpy(name, base);
    if (leaf) *leaf = 0; else parent[0] = 0;
    return 0;
}
int finder_recent_valid(const Session *s, const char *path) {
    char parent[OSPI_PATH], name[OSPI_NAME];
    if (split(path, parent, name) || !finder_directory_exists(s->root, parent)) return 0;
    /* Walk each directory without following symlinks, including after validation. */
    int fd = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    char *part = parent;
    while (fd >= 0 && part && part[0]) {
        char *slash = strchr(part, '/'); if (slash) *slash = 0;
        int next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(fd); fd = next; part = slash ? slash + 1 : NULL;
    }
    if (fd < 0) return 0;
    struct stat st; int valid = !fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode);
    close(fd); return valid;
}
static void save(Session *s) {
    int dir = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) return;
    char temp[80]; snprintf(temp, sizeof temp, ".os999-recent.%ld.tmp", (long)getpid());
    int fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return; }
    FILE *f = fdopen(fd, "w"); int failed = !f;
    if (f) {
        failed = fprintf(f, "OS999-RECENT-1\n") < 0;
        for (int i = 0; i < s->nrecent; i++)
            failed |= fprintf(f, "%d\t%s\n", s->recent[i].application, s->recent[i].path) < 0;
        failed |= fflush(f) != 0; failed |= fsync(fd) != 0; failed |= fclose(f) != 0;
    } else close(fd);
    if (!failed) failed = renameat(dir, temp, dir, ".os999-recent") != 0;
    if (failed) { unlinkat(dir, temp, 0); fprintf(stderr, "OS999: recent items could not be saved.\n"); }
    close(dir);
}
void finder_recent_retarget(Session *s, const char *old, const char *next) {
    size_t length = strlen(old);
    if (!length) return;
    int changed = 0;
    for (int i = 0; i < s->nrecent; i++) {
        char *path = s->recent[i].path;
        if (strncmp(path, old, length) || (path[length] && path[length] != '/')) continue;
        char result[OSPI_PATH];
        int n = snprintf(result, sizeof result, "%s%s", next, path + length);
        if (n < 0 || (size_t)n >= sizeof result) continue;
        strcpy(path, result); changed = 1;
    }
    if (changed) save(s);
}
void finder_recent_load(Session *s) {
    int dir = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); if (dir < 0) return;
    int fd = openat(dir, ".os999-recent", O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 65536) { close(fd); return; }
    FILE *f = fdopen(fd, "r"); if (!f) { close(fd); return; }
    char line[OSPI_PATH + 8];
    if (!fgets(line, sizeof line, f) || strcmp(line, "OS999-RECENT-1\n")) { fclose(f); return; }
    while (s->nrecent < 16 && fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        if (n < 4 || line[n - 1] != '\n') break;
        line[n - 1] = 0;
        if ((line[0] != '0' && line[0] != '1') || line[1] != '\t' || !finder_recent_valid(s, line + 2)) continue;
        int duplicate = 0;
        for (int i = 0; i < s->nrecent; i++) if (!strcmp(s->recent[i].path, line + 2)) duplicate = 1;
        if (duplicate) continue;
        RecentItem *item = &s->recent[s->nrecent++];
        strcpy(item->path, line + 2); item->application = line[0] == '1';
    }
    fclose(f);
}
void finder_recent_note(Session *s, const char *path, int application) {
    if (!finder_recent_valid(s, path)) return;
    int index = s->nrecent;
    for (int i = 0; i < s->nrecent; i++) if (!strcmp(s->recent[i].path, path)) { index = i; break; }
    if (index == s->nrecent && s->nrecent < 16) s->nrecent++;
    if (index >= 16) index = 15;
    memmove(&s->recent[1], &s->recent[0], (size_t)index * sizeof s->recent[0]);
    snprintf(s->recent[0].path, sizeof s->recent[0].path, "%s", path);
    s->recent[0].application = !!application; save(s);
}
int finder_recent_command(Session *s, int command) {
    if (command == CMD_RECENT_CLEAR_APPS || command == CMD_RECENT_CLEAR_DOCS) {
        int application = command == CMD_RECENT_CLEAR_APPS, kept = 0;
        for (int i = 0; i < s->nrecent; i++) if (s->recent[i].application != application) s->recent[kept++] = s->recent[i];
        s->nrecent = kept; save(s); return 1;
    }
    if (command < CMD_RECENT_BASE || command >= CMD_RECENT_BASE + 16) return 0;
    int index = command - CMD_RECENT_BASE;
    char parent[OSPI_PATH], name[OSPI_NAME];
    if (index >= s->nrecent || !finder_recent_valid(s, s->recent[index].path) || split(s->recent[index].path, parent, name)) {
        dialog_info(s, "Recent Items", "This item is no longer available at its saved location."); return 1;
    }
    if (open_folder(s, parent)) { dialog_info(s, "Recent Items", "The folder could not be opened. Close a window and try again."); return 1; }
    Window *w = &s->win[s->nwin - 1];
    for (int i = 0; i < w->nitems; i++) if (!strcmp(window_items(w)[i].name, name)) { select_only(w, i); open_item(s, w, i); return 1; }
    dialog_info(s, "Recent Items", "The item is not available in this folder listing."); return 1;
}
