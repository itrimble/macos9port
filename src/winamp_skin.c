#include "winamp_skin.h"
#include <stdio.h>
#include <string.h>

enum {
    C_WA_BG      = 0xFF232735u,
    C_WA_PANEL   = 0xFF2E3346u,
    C_WA_HILITE  = 0xFF56607Eu,
    C_WA_SHADOW  = 0xFF12151Eu,
    C_WA_GOLD    = 0xFFD9A94Au,
    C_WA_BRONZE  = 0xFF76571Eu,
    C_WA_LCD     = 0xFF080C10u,
    C_WA_LCD_BDR = 0xFF18222Eu,
    C_WA_GREEN   = 0xFF00E84Au,
    C_WA_DIMGRN  = 0xFF0A4820u,
    C_WA_YELLOW  = 0xFFF8E018u,
    C_WA_ORANGE  = 0xFFFF8810u,
    C_WA_RED     = 0xFFFF2818u,
    C_WA_CYAN    = 0xFF68E8FFu,
    C_WA_BTN     = 0xFFAEB6C8u,
    C_WA_BTN_ACT = 0xFFDCE2F0u,
    C_WA_SEL_BG  = 0xFF163878u
};

Rect winamp_main_rect(const WinampState *st) {
    return (Rect){st->x, st->y, WINAMP_W, st->shaded ? WINAMP_SHADE_H : WINAMP_MAIN_H};
}

Rect winamp_eq_rect(const WinampState *st) {
    Rect m = winamp_main_rect(st);
    return (Rect){st->x, m.y + m.h, WINAMP_W, st->show_eq ? WINAMP_EQ_H : 0};
}

Rect winamp_pl_rect(const WinampState *st) {
    Rect e = winamp_eq_rect(st);
    return (Rect){st->x, e.y + e.h, WINAMP_W, st->show_pl ? WINAMP_PL_H : 0};
}

Rect winamp_total_rect(const WinampState *st) {
    Rect m = winamp_main_rect(st);
    Rect e = winamp_eq_rect(st);
    Rect p = winamp_pl_rect(st);
    return (Rect){st->x, st->y, WINAMP_W, m.h + e.h + p.h};
}

Rect winamp_close_rect(Rect m) { return (Rect){m.x + m.w - 18, m.y + 3, 14, 14}; }
Rect winamp_shade_rect(Rect m) { return (Rect){m.x + m.w - 35, m.y + 3, 14, 14}; }
Rect winamp_clock_rect(Rect m) { return (Rect){m.x + 18, m.y + 28, 120, 24}; }
Rect winamp_vis_rect(Rect m)   { return (Rect){m.x + 18, m.y + 54, 184, 34}; }
Rect winamp_vol_rect(Rect m)   { return (Rect){m.x + 252, m.y + 74, 118, 14}; }
Rect winamp_bal_rect(Rect m)   { return (Rect){m.x + 412, m.y + 74, 116, 14}; }
Rect winamp_seek_rect(Rect m)  { return (Rect){m.x + 12, m.y + 98, 516, 12}; }

Rect winamp_transport_rect(Rect m, int idx) {
    static const int xs[11] = {12, 54, 96, 138, 180, 226, 280, 330, 376, 416, 456};
    static const int ws[11] = {40, 40, 40,  40,  40,  48,  48,  44,  38,  38,  72};
    if (idx < 0 || idx >= 11) return (Rect){0, 0, 0, 0};
    return (Rect){m.x + xs[idx], m.y + 116, ws[idx], 24};
}

Rect winamp_eq_on_rect(Rect e)     { return (Rect){e.x + 12, e.y + 23, 44, 20}; }
Rect winamp_eq_auto_rect(Rect e)   { return (Rect){e.x + 60, e.y + 23, 52, 20}; }
Rect winamp_eq_preset_rect(Rect e) { return (Rect){e.x + 244, e.y + 23, 196, 20}; }
Rect winamp_eq_reset_rect(Rect e)  { return (Rect){e.x + 446, e.y + 23, 82, 20}; }
Rect winamp_eq_preamp_rect(Rect e) { return (Rect){e.x + 18, e.y + 49, 28, 62}; }
Rect winamp_eq_band_rect(Rect e, int band) {
    return (Rect){e.x + 70 + band * 46, e.y + 49, 28, 62};
}

