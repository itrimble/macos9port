#include "native_apps.h"
#include "media_player_panel.h"
#include "winamp_skin.h"
#include "finder_apps.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PANEL_CLOSED = 0, PANEL_OPEN = 1 };

static Session *owner;
static int state_mode = PANEL_CLOSED;
static AudioMediaPlayer *active_player;
static char current_path[1024];
static WinampState wa = {
    .x = (OSPI_DEFAULT_W - WINAMP_W) / 2,
    .y = 32,
    .show_eq = 1,
    .show_pl = 1,
    .repeat = 1
};

static int has_media_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) return 0;
    static const char *const exts[] = {
        ".mp3", ".m4a", ".aac", ".flac", ".alac", ".ogg", ".oga", ".opus",
        ".wav", ".wave", ".aif", ".aiff", ".aifc", ".caf", ".wma", ".ra",
        ".rm", ".au", ".snd", ".ac3", ".eac3", ".dts", ".thd", ".wv",
        ".ape", ".tak", ".mov", ".mp4", ".mid", ".midi", ".kar", NULL
    };
    for (int i = 0; exts[i]; i++)
        if (strcasecmp(dot, exts[i]) == 0) return 1;
    return 0;
}

int media_panel_is_media_file(const char *full_path) {
    if (!full_path || !*full_path) return 0;
    const char *base = strrchr(full_path, '/');
    base = base ? base + 1 : full_path;
    if (strcasecmp(base, "OS999 Amp") == 0 || strcasecmp(base, "Winamp") == 0)
        return 1;
    if (!has_media_ext(full_path)) return 0;
    return audio_decoder_probe(full_path, NULL) == 0;
}

int media_panel_launch_vlc(const char *full_path) {
    if (!full_path || !*full_path) return -1;
#if defined(__APPLE__)
    struct stat st;
    pid_t pid = fork();
    if (pid == 0) {
        if (stat("/Applications/VLC.app/Contents/MacOS/VLC", &st) == 0)
            execl("/usr/bin/open", "open", "-a", "VLC", full_path, (char *)NULL);
        execl("/usr/bin/open", "open", full_path, (char *)NULL);
        _exit(127);
    }
    return pid > 0 ? 0 : -1;
#else
    (void)full_path;
    return -1;
#endif
}

static int playlist_add_track(const char *path) {
    if (!path || !*path || wa.track_count >= WINAMP_MAX_TRACKS) return -1;
    for (int i = 0; i < wa.track_count; i++)
        if (strcmp(wa.tracks[i].path, path) == 0) return i;
    AudioMetadata meta = {0};
    if (audio_decoder_probe(path, &meta) != 0) return -1;
    WinampTrack *t = &wa.tracks[wa.track_count];
    snprintf(t->path, sizeof t->path, "%s", path);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (meta.artist[0] && meta.title[0])
        snprintf(t->title, sizeof t->title, "%s - %s", meta.artist, meta.title);
    else if (meta.title[0])
        snprintf(t->title, sizeof t->title, "%s", meta.title);
    else
        snprintf(t->title, sizeof t->title, "%s", base);
    t->duration = meta.duration_seconds;
    return wa.track_count++;
}

static void scan_dir_tracks(const char *dir_path) {
    if (!dir_path || !*dir_path) return;
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && wa.track_count < WINAMP_MAX_TRACKS) {
        if (de->d_name[0] == '.') continue;
        if (!has_media_ext(de->d_name)) continue;
        char full[1024];
        snprintf(full, sizeof full, "%s/%s", dir_path, de->d_name);
        (void)playlist_add_track(full);
    }
    closedir(d);
}

