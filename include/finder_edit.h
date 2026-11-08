/* Finder name editing and keyboard navigation, as Mac OS 8 and 9 did it:
 * Return (or a pause after clicking a selected name) edits an icon's name,
 * arrow keys walk the icons, and typing a few letters selects by name. */
#ifndef OS999_FINDER_EDIT_H
#define OS999_FINDER_EDIT_H

#include "draw.h"
#include "ospi.h"

/* SDL keycodes, the values every host already hands session_key. */
enum {
    FINDER_KEY_RIGHT = 1073741903, FINDER_KEY_LEFT = 1073741904,
    FINDER_KEY_DOWN = 1073741905, FINDER_KEY_UP = 1073741906,
    FINDER_KEY_HOME = 1073741898, FINDER_KEY_END = 1073741901
};
enum { FINDER_NAME_CHARS = 31, FINDER_EDIT_DELAY_MS = 500, FINDER_TYPE_MS = 1000 };

/* session_key: 1 when the key was used by the name editor or navigation. */
int finder_edit_key(Session *s, int key, int mods);
int finder_nav_key(Session *s, int key, int mods);
/* Return with one renameable icon selected: start editing it. */
int finder_edit_return(Session *s);
/* session_down: a press inside the name box moves the caret (returns 1);
 * any other press commits the edit first (returns 0). */
int finder_edit_down(Session *s, int x, int y, int clicks);
void finder_edit_commit(Session *s);
/* A press on an icon that was already selected may become a rename once the
 * click completes and the double-click time passes without a second click. */
void finder_edit_press(Session *s, Window *w, int index, int x, int y, int was_selected);
void finder_edit_release(Session *s, int clean_click);
void finder_edit_tick(Session *s);
/* Paints the editing box in place of the label; 0 when this icon isn't being edited. */
int finder_edit_paint(Fb *fb, const Session *s, const Window *w, int index, int cx, int y);

void finder_standard_icon(Session *s);
const char *finder_fit_label(const char *name, char *out, size_t n);

#endif
