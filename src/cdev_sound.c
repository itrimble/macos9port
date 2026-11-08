#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <string.h>

enum { ALERT_COUNT = 5 };

static const struct {
    const char *id;
    const char *label;
} k_alerts[ALERT_COUNT] = {
    {"alert.beep", "Plain Beep"},
    {"alert.chime", "Tin Chime"},
    {"alert.droplet", "Droplet"},
    {"alert.indigo", "Indigo"},
    {"alert.appear", "Alert Appear"},
};

typedef struct {
    int volume;
    int muted;
    int alert_index;
    int startup_chime;
    int startup_intro;
    int dragging_volume;
} SoundState;

static SoundState g_snd = {80, 0, 0, 1, 1, 0};

static int clamp_vol(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static int alert_idx_for_name(const char *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < ALERT_COUNT; i++) {
        if (strcmp(k_alerts[i].id, name) == 0) return i;
    }
    return 0;
}

static void sync_session(const SoundState *st, const os999_cdev_host_t *host) {
    if (!host || !host->session) return;
    Session *s = host->session;
    s->system_volume = clamp_vol(st->volume);
    s->sound_muted = !!st->muted;
    s->host_sound_enabled = !st->muted;
    s->startup_chime_enabled = !!st->startup_chime;
    s->host_startup_enabled = !!st->startup_intro;
    snprintf(s->alert_sound, sizeof s->alert_sound, "%s", k_alerts[st->alert_index].id);
}

static int sound_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->session) {
        g_snd.startup_intro = host->session->host_startup_enabled ? 1 : 0;
    }
    if (host && host->store) {
        if (host->get_int)
            g_snd.volume = clamp_vol(host->get_int(host->store, "sound", "volume", 80));
        if (host->get_bool) {
            g_snd.muted = host->get_bool(host->store, "sound", "muted", 0);
            g_snd.startup_chime = host->get_bool(host->store, "sound", "startup_chime", 1);
            g_snd.startup_intro = host->get_bool(host->store, "sound", "startup_intro",
                                                 g_snd.startup_intro);
        }
        if (host->get_str) {
            char alert_name[32];
            host->get_str(host->store, "sound", "alert_sound",
                          alert_name, sizeof alert_name, "alert.beep");
            g_snd.alert_index = alert_idx_for_name(alert_name);
        }
    }
    sync_session(&g_snd, host);
    return 0;
}

static int sound_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_snd;
    g_snd.volume = 80;
    g_snd.muted = 0;
    g_snd.alert_index = 0;
    g_snd.startup_chime = 1;
    g_snd.startup_intro = 1;
    g_snd.dragging_volume = 0;
    sound_load(self, host);
    return 0;
}

static void sound_shutdown(os999_cdev_t *self) {
    g_snd.dragging_volume = 0;
    self->priv = NULL;
}

static int sound_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_int) host->set_int(host->store, "sound", "volume", g_snd.volume);
    if (host->set_bool) {
        host->set_bool(host->store, "sound", "muted", g_snd.muted);
        host->set_bool(host->store, "sound", "startup_chime", g_snd.startup_chime);
        host->set_bool(host->store, "sound", "startup_intro", g_snd.startup_intro);
    }
    if (host->set_str)
        host->set_str(host->store, "sound", "alert_sound", k_alerts[g_snd.alert_index].id);
    return host->flush_store(host->store);
}

static Rect vol_minus_btn(Rect c) { return (Rect){c.x + 16, c.y + 24, 32, 24}; }
static Rect vol_track_rect(Rect c) { return (Rect){c.x + 56, c.y + 24, 272, 24}; }
static Rect vol_plus_btn(Rect c) { return (Rect){c.x + 336, c.y + 24, 32, 24}; }
static Rect mute_chk_rect(Rect c) { return (Rect){c.x + 436, c.y + 24, 136, 24}; }
static Rect alert_list_rect(Rect c) { return (Rect){c.x + 14, c.y + 120, 308, 127}; }
static Rect play_alert_btn(Rect c) { return (Rect){c.x + 14, c.y + 258, 176, 26}; }
static Rect chime_chk_rect(Rect c) { return (Rect){c.x + 362, c.y + 124, 210, 24}; }
static Rect test_chime_btn(Rect c) { return (Rect){c.x + 362, c.y + 156, 208, 26}; }
static Rect intro_chk_rect(Rect c) { return (Rect){c.x + 362, c.y + 202, 210, 24}; }
static Rect preview_intro_btn(Rect c) { return (Rect){c.x + 362, c.y + 234, 208, 26}; }