static int add_synth_midi_track(void) {
    static const uint8_t smf[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,120,
        'M','T','r','k', 0,0,0,53,
        0x00, 0x90, 60, 100, 0x00, 0x90, 64, 90, 0x00, 0x90, 67, 88,
        0x60, 0x80, 60, 0,   0x00, 0x90, 62, 100,
        0x60, 0x80, 62, 0,   0x00, 0x90, 69, 102,
        0x60, 0x80, 69, 0,   0x00, 0x90, 72, 108,
        0x81, 0x40, 0x80, 72, 0, 0x00, 0x80, 64, 0, 0x00, 0x80, 67, 0,
        0x00, 0xFF, 0x2F, 0x00
    };
    const char *path = "/tmp/OS999_Synth_Chime_Anthem.mid";
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(smf, 1, sizeof smf, f); fclose(f); }
    return playlist_add_track(path);
}

static void populate_default_playlist(const Session *s) {
    if (s && s->root[0]) {
        scan_dir_tracks(s->root);
        char sub[1024];
        snprintf(sub, sizeof sub, "%s/Music", s->root);
        scan_dir_tracks(sub);
        snprintf(sub, sizeof sub, "%s/Documents", s->root);
        scan_dir_tracks(sub);
    }
    if (wa.track_count == 0) {
        (void)playlist_add_track("/tmp/os999-sound-volume/Phil Collins - In the Air Tonight.mp3");
        (void)add_synth_midi_track();
        (void)playlist_add_track("assets/sounds/classic9/01-startup_chord.wav");
    }
}

static int play_track_index(int idx) {
    if (idx < 0 || idx >= wa.track_count) return -1;
    if (!audio_service_is_initialized())
        (void)audio_service_init("assets/sounds/classic9");
    AudioMediaPlayer *np = audio_open_media(wa.tracks[idx].path);
    if (!np) return -1;
    if (active_player) audio_player_close(active_player);
    active_player = np;
    wa.current_track = idx;
    snprintf(current_path, sizeof current_path, "%s", wa.tracks[idx].path);
    audio_player_set_looping(active_player, wa.repeat && wa.track_count <= 1);
    audio_player_play(active_player);
    if (idx < wa.pl_scroll) wa.pl_scroll = idx;
    if (idx >= wa.pl_scroll + WINAMP_PL_ROWS) wa.pl_scroll = idx - WINAMP_PL_ROWS + 1;
    return 0;
}

int media_panel_open_file(Session *s, const char *full_path) {
    if (!full_path || !*full_path) return -1;
    const char *base = strrchr(full_path, '/');
    base = base ? base + 1 : full_path;
    populate_default_playlist(s);
    if (strcasecmp(base, "OS999 Amp") == 0 || strcasecmp(base, "Winamp") == 0) {
        if (!active_player && wa.track_count > 0) (void)play_track_index(0);
        if (s) media_panel_open(s, MEDIA_PANEL_PLAYER_TAB);
        return 0;
    }
    int idx = playlist_add_track(full_path);
    if (idx < 0) return -1;
    if (play_track_index(idx) != 0) return -1;
    if (s) media_panel_open(s, MEDIA_PANEL_PLAYER_TAB);
    return 0;
}

AudioMediaPlayer *media_panel_current_player(void) { return active_player; }

void media_panel_open(Session *s, int selected_tab) {
    if (!s) return;
    owner = s;
    native_app_note(s, NATIVE_MUSIC);
    s->down = s->drag = 0;
    state_mode = PANEL_OPEN;
    finder_apps_command(s, CMD_APP_MEDIA);
    populate_default_playlist(s);
    wa.bottom_mode = selected_tab == MEDIA_PANEL_FORMATS_TAB ?
                     WINAMP_BOT_FORMATS : WINAMP_BOT_PLAYLIST;
    if (selected_tab == MEDIA_PANEL_FORMATS_TAB) wa.show_pl = 1;
    s->menu = s->menu_hot = -1;
}

void media_panel_close(void) {
    if (owner) {
        owner->down = owner->drag = 0;
        if (owner->media_focused) finder_apps_return(owner, 3);
        owner->media_focused = owner->media_hidden = 0;
    }
    state_mode = PANEL_CLOSED;
}

