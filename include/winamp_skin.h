#ifndef OS999_WINAMP_SKIN_H
#define OS999_WINAMP_SKIN_H

#include "audio_service.h"
#include "draw.h"
#include "ospi.h"

enum {
    WINAMP_W = 540,
    WINAMP_MAIN_H = 148,
    WINAMP_SHADE_H = 22,
    WINAMP_EQ_H = 134,
    WINAMP_PL_H = 186,
    WINAMP_MAX_TRACKS = 32,
    WINAMP_PL_ROWS = 5
};

enum {
    WINAMP_BOT_PLAYLIST = 0,
    WINAMP_BOT_MIXER = 1,
    WINAMP_BOT_FORMATS = 2
};

typedef struct {
    char path[1024];
    char title[128];
    double duration;
} WinampTrack;

typedef struct {
    int x, y;
    int dragging, drag_dx, drag_dy;
    int shaded;
    int show_eq;
    int show_pl;
    int bottom_mode;
    int vis_mode;
    int time_remaining;
    int shuffle;
    int repeat;
    int format_page;
    uint32_t tick_count;
    WinampTrack tracks[WINAMP_MAX_TRACKS];
    int track_count;
    int current_track;
    int pl_scroll;
} WinampState;

Rect winamp_main_rect(const WinampState *st);
Rect winamp_eq_rect(const WinampState *st);
Rect winamp_pl_rect(const WinampState *st);
Rect winamp_total_rect(const WinampState *st);

/* Main deck hit rects */
Rect winamp_close_rect(Rect m);
Rect winamp_shade_rect(Rect m);
Rect winamp_clock_rect(Rect m);
Rect winamp_vis_rect(Rect m);
Rect winamp_vol_rect(Rect m);
Rect winamp_bal_rect(Rect m);
Rect winamp_seek_rect(Rect m);
Rect winamp_transport_rect(Rect m, int idx);

/* Equalizer deck hit rects */
Rect winamp_eq_on_rect(Rect e);
Rect winamp_eq_auto_rect(Rect e);
Rect winamp_eq_preset_rect(Rect e);
Rect winamp_eq_reset_rect(Rect e);
Rect winamp_eq_preamp_rect(Rect e);
Rect winamp_eq_band_rect(Rect e, int band);

/* Bottom deck (Playlist / Mixer / Formats) hit rects */
Rect winamp_bot_tab_rect(Rect p, int idx);
Rect winamp_pl_row_rect(Rect p, int row);
Rect winamp_pl_btn_rect(Rect p, int idx);
Rect winamp_bus_bar_rect(Rect p, int bus);
Rect winamp_bus_minus_rect(Rect p, int bus);
Rect winamp_bus_plus_rect(Rect p, int bus);
Rect winamp_mute_rect(Rect p);
Rect winamp_snd_btn_rect(Rect p, int idx);
Rect winamp_fmt_nav_rect(Rect p, int next);

void winamp_skin_draw(Fb *fb, const WinampState *st, AudioMediaPlayer *player,
                      const char *current_path);

#endif
