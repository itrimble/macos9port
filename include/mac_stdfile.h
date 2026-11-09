#ifndef MAC_STDFILE_H
#define MAC_STDFILE_H
#include "mac_runtime.h"
/* The file list of the native Standard File dialog: pure selection, scrolling and typing logic with no CPU, drawing or
 * host access, so it can be tested alone. Entries are copied in, sorted case-insensitively (ties by byte order), and
 * owned by the list. `rows` is the number of rows the list area shows. */
typedef struct {
    MacFileEntry *entries;
    unsigned count, rows, top;
    int selected;                    /* index into entries, or -1 */
    int last_row;                    /* entry index of the previous click, or -1 */
    uint32_t last_ticks;
    char typed[32];
    unsigned typed_length;
    uint32_t typed_ticks;
} MacSfList;
enum { MAC_SF_TYPE_WINDOW = 60 };    /* ticks between letters of one type-ahead word (our choice, not Apple's) */
void mac_sflist_init(MacSfList *list, unsigned rows);
void mac_sflist_free(MacSfList *list);
/* Replace the contents; the selection is cleared and the view returns to the top. Returns 0, or -1 on allocation failure. */
int mac_sflist_set(MacSfList *list, const MacFileEntry *source, unsigned count);
/* A click on visible row `row` (0 = first shown row). Returns 1 if it completes a double click, else 0. A click past the
 * last entry changes nothing. `double_ticks` is the system double-click time. */
int mac_sflist_click(MacSfList *list, unsigned row, uint32_t ticks, uint32_t double_ticks);
void mac_sflist_move(MacSfList *list, int delta);    /* arrow keys: select the neighbour, keep it in view */
void mac_sflist_page(MacSfList *list, int direction); /* scroll bar page region: -1 up, +1 down; selection unchanged */
void mac_sflist_scroll_to(MacSfList *list, unsigned top); /* scroll bar thumb or line arrows, clamped */
/* Type-ahead: select the first entry whose name starts (case-insensitively) with the letters typed within the window. */
void mac_sflist_type(MacSfList *list, char c, uint32_t ticks);
unsigned mac_sflist_max_top(const MacSfList *list);
#endif
