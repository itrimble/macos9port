#include "finder_apps.h"
#include "app_switcher.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include <string.h>

static void order(const Session *s, int out[4]) {
    const int defaults[4] = {1, 3, 2, 4};
    unsigned used = 0; int n = 0;
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 4; i++) {
        int app = pass ? defaults[i] : s->app_order[i];
        if (app < 1 || app > 4 || (used & (1u << app))) continue;
        out[n++] = app; used |= 1u << app;
    }
}
static int available(const Session *s, int app) {
    return app == 1 || (app == 2 && s->guest_active) ||
        (app == 3 && finder_apps_media_running(s)) || (app == 4 && s->browser_active);
}
static int hidden(const Session *s, int app) {
    return app == 1 ? s->finder_hidden : app == 2 ? s->guest_hidden :
           app == 3 ? s->media_hidden : s->browser_hidden;
}
int finder_apps_front(const Session *s) {
    return s->browser_focused ? 4 : s->media_focused ? 3 : s->guest_focused ? 2 : 1;
}
void finder_apps_note(Session *s, int app) {
    if (!s || app < 1 || app > 4) return;
    int previous[4]; order(s, previous);
    s->app_order[0] = app;
    for (int i = 0, n = 1; i < 4; i++) if (previous[i] != app) s->app_order[n++] = previous[i];
}
int finder_apps_rank(const Session *s, int app) {
    if (!s || app < 1 || app > 4) return 4;
    int front = finder_apps_front(s), ordered[4]; order(s, ordered);
    if (app == front) return 0;
    for (int i = 0, rank = 1; i < 4; i++) if (ordered[i] != front) {
        if (ordered[i] == app) return rank;
        rank++;
    }
    return 4;
}
int finder_apps_above(const Session *s, int app, int other) {
    return finder_apps_rank(s, app) < finder_apps_rank(s, other);
}
void finder_apps_return(Session *s, int departing) {
    int ordered[4]; order(s, ordered);
    const int commands[4] = {CMD_APP_FINDER, CMD_APP_GUEST, CMD_APP_MEDIA, CMD_APP_BROWSER};
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < 4; i++) {
        int app = ordered[i];
        if (app != departing && available(s, app) && (pass || !hidden(s, app))) {
            finder_apps_command(s, commands[app - 1]); return;
        }
    }
    finder_apps_command(s, CMD_APP_FINDER);
}
int finder_apps_modal(const Session *s) {
    if (!s || s->dialog || s->menu >= 0 || context_active()) return 1;
    Rect regions[2];
    if (appearance_panel_regions(s, regions) > 1) return 1;
    const SessionMediaHooks *media = session_media_hooks();
    return media && !media->running && media->active && media->active();
}
int finder_apps_pointer_capture(const Session *s) {
    return s && (s->switcher.gesture || (s->down && s->press_zone != Z_NONE));
}
int finder_apps_occluded(const Session *s, int app, int x, int y) {
    Rect palette;
    if (app_switcher_bounds(s, &palette) && rect_has(palette, x, y)) return 1;
    if (app != 1 && !s->finder_hidden && finder_apps_above(s, 1, app)) {
        for (int i = 0; i < s->nwin; i++) {
            Layout layout; window_layout(&s->win[i], &layout);
            if (rect_has(layout.outer, x, y)) return 1;
        }
        Rect regions[2]; int count = appearance_panel_regions(s, regions);
        for (int i = 0; i < count; i++) if (rect_has(regions[i], x, y)) return 1;
    }
    if (app != 2 && s->guest_active && !s->guest_hidden && finder_apps_above(s, 2, app) &&
        rect_has(s->guest_bounds, x, y)) return 1;
    const SessionMediaHooks *media = session_media_hooks(); Rect region;
    if (app != 3 && !s->media_hidden && media && media->bounds &&
        (!media->running || finder_apps_above(s, 3, app)) && media->bounds(s, &region) &&
        rect_has(region, x, y)) return 1;
    return 0;
}
