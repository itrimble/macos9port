#include "finder_print.h"
#include "finder_state.h"
#include "appearance_panel.h"
#include "session_int.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const Window *front(const Session *s) {
    return !appearance_panel_active() ? finder_current(s) : NULL;
}
static const Item *selected(const Window *w) {
    const Item *item = NULL;
    if (!w || w->kind != WIN_FOLDER || w->search_results) return NULL;
    for (int i = 0; i < w->nitems; i++) if (window_items(w)[i].selected) {
        if (item || window_items(w)[i].kind != ITEM_FILE) return NULL;
        item = &window_items(w)[i];
    }
    return item;
}
int finder_print_enabled(const Session *s, int command) {
    if (!s || !s->print_available || s->dialog || s->print_request) return 0;
    const Window *w = front(s);
    if (command == CMD_PAGE_SETUP) return 1;
    if (command == CMD_PRINT_WINDOW) return w && !w->collapsed && !w->desktop_surface;
    if (command == CMD_PRINT) return w && (w->kind == WIN_TEXT || selected(w));
    return 0;
}
static int read_document(Session *s, const Window *w, const Item *item) {
    if (!finder_directory_exists(s->root, w->rel) || strchr(item->name, '/') || item->name[0] == '.') return -1;
    int dir = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    char path[OSPI_PATH]; snprintf(path, sizeof path, "%s", w->rel);
    char *part = path;
    while (dir >= 0 && part && part[0]) {
        char *slash = strchr(part, '/'); if (slash) *slash = 0;
        int next = openat(dir, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(dir); dir = next; part = slash ? slash + 1 : NULL;
    }
    if (dir < 0) return -1;
    int fd = openat(dir, item->name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size >= (off_t)sizeof s->print_text) { close(fd); return -1; }
    size_t used = 0; ssize_t n;
    while (used < sizeof s->print_text - 1 && (n = read(fd, s->print_text + used, sizeof s->print_text - 1 - used)) > 0) used += (size_t)n;
    char extra; int complete = used == (size_t)st.st_size && read(fd, &extra, 1) == 0;
    close(fd);
    if (!complete) return -1;
    for (size_t i = 0; i < used; i++) {
        unsigned char c = (unsigned char)s->print_text[i];
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') return -1;
    }
    s->print_text[used] = 0;
    return 0;
}
int finder_print_command(Session *s, int command) {
    if (command != CMD_PRINT && command != CMD_PRINT_WINDOW && command != CMD_PAGE_SETUP) return 0;
    if (!finder_print_enabled(s, command)) return 1;
    if (command == CMD_PAGE_SETUP) { s->print_request = PRINT_PAGE_SETUP; return 1; }
    const Window *w = front(s);
    snprintf(s->print_title, sizeof s->print_title, "%s", w->title);
    if (command == CMD_PRINT_WINDOW) {
        Layout lay; window_layout(w, &lay); s->print_rect = lay.outer;
        s->print_request = PRINT_WINDOW; return 1;
    }
    if (w->kind == WIN_TEXT) {
        if (strlen(window_text(w)) >= sizeof s->print_text) {
            dialog_info(s, "Print", "This text is larger than the 64 KiB printing limit. Nothing was sent to a printer."); return 1;
        }
        snprintf(s->print_text, sizeof s->print_text, "%s", window_text(w));
    }
    else {
        const Item *item = selected(w);
        if (!item || read_document(s, w, item)) {
            dialog_info(s, "Print", "This Finder prints plain text documents smaller than 64 KiB. Binary documents, links and larger files must be printed from an application that supports them. Nothing was sent to a printer.");
            return 1;
        }
        snprintf(s->print_title, sizeof s->print_title, "%s", item->name);
    }
    s->print_request = PRINT_TEXT; return 1;
}