Rect winamp_bot_tab_rect(Rect p, int idx) {
    if (idx == 3) return (Rect){p.x + 426, p.y + 22, 102, 20};
    return (Rect){p.x + 12 + idx * 116, p.y + 22, 112, 20};
}
Rect winamp_pl_row_rect(Rect p, int row) {
    return (Rect){p.x + 14, p.y + 48 + row * 20, 512, 20};
}
Rect winamp_pl_btn_rect(Rect p, int idx) {
    static const int xs[4] = {12, 116, 224, 278};
    static const int ws[4] = {100, 104, 50, 56};
    return (Rect){p.x + xs[idx], p.y + 156, ws[idx], 22};
}
Rect winamp_bus_minus_rect(Rect p, int b) { return (Rect){p.x + 114, p.y + 52 + b * 24, 22, 18}; }
Rect winamp_bus_bar_rect(Rect p, int b)   { return (Rect){p.x + 142, p.y + 54 + b * 24, 210, 14}; }
Rect winamp_bus_plus_rect(Rect p, int b)  { return (Rect){p.x + 358, p.y + 52 + b * 24, 22, 18}; }
Rect winamp_mute_rect(Rect p)             { return (Rect){p.x + 438, p.y + 52, 86, 20}; }
Rect winamp_snd_btn_rect(Rect p, int i)   { return (Rect){p.x + 14 + i * 104, p.y + 148, 98, 24}; }
Rect winamp_fmt_nav_rect(Rect p, int nxt) { return (Rect){p.x + (nxt ? 444 : 356), p.y + 158, 82, 20}; }

static void draw_bevel(Fb *fb, Rect r, uint32_t fill, int sunken) {
    uint32_t tl = sunken ? C_WA_SHADOW : C_WA_HILITE;
    uint32_t br = sunken ? C_WA_HILITE : C_WA_SHADOW;
    fb_fill(fb, r.x, r.y, r.w, r.h, fill);
    fb_hline(fb, r.x, r.y, r.w, tl);
    fb_fill(fb, r.x, r.y, 1, r.h, tl);
    fb_hline(fb, r.x, r.y + r.h - 1, r.w, br);
    fb_fill(fb, r.x + r.w - 1, r.y, 1, r.h, br);
}

static void draw_deck_shell(Fb *fb, Rect r, const char *title, int top_deck) {
    fb_fill(fb, r.x + 4, r.y + 4, r.w, r.h, 0xFF10141Eu);
    draw_bevel(fb, r, C_WA_BG, 0);
    fb_frame(fb, r.x + 2, r.y + 2, r.w - 4, r.h - 4, C_WA_PANEL);
    Rect tb = {r.x + 3, r.y + 3, r.w - 6, 16};
    fb_fill(fb, tb.x, tb.y, tb.w, tb.h, C_WA_SHADOW);
    for (int y = 3; y <= 12; y += 3) {
        fb_hline(fb, tb.x + 6, tb.y + y, tb.w - (top_deck ? 46 : 12), C_WA_GOLD);
        fb_hline(fb, tb.x + 6, tb.y + y + 1, tb.w - (top_deck ? 46 : 12), C_WA_BRONZE);
    }
    int tw = fb_text_width(title) + 12;
    int tx = tb.x + (tb.w - tw) / 2;
    fb_fill(fb, tx, tb.y, tw, tb.h, C_WA_SHADOW);
    fb_text(fb, tx + 6, tb.y + 1, C_WA_GOLD, title);
}