void media_panel_shutdown(void) {
    media_panel_close();
    owner = NULL;
    if (active_player) { audio_player_close(active_player); active_player = NULL; }
    current_path[0] = 0;
    wa.track_count = wa.current_track = wa.pl_scroll = 0;
}

int media_panel_active(void) {
    return state_mode != PANEL_CLOSED && owner && owner->media_focused && !owner->media_hidden;
}
static int media_running(const Session *s) { return s && owner == s; }
static void media_activate(Session *s) {
    if (owner == s) { state_mode = PANEL_OPEN; s->down = s->drag = 0; }
}
static void media_quit(Session *s) { if (s && owner == s) media_panel_shutdown(); }

void media_panel_tick(void) {
    wa.tick_count++;
    if (active_player && audio_player_is_eof(active_player) && wa.track_count > 0) {
        int next = wa.current_track;
        if (wa.shuffle && wa.track_count > 1)
            next = (wa.current_track + 1 + (int)(wa.tick_count % (uint32_t)(wa.track_count - 1))) % wa.track_count;
        else if (wa.current_track + 1 < wa.track_count)
            next = wa.current_track + 1;
        else if (wa.repeat)
            next = 0;
        else
            return;
        (void)play_track_index(next);
    }
}

void media_panel_draw(Session *s, Fb *fb) {
    if (!s || !fb || owner != s || state_mode == PANEL_CLOSED || s->media_hidden) return;
    wa.x = s->sw > WINAMP_W ? (s->sw - WINAMP_W) / 2 : 0;   /* the player stays centred in any display mode */
    winamp_skin_draw(fb, &wa, active_player, current_path);
}

static int handle_eq_click(Rect e, int x, int y) {
    if (rect_has(winamp_eq_on_rect(e), x, y)) {
        audio_eq_set_enabled(!audio_eq_get_enabled());
        return 1;
    }
    if (rect_has(winamp_eq_auto_rect(e), x, y)) {
        audio_eq_set_auto(!audio_eq_get_auto());
        return 1;
    }
    if (rect_has(winamp_eq_preset_rect(e), x, y)) {
        audio_eq_apply_preset(audio_eq_get_preset() + 1);
        return 1;
    }
    if (rect_has(winamp_eq_reset_rect(e), x, y)) {
        audio_eq_apply_preset(0);
        return 1;
    }
    Rect pr = winamp_eq_preamp_rect(e);
    if (rect_has(pr, x, y)) {
        float db = 12.0f - ((float)(y - pr.y) / (float)pr.h) * 24.0f;
        audio_eq_set_preamp(db);
        return 1;
    }
    for (int b = 0; b < AUDIO_EQ_BANDS; b++) {
        Rect br = winamp_eq_band_rect(e, b);
        if (rect_has(br, x, y)) {
            float db = 12.0f - ((float)(y - br.y) / (float)br.h) * 24.0f;
            audio_eq_set_band(b, db);
            return 1;
        }
    }
    return 1;
}

