#include "finder_desktop_positions.h"
#include "session_int.h"
#include "finder_files.h"
#include "folder_view.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int compare_names(const void *a, const void *b) { return strcmp(a, b); }

int reload_folder_window(Session *s, Window *w) {
    if (!w || w->kind != WIN_FOLDER || w->search_results) return -1;
    w->folder_stamp_valid = 0;
    Item *listed = NULL; int limited = 0;
    int n = volume_list_dynamic(s->root, w->rel, &listed, &limited);
    if (n < 0) return -1;
    char anchor[OSPI_NAME] = {0}, cursor[OSPI_NAME] = {0};
    const Item *old = window_items(w);
    if (w->selection_anchor >= 0 && w->selection_anchor < w->nitems)
        snprintf(anchor, sizeof anchor, "%s", old[w->selection_anchor].name);
    if (w->selection_cursor >= 0 && w->selection_cursor < w->nitems)
        snprintf(cursor, sizeof cursor, "%s", old[w->selection_cursor].name);
    int count = 0;
    for (int i = 0; i < w->nitems; i++) count += !!old[i].selected;
    char (*keep)[OSPI_NAME] = count ? calloc((size_t)count, OSPI_NAME) : NULL;
    if (count && !keep) { free(listed); return -1; }
    for (int i = 0, k = 0; i < w->nitems; i++)
        if (old[i].selected) strcpy(keep[k++], old[i].name);
    if (count) qsort(keep, (size_t)count, OSPI_NAME, compare_names);
    for (int i = 0; i < n; i++) {
        listed[i].label = finder_label(s->root, w->rel, listed[i].name, -1);
        if (finder_is_alias(s->root, w->rel, listed[i].name)) listed[i].kind = ITEM_ALIAS;
        listed[i].selected = count && bsearch(listed[i].name, keep, (size_t)count, OSPI_NAME, compare_names) != NULL;
    }
    free(keep); free(w->expanded_items); w->expanded_items = NULL;
    if (n > OSPI_MAX_ITEMS) { w->expanded_items = listed; w->item_capacity = n; }
    else { memcpy(w->items, listed, (size_t)n * sizeof *listed); free(listed); w->item_capacity = 0; }
    w->nitems = n; w->listing_limited = limited;
    folder_view_sort(w);
    if (w->desktop_surface) finder_desktop_positions_load(s);
    const Item *items = window_items(w);
    for (int i = 0; i < n; i++) {
        if (anchor[0] && !strcmp(anchor, items[i].name)) w->selection_anchor = i;
        if (cursor[0] && !strcmp(cursor, items[i].name)) w->selection_cursor = i;
    }
    return 0;
}
