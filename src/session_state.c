#include "finder_desktop_surface.h"
#include "finder_spring.h"
#include "finder_preferences.h"
#include "finder_labels.h"
#include "finder_recent.h"
#include "app_switcher.h"
#include "desktop_theme.h"
#include "appearance_panel.h"
#include "control_panels.h"
#include "desktop_context.h"
#include "ospi.h"
#include "desktop_wallpaper.h"
#include "draw.h"
#include "movie_window.h"
#include "finder_edit.h"
#include "session_int.h"
#include "term.h"
#include "finder_state.h"
#include "finder_popup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const SessionMediaHooks *g_media_hooks;

void session_set_media_hooks(const SessionMediaHooks *hooks) { g_media_hooks = hooks; }
const SessionMediaHooks *session_media_hooks(void) { return g_media_hooks; }

void session_clamp(Session *s) {
    if (s->sw < 200) {
        s->sw = OSPI_W;
    }
    if (s->sh < 200) {
        s->sh = OSPI_H;
    }
    finder_desktop_layout(s);
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (w->popup) finder_popup_layout(s, w);
        int maxs = window_max_scroll(w);
        if (!w->collapsed && w->scroll < 0) {
            w->scroll = 0;
        }
        if (!w->collapsed && w->scroll > maxs) {
            w->scroll = maxs;
        }
        if (w->popup && w->collapsed) continue;
        if (w->w < 260) {
            w->w = 260;
        }
        if (w->collapsed) {
            w->h = TITLE_H;
        } else if (w->h < 140) {
            w->h = 140;
        }
        if (w->x < 0) {
            w->x = 0;
        }
        if (w->y < 20) {
            w->y = 20;
        }
        if (w->w > s->sw) w->w = s->sw;
        if (w->h > s->sh - 20) w->h = s->sh - 20;
        if (w->x + w->w > s->sw) w->x = s->sw - w->w;
        if (w->y + w->h > s->sh) w->y = s->sh - w->h;
        if (w->kind == WIN_TERMINAL && !w->collapsed) terminal_sync_size(w);
    }
}

Session *session_create(const char *root) {
    struct stat st;
    if (!root || strlen(root) >= OSPI_PATH || stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return NULL;
    }
    Session *s = calloc(1, sizeof *s);
    if (!s) {
        return NULL;
    }
    memcpy(s->root, root, strlen(root) + 1);
    s->menu = -1;
    s->menu_hot = -1;
    s->desk_sel = -1;
    s->press_win = -1;
    s->sound_preview_request = -1;
    s->sw = OSPI_W;
    s->sh = OSPI_H;
    char wallpaper_preference[OSPI_PATH + 32];
    snprintf(wallpaper_preference, sizeof wallpaper_preference, "%s/.os999-wallpaper", s->root);
    desktop_wallpaper_init(NULL, wallpaper_preference);
    snprintf(wallpaper_preference, sizeof wallpaper_preference, "%s/.os999-theme", s->root);
    desktop_theme_init(wallpaper_preference);
    int selected = desktop_wallpaper_current();
    menu_wallpaper_page(selected >= 0 ? selected / WALLPAPERS_PER_PAGE : 0);
    typeface_init(s->root);
    s->ram = host_ram();
    snprintf(s->arch, sizeof s->arch, "%s", host_arch());
    volume_free_bytes(s->root, &s->free_bytes);
    s->last_stat = 1;
    if (finder_labels_load(s) < 0) fprintf(stderr, "OS999: Label preferences could not be read.\n");
    finder_recent_load(s);
    if (finder_preferences_load(s) < 0) fprintf(stderr, "OS999: Finder preferences could not be read.\n");
    if (system_icon_positions_load(s->root, &s->system_icons) < 0) fprintf(stderr, "OS999: Desktop icon positions could not be read.\n");
    if (finder_desktop_init(s)) fprintf(stderr, "OS999: Desktop folder unavailable.\n");
    if (finder_state_load(s) <= 0) open_folder(s, "");
    if (app_switcher_load(s) < 0) fprintf(stderr, "OS999: Application Switcher settings could not be read.\n");
    control_panels_session_init(s);
    s->sound_event[0] = 0;
    return s;
}

void session_destroy(Session *s) {
    finder_spring_finish(s, 0, 0, 0);
    if (s) {
        free(s->desktop.expanded_items);
        for (int i = 0; i < s->nwin; i++) {
            if (s->win[i].kind == WIN_TERMINAL) terminal_window_close(s, &s->win[i]);
            movie_window_release(&s->win[i]);
            free(s->win[i].expanded_items);
            free(s->win[i].expanded_text);
        }
    }
    if (s && finder_state_save(s)) fprintf(stderr, "OS999: Finder window settings could not be saved.\n");
    if (s && app_switcher_save(s)) fprintf(stderr, "OS999: Application Switcher settings could not be saved.\n");
    context_close(); appearance_panel_close();
    control_panels_session_shutdown(s);
    if (g_media_hooks && g_media_hooks->quit) g_media_hooks->quit(s);
    else if (g_media_hooks && g_media_hooks->close) g_media_hooks->close();
    desktop_theme_shutdown(); desktop_wallpaper_shutdown(); if (s) { free(s->expanded_undo); free(s->clipboard_large_in); free(s->clipboard_large_out); } free(s);
}

int session_take_startup_toggle(Session *s) {
    if (!s) return 0;
    int request = s->startup_toggle_request;
    s->startup_toggle_request = 0;
    return request;
}

int session_take_startup_preview(Session *s) {
    if (!s) return 0;
    int request = s->startup_preview_request;
    s->startup_preview_request = 0;
    return request;
}

int session_quit(const Session *s) { return s && s->quit; }

void session_tick(Session *s, uint32_t now_ms) {
    s->input_now_ms = now_ms;
    finder_spring_tick(s, now_ms);
    terminal_tick(s);
    movie_windows_tick(s, now_ms);
    finder_edit_tick(s);
    if (s->last_stat != 0 && now_ms - s->last_stat < 1000) {
        return;
    }
    uint64_t b = 0;
    if (volume_free_bytes(s->root, &b) == 0) {
        s->free_bytes = b;
    }
    finder_refresh(s);
    s->last_stat = now_ms;
}
