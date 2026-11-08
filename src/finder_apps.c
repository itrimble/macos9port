#include "finder_apps.h"
#include "app_switcher.h"
#include "session_int.h"
#include "appearance_panel.h"
#include <stdio.h>

int finder_apps_media_running(const Session *s) {
    const SessionMediaHooks *media = session_media_hooks();
    return media && media->running && media->running(s);
}
static void choose_app(Session *s, int app) {
    s->app_request = app;
    finder_apps_note(s, app);
    s->guest_focused = app == 2 && s->guest_active;
    if (app != 1) appearance_panel_deactivate();
    s->media_focused = app == 3;
    s->browser_focused = app == 4;
    if (app != 4 && s->browser_action == BROWSER_ACTIVATE) s->browser_action = 0;
    if (app == 4) { s->browser_hidden = 0; s->guest_focused = 0; s->browser_action = BROWSER_ACTIVATE; }
    if (app == 1) s->finder_hidden = 0;
    if (app == 2) s->guest_hidden = 0;
    if (app == 3) {
        s->media_hidden = 0; s->guest_focused = 0;
        const SessionMediaHooks *media = session_media_hooks();
        if (media && media->activate) media->activate(s);
    }
}
int finder_apps_key(Session *s, int key, int mods, int down, int repeat) {
    if (!s || key != '\t') return 0;
    if (!down && s->app_switch_tab) { s->app_switch_tab = 0; return 1; }
    if (down && repeat && s->app_switch_tab) return 1;
    if (!down || !(mods & MOD_CMD) || (mods & (MOD_OPTION | MOD_OTHER)) ||
        finder_apps_modal(s)) return 0;
    s->app_switch_tab = 1;
    if (repeat) return 1;
    int current = s->browser_focused ? 4 : s->media_focused ? 3 : s->guest_focused ? 2 : 1;
    int available[4] = {1, s->guest_active, finder_apps_media_running(s), s->browser_active};
    int step = mods & MOD_SHIFT ? -1 : 1;
    for (int i = 1; i <= 4; i++) {
        int next = (current - 1 + step * i + 4) % 4;
        if (available[next]) { choose_app(s, next + 1); break; }
    }
    return 1;
}
void finder_apps_close_guest_window(Session *s) {
    if (!s || !s->guest_active || session_overlay_active(s)) return;
    choose_app(s, 2);
    finder_apps_command(s, CMD_CLOSE);
}
int finder_apps_enabled(const Session *s, int command) {
    if (!s || s->dialog) return 0;
    switch (command) {
    case CMD_APP_FINDER: case CMD_APP_SWITCHER: return 1;
    case CMD_APP_GUEST: return s->guest_active;
    case CMD_APP_MEDIA: return finder_apps_media_running(s);
    case CMD_APP_BROWSER: return s->browser_active;
    case CMD_APP_HIDE: case CMD_APP_HIDE_OTHERS: return s->guest_active || finder_apps_media_running(s) || s->browser_active;
    case CMD_APP_SHOW_ALL: return s->finder_hidden || s->guest_hidden || s->media_hidden || s->browser_hidden;
    default: return 0;
    }
}
int finder_apps_command(Session *s, int command) {
    if (s->guest_focused && !s->dialog && (command == CMD_CLOSE || command == CMD_QUIT || command == CMD_CLOSE_ALL)) {
        if (command != CMD_CLOSE_ALL && s->guest_active && s->guest_menu_command &&
            s->guest_menu_command(command) != 1)
            dialog_info(s, "Application Busy", "The application is not ready. Try again.");
        return 1;
    }
    if (s->browser_focused && !s->dialog) {
        int action = command == CMD_COPY ? BROWSER_COPY : command == CMD_PASTE ? BROWSER_PASTE :
                     command == CMD_SELECT_ALL ? BROWSER_SELECT_ALL : command == CMD_UNDO ? BROWSER_UNDO : 0;
        if (action) { s->browser_action = action; return 1; }
    }
    if (s->browser_focused && !s->dialog && command == CMD_NEW_FOLDER) {
        s->browser_action = BROWSER_NEW_WINDOW; return 1;
    }
    if (s->browser_focused && !s->dialog &&
        (command == CMD_CLOSE || command == CMD_CLOSE_ALL || command == CMD_QUIT)) {
        s->browser_action = command == CMD_QUIT ? BROWSER_QUIT :
                            command == CMD_CLOSE_ALL ? BROWSER_CLOSE_ALL : BROWSER_CLOSE;
        return 1;
    }
    if (command < CMD_APP_FINDER || command > CMD_APP_SWITCHER) return 0;
    if (!finder_apps_enabled(s, command)) return 1;
    switch (command) {
    case CMD_APP_SWITCHER: app_switcher_open(s); break;
    case CMD_APP_FINDER: choose_app(s, 1); break;
    case CMD_APP_GUEST: choose_app(s, 2); break;
    case CMD_APP_MEDIA: choose_app(s, 3); break;
    case CMD_APP_BROWSER: choose_app(s, 4); break;
    case CMD_APP_HIDE:
        if (s->browser_focused) { s->browser_hidden = 1; finder_apps_return(s, 4); }
        else if (s->media_focused) { s->media_hidden = 1; finder_apps_return(s, 3); }
        else if (s->guest_focused) { s->guest_hidden = 1; finder_apps_return(s, 2); }
        else { s->finder_hidden = 1; finder_apps_return(s, 1); }
        break;
    case CMD_APP_HIDE_OTHERS:
        s->finder_hidden = s->guest_focused || s->media_focused || s->browser_focused;
        s->browser_hidden = s->browser_active && !s->browser_focused;
        s->guest_hidden = s->guest_active && !s->guest_focused;
        s->media_hidden = finder_apps_media_running(s) && !s->media_focused;
        break;
    case CMD_APP_SHOW_ALL: s->finder_hidden = s->guest_hidden = s->media_hidden = s->browser_hidden = 0; break;
    default: break;
    }
    return 1;
}
int finder_apps_sync(Session *s, int active, const char *title, int *focus) {
    int previous = *focus;
    int lost_front = s->guest_active && (s->guest_focused || *focus);
    if (!active) {
        *focus = 0; s->guest_hidden = 0;
        if (lost_front && !s->media_focused && !s->browser_focused && !s->app_request) finder_apps_return(s, 2);
    } else if (!s->guest_active) { *focus = 1; s->guest_hidden = 0; choose_app(s, 2); }
    s->guest_active = !!active;
    snprintf(s->guest_title, sizeof s->guest_title, "%s", title && title[0] ? title : "Classic Application");
    if (s->app_request) {
        finder_apps_note(s, s->app_request);
        s->media_focused = s->app_request == 3 && finder_apps_media_running(s);
        s->browser_focused = s->app_request == 4 && s->browser_active;
        *focus = s->app_request == 2 && active;
        s->app_request = 0;
    }
    if (s->guest_hidden || s->media_focused || s->browser_focused) *focus = 0;
    s->guest_focused = *focus;
    return previous && !*focus;
}
int finder_apps_guest_hit(const Session *s, int focus, int x, int y) {
    (void)focus;
    return !s->guest_hidden && y >= TITLE_H && !finder_apps_modal(s) && !finder_apps_pointer_capture(s) &&
           !finder_apps_occluded(s, 2, x, y);
}