static int handle_pl_click(Session *s, Rect p, int x, int y) {
    for (int i = 0; i < 4; i++) {
        if (rect_has(winamp_bot_tab_rect(p, i), x, y)) {
            if (i == 3) {
                if (current_path[0]) (void)media_panel_launch_vlc(current_path);
            } else {
                wa.bottom_mode = i;
            }
            return 1;
        }
    }
    if (wa.bottom_mode == WINAMP_BOT_PLAYLIST) {
        for (int row = 0; row < WINAMP_PL_ROWS; row++) {
            int idx = wa.pl_scroll + row;
            if (idx < wa.track_count && rect_has(winamp_pl_row_rect(p, row), x, y)) {
                (void)play_track_index(idx);
                return 1;
            }
        }
        if (rect_has(winamp_pl_btn_rect(p, 0), x, y)) { populate_default_playlist(s); return 1; }
        if (rect_has(winamp_pl_btn_rect(p, 1), x, y)) {
            int idx = add_synth_midi_track();
            if (idx >= 0) (void)play_track_index(idx);
            return 1;
        }
        if (rect_has(winamp_pl_btn_rect(p, 2), x, y)) {
            if (wa.pl_scroll > 0) wa.pl_scroll--;
            return 1;
        }
        if (rect_has(winamp_pl_btn_rect(p, 3), x, y)) {
            if (wa.pl_scroll + WINAMP_PL_ROWS < wa.track_count) wa.pl_scroll++;
            return 1;
        }
    } else if (wa.bottom_mode == WINAMP_BOT_MIXER) {
        for (int b = 0; b < 3; b++) {
            if (rect_has(winamp_bus_minus_rect(p, b), x, y)) {
                audio_set_bus_volume((AudioBus)b, audio_get_bus_volume((AudioBus)b) - 0.1f);
                return 1;
            }
            if (rect_has(winamp_bus_plus_rect(p, b), x, y)) {
                audio_set_bus_volume((AudioBus)b, audio_get_bus_volume((AudioBus)b) + 0.1f);
                return 1;
            }
            Rect bar = winamp_bus_bar_rect(p, b);
            if (rect_has(bar, x, y)) {
                audio_set_bus_volume((AudioBus)b, (float)(x - bar.x) / (float)bar.w);
                return 1;
            }
        }
        if (rect_has(winamp_mute_rect(p), x, y)) {
            audio_set_master_muted(!audio_get_master_muted());
            return 1;
        }
        static const char *ev[5] = {"startup", "trash.empty", "alert.beep", "window.zoom", "folder.open"};
        for (int i = 0; i < 5; i++)
            if (rect_has(winamp_snd_btn_rect(p, i), x, y)) { (void)audio_play_system_sound(ev[i]); return 1; }
    } else {
        AudioFormatSupport list[32];
        size_t total = audio_decoder_enumerate_formats(list, 32);
        int max_page = total > 0 ? (int)((total - 1) / 5) : 0;
        if (rect_has(winamp_fmt_nav_rect(p, 0), x, y) && wa.format_page > 0) wa.format_page--;
        if (rect_has(winamp_fmt_nav_rect(p, 1), x, y) && wa.format_page < max_page) wa.format_page++;
    }
    return 1;
}

