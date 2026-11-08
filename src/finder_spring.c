#include "finder_spring.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "finder_popup.h"
#include "finder_files.h"
#include "finder_state.h"
#include "appearance_panel.h"
#include "session_int.h"
#include <stdio.h>
#include <string.h>

static int folder(const Session *s, const char *path) {
    for (int i = 0; i < s->nwin; i++)
        if (s->win[i].kind == WIN_FOLDER && !s->win[i].search_results && !strcmp(s->win[i].rel, path)) return i;
    return -1;
}
static int original(const Session *s, const char *path) {
    for (int i = 0; i < s->spring.count; i++) if (!strcmp(s->spring.paths[i], path)) return i;
    return -1;
}
static int owned(const Session *s, const char *path) {
    for (int i = 0; i < s->spring.made; i++) if (!strcmp(s->spring.created[i], path)) return 1;
    return 0;
}
static int window_at(const Session *s, int x, int y) {
    if (app_switcher_hit(s, x, y) || finder_apps_occluded(s, 1, x, y)) return -2;
    Rect panels[2]; int count = appearance_panel_regions(s, panels);
    for (int i = 0; i < count; i++) if (rect_has(panels[i], x, y)) return -2;
    for (int i = s->nwin - 1; i >= 0; i--) {
        Layout layout; window_layout(&s->win[i], &layout);
        if (rect_has(layout.outer, x, y)) return i;
    }
    return -1;
}
void finder_spring_finish(Session *s, int x, int y, int keep_destination) {
    if (!s || !s->spring.active) return;
    int keep = keep_destination ? window_at(s, x, y) : -1;
    char destination[OSPI_PATH] = {0}; int valid = 0;
    if (keep >= 0 && s->win[keep].kind == WIN_FOLDER && !s->win[keep].collapsed) {
        Layout layout; window_layout(&s->win[keep], &layout);
        valid = rect_has(layout.content, x, y);
        if (valid) snprintf(destination, sizeof destination, "%s", s->win[keep].rel);
    }
    for (int i = s->nwin - 1; i >= 0; i--) {
        Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || w->search_results) continue;
        if (valid && !strcmp(destination, w->rel)) continue;
        int before = original(s, w->rel);
        if (before < 0 && owned(s, w->rel)) close_index(s, i);
        else if (before >= 0 && s->spring.collapsed[before] && !w->collapsed) {
            if (w->popup) finder_popup_toggle(s, w); else toggle_shade(w);
        }
    }
    memset(&s->spring, 0, sizeof s->spring);
}
static int candidate(Session *s, char path[OSPI_PATH]) {
    int index = window_at(s, s->mx, s->my);
    if (index == -2) return 0;
    if (index < 0) {
        if (app_switcher_hit(s, s->mx, s->my) || finder_apps_occluded(s, 1, s->mx, s->my)) return 0;
        int disk = session_desktop_which(s, s->mx, s->my);
        if (disk >= 0) { snprintf(path, OSPI_PATH, "%s", disk == 1 ? "Trash" : ""); return 1; }
        index = OSPI_DESKTOP_WINDOW;
    }
    Window *w = finder_window(s, index);
    if (!w) return 0;
    if (w->kind != WIN_FOLDER || w->search_results) return 0;
    if (w->popup && w->collapsed) { snprintf(path, OSPI_PATH, "%s", w->rel); return 1; }
    if (w->collapsed) return 0;
    int item = item_index_at(w, s->mx, s->my);
    if (item < 0) return 0;
    const Item *entry = &window_items(w)[item];
    if (index == s->press_win && entry->selected) return 0;
    if (entry->kind == ITEM_ALIAS)
        return finder_alias_target(s->root, w->rel, entry->name, path) == 1 && finder_directory_exists(s->root, path);
    if (entry->kind != ITEM_FOLDER && entry->kind != ITEM_SYSTEM) return 0;
    int n = snprintf(path, OSPI_PATH, "%s%s%s", w->rel, w->rel[0] ? "/" : "", entry->name);
    return n >= 0 && n < OSPI_PATH;
}
void finder_spring_tick(Session *s, uint32_t now_ms) {
    if (!s) return;
    if (s->spring_disabled || !s->down || s->drag != 3 || s->press_zone != Z_ITEM || finder_apps_modal(s) || finder_apps_front(s) != 1) {
        if (s->spring.active) s->down = s->drag = s->click_valid = 0;
        finder_spring_finish(s, 0, 0, 0); return;
    }
    if (!s->spring.active) {
        Window *source = finder_window(s, s->press_win);
        if (!source) return;
        if (source->kind != WIN_FOLDER || source->search_results) return;
        s->spring.active = 1; s->spring.desktop_source = source == &s->desktop;
        snprintf(s->spring.source, sizeof s->spring.source, "%s", source->rel);
        for (int i = 0; i < s->nwin; i++) if (s->win[i].kind == WIN_FOLDER && !s->win[i].search_results) {
            int n = s->spring.count++;
            snprintf(s->spring.paths[n], OSPI_PATH, "%s", s->win[i].rel);
            s->spring.collapsed[n] = s->win[i].collapsed;
        }
    }
    int source = s->spring.desktop_source ? OSPI_DESKTOP_WINDOW : folder(s, s->spring.source);
    if (source < 0) { s->down = s->drag = 0; finder_spring_finish(s, 0, 0, 0); return; }
    s->press_win = source;
    char path[OSPI_PATH];
    if (!candidate(s, path)) { s->spring.hover = 0; return; }
    if (!s->spring.hover || strcmp(path, s->spring.target)) {
        snprintf(s->spring.target, sizeof s->spring.target, "%s", path);
        s->spring.since = now_ms; s->spring.hover = 1; return;
    }
    uint32_t delay = s->spring_delay_ms >= 100 && s->spring_delay_ms <= 2000 ? (uint32_t)s->spring_delay_ms : 600;
    if (s->spring.hover == 2 || (uint32_t)(now_ms - s->spring.since) < delay) return;
    s->spring.hover = 2;
    /* Opening/reordering a folder must not change the source of this drag. */
    int existed = folder(s, path) >= 0;
    if (!open_folder(s, path)) {
        if (!existed && s->spring.made < OSPI_MAX_WIN)
            snprintf(s->spring.created[s->spring.made++], OSPI_PATH, "%s", path);
        Window *w = &s->win[s->nwin - 1];
        if (w->collapsed) { if (w->popup) finder_popup_toggle(s, w); else toggle_shade(w); }
        s->press_win = s->spring.desktop_source ? OSPI_DESKTOP_WINDOW : folder(s, s->spring.source);
    }
}
