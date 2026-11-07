#include "desktop_commands.h"
#include "session_int.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { INFO_COMMAND = 420, SELECT_ALL_COMMAND = 421, COPY_COMMAND = 422 };
static Window *front(Session *s) {
    if (!s || s->nwin < 1 || s->nwin > OSPI_MAX_WIN) return NULL;
    Window *w = &s->win[s->nwin - 1];
    return w->nitems >= 0 && w->nitems <= OSPI_MAX_ITEMS ? w : NULL;
}
static int selections(const Window *w) {
    int count = 0;
    if (w && w->kind == WIN_FOLDER)
        for (int i = 0; i < w->nitems; i++) count += !!w->items[i].selected;
    return count;
}
int desktop_command_enabled(Session *s, int command) {
    if (!s || s->dialog || s->desk_sel == DESK_BACKGROUND) return 0;
    Window *w = front(s);
    if (command == INFO_COMMAND) {
        if (s->desk_sel == 0 || s->desk_sel == 1) return 1;
        return w && w->kind == WIN_FOLDER && selections(w) <= 1;
    }
    if (s->desk_sel >= 0 || !w) return 0;
    if (command == SELECT_ALL_COMMAND) return w->kind == WIN_FOLDER && w->nitems > 0;
    if (command == COPY_COMMAND)
        return w->kind == WIN_TEXT ? w->text[0] != 0 : selections(w) > 0;
    return 0;
}

static void copy_selection(Session *s, Window *w) {
    char text[OSPI_TEXT];
    size_t used = 0;
    text[0] = 0;
    if (w->kind == WIN_TEXT) {
        size_t n = strnlen(w->text, sizeof w->text);
        if (n >= sizeof text) goto oversized;
        memcpy(text, w->text, n + 1);
    } else {
        for (int i = 0; i < w->nitems; i++) {
            if (!w->items[i].selected) continue;
            size_t n = strnlen(w->items[i].name, sizeof w->items[i].name);
            size_t separator = used ? 1 : 0;
            if (n == sizeof w->items[i].name || n + separator >= sizeof text - used) goto oversized;
            if (separator) text[used++] = '\n';
            memcpy(text + used, w->items[i].name, n); used += n;
            text[used] = 0;
        }
    }
    memcpy(s->clipboard_out, text, strlen(text) + 1);
    s->clipboard_write_request = 1;
    return;
oversized:
    dialog_info(s, "Copy", "The selection is too large to copy completely. Select fewer items.");
}

/* Each directory is opened relative to the last verified directory. No parent
 * symlink is followed and no absolute or dot-component path is accepted. */
static int metadata(const char *root, const char *relative, struct stat *st) {
    if (!root || strnlen(root, OSPI_PATH) == OSPI_PATH || !relative ||
        strnlen(relative, OSPI_PATH) == OSPI_PATH || relative[0] == '/') return -1;
    int dir = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
    if (dir < 0) return -1;
    if (!relative[0]) { int result = fstat(dir, st); close(dir); return result; }
    const char *p = relative;
    int result = -1;
    for (;;) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        char name[OSPI_PATH];
        if (!n || n >= sizeof name || (n == 1 && p[0] == '.') ||
            (n == 2 && p[0] == '.' && p[1] == '.')) break;
        memcpy(name, p, n); name[n] = 0;
        if (!slash) { result = fstatat(dir, name, st, AT_SYMLINK_NOFOLLOW); break; }
        int next = openat(dir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK);
        if (next < 0) break;
        close(dir); dir = next; p = slash + 1;
    }
    close(dir);
    return result;
}

static void get_info(Session *s, Window *w) {
    char rel[OSPI_PATH] = {0}, name[OSPI_NAME] = "OS999 HD";
    int application = 0, disk = s->desk_sel == 0;
    if (s->desk_sel == 1) { strcpy(rel, "Trash"); strcpy(name, "Trash"); }
    else if (!disk && w) {
        size_t n = strnlen(w->rel, sizeof w->rel);
        if (n >= sizeof rel) goto invalid;
        memcpy(rel, w->rel, n + 1);
        const char *leaf = strrchr(rel, '/');
        if (rel[0]) snprintf(name, sizeof name, "%s", leaf ? leaf + 1 : rel);
        for (int i = 0; i < w->nitems; i++) {
            Item *item = &w->items[i];
            if (!item->selected) continue;
            size_t length = strnlen(item->name, sizeof item->name);
            if (!length || length == sizeof item->name || strchr(item->name, '/') ||
                !strcmp(item->name, ".") || !strcmp(item->name, "..")) goto invalid;
            size_t separator = n ? 1 : 0;
            if (n + separator + length >= sizeof rel) goto invalid;
            if (separator) rel[n++] = '/';
            memcpy(rel + n, item->name, length + 1);
            memcpy(name, item->name, length + 1);
            application = item->kind == ITEM_APPLICATION;
            break;
        }
    }
    if (!rel[0]) disk = 1;
    struct stat st;
    if (metadata(s->root, rel, &st)) goto invalid;
    const char *kind = S_ISLNK(st.st_mode) ? "Symbolic link (not followed)" :
        S_ISDIR(st.st_mode) ? disk ? "Volume" : "Folder" :
        S_ISREG(st.st_mode) ? application ? "Classic application" : "File" : "Special file";
    char body[384];
    if (S_ISREG(st.st_mode))
        snprintf(body, sizeof body, "Name: %s\nKind: %s\nData size: %llu bytes\n\nResource forks are not included in this size.",
                 name, kind, (unsigned long long)st.st_size);
    else snprintf(body, sizeof body, "Name: %s\nKind: %s\n\nContents and link targets were not read.", name, kind);
    dialog_info(s, "Get Info", body);
    return;
invalid:
    dialog_info(s, "Get Info", "Information is unavailable. The item may have moved or its path is unsupported.");
}

int desktop_command(Session *s, int command) {
    if (command != INFO_COMMAND && command != SELECT_ALL_COMMAND && command != COPY_COMMAND) return 0;
    if (!desktop_command_enabled(s, command)) return 1;
    Window *w = front(s);
    if (command == INFO_COMMAND) get_info(s, w);
    else if (command == SELECT_ALL_COMMAND) {
        for (int i = 0; i < w->nitems; i++) w->items[i].selected = 1;
    } else copy_selection(s, w);
    return 1;
}
