#include "browser_window_int.h"
#include "browser_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void bevel(Fb *fb, Rect r, int pressed) {
    fb_fill(fb, r.x, r.y, r.w, r.h, pressed ? 0xFF9C9C9Cu : 0xFFDDDDDDu);
    fb_frame(fb, r.x, r.y, r.w, r.h, C_BLACK);
    fb_hline(fb, r.x + 1, r.y + 1, r.w - 2, pressed ? 0xFF777777u : C_WHITE);
    fb_hline(fb, r.x + 1, r.y + r.h - 2, r.w - 2, pressed ? C_WHITE : 0xFF888888u);
}

static void arrow(Fb *fb, Rect r, int dir, uint32_t c) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    for (int i = 0; i < 7; i++) fb_fill(fb, dir < 0 ? cx - 6 + i : cx + 6 - i, cy - i, 1, 2 * i + 1, c);
    fb_fill(fb, dir < 0 ? cx : cx - 7, cy - 2, 8, 5, c);
}

static void glyph_reload(Fb *fb, Rect r, int stop) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    if (stop) { fb_fill(fb, cx - 6, cy - 6, 13, 13, 0xFFCC2222u); return; }
    uint32_t c = 0xFF2B5DBFu;
    fb_frame(fb, cx - 6, cy - 6, 13, 13, c);
    fb_frame(fb, cx - 5, cy - 5, 11, 11, c);
    fb_fill(fb, cx + 1, cy - 7, 7, 5, 0xFFDDDDDDu);
    for (int i = 0; i < 4; i++) fb_fill(fb, cx + 2 + i, cy - 8 + i, 1, 8 - 2 * i, c);
}

static void glyph_home(Fb *fb, Rect r) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    uint32_t c = 0xFFC0702Au;
    for (int i = 0; i < 7; i++) fb_fill(fb, cx - i, cy - 7 + i, 2 * i + 1, 1, c);
    fb_fill(fb, cx - 5, cy, 11, 7, c);
    fb_fill(fb, cx - 1, cy + 3, 3, 4, 0xFFDDDDDDu);
}

static void cross(Fb *fb, Rect r, uint32_t c) {
    for (int i = 0; i < r.w - 2; i++) {
        fb_fill(fb, r.x + 1 + i, r.y + 1 + i, 1, 1, c);
        fb_fill(fb, r.x + r.w - 2 - i, r.y + 1 + i, 1, 1, c);
    }
}

static void clipped_text(Fb *fb, Rect r, int x, int y, uint32_t c, const char *s) {
    fb_clip(fb, r.x, r.y, r.w, r.h);
    fb_text(fb, x, y, c, s);
    fb_unclip(fb);
}

static void paint_title(Fb *fb, BrowserWin *b, int key) {
    fb_fill(fb, b->x, b->y, b->w, BW_TITLE, C_BLACK);
    for (int i = 1; i < BW_TITLE - 1; i++) {
        uint32_t c = !key ? 0xFFDDDDDDu : i == 1 ? C_WHITE : i == BW_TITLE - 2 ? 0xFFDCDCDCu :
                     (i & 1) ? C_STRIPE_B : C_STRIPE_A;
        fb_hline(fb, b->x + 1, b->y + i, b->w - 2, c);
    }
    if (key) bevel(fb, bw_close_rect(b), BS.pressed == P_CLOSE && BS.press_win == (int)(b - BS.win));
    BrowserTab *tab = b->ntabs ? &b->tabs[b->active] : NULL;
    char title[300];
    snprintf(title, sizeof title, " %s ", tab ? tab->title : "Chromium - OS999 Edition");
    int tw = fb_text_width(title);
    if (tw > b->w - 80) tw = b->w - 80;
    int tx = b->x + (b->w - tw) / 2;
    fb_fill(fb, tx, b->y + 2, tw, BW_TITLE - 4, 0xFFDDDDDDu);
    clipped_text(fb, (Rect){tx, b->y, tw, BW_TITLE}, tx, b->y + 4, key ? C_BLACK : 0xFF777777u, title);
}

