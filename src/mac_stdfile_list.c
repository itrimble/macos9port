#include "mac_stdfile.h"
#include <stdlib.h>
#include <string.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int compare(const void *a, const void *b) {
    const char *x = ((const MacFileEntry *)a)->name, *y = ((const MacFileEntry *)b)->name;
    for (; *x && *y; x++, y++)
        if (lower((unsigned char)*x) != lower((unsigned char)*y)) return lower((unsigned char)*x) - lower((unsigned char)*y);
    if (*x || *y) return *x ? 1 : -1;
    return strcmp(((const MacFileEntry *)a)->name, ((const MacFileEntry *)b)->name);
}

void mac_sflist_init(MacSfList *list, unsigned rows) {
    memset(list, 0, sizeof *list);
    list->rows = rows ? rows : 1;
    list->selected = list->last_row = -1;
}
void mac_sflist_free(MacSfList *list) { free(list->entries); list->entries = NULL; list->count = 0; }
unsigned mac_sflist_max_top(const MacSfList *list) { return list->count > list->rows ? list->count - list->rows : 0; }

int mac_sflist_set(MacSfList *list, const MacFileEntry *source, unsigned count) {
    MacFileEntry *copy = count ? malloc((size_t)count * sizeof *copy) : NULL;
    if (count && !copy) return -1;
    if (count) { memcpy(copy, source, (size_t)count * sizeof *copy); qsort(copy, count, sizeof *copy, compare); }
    free(list->entries);
    list->entries = copy; list->count = count; list->top = 0;
    list->selected = list->last_row = -1; list->typed_length = 0;
    return 0;
}

static void reveal(MacSfList *list) {
    if (list->selected < 0) return;
    if ((unsigned)list->selected < list->top) list->top = (unsigned)list->selected;
    else if ((unsigned)list->selected >= list->top + list->rows) list->top = (unsigned)list->selected - list->rows + 1;
    if (list->top > mac_sflist_max_top(list)) list->top = mac_sflist_max_top(list);
}

int mac_sflist_click(MacSfList *list, unsigned row, uint32_t ticks, uint32_t double_ticks) {
    unsigned index = list->top + row;
    if (row >= list->rows || index >= list->count) return 0;
    int again = list->last_row == (int)index && (uint32_t)(ticks - list->last_ticks) <= double_ticks;
    list->selected = (int)index;
    list->last_row = again ? -1 : (int)index; /* a double click is consumed; a third click starts over */
    list->last_ticks = ticks;
    list->typed_length = 0;
    return again;
}

void mac_sflist_move(MacSfList *list, int delta) {
    if (!list->count) return;
    int next = list->selected < 0 ? (delta > 0 ? 0 : (int)list->count - 1) : list->selected + delta;
    if (next < 0) next = 0;
    if (next >= (int)list->count) next = (int)list->count - 1;
    list->selected = next; list->typed_length = 0;
    reveal(list);
}

void mac_sflist_page(MacSfList *list, int direction) {
    unsigned step = list->rows > 1 ? list->rows - 1 : 1;
    if (direction < 0) list->top = list->top > step ? list->top - step : 0;
    else list->top = list->top + step > mac_sflist_max_top(list) ? mac_sflist_max_top(list) : list->top + step;
}

void mac_sflist_scroll_to(MacSfList *list, unsigned top) {
    list->top = top > mac_sflist_max_top(list) ? mac_sflist_max_top(list) : top;
}

void mac_sflist_type(MacSfList *list, char c, uint32_t ticks) {
    if (!list->count || c < 32 || c > 126) return;
    if (list->typed_length && (uint32_t)(ticks - list->typed_ticks) > MAC_SF_TYPE_WINDOW) list->typed_length = 0;
    if (list->typed_length + 1 >= sizeof list->typed) list->typed_length = 0;
    list->typed[list->typed_length++] = c;
    list->typed_ticks = ticks;
    for (unsigned i = 0; i < list->count; i++) {
        unsigned k = 0;
        while (k < list->typed_length && list->entries[i].name[k] &&
               lower((unsigned char)list->entries[i].name[k]) == lower((unsigned char)list->typed[k])) k++;
        if (k == list->typed_length) { list->selected = (int)i; reveal(list); return; }
    }
}
