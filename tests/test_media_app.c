#include "audio_service.h"
#include "desktop_sound.h"
#include "media_player_panel.h"
#include "finder_apps.h"
#include "app_switcher.h"
#include "finder_drag.h"
#include "appearance_panel.h"
#include "ospi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL media app %d: %s\n", __LINE__, #c); } } while (0)

int test_media_panel_and_session(const char *dir, const char *sounds_dir) {
    char vol[512], pref[512], cmd[1024];
    snprintf(vol, sizeof vol, "%s/vol", dir);
    snprintf(pref, sizeof pref, "%s/.os999-sound", vol);
    CHECK(volume_mkdir_p(vol) == 0 && volume_seed(vol) == 0);
    snprintf(cmd, sizeof cmd, "cp \"%s/test.mp3\" \"%s/DemoSong.mp3\"", dir, vol);
    CHECK(system(cmd) == 0);

    CHECK(desktop_sound_init(sounds_dir, pref));
    media_panel_init_hooks();
    Session *s = session_create(vol);
    CHECK(s != NULL);
    if (s) {
        session_command(s, CMD_SUPPORTED_FORMATS);
        CHECK(media_panel_active());
        session_command(s, CMD_MEDIA_PLAYER);
        CHECK(media_panel_active());
        uint32_t *fb = calloc((size_t)OSPI_W * OSPI_H, sizeof *fb);
        Rect panel;
        CHECK(session_media_hooks()->bounds && session_media_hooks()->bounds(s, &panel));
        CHECK(panel.w > 0 && panel.h > 0);
        menu_update(s);
        CHECK(!strcmp(menu_title(7), "OS999Amp"));
        int player_row = -1;
        for (int i = 0; i < menu_nitems(7); i++)
            if (menu_at(7, i)->cmd == CMD_APP_MEDIA) player_row = i;
        CHECK(player_row >= 0 && (menu_at(7, player_row)->flags & MI_CHECKED));
        session_command(s, CMD_APP_FINDER);
        CHECK(!media_panel_active() && !s->media_hidden);
        CHECK(session_media_hooks()->running(s));
        CHECK(session_media_hooks()->bounds(s, &panel));
        CHECK(!media_panel_key(s, ' ', 0));
        session_command(s, CMD_APP_MEDIA);
        CHECK(media_panel_active());
        session_command(s, CMD_CONTROL_PANELS);
        CHECK(session_media_hooks()->running(s) && !s->media_focused);
        session_command(s, CMD_CLOSE);
        session_command(s, CMD_APP_MEDIA);
        session_command(s, CMD_APP_HIDE);
        CHECK(!media_panel_active() && s->media_hidden && session_media_hooks()->running(s));
        CHECK(!session_media_hooks()->bounds(s, &panel));
        CHECK(!media_panel_down(s, 300, 300));
        session_command(s, CMD_APP_SHOW_ALL);
        CHECK(!s->media_hidden && !s->media_focused);
        session_command(s, CMD_APP_MEDIA);
        session_command(s, CMD_APP_HIDE_OTHERS);
        CHECK(s->finder_hidden && !s->media_hidden);
        int guest_focus = 0;
        finder_apps_sync(s, 0, "", &guest_focus);
        CHECK(s->finder_hidden && s->media_focused);
        session_command(s, CMD_APP_FINDER);
        session_command(s, CMD_APP_HIDE_OTHERS);
        CHECK(!s->finder_hidden && s->media_hidden);
        session_command(s, CMD_APP_SHOW_ALL);
        session_command(s, CMD_APP_MEDIA);
        CHECK(fb != NULL);
        if (fb) {
            session_paint(s, fb, OSPI_W, OSPI_H);
            free(fb);
        }
        int windows_before_close = s->nwin;
        session_command(s, CMD_CLOSE);
        CHECK(s->nwin == windows_before_close);
        CHECK(!session_media_hooks()->bounds(s, &panel));
        CHECK(session_media_hooks()->running(s) && !s->media_focused);
        CHECK(finder_apps_enabled(s, CMD_APP_MEDIA));
        session_command(s, CMD_APP_MEDIA);
        CHECK(media_panel_active() && session_media_hooks()->bounds(s, &panel));
        session_command(s, CMD_APPEARANCE_DESKTOP);
        CHECK(appearance_panel_active());
        session_command(s, CMD_APP_MEDIA);
        CHECK(!appearance_panel_active() && media_panel_active());
        CHECK(!appearance_panel_key(s, 27, 0));
        session_command(s, CMD_APP_FINDER);
        CHECK(appearance_panel_active());
        session_command(s, CMD_MEDIA_PLAYER);
        CHECK(!appearance_panel_active() && media_panel_active());
        /* Opening DemoSong.mp3 via SessionMediaHooks opens the media player */
        char song_full[512];
        snprintf(song_full, sizeof song_full, "%s/DemoSong.mp3", vol);
        CHECK(media_panel_is_media_file(song_full));
        session_command(s, CMD_APP_FINDER);
        int song = -1;
        Window *folder = &s->win[0];
        for (int i = 0; i < folder->nitems; i++) {
            window_items(folder)[i].selected = !strcmp(window_items(folder)[i].name, "DemoSong.mp3");
            if (window_items(folder)[i].selected) song = i;
        }
        CHECK(song >= 0);
        app_switcher_open(s); Rect switcher; CHECK(app_switcher_bounds(s, &switcher));
        finder_drag_drop(s, 0, switcher.x + 35, switcher.y + 22 + 42 + 15);
        CHECK(!s->dialog);
        s->switcher.open = 0;
        CHECK(media_panel_active());
        CHECK(media_panel_current_player() != NULL);
        AudioMediaPlayer *playing = media_panel_current_player();
        audio_player_set_looping(playing, 1);
        audio_player_play(playing);
        CHECK(audio_player_is_playing(playing));
        session_command(s, CMD_APP_HIDE);
        CHECK(media_panel_current_player() == playing && audio_player_is_playing(playing));
        session_command(s, CMD_APP_MEDIA);
        CHECK(media_panel_current_player() == playing && media_panel_active());
        session_command(s, CMD_CLOSE);
        CHECK(media_panel_current_player() == playing && audio_player_is_playing(playing));
        CHECK(!media_panel_active() && session_media_hooks()->running(s));
        session_command(s, CMD_APP_MEDIA);
        CHECK(media_panel_current_player() == playing && media_panel_active());
        s->browser_active = 1;
        session_command(s, CMD_APP_BROWSER); session_command(s, CMD_APP_MEDIA);
        session_command(s, CMD_CLOSE);
        CHECK(s->browser_focused && !s->media_focused && media_panel_current_player() == playing);
        s->browser_active = s->browser_focused = s->browser_action = 0;
        session_command(s, CMD_APP_MEDIA);
        session_command(s, CMD_APP_HIDE_OTHERS);
        menu_update(s);
        int quit_found = 0;
        for (int i = 0; i < menu_nitems(1); i++) {
            const MenuItem *item = menu_at(1, i);
            if (item->cmd == CMD_QUIT) {
                CHECK(!strcmp(item->label, "Quit OS999Amp")); quit_found = 1;
            }
        }
        CHECK(quit_found);
        session_key(s, 'q', MOD_CMD);
        CHECK(!session_quit(s) && !session_media_hooks()->running(s));
        CHECK(!media_panel_current_player() && !s->finder_hidden && !s->media_focused);
        CHECK(appearance_panel_active());
        appearance_panel_close();
        CHECK(media_panel_open_file(s, song_full) == 0);
        CHECK(media_panel_current_player());
        session_command(s, CMD_CLOSE);
        session_destroy(s);
        CHECK(!media_panel_current_player());
    }
    media_panel_shutdown();
    desktop_sound_shutdown();
    return failures;
}