static void paint_tabs(Fb *fb, BrowserWin *b, int w) {
    int y = b->y + BW_TITLE;
    fb_fill(fb, b->x + 1, y, b->w - 2, BW_TABS, 0xFFBBBBBBu);
    for (int i = 0; i < b->ntabs; i++) {
        Rect r = bw_tab_rect(b, i);
        int on = i == b->active;
        fb_fill(fb, r.x, r.y, r.w, r.h + (on ? 2 : 0), on ? 0xFFDDDDDDu : 0xFFC8C8C8u);
        fb_frame(fb, r.x, r.y, r.w, r.h + (on ? 2 : 0), on ? C_BLACK : 0xFF777777u);
        if (on) fb_hline(fb, r.x + 1, r.y + r.h + 1, r.w - 2, 0xFFDDDDDDu);
        Rect tr = {r.x + 4, r.y, r.w - 22, r.h};
        clipped_text(fb, tr, r.x + 6, r.y + 3, on ? C_BLACK : 0xFF444444u, b->tabs[i].title);
        Rect x = bw_tabx_rect(b, i);
        int held = BS.pressed == P_TABX + i && BS.press_win == w;
        if (held) fb_fill(fb, x.x, x.y, x.w, x.h, 0xFF999999u);
        cross(fb, x, 0xFF333333u);
    }
    if (b->ntabs < BW_MAX_TABS) {
        Rect p = bw_plus_rect(b);
        bevel(fb, p, BS.pressed == P_PLUS && BS.press_win == w);
        fb_fill(fb, p.x + p.w / 2 - 4, p.y + p.h / 2, 9, 1, C_BLACK);
        fb_fill(fb, p.x + p.w / 2, p.y + p.h / 2 - 4, 1, 9, C_BLACK);
    }
    fb_hline(fb, b->x + 1, y + BW_TABS - 1 + 1, b->w - 2, 0xFF888888u);
}

static void paint_toolbar(Fb *fb, BrowserWin *b, int w) {
    BrowserTab *tab = b->ntabs ? &b->tabs[b->active] : NULL;
    int y = b->y + BW_TITLE + BW_TABS;
    fb_fill(fb, b->x + 1, y, b->w - 2, BW_TOOL, 0xFFDDDDDDu);
    for (int i = 0; i < 6; i++) {
        Rect r = bw_button_rect(b, i);
        int enabled = i == 0 ? tab && tab->back : i == 1 ? tab && tab->forward : 1;
        bevel(fb, r, BS.pressed == P_BACK + i && BS.press_win == w);
        uint32_t green = enabled ? 0xFF3FA444u : 0xFFAAAAAAu;
        if (i == 0) arrow(fb, r, -1, green);
        else if (i == 1) arrow(fb, r, 1, green);
        else if (i == 2) glyph_reload(fb, r, tab && tab->loading);
        else if (i == 3) glyph_home(fb, r);
        else fb_text(fb, r.x + 8, r.y + 5, C_BLACK, i == 4 ? "+" : "A");
    }
    Rect u = bw_url_rect(b);
    fb_fill(fb, u.x, u.y, u.w, u.h, C_WHITE);
    fb_frame(fb, u.x - 1, u.y - 1, u.w + 2, u.h + 2, C_BLACK);
    const char *shown = b->editing ? b->edit : tab ? tab->url : "";
    int sw = fb_text_width(shown), off = 0;
    if (b->editing && sw > u.w - 10) off = sw - (u.w - 10);
    fb_clip(fb, u.x + 3, u.y, u.w - 6, u.h);
    fb_text(fb, u.x + 4 - off, u.y + 3, C_BLACK, shown);
    if (b->editing) {
        if (b->edit_fresh) fb_invert(fb, u.x + 3, u.y + 2, sw + 2 > u.w - 6 ? u.w - 6 : sw + 2, u.h - 4);
        else fb_fill(fb, u.x + 5 + sw - off, u.y + 2, 1, u.h - 4, C_BLACK);
    }
    fb_unclip(fb);
    fb_hline(fb, b->x + 1, y + BW_TOOL - 1, b->w - 2, C_BLACK);
}