static void update_volume(os999_cdev_t *self, const os999_cdev_host_t *host, int next_vol) {
    g_snd.volume = clamp_vol(next_vol);
    if (g_snd.volume > 0 && g_snd.muted) {
        g_snd.muted = 0;
        if (host && host->set_muted) host->set_muted(host->session, 0);
    }
    sync_session(&g_snd, host);
    if (host && host->set_volume) host->set_volume(host->session, g_snd.volume);
    sound_save(self, host);
}

static int volume_from_x(Rect track, int x) {
    int span = track.w - 12;
    if (span < 1) return 0;
    int rel = x - (track.x + 6);
    return clamp_vol((rel * 100 + span / 2) / span);
}

static int sound_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                       const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_MOVE && g_snd.dragging_volume) {
        update_volume(self, host, volume_from_x(vol_track_rect(c), ev->x));
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_UP && g_snd.dragging_volume) {
        g_snd.dragging_volume = 0;
        return 1;
    }
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key == '-' || ev->key == '[') { update_volume(self, host, g_snd.volume - 10); return 1; }
        if (ev->key == '=' || ev->key == '+' || ev->key == ']') {
            update_volume(self, host, g_snd.volume + 10);
            return 1;
        }
        if (ev->key == 'm') {
            g_snd.muted = !g_snd.muted;
            sync_session(&g_snd, host);
            if (host && host->set_muted) host->set_muted(host->session, g_snd.muted);
            sound_save(self, host);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    if (rect_has(vol_minus_btn(c), x, y)) { update_volume(self, host, g_snd.volume - 10); return 1; }
    if (rect_has(vol_plus_btn(c), x, y)) { update_volume(self, host, g_snd.volume + 10); return 1; }
    if (rect_has(vol_track_rect(c), x, y)) {
        g_snd.dragging_volume = 1;
        update_volume(self, host, volume_from_x(vol_track_rect(c), x));
        return 1;
    }
    if (rect_has(mute_chk_rect(c), x, y)) {
        g_snd.muted = !g_snd.muted;
        sync_session(&g_snd, host);
        if (host && host->set_muted) host->set_muted(host->session, g_snd.muted);
        sound_save(self, host);
        return 1;
    }
    Rect list = alert_list_rect(c);
    if (x > list.x && x < list.x + list.w - 1 && y > list.y && y < list.y + list.h - 1) {
        int row = (y - list.y - 1) / 25;
        if (row >= 0 && row < ALERT_COUNT) {
            g_snd.alert_index = row;
            sync_session(&g_snd, host);
            sound_save(self, host);
            if (host && host->play_sound)
                host->play_sound(host->session, k_alerts[row].id);
            return 1;
        }
    }
    if (rect_has(play_alert_btn(c), x, y)) {
        if (host && host->play_sound)
            host->play_sound(host->session, k_alerts[g_snd.alert_index].id);
        return 1;
    }
    if (rect_has(chime_chk_rect(c), x, y)) {
        g_snd.startup_chime = !g_snd.startup_chime;
        sync_session(&g_snd, host);
        if (host && host->set_startup_chime)
            host->set_startup_chime(host->session, g_snd.startup_chime);
        sound_save(self, host);
        return 1;
    }
    if (rect_has(test_chime_btn(c), x, y)) {
        if (host && host->play_sound) host->play_sound(host->session, "startup");
        return 1;
    }
    if (rect_has(intro_chk_rect(c), x, y)) {
        g_snd.startup_intro = !g_snd.startup_intro;
        sync_session(&g_snd, host);
        if (host && host->set_startup_intro)
            host->set_startup_intro(host->session, g_snd.startup_intro);
        sound_save(self, host);
        return 1;
    }
    if (rect_has(preview_intro_btn(c), x, y)) {
        if (host && host->session) host->session->startup_preview_request = 1;
        return 1;
    }
    return 0;
}

