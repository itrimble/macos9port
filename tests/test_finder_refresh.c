#include "session_int.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static void write_file(const char *root, const char *name, const char *text) {
    char path[1024]; snprintf(path, sizeof path, "%s/Refresh/%s", root, name);
    FILE *f = fopen(path, "w"); CHECK(f != NULL);
    if (f) { fputs(text, f); fclose(f); }
}
static int index_of(Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) if (!strcmp(w->items[i].name, name)) return i;
    return -1;
}
int main(void) {
    char root[] = "/tmp/os999-refresh-XXXXXX", path[1024], moved[1024];
    CHECK(mkdtemp(root) != NULL); CHECK(volume_seed(root) == 0);
    snprintf(path, sizeof path, "%s/Refresh", root); CHECK(mkdir(path, 0755) == 0);
    write_file(root, "Charlie", "old"); write_file(root, "Delta", "d");
    Session *s = session_create(root); CHECK(s != NULL); if (!s) return 1;
    CHECK(open_folder(s, "Refresh") == 0); Window *w = &s->win[s->nwin - 1];
    w->view_mode = VIEW_LIST; w->w = 600; w->h = 300; w->list_columns[0] = 350;
    int c = index_of(w, "Charlie"); CHECK(c >= 0); select_only(w, c);
    uint32_t now = 1001; session_tick(s, now); CHECK(w->folder_stamp_valid);
    write_file(root, "Alpha", "a"); s->click_valid = 1;
    session_tick(s, now += 1000); c = index_of(w, "Charlie");
    CHECK(w->nitems == 3 && c == 1 && w->items[c].selected);
    CHECK(w->selection_anchor == c && w->selection_cursor == c && !s->click_valid);
    CHECK(w->w == 600 && w->h == 300 && w->list_columns[0] == 350);
    s->down = 1; write_file(root, "Bravo", "b"); session_tick(s, now += 1000);
    CHECK(w->nitems == 3); s->down = 0; session_tick(s, now += 1000);
    c = index_of(w, "Charlie"); CHECK(w->nitems == 4 && c == 2 && w->selection_anchor == c);
    s->menu = 1; write_file(root, "Echo", "e"); session_tick(s, now += 1000);
    CHECK(w->nitems == 4); s->menu = -1; session_tick(s, now += 1000); CHECK(w->nitems == 5);
    write_file(root, "Charlie", "new contents"); session_tick(s, now += 1000);
    c = index_of(w, "Charlie"); CHECK(w->items[c].data_size == strlen("new contents"));
    snprintf(path, sizeof path, "%s/Refresh/Charlie", root); CHECK(unlink(path) == 0);
    session_tick(s, now += 1000); CHECK(index_of(w, "Charlie") < 0);
    CHECK(w->selection_anchor == -1 && w->selection_cursor == -1);
    snprintf(moved, sizeof moved, "%s-away", root); CHECK(rename(root, moved) == 0);
    int windows = s->nwin; session_tick(s, now += 1000); CHECK(s->nwin == windows);
    select_only(w, 0); char retained[OSPI_NAME]; strcpy(retained, w->items[0].name);
    int previous = w->nitems; CHECK(reload_folder_window(s, w) == -1);
    CHECK(w->nitems == previous && w->items[0].selected && !strcmp(w->items[0].name, retained));
    CHECK(w->selection_anchor == 0 && w->selection_cursor == 0 && !w->folder_stamp_valid);
    CHECK(rename(moved, root) == 0);
    snprintf(path, sizeof path, "%s/Refresh", root); snprintf(moved, sizeof moved, "%s/Moved", root);
    CHECK(rename(path, moved) == 0); session_tick(s, now += 1000); CHECK(s->nwin == windows - 1);
    CHECK(mkdir(path, 0755) == 0); CHECK(open_folder(s, "Refresh") == 0);
    w = &s->win[s->nwin - 1]; CHECK(w->w == 600 && w->h == 300 && w->view_mode == VIEW_LIST);
    CHECK(rmdir(path) == 0); CHECK(symlink("/tmp", path) == 0);
    session_tick(s, now += 1000); CHECK(s->nwin == windows - 1);
    session_destroy(s); CHECK(archive_remove_tree(root) == 0);
    printf("Finder refresh: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
