#ifndef OS999_BROWSER_WINDOW_INT_H
#define OS999_BROWSER_WINDOW_INT_H

/* Embedded Chromium: desktop-side model shared by browser_*.c.
 * One helper process serves up to BW_MAX_WIN windows of BW_MAX_TABS tabs. */

#include <stddef.h>
#include "browser_window.h"
#include "draw.h"
#include "os999_browser_ipc.h"

enum { BW_MAX_WIN = 4, BW_MAX_TABS = 8 };
enum { BW_MAX_OCCLUDERS = 22 + OSPI_MAX_WIN };
enum { BW_TITLE = 20, BW_TABS = 20, BW_TOOL = 28, BW_STATUS = 16, BW_MIN_W = 360, BW_MIN_H = 240 };
enum { BW_HEAD = BW_TITLE + BW_TABS + BW_TOOL };
enum { F_SHIFT = 1 << 1, F_CTRL = 1 << 2, F_ALT = 1 << 3, F_LEFT = 1 << 4,
       F_MIDDLE = 1 << 5, F_RIGHT = 1 << 6, F_CMD = 1 << 7 };
/* pressed control ids */
enum { P_NONE, P_BACK, P_FORWARD, P_RELOAD, P_HOME, P_INSTALL, P_APPS, P_CLOSE, P_PLUS, P_TAB = 100, P_TABX = 200 };

typedef struct {
    int id;
    char title[256], url[OS999B_TEXT];
    int loading, back, forward, cursor, verified;
    char tls_message[120];
    int sent_w, sent_h, sent_scale;
} BrowserTab;

typedef struct {
    int open, fullscreen;
    int x, y, w, h, sx, sy, sw, sh;
    BrowserTab tabs[BW_MAX_TABS];
    int ntabs, active;
    int editing, edit_fresh;
    char edit[OS999B_TEXT], edit_undo[OS999B_TEXT];
    int edit_undo_valid, edit_undo_fresh;
    char surface[128];
    uint64_t last_frames;
    uint32_t last_ms;
    int shown_fps;
    char notice[200];
    uint32_t notice_ms;
} BrowserWin;

typedef struct {
    int pid, fd, ready;
    Os999BrowserMsg in;
    size_t in_len;
    BrowserWin win[BW_MAX_WIN];
    int order[BW_MAX_WIN], norder;  /* z-order, bottom first */
    int input_id;
    int focus_win, focus;           /* keyboard to that window's page */
    int overlay_focus;              /* restore page focus after desktop overlays */
    int drag, grow, capture, pressed, hover, press_win, composing;
    int px, py, ow, oh;
    int next_id;
    char status[160], error[200], root[OSPI_PATH];
    char security[200];             /* version-guard message (B06) */
    int security_level, security_block;
    uint32_t security_ms;
} BrowserState;

extern BrowserState BS;

/* browser_host.c: the helper process */
int bh_ensure(const char *root);
void bh_send(uint32_t kind, int id, int x, int y, int a, int b, const char *text);
void bh_send_full(Os999BrowserMsg *m);
void bh_drain(void);
void bh_stop(void);

/* browser_window.c: windows and tabs */
void bw_application_sync(Session *s);
void bw_publish_menu_state(Session *s);
void bw_menu_edit(int action);
void bw_cancel_interaction(void);
int bw_open_window(const char *url);
void bw_close_window(int w);
int bw_new_tab(int w, const char *url, int activate);
void bw_close_tab(int w, int t);
void bw_activate_tab(int w, int t);
void bw_raise(int w);
int bw_top(void);
void bw_set_fullscreen(int w, int on);
int bw_find_tab(int id, int *t);
void bw_navigate(int w, const char *text);
BrowserTab *bw_tab(int w);

/* browser_script.c: test hook */
void bw_script_tick(Session *s, SDL_Window *win);

/* browser_paint.c */
void bw_paint(Fb *fb, int w, int key);

/* geometry */
static inline Rect bw_outer(const BrowserWin *b) { return (Rect){b->x, b->y, b->w, b->h}; }
static inline Rect bw_content(const BrowserWin *b) {
    if (b->fullscreen) return (Rect){0, 0, OSPI_W, OSPI_H};
    return (Rect){b->x + 1, b->y + BW_HEAD, b->w - 2, b->h - BW_HEAD - BW_STATUS - 1};
}
static inline Rect bw_close_rect(const BrowserWin *b) { return (Rect){b->x + 8, b->y + 4, 12, 12}; }
static inline int bw_tab_w(const BrowserWin *b) {
    int n = b->ntabs > 0 ? b->ntabs : 1, w = (b->w - 34) / n;
    return w > 180 ? 180 : w;
}
static inline Rect bw_tab_rect(const BrowserWin *b, int i) {
    int tw = bw_tab_w(b);
    return (Rect){b->x + 4 + i * tw, b->y + BW_TITLE + 2, tw - 2, BW_TABS - 2};
}
static inline Rect bw_tabx_rect(const BrowserWin *b, int i) {
    Rect r = bw_tab_rect(b, i);
    return (Rect){r.x + r.w - 14, r.y + 4, 10, 10};
}
static inline Rect bw_plus_rect(const BrowserWin *b) {
    Rect r = bw_tab_rect(b, b->ntabs);
    return (Rect){r.x, r.y + 1, 22, BW_TABS - 4};
}
static inline Rect bw_button_rect(const BrowserWin *b, int i) {
    return (Rect){b->x + 6 + i * 30, b->y + BW_TITLE + BW_TABS + 3, 26, 22};
}
static inline Rect bw_url_rect(const BrowserWin *b) {
    return (Rect){b->x + 190, b->y + BW_TITLE + BW_TABS + 5, b->w - 198, 18};
}
static inline Rect bw_grow_rect(const BrowserWin *b) { return (Rect){b->x + b->w - 15, b->y + b->h - 15, 14, 14}; }
static inline int bw_has(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

#endif