static void draw_wa_btn(Fb *fb, Rect r, const char *label, int led_state) {
    draw_bevel(fb, r, led_state > 0 ? C_WA_BTN_ACT : C_WA_BTN, 0);
    if (led_state >= 0) {
        fb_fill(fb, r.x + 3, r.y + 3, 5, 5, led_state ? C_WA_GREEN : C_WA_DIMGRN);
    }
    int tx = r.x + (r.w - fb_text_width(label)) / 2 + (led_state >= 0 ? 3 : 0);
    int ty = r.y + (r.h - typeface_line_height(0)) / 2;
    fb_text(fb, tx, ty, C_BLACK, label);
}

static void draw_7seg_digit(Fb *fb, int x, int y, int d, uint32_t on_c, uint32_t off_c) {
    static const uint8_t segs[11] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40
    };
    uint8_t m = (d >= 0 && d <= 10) ? segs[d] : 0;
    fb_fill(fb, x + 2, y,      8, 2, (m & 0x01) ? on_c : off_c);
    fb_fill(fb, x + 10, y + 2, 2, 7, (m & 0x02) ? on_c : off_c);
    fb_fill(fb, x + 10, y + 11,2, 7, (m & 0x04) ? on_c : off_c);
    fb_fill(fb, x + 2, y + 18, 8, 2, (m & 0x08) ? on_c : off_c);
    fb_fill(fb, x,     y + 11, 2, 7, (m & 0x10) ? on_c : off_c);
    fb_fill(fb, x,     y + 2,  2, 7, (m & 0x20) ? on_c : off_c);
    fb_fill(fb, x + 2, y + 9,  8, 2, (m & 0x40) ? on_c : off_c);
}

static void draw_lcd_and_vis(Fb *fb, Rect m, const WinampState *st,
                             double pos_s, double dur_s, int playing) {
    Rect lcd = {m.x + 12, m.y + 24, 200, 68};
    draw_bevel(fb, lcd, C_WA_LCD, 1);

    /* Play/Pause indicator + 7-segment MM:SS clock */
    fb_fill(fb, lcd.x + 8, lcd.y + 8, 8, 8, playing ? C_WA_GREEN : C_WA_YELLOW);
    double disp = st->time_remaining && dur_s > pos_s ? (dur_s - pos_s) : pos_s;
    if (disp < 0.0) disp = 0.0;
    int sec = (int)disp;
    int mm = (sec / 60) % 100, ss = sec % 60;
    int cx = lcd.x + 26, cy = lcd.y + 5;
    if (st->time_remaining) draw_7seg_digit(fb, cx - 12, cy, 10, C_WA_GREEN, C_WA_DIMGRN);
    draw_7seg_digit(fb, cx,      cy, mm / 10, C_WA_GREEN, C_WA_DIMGRN);
    draw_7seg_digit(fb, cx + 16, cy, mm % 10, C_WA_GREEN, C_WA_DIMGRN);
    fb_fill(fb, cx + 32, cy + 5,  3, 3, C_WA_GREEN);
    fb_fill(fb, cx + 32, cy + 13, 3, 3, C_WA_GREEN);
    draw_7seg_digit(fb, cx + 38, cy, ss / 10, C_WA_GREEN, C_WA_DIMGRN);
    draw_7seg_digit(fb, cx + 54, cy, ss % 10, C_WA_GREEN, C_WA_DIMGRN);

    /* Visualizer area (Spectrum Analyzer or Oscilloscope) */
    Rect vr = winamp_vis_rect(m);
    for (int gy = vr.y; gy < vr.y + vr.h; gy += 2)
        for (int gx = vr.x; gx < vr.x + vr.w; gx += 2)
            fb_fill(fb, gx, gy, 1, 1, 0xFF102018u);

    uint8_t spec[AUDIO_VIS_BANDS], peaks[AUDIO_VIS_BANDS];
    int8_t wave[AUDIO_VIS_WAVE];
    audio_get_visualizer(spec, peaks, wave);

    if (st->vis_mode == 0) {
        for (int b = 0; b < AUDIO_VIS_BANDS; b++) {
            int bx = vr.x + 4 + b * 11;
            int h = playing ? spec[b] : 0;
            for (int seg = 0; seg < h && seg < 30; seg += 2) {
                uint32_t c = seg > 22 ? C_WA_RED : (seg > 14 ? C_WA_YELLOW : C_WA_GREEN);
                fb_fill(fb, bx, vr.y + vr.h - 3 - seg, 9, 2, c);
            }
            int pk = playing ? peaks[b] : 0;
            if (pk > 0 && pk < 31)
                fb_fill(fb, bx, vr.y + vr.h - 3 - pk, 9, 2, C_WA_CYAN);
        }
    } else {
        int mid_y = vr.y + vr.h / 2;
        fb_hline(fb, vr.x + 2, mid_y, vr.w - 4, C_WA_DIMGRN);
        for (int i = 0; i < AUDIO_VIS_WAVE; i++) {
            int wx = vr.x + 4 + (i * (vr.w - 8)) / AUDIO_VIS_WAVE;
            int wy = mid_y - (playing ? wave[i] : 0);
            uint32_t c = (wave[i] > 10 || wave[i] < -10) ? C_WA_RED :
                         ((wave[i] > 5 || wave[i] < -5) ? C_WA_YELLOW : C_WA_GREEN);
            fb_fill(fb, wx, wy, 2, 2, c);
        }
    }
}

