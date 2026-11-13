/* Window-specific wheel and key routing for the front window, split out of
 * input.c so that file stays under the 500-line limit. */
#include "classic_apps.h"
#include "font_book_window.h"
#include "movie_window.h"
#include "ospi.h"
#include "quake_pick.h"
#include "session_int.h"

/* 1 when the front window's own panel consumed the wheel step. */
int session_window_wheel(Window *w, int eff) {
    return (w->kind == WIN_FONT_BOOK && font_book_window_wheel(w, eff)) ||
           (w->kind == WIN_MOVIE && movie_picker_wheel(w, -eff)) ||
           classic_apps_panel_wheel(w, -eff) || quake_pick_panel_wheel(w, -eff);
}

/* 1 when the front window's own panel consumed the key. */
int session_window_key(Session *s, Window *w, int key, int mods) {
    return font_book_window_key(s, w, key, mods) || movie_picker_key(s, w, key, mods) ||
           quake_pick_panel_key(s, w, key, mods);
}
