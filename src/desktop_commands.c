#include "desktop_commands.h"
#include "session_int.h"
#include "volume_directory.h"
#include "app_mode.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { INFO_COMMAND = 420, SELECT_ALL_COMMAND = 421, COPY_COMMAND = 422 };
static Window *front(Session *s) {
    Window *w = finder_current(s);
    if (!w) return NULL;
    return w->nitems >= 0 && w->nitems <= window_item_capacity(w) ? w : NULL;
}
static int selections(const Window *w) {
    int count = 0;
    if (w && w->kind == WIN_FOLDER)
        for (int i = 0; i < w->nitems; i++) count += !!window_items(w)[i].selected;
    return count;
}
int desktop_command_enabled(Session *s, int command) {
    if (!s || s->dialog) return 0;
    Window *w = front(s);
    if (command == INFO_COMMAND) {
        if (s->desk_sel == 0 || s->desk_sel == 1) return 1;
        return w && w->kind == WIN_FOLDER && !w->search_results &&
            (w->desktop_surface ? selections(w) == 1 : selections(w) <= 1);
    }
    if (s->desk_sel >= 0 || !w) return 0;
    if (command == SELECT_ALL_COMMAND) return w->kind == WIN_FOLDER && w->nitems > 0;
    if (command == COPY_COMMAND)
        return w->kind == WIN_TEXT ? window_text(w)[0] != 0 : selections(w) > 0;
    return 0;
}

static void copy_selection(Session *s, Window *w) {
    size_t used = 0;
    if (w->kind == WIN_TEXT) {
        size_t bound = w->expanded_text ? OSPI_CLIPBOARD_LIMIT + 1 : sizeof w->text;
        used = strnlen(window_text(w), bound);
        if (used == bound) goto unavailable;
    } else {
        for (int i = 0; i < w->nitems; i++) {
            const Item *item = &window_items(w)[i];
            if (!item->selected) continue;
            size_t n = strnlen(item->name, sizeof item->name), separator = used ? 1 : 0;
            if (n == sizeof item->name || n + separator > OSPI_CLIPBOARD_LIMIT - used) goto unavailable;
            used += n + separator;
        }
    }
    char *text = malloc(used + 1);
    if (!text) goto unavailable;
    if (w->kind == WIN_TEXT) memcpy(text, window_text(w), used + 1);
    else {
        size_t at = 0;
        for (int i = 0; i < w->nitems; i++) {
            const Item *item = &window_items(w)[i];
            if (!item->selected) continue;
            if (at) text[at++] = '\n';
            size_t n = strlen(item->name); memcpy(text + at, item->name, n); at += n;
        }
        text[at] = 0;
    }
    free(s->clipboard_large_out); s->clipboard_large_out = NULL;
    if (used < sizeof s->clipboard_out) { memcpy(s->clipboard_out, text, used + 1); free(text); }
    else { s->clipboard_large_out = text; s->clipboard_out[0] = 0; }
    s->clipboard_write_request = 1;
    return;
unavailable:
    dialog_info(s, "Copy", "The complete selection could not be copied. No clipboard change was requested.");
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

/* Sizes read like the Finder: "12.3 KB (12,345 bytes)". */
static void format_size(unsigned long long n, char *out, size_t cap) {
    char grouped[48]; char digits[32]; snprintf(digits, sizeof digits, "%llu", n);
    size_t len = strlen(digits), j = 0;
    for (size_t i = 0; i < len && j + 2 < sizeof grouped; i++) {
        if (i && (len - i) % 3 == 0) grouped[j++] = ',';
        grouped[j++] = digits[i];
    }
    grouped[j] = 0;
    if (n < 1024) snprintf(out, cap, "%s bytes", grouped);
    else if (n < 1024ULL * 1024) snprintf(out, cap, "%.1f KB (%s bytes)", n / 1024.0, grouped);
    else if (n < 1024ULL * 1024 * 1024) snprintf(out, cap, "%.1f MB (%s bytes)", n / 1048576.0, grouped);
    else snprintf(out, cap, "%.2f GB (%s bytes)", n / 1073741824.0, grouped);
}
/* Immediate children only, through the no-link opener; hidden names and
 * resource-fork sidecars are not counted, matching the Finder listing. */
static int count_items(const char *root, const char *rel) {
    int fd = volume_directory_open(root, rel);
    if (fd < 0) return -1;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return -1; }
    int count = 0;
    for (struct dirent *e; (e = readdir(dir)); ) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.') continue;
        if (n > 5 && !strcmp(e->d_name + n - 5, ".rsrc")) continue;
        count++;
    }
    closedir(dir);
    return count;
}
static void format_when(time_t t, char *out, size_t cap) {
    struct tm when;
    if (!localtime_r(&t, &when) || !strftime(out, cap, "%Y-%m-%d %H:%M", &when)) snprintf(out, cap, "unknown");
}