static void paint_status(Fb *fb, BrowserWin *b, int w) {
    BrowserTab *tab = b->ntabs ? &b->tabs[b->active] : NULL;
    uint32_t now = SDL_GetTicks();
    if (now - b->last_ms >= 1000) {  /* frames OS999 actually received and presented */
        uint64_t f = overlay_frames(w);
        b->shown_fps = (int)((f - b->last_frames) * 1000 / (now - b->last_ms ? now - b->last_ms : 1));
        b->last_frames = f;
        b->last_ms = now;
        if (getenv("OSPI_BROWSER_LOG") && BS.fd >= 0) {
#ifdef OS999_ENABLE_TEST_HOOKS
            int sw = 0, sh = 0; SurfaceStats st;
            if (overlay_probe(w, &sw, &sh, &st) == 0)
                fprintf(stderr, "os999-browser window %d presented %d fps, frames %llu, surface %dx%d colors=%d mean=%d range=%d..%d\n",
                        w, b->shown_fps, (unsigned long long)f, sw, sh, st.distinct, st.mean, st.min, st.max);
            else
#endif
            fprintf(stderr, "os999-browser window %d presented %d fps\n", w, b->shown_fps);
        }
    }
    int sy = b->y + b->h - BW_STATUS - 1;
    fb_fill(fb, b->x + 1, sy, b->w - 2, BW_STATUS, 0xFFDDDDDDu);
    fb_hline(fb, b->x + 1, sy, b->w - 2, C_BLACK);
    char line[400];
    /* Version guard: always while blocking, briefly for "update available". */
    int show_security = BS.security[0] &&
                        (BS.security_block || BS.security_level == 3 || now - BS.security_ms < 30000);
    if (b->notice[0] && now - b->notice_ms < 8000) snprintf(line, sizeof line, "%s", b->notice);
    else if (tab && tab->tls_message[0]) snprintf(line, sizeof line, "%s", tab->tls_message);
    else if (show_security) snprintf(line, sizeof line, "%s", BS.security);
    else snprintf(line, sizeof line, "OS999 %d fps  |  %s%s", b->shown_fps,
                  tab && tab->verified && !b->editing ? "Verified HTTPS | " : "",
                  tab && tab->loading ? "Loading..." : BS.status[0] ? BS.status : "Ready");
    clipped_text(fb, (Rect){b->x + 6, sy + 1, b->w - 30, BW_STATUS}, b->x + 6, sy + 2, 0xFF333333u, line);
    Rect g = bw_grow_rect(b);
    for (int i = 0; i < 3; i++) {
        fb_fill(fb, g.x + 3 + i * 4, g.y + 11 - i * 4, 9 - i * 4, 1, C_WHITE);
        fb_fill(fb, g.x + 3 + i * 4, g.y + 12 - i * 4, 9 - i * 4, 1, 0xFF777777u);
    }
}

void bw_paint(Fb *fb, int w, int key) {
    BrowserWin *b = &BS.win[w];
    Rect c = bw_content(b);
    if (b->fullscreen) {
        fb_fill(fb, 0, 0, OSPI_W, OSPI_H, C_BLACK);
        return;
    }
    fb_fill(fb, b->x + 3, b->y + 3, b->w, b->h, 0xFF26304Au);
    fb_fill(fb, b->x, b->y, b->w, b->h, 0xFFDDDDDDu);
    fb_frame(fb, b->x, b->y, b->w, b->h, C_BLACK);
    paint_title(fb, b, key);
    paint_tabs(fb, b, w);
    paint_toolbar(fb, b, w);
    fb_fill(fb, c.x, c.y, c.w, c.h, C_WHITE);
    const char *msg = BS.error[0] ? BS.error : "Starting Chromium...";
    if (BS.error[0] || overlay_frames(w) == 0) fb_text(fb, c.x + 16, c.y + 16, C_BLACK, msg);
    paint_status(fb, b, w);
}