static void draw_main_deck(Fb *fb, const WinampState *st, AudioMediaPlayer *player,
                           const char *current_path) {
    Rect m = winamp_main_rect(st);
    draw_deck_shell(fb, m, "OS999 AMP", 1);
    draw_wa_btn(fb, winamp_shade_rect(m), st->shaded ? "+" : "_", -1);
    draw_wa_btn(fb, winamp_close_rect(m), "X", -1);
    if (st->shaded) return;

    AudioMetadata meta = {0};
    double pos_s = 0.0, dur_s = 0.0;
    int playing = 0;
    if (player) {
        audio_player_metadata(player, &meta);
        pos_s = audio_player_position(player);
        dur_s = audio_player_duration(player);
        playing = audio_player_is_playing(player);
    }
    draw_lcd_and_vis(fb, m, st, pos_s, dur_s, playing);

    /* Marquee ticker box */
    Rect mq = {m.x + 220, m.y + 24, 308, 22};
    draw_bevel(fb, mq, C_WA_LCD, 1);
    const char *base = strrchr(current_path ? current_path : "", '/');
    base = base ? base + 1 : (current_path && *current_path ? current_path : "OS999 Amp Ready");
    char ticker[320];
    snprintf(ticker, sizeof ticker, "%d. %s (%d:%02d)  ***  [%s %dHz %s]  ***  ",
             st->current_track + 1, meta.title[0] ? meta.title : base,
             (int)dur_s / 60, (int)dur_s % 60,
             meta.codec_name[0] ? meta.codec_name : "PCM",
             meta.sample_rate ? meta.sample_rate : 48000,
             meta.channel_layout_desc[0] ? meta.channel_layout_desc : "stereo");
    int tlen = (int)strlen(ticker);
    int shift = tlen > 0 ? (int)((st->tick_count / 6) % (uint32_t)tlen) : 0;
    char vis_ticker[64];
    for (int i = 0; i < 42; i++) vis_ticker[i] = ticker[(shift + i) % (tlen > 0 ? tlen : 1)];
    vis_ticker[42] = 0;
    fb_clip(fb, mq.x + 4, mq.y + 2, mq.w - 8, mq.h - 4);
    fb_text(fb, mq.x + 6, mq.y + 4, C_WA_GREEN, vis_ticker);
    fb_unclip(fb);

    /* Bitrate / Sample rate / Stereo indicators */
    Rect br_box = {m.x + 220, m.y + 50, 52, 18};
    Rect sr_box = {m.x + 296, m.y + 50, 44, 18};
    draw_bevel(fb, br_box, C_WA_LCD, 1);
    draw_bevel(fb, sr_box, C_WA_LCD, 1);
    char sr_txt[16];
    snprintf(sr_txt, sizeof sr_txt, "%d", (meta.sample_rate ? meta.sample_rate : 48000) / 1000);
    fb_text(fb, br_box.x + 6, br_box.y + 2, C_WA_GREEN, player ? "192" : "---");
    fb_text(fb, br_box.x + 55, br_box.y + 2, C_WA_GOLD, "kbps");
    fb_text(fb, sr_box.x + 6, sr_box.y + 2, C_WA_GREEN, sr_txt);
    fb_text(fb, sr_box.x + 47, sr_box.y + 2, C_WA_GOLD, "kHz");
    fb_text(fb, m.x + 382, m.y + 52, meta.channels == 1 ? C_WA_GREEN : C_WA_DIMGRN, "MONO");
    fb_text(fb, m.x + 440, m.y + 52, meta.channels != 1 ? C_WA_GREEN : C_WA_DIMGRN, "STEREO");

    /* Volume and Balance sliders */
    fb_text(fb, m.x + 220, m.y + 74, C_WA_GOLD, "VOL");
    Rect vr = winamp_vol_rect(m);
    float vol = audio_get_bus_volume(AUDIO_BUS_MEDIA);
    uint32_t vc = vol > 0.8f ? C_WA_ORANGE : (vol > 0.4f ? C_WA_YELLOW : C_WA_GREEN);
    draw_bevel(fb, vr, C_WA_LCD, 1);
    fb_fill(fb, vr.x + 2, vr.y + 2, (int)((vr.w - 4) * vol), vr.h - 4, vc);
    int vtx = vr.x + (int)((vr.w - 12) * vol);
    draw_bevel(fb, (Rect){vtx, vr.y - 1, 12, vr.h + 2}, C_WA_BTN_ACT, 0);

    fb_text(fb, m.x + 378, m.y + 74, C_WA_GOLD, "BAL");
    Rect bl = winamp_bal_rect(m);
    float pan = audio_get_media_balance();
    draw_bevel(fb, bl, C_WA_LCD, 1);
    fb_fill(fb, bl.x + bl.w / 2, bl.y + 2, 2, bl.h - 4, C_WA_GREEN);
    int btx = bl.x + (int)((bl.w - 12) * ((pan + 1.0f) * 0.5f));
    draw_bevel(fb, (Rect){btx, bl.y - 1, 12, bl.h + 2}, C_WA_BTN_ACT, 0);

    /* Position Seek Bar */
    Rect sk = winamp_seek_rect(m);
    draw_bevel(fb, sk, C_WA_LCD, 1);
    double pct = (dur_s > 0.0 && pos_s >= 0.0) ? (pos_s / dur_s) : 0.0;
    if (pct > 1.0) pct = 1.0;
    fb_fill(fb, sk.x + 2, sk.y + 3, (int)((sk.w - 4) * pct), sk.h - 6, C_WA_GOLD);
    int stx = sk.x + (int)((sk.w - 22) * pct);
    draw_bevel(fb, (Rect){stx, sk.y - 1, 22, sk.h + 2}, C_WA_BTN_ACT, 0);

    /* Transport & Toggle Buttons */
    static const char *tlabels[11] = {
        "|<<", "|>", "||", "[]", ">>|", "EJECT", "SHUF", "REP", "EQ", "PL", "BUS/FMT"
    };
    int leds[11] = {
        -1, playing ? 1 : -1, (!playing && pos_s > 0.0) ? 1 : -1, -1, -1, -1,
        st->shuffle, st->repeat, st->show_eq, st->show_pl, st->bottom_mode != 0
    };
    for (int i = 0; i < 11; i++)
        draw_wa_btn(fb, winamp_transport_rect(m, i), tlabels[i], leds[i]);
}

