#include "finder_search.h"
#include "finder_files.h"
#include "session_int.h"
#include "folder_view.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct { const char *root, *query; Item *items; int count, capacity, visited, limited, grow, stopped, failed; } Search;
static int matches(const char *name, const char *query) {
    size_t n = strlen(query);
    for (; *name; name++) if (!strncasecmp(name, query, n)) return 1;
    return 0;
}
static void walk(int fd, const char *parent, int depth, Search *search) {
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); search->limited = 1; return; }
    struct dirent *entry;
    for (;;) {
        errno = 0; entry = readdir(dir);
        if (!entry) { if (errno) search->limited = 1; break; }
        const char *name = entry->d_name; size_t length = strlen(name);
        if (name[0] == '.' || (length >= 5 && !strcasecmp(name + length - 5, ".rsrc"))) continue;
        if (++search->visited > 131072) { search->limited = 1; break; }
        if (length >= OSPI_NAME) { search->limited = 1; continue; }
        struct stat st;
        if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW)) { search->limited = 1; continue; }
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
        char path[OSPI_PATH];
        int n = snprintf(path, sizeof path, "%s%s%s", parent, parent[0] ? "/" : "", name);
        if (n < 0 || (size_t)n >= sizeof path) { search->limited = 1; continue; }
        if (matches(name, search->query)) {
            if (search->count == search->capacity) {
                if (!search->grow || search->capacity == OSPI_FOLDER_LIMIT) {
                    search->limited = search->stopped = 1; break;
                }
                int next = search->capacity * 2;
                if (next > OSPI_FOLDER_LIMIT) next = OSPI_FOLDER_LIMIT;
                Item *more = realloc(search->items, (size_t)next * sizeof *more);
                if (!more) { search->failed = search->stopped = 1; break; }
                search->items = more; search->capacity = next;
            }
            Item *item = &search->items[search->count++]; memset(item, 0, sizeof *item);
            strcpy(item->name, name); strcpy(item->location, path);
            item->kind = S_ISDIR(st.st_mode) ? ITEM_FOLDER : ITEM_FILE;
            item->data_size = S_ISREG(st.st_mode) && st.st_size > 0 ? (uint64_t)st.st_size : 0;
            item->modified = (int64_t)st.st_mtime;
            item->label = finder_label(search->root, parent, name, -1);
#ifdef __APPLE__
            item->created = (int64_t)st.st_birthtime;
#endif
        }
        if (S_ISDIR(st.st_mode)) {
            if (depth >= 32) { search->limited = 1; continue; }
            int child = openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (child < 0) search->limited = 1;
            else walk(child, path, depth + 1, search);
        }
        if (search->visited > 131072 || search->stopped) break;
    }
    closedir(dir);
}
int finder_search(const char *root, const char *query, Item *items, int capacity, int *limited) {
    if (!root || !query || !query[0] || !items || capacity < 1 || capacity > OSPI_FOLDER_LIMIT || !limited) return -1;
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) return -1;
    Search search = {.root=root, .query=query, .items=items, .capacity=capacity};
    walk(fd, "", 0, &search); *limited = search.limited;
    return search.count;
}
int finder_search_command(Session *s, int command) {
    if (command != CMD_FIND && command != CMD_FIND_DO && command != CMD_FIND_AGAIN) return 0;
    if (s->dialog) return 1;
    if (command == CMD_FIND) {
        dialog_info(s, "Find", "Find items whose names contain:");
        snprintf(s->edit_text, sizeof s->edit_text, "%s", s->find_query);
        s->edit_selected = 1; s->dialog = 3; s->dialog_cmd = CMD_FIND_DO; return 1;
    }
    if (command == CMD_FIND_DO) snprintf(s->find_query, sizeof s->find_query, "%s", s->edit_text);
    if (!s->find_query[0]) { dialog_info(s, "Find", "Enter part of a file or folder name."); return 1; }
    int fd = open(s->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) { dialog_info(s, "Find", "OS999 HD could not be searched. Previous results were kept."); return 1; }
    Search search = {.root=s->root, .query=s->find_query, .capacity=OSPI_MAX_ITEMS, .grow=1};
    search.items = calloc((size_t)search.capacity, sizeof *search.items);
    if (!search.items) { close(fd); dialog_info(s, "Find", "There is not enough memory to search. Previous results were kept."); return 1; }
    walk(fd, "", 0, &search);
    if (search.failed) { free(search.items); dialog_info(s, "Find", "There is not enough memory for the results. Previous results were kept."); return 1; }
    int index = -1;
    for (int i = 0; i < s->nwin; i++) if (s->win[i].search_results) { index = i; break; }
    if (index >= 0) bring_front(s, index);
    else if (session_open_text(s, "Find Results", ":find", "")) {
        free(search.items); dialog_info(s, "Find", "Close a window first."); return 1;
    }
    Window *w = &s->win[s->nwin - 1];
    w->kind = WIN_FOLDER; w->search_results = 1; w->view_mode = VIEW_LIST; w->scroll = 0;
    free(w->expanded_items); w->expanded_items = NULL;
    if (search.count > OSPI_MAX_ITEMS) { w->expanded_items = search.items; w->item_capacity = search.capacity; }
    else { memcpy(w->items, search.items, (size_t)search.count * sizeof *search.items); free(search.items); w->item_capacity = 0; }
    int limited = search.limited;
    w->nitems = search.count; w->listing_limited = limited;
    snprintf(w->title, sizeof w->title, "Find Results%s", limited ? " (partial)" : "");
    folder_view_sort(w);
    if (limited) dialog_info(s, "Find", "Showing a partial result. The search reached its result, depth or item limit, or could not read a folder. Narrow the search to find more specific names.");
    return 1;
}