int media_panel_down(Session *s, int x, int y) {
    if (!s || owner != s || state_mode == PANEL_CLOSED || s->media_hidden) return 0;
    Rect tot = winamp_total_rect(&wa);
    if (!rect_has(tot, x, y)) {
        if (y >= TITLE_H && s->media_focused) finder_apps_command(s, CMD_APP_FINDER);
        return 0;
    }
    if (finder_apps_occluded(s, 3, x, y)) return 0;
    if (!s->media_focused) finder_apps_command(s, CMD_APP_MEDIA);

    Rect m = winamp_main_rect(&wa);
    if (rect_has(winamp_close_rect(m), x, y)) { media_panel_close(); return 1; }
    if (rect_has(winamp_shade_rect(m), x, y)) { wa.shaded = !wa.shaded; return 1; }
    if (!wa.shaded && rect_has(m, x, y)) {
        if (rect_has(winamp_clock_rect(m), x, y)) { wa.time_remaining = !wa.time_remaining; return 1; }
        if (rect_has(winamp_vis_rect(m), x, y))   { wa.vis_mode = !wa.vis_mode; return 1; }
        Rect vr = winamp_vol_rect(m);
        if (rect_has(vr, x, y)) {
            audio_set_bus_volume(AUDIO_BUS_MEDIA, (float)(x - vr.x) / (float)vr.w);
            return 1;
        }
        Rect bl = winamp_bal_rect(m);
        if (rect_has(bl, x, y)) {
            float pan = ((float)(x - bl.x) / (float)bl.w) * 2.0f - 1.0f;
            if (pan > -0.12f && pan < 0.12f) pan = 0.0f;
            audio_set_media_balance(pan);
            return 1;
        }
        Rect sk = winamp_seek_rect(m);
        if (active_player && rect_has(sk, x, y)) {
            double dur = audio_player_duration(active_player);
            if (dur > 0.0) audio_player_seek(active_player, ((double)(x - sk.x) / (double)sk.w) * dur);
            return 1;
        }
        for (int i = 0; i < 11; i++) {
            if (!rect_has(winamp_transport_rect(m, i), x, y)) continue;
            if (i == 0 && wa.track_count > 0)
                (void)play_track_index((wa.current_track - 1 + wa.track_count) % wa.track_count);
            else if (i == 1) {
                if (active_player) audio_player_play(active_player);
                else if (wa.track_count > 0) (void)play_track_index(wa.current_track);
            } else if (i == 2 && active_player) {
                if (audio_player_is_playing(active_player)) audio_player_pause(active_player);
                else audio_player_play(active_player);
            } else if (i == 3 && active_player) {
                audio_player_pause(active_player);
                audio_player_seek(active_player, 0.0);
            } else if (i == 4 && wa.track_count > 0)
                (void)play_track_index((wa.current_track + 1) % wa.track_count);
            else if (i == 5) {
                populate_default_playlist(s);
                if (!active_player && wa.track_count > 0) (void)play_track_index(0);
            } else if (i == 6) wa.shuffle = !wa.shuffle;
            else if (i == 7) {
                wa.repeat = !wa.repeat;
                if (active_player) audio_player_set_looping(active_player, wa.repeat && wa.track_count <= 1);
            } else if (i == 8) wa.show_eq = !wa.show_eq;
            else if (i == 9) wa.show_pl = !wa.show_pl;
            else if (i == 10) {
                wa.show_pl = 1;
                wa.bottom_mode = (wa.bottom_mode + 1) % 3;
            }
            return 1;
        }
        return 1;
    }
    Rect e = winamp_eq_rect(&wa);
    if (wa.show_eq && rect_has(e, x, y)) return handle_eq_click(e, x, y);
    Rect p = winamp_pl_rect(&wa);
    if (wa.show_pl && rect_has(p, x, y)) return handle_pl_click(s, p, x, y);
    return 1;
}

int media_panel_key(Session *s, int key, int mods) {
    if (!s || owner != s || !media_panel_active()) return 0;
    if ((mods & MOD_CMD) && (key == 'q' || key == 'Q')) return 0;
    if (key == 27 || ((mods & MOD_CMD) && (key == 'w' || key == 'W'))) {
        media_panel_close();
        return 1;
    }
    if (key == '\t') { wa.bottom_mode = (wa.bottom_mode + 1) % 3; return 1; }
    if (key == ' ' && active_player) {
        if (audio_player_is_playing(active_player)) audio_player_pause(active_player);
        else audio_player_play(active_player);
        return 1;
    }
    return 1;
}

static void hook_open_file(Session *s, const char *full_path) { (void)media_panel_open_file(s, full_path); }
static void hook_open_panel(Session *s, int selected_tab) { media_panel_open(s, selected_tab); }
static void hook_draw(Session *s, void *fb) { media_panel_draw(s, (Fb *)fb); }
static int hook_bounds(const Session *s, Rect *out) {
    if (owner != s || state_mode == PANEL_CLOSED || s->media_hidden) return 0;
    *out = winamp_total_rect(&wa);
    out->w += 3; out->h += 3;
    return 1;
}

void media_panel_init_hooks(void) {
    static const SessionMediaHooks hooks = {
        .is_media_file = media_panel_is_media_file,
        .open_file = hook_open_file,
        .try_open_file = media_panel_open_file,
        .open_panel = hook_open_panel,
        .active = media_panel_active,
        .close = media_panel_close,
        .draw = hook_draw,
        .down = media_panel_down,
        .key = media_panel_key,
        .bounds = hook_bounds,
        .running = media_running,
        .activate = media_activate,
        .quit = media_quit,
    };
    session_set_media_hooks(&hooks);
}