static void draw_eq_deck(Fb *fb, const WinampState *st) {
    if (!st->show_eq) return;
    Rect e = winamp_eq_rect(st);
    draw_deck_shell(fb, e, "OS999 GRAPHIC EQUALIZER", 0);

    int eq_on = audio_eq_get_enabled();
    draw_wa_btn(fb, winamp_eq_on_rect(e), "ON", eq_on);
    draw_wa_btn(fb, winamp_eq_auto_rect(e), "AUTO", audio_eq_get_auto());
    char pbuf[48];
    snprintf(pbuf, sizeof pbuf, "PRESET: %s", audio_eq_preset_name(audio_eq_get_preset()));
    draw_wa_btn(fb, winamp_eq_preset_rect(e), pbuf, -1);
    draw_wa_btn(fb, winamp_eq_reset_rect(e), "0dB FLAT", -1);

    /* Mini Live EQ Spline Graph */
    Rect gr = {e.x + 118, e.y + 22, 120, 22};
    draw_bevel(fb, gr, C_WA_LCD, 1);
    fb_hline(fb, gr.x + 2, gr.y + gr.h / 2, gr.w - 4, C_WA_DIMGRN);
    for (int b = 0; b < AUDIO_EQ_BANDS; b++) {
        float db = audio_eq_get_band(b);
        int gx = gr.x + 6 + b * 11;
        int gy = gr.y + gr.h / 2 - (int)(db * 0.7f);
        uint32_t c = db > 3.0f ? C_WA_RED : (db > -2.0f ? C_WA_YELLOW : C_WA_GREEN);
        fb_fill(fb, gx, gy, 8, 2, eq_on ? c : C_WA_DIMGRN);
    }

    /* Preamp + 10 ISO Band vertical sliders */
    for (int b = -1; b < AUDIO_EQ_BANDS; b++) {
        Rect sr = b < 0 ? winamp_eq_preamp_rect(e) : winamp_eq_band_rect(e, b);
        float db = b < 0 ? audio_eq_get_preamp() : audio_eq_get_band(b);
        draw_bevel(fb, (Rect){sr.x + 10, sr.y, 8, sr.h}, C_WA_LCD, 1);
        fb_hline(fb, sr.x + 4, sr.y + sr.h / 2, 20, C_WA_HILITE);
        float norm = (12.0f - db) / 24.0f;
        int ty = sr.y + (int)((sr.h - 10) * norm);
        uint32_t tc = db > 4.0f ? C_WA_ORANGE : (db < -4.0f ? C_WA_CYAN : C_WA_BTN_ACT);
        draw_bevel(fb, (Rect){sr.x + 2, ty, 24, 10}, tc, 0);
        const char *lbl = b < 0 ? "PRE" : audio_eq_band_label(b);
        fb_text(fb, sr.x + (sr.w - fb_text_width(lbl)) / 2, sr.y + sr.h + 2, C_WA_GOLD, lbl);
    }
}