static void sound_render(os999_cdev_t *self, const os999_cdev_host_t *host, Fb *fb, Rect c) {
    (void)self;
    (void)host;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 86}, "System Output Volume");
    cdev_ui_button(fb, vol_minus_btn(c), "-", 0, g_snd.volume > 0);
    cdev_ui_slider(fb, vol_track_rect(c), g_snd.volume, 100, g_snd.muted);
    cdev_ui_button(fb, vol_plus_btn(c), "+", 0, g_snd.volume < 100);
    char pct[16];
    snprintf(pct, sizeof pct, g_snd.muted ? "Muted" : "%d%%", g_snd.volume);
    fb_text(fb, c.x + 378, c.y + 29, fg, pct);
    cdev_ui_checkbox(fb, mute_chk_rect(c), "Mute Audio", g_snd.muted);

    for (int seg = 0; seg < 10; seg++) {
        int lit = !g_snd.muted && g_snd.volume >= (seg + 1) * 10;
        Rect bar = {c.x + 56 + seg * 27, c.y + 58, 22, 12};
        fb_fill(fb, bar.x, bar.y, bar.w, bar.h,
                lit ? cdev_ui_selection_color() : desktop_theme_color(THEME_TRACK));
        fb_frame(fb, bar.x, bar.y, bar.w, bar.h, desktop_theme_color(THEME_FRAME));
    }

    cdev_ui_group(fb, (Rect){c.x, c.y + 96, 336, 260}, "Alert Sounds");
    Rect list = alert_list_rect(c);
    fb_fill(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, list.x, list.y, list.w, list.h, desktop_theme_color(THEME_FRAME));
    for (int i = 0; i < ALERT_COUNT; i++) {
        int ry = list.y + 1 + i * 25;
        int sel = (g_snd.alert_index == i);
        if (sel) fb_fill(fb, list.x + 1, ry, list.w - 2, 25, cdev_ui_selection_color());
        uint32_t ink = sel ? cdev_ui_accent_text() : fg;
        fb_text(fb, list.x + 10, ry + 5, ink, sel ? "*" : " ");
        fb_text(fb, list.x + 26, ry + 5, ink, k_alerts[i].label);
    }
    cdev_ui_button(fb, play_alert_btn(c), "Play Selected Alert", 0, !g_snd.muted);
    char active_line[64];
    snprintf(active_line, sizeof active_line, "Event: %s", k_alerts[g_snd.alert_index].id);
    fb_text(fb, c.x + 14, c.y + 298, desktop_theme_color(THEME_DISABLED), active_line);

    cdev_ui_group(fb, (Rect){c.x + 348, c.y + 96, c.w - 348, 260}, "Startup Preferences");
    cdev_ui_checkbox(fb, chime_chk_rect(c), "Classic Startup Chime", g_snd.startup_chime);
    cdev_ui_button(fb, test_chime_btn(c), "Test Startup Chime", 0, !g_snd.muted);
    cdev_ui_checkbox(fb, intro_chk_rect(c), "Startup Animation", g_snd.startup_intro);
    cdev_ui_button(fb, preview_intro_btn(c), "Preview Intro", 0, 1);
    fb_text(fb, c.x + 362, c.y + 286, desktop_theme_color(THEME_DISABLED), "Boot chord & splash");
    fb_text(fb, c.x + 362, c.y + 306, desktop_theme_color(THEME_DISABLED), "persist across restarts.");
}

static const os999_cdev_t k_sound_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "sound",
    .title = "Sound & Startup",
    .summary = "System volume, classic alert sounds, and startup chime",
    .icon_name = "speaker",
    .category = OS999_CDEV_CAT_SOUND,
    .init = sound_init,
    .shutdown = sound_shutdown,
    .load_state = sound_load,
    .save_state = sound_save,
    .handle_event = sound_event,
    .render = sound_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_sound_module(void) { return &k_sound_cdev; }
