#include "session_int.h"
#include "folder_navigation.h"
#include "folder_view.h"
#include "finder_selection.h"
#include "finder_batch.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static int selected(const Window *w) {
    int count = 0;
    for (int i = 0; i < w->nitems; i++) count += !!window_items(w)[i].selected;
    return count;
}
int main(void) {
    char root[] = "/tmp/os999-capacity-XXXXXX", path[1024];
    CHECK(mkdtemp(root) != NULL); CHECK(volume_seed(root) == 0);
    snprintf(path, sizeof path, "%s/Large", root); CHECK(mkdir(path, 0755) == 0);
    for (int i = 0; i < 137; i++) {
        snprintf(path, sizeof path, "%s/Large/File %03d", root, i);
        FILE *f = fopen(path, "w"); CHECK(f != NULL); if (f) { fputs("fixture", f); fclose(f); }
    }
    Session *s = session_create(root); CHECK(s != NULL); if (!s) return 1;
    CHECK(open_folder(s, "Large") == 0); Window *w = &s->win[s->nwin - 1];
    CHECK(w->nitems == 137 && w->expanded_items && !w->listing_limited);
    CHECK(!strcmp(window_items(w)[136].name, "File 136"));
    w->view_mode = VIEW_LIST;
    CHECK(folder_navigation_key(s, FOLDER_KEY_HOME, 0));
    CHECK(folder_navigation_key(s, FOLDER_KEY_END, MOD_SHIFT));
    CHECK(selected(w) == 137 && w->scroll > 0);
    finder_selection_press(s, w, 136); CHECK(s->selection_before[136]);
    CHECK(reload_folder_window(s, w) == 0 && selected(w) == 137);
    CHECK(w->selection_anchor == 0 && w->selection_cursor == 136);
    w->sort_reverse = 1; folder_view_sort(w);
    CHECK(!strcmp(window_items(w)[0].name, "File 136") && selected(w) == 137);
    CHECK(finder_batch_command(s, CMD_MOVE_TRASH));
    CHECK(w->nitems == 0 && !w->expanded_items && s->undo_count == 137);
    CHECK(s->expanded_undo != NULL && volume_count(root, "Trash") == 137);
    CHECK(finder_batch_command(s, CMD_UNDO));
    CHECK(w->nitems == 137 && s->undo_count == 0 && volume_count(root, "Trash") == 0);
    session_command(s, CMD_SELECT_ALL);
    CHECK(finder_batch_command(s, CMD_DUPLICATE));
    CHECK(w->nitems == 274 && s->undo_count == 137);
    CHECK(finder_batch_command(s, CMD_UNDO));
    CHECK(w->nitems == 137 && volume_count(root, "Trash") == 137);
    /* Inline and owned window storage survive reordering and closing. */
    Item *owned = w->expanded_items;
    CHECK(open_folder(s, "Documents") == 0); bring_front(s, s->nwin - 2);
    w = &s->win[s->nwin - 1]; CHECK(w->nitems == 137 && w->expanded_items == owned);
    CHECK(open_folder(s, "Large") == 0);
    int before = s->nwin; close_index(s, s->nwin - 1); CHECK(s->nwin == before - 1);
    CHECK(open_folder(s, "Large") == 0); w = &s->win[s->nwin - 1];
    select_only(w, 136); CHECK(reload_folder_window(s, w) == 0 && selected(w) == 1);
    /* Long host names must be reported as a partial listing, never silently hidden. */
    char long_name[110]; memset(long_name, 'x', sizeof long_name - 1); long_name[sizeof long_name - 1] = 0;
    snprintf(path, sizeof path, "%s/Large/%s", root, long_name);
    FILE *f = fopen(path, "w"); CHECK(f != NULL); if (f) fclose(f);
    CHECK(reload_folder_window(s, w) == 0 && w->listing_limited);
    CHECK(unlink(path) == 0);
    session_destroy(s); CHECK(archive_remove_tree(root) == 0);
    printf("Folder capacity: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