static void draw_pl_deck(Fb *fb, const WinampState *st, const char *current_path) {
    if (!st->show_pl) return;
    Rect p = winamp_pl_rect(st);
    draw_deck_shell(fb, p, "OS999 PLAYLIST & MIXER", 0);

    static const char *tabs[4] = {"PLAYLIST", "BUS MIXER", "28 CODECS", "OPEN VLC"};
    for (int i = 0; i < 4; i++)
        draw_wa_btn(fb, winamp_bot_tab_rect(p, i), tabs[i], i < 3 ? (st->bottom_mode == i) : -1);

    Rect lcd = {p.x + 12, p.y + 46, 516, 104};
    draw_bevel(fb, lcd, C_WA_LCD, 1);

    if (st->bottom_mode == WINAMP_BOT_PLAYLIST) {
        for (int row = 0; row < WINAMP_PL_ROWS; row++) {
            int idx = st->pl_scroll + row;
            if (idx >= st->track_count) break;
            Rect rr = winamp_pl_row_rect(p, row);
            int cur = (idx == st->current_track) ||
                      (current_path && strcmp(st->tracks[idx].path, current_path) == 0);
            if (cur) fb_fill(fb, rr.x, rr.y, rr.w, rr.h - 1, C_WA_SEL_BG);
            char line[160], dur[16];
            snprintf( line, sizeof line, "%d. %s", idx + 1, st->tracks[idx].title);
            int d = (int)st->tracks[idx].duration;
            snprintf(dur, sizeof dur, "%d:%02d", d / 60, d % 60);
            fb_text(fb, rr.x + 6, rr.y + 2, cur ? C_WHITE : C_WA_GREEN, line);
            fb_text(fb, rr.x + rr.w - fb_text_width(dur) - 8, rr.y + 2,
                    cur ? C_WA_YELLOW : C_WA_GREEN, dur);
        }
        static const char *b_lbl[4] = {"+ SCAN VOL", "+ SYNTH MIDI", "UP", "DOWN"};
        for (int i = 0; i < 4; i++)
            draw_wa_btn(fb, winamp_pl_btn_rect(p, i), b_lbl[i], -1);
        char stat[48];
        snprintf(stat, sizeof stat, "%d TRACKS READY", st->track_count);
        fb_text(fb, p.x + 360, p.y + 160, C_WA_GREEN, stat);
    } else if (st->bottom_mode == WINAMP_BOT_MIXER) {
        static const char *bnames[3] = {"System Bus", "Media Bus", "App / Game"};
        for (int b = 0; b < 3; b++) {
            int y = p.y + 54 + b * 24;
            fb_text(fb, p.x + 20, y, C_WA_GREEN, bnames[b]);
            draw_wa_btn(fb, winamp_bus_minus_rect(p, b), "-", -1);
            Rect bar = winamp_bus_bar_rect(p, b);
            float v = audio_get_bus_volume((AudioBus)b);
            draw_bevel(fb, bar, 0xFF040608u, 1);
            fb_fill(fb, bar.x + 2, bar.y + 2, (int)((bar.w - 4) * v), bar.h - 4, C_WA_GREEN);
            draw_wa_btn(fb, winamp_bus_plus_rect(p, b), "+", -1);
            char pct[16];
            snprintf(pct, sizeof pct, "%d%%", (int)(v * 100.0f + 0.5f));
            fb_text(fb, p.x + 388, y, C_WA_YELLOW, pct);
        }
        int muted = audio_get_master_muted();
        draw_wa_btn(fb, winamp_mute_rect(p), muted ? "MUTED" : "MUTE", muted);
        static const char *slbls[5] = {"Startup", "Trash", "Alert", "Zoom", "Folder"};
        for (int i = 0; i < 5; i++)
            draw_wa_btn(fb, winamp_snd_btn_rect(p, i), slbls[i], -1);
    } else {
        AudioFormatSupport list[32];
        size_t n = audio_decoder_enumerate_formats(list, 32);
        size_t start = (size_t)(st->format_page * 5);
        for (size_t i = 0; i < 5 && start + i < n; i++) {
            const AudioFormatSupport *f = &list[start + i];
            int ry = lcd.y + 4 + (int)i * 19;
            fb_text(fb, lcd.x + 8, ry, C_WA_YELLOW, f->group);
            fb_text(fb, lcd.x + 128, ry, C_WHITE, f->label);
            fb_text(fb, lcd.x + 350, ry, C_WA_CYAN, f->codec_name);
            fb_text(fb, lcd.x + 448, ry, f->available ? C_WA_GREEN : C_WA_RED,
                    f->available ? "READY" : "N/A");
        }
        fb_text(fb, p.x + 14, p.y + 160, C_WA_GOLD, audio_decoder_ffmpeg_version());
        draw_wa_btn(fb, winamp_fmt_nav_rect(p, 0), "< PREV", -1);
        draw_wa_btn(fb, winamp_fmt_nav_rect(p, 1), "NEXT >", -1);
    }
}

void winamp_skin_draw(Fb *fb, const WinampState *st, AudioMediaPlayer *player,
                      const char *current_path) {
    if (!fb || !st) return;
    draw_main_deck(fb, st, player, current_path);
    draw_eq_deck(fb, st);
    draw_pl_deck(fb, st, current_path);
}