static void get_info(Session *s, Window *w) {
    char rel[OSPI_PATH] = {0}, name[OSPI_NAME] = "OS999 HD";
    int special = 0; s->info_rel[0] = 0;
    int application = 0, disk = s->desk_sel == 0;
    if (s->desk_sel == 1) { strcpy(rel, "Trash"); strcpy(name, "Trash"); }
    else if (!disk && w) {
        size_t n = strnlen(w->rel, sizeof w->rel);
        if (n >= sizeof rel) goto invalid;
        memcpy(rel, w->rel, n + 1);
        const char *leaf = strrchr(rel, '/');
        if (rel[0]) snprintf(name, sizeof name, "%s", leaf ? leaf + 1 : rel);
        for (int i = 0; i < w->nitems; i++) {
            Item *item = &window_items(w)[i];
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
            special = item->kind == ITEM_FOLDER && item->special[0];
            break;
        }
    }
    if (!rel[0]) disk = 1;
    struct stat st;
    if (metadata(s->root, rel, &st)) goto invalid;
    const char *kind = S_ISLNK(st.st_mode) ? "Symbolic link (not followed)" :
        S_ISDIR(st.st_mode) ? disk ? "Volume" : "Folder" :
        S_ISREG(st.st_mode) ? application ? "Classic application" : "File" : "Special file";
    char body[640], size_line[96] = "", modified[48], created[48];
    format_when(st.st_mtime, modified, sizeof modified);
#ifdef __APPLE__
    format_when(st.st_birthtime, created, sizeof created);
#else
    format_when(st.st_ctime, created, sizeof created);
#endif
    if (S_ISREG(st.st_mode)) {
        char data[80]; format_size((unsigned long long)st.st_size, data, sizeof data);
        snprintf(size_line, sizeof size_line, "Size: %s", data);
        char sidecar[OSPI_PATH]; struct stat fork;
        if (snprintf(sidecar, sizeof sidecar, "%s.rsrc", rel) < (int)sizeof sidecar &&
            !metadata(s->root, sidecar, &fork) && S_ISREG(fork.st_mode)) {
            char r[80]; format_size((unsigned long long)fork.st_size, r, sizeof r);
            snprintf(size_line + strlen(size_line), sizeof size_line - strlen(size_line), "\nResource fork: %s", r);
        }
    } else if (S_ISDIR(st.st_mode)) {
        int items = count_items(s->root, rel);
        if (items >= 0) snprintf(size_line, sizeof size_line, "Items: %d", items);
        else snprintf(size_line, sizeof size_line, "Items: unavailable");
    } else if (S_ISLNK(st.st_mode)) snprintf(size_line, sizeof size_line, "Link target was not followed.");
    snprintf(body, sizeof body, "Name: %s\nKind: %s\n%s\nWhere: OS999 HD%s%s\nCreated: %s\nModified: %s",
             name, kind, size_line, rel[0] ? ": " : "", rel, created, modified);
    if (special && S_ISDIR(st.st_mode)) {
        snprintf(s->info_rel, sizeof s->info_rel, "%s", rel);
        strncat(body, "\nKept its special icon when renamed. OK restores the standard icon.", sizeof body - strlen(body) - 1);
    }
    if (application && S_ISREG(st.st_mode)) {
        char brief[256];   /* the Memory mode rows start below the first lines, so keep this to four */
        snprintf(brief, sizeof brief, "Name: %s\nKind: %s\n%.60s\nModified: %s", name, kind, size_line, modified);
        if (app_mode_open(s, rel, brief) == 0) return;
    }
    dialog_info(s, "Get Info", body);
    if (s->info_rel[0]) { s->dialog = 2; s->dialog_cmd = CMD_STANDARD_ICON; }
    return;
invalid:
    dialog_info(s, "Get Info", "Information is unavailable. The item may have moved or its path is unsupported.");
}

int desktop_command(Session *s, int command) {
    if (command != INFO_COMMAND && command != SELECT_ALL_COMMAND && command != COPY_COMMAND) return 0;
    Window *w = front(s);
    /* Terminal windows own Copy and Select All; let session_command route them. */
    if (w && w->kind == WIN_TERMINAL && command != INFO_COMMAND) return 0;
    if (!desktop_command_enabled(s, command)) return 1;
    if (command == INFO_COMMAND) get_info(s, w);
    else if (command == SELECT_ALL_COMMAND) {
        for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = 1;
    } else copy_selection(s, w);
    return 1;
}
