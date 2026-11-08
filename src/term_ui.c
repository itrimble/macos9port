#include "finder_apps.h"
#include "native_apps.h"
#include "term.h"
#include "term_font.h"
#include "desktop_theme.h"
#include "session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp_i(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void calc_grid_dims(const Layout *lay, int *out_rows, int *out_cols) {
    int cw = lay->content.w - 8;
    int ch = lay->content.h - 6;
    int cols = cw > 0 ? cw / TERM_CELL_W : TERM_DEFAULT_COLS;
    int rows = ch > 0 ? ch / TERM_CELL_H : TERM_DEFAULT_ROWS;
    *out_cols = clamp_i(cols, TERM_MIN_COLS, TERM_MAX_COLS);
    *out_rows = clamp_i(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
}

static void send_bytes(TermSession *ts, const char *buf, size_t n) {
    if (!ts || !buf || n == 0) return;
    if (ts->mode == TERM_MODE_SHELL) {
        (void)term_pty_write(&ts->pty, buf, n);
    } else {
        term_console_input(&ts->console, &ts->vt, buf, n);
    }
}

Window *terminal_front(Session *s) {
    if (!s || s->nwin <= 0) return NULL;
    Window *w = &s->win[s->nwin - 1];
    return (w->kind == WIN_TERMINAL && w->term) ? w : NULL;
}

int terminal_open_custom(Session *s, int mode, char *const argv[]) {
    if (!s) return -1;
    if (s->nwin >= OSPI_MAX_WIN) {
        dialog_info(s, "Terminal", "Close a window first.");
        return -1;
    }
    TermSession *ts = (TermSession *)calloc(1, sizeof(TermSession));
    if (!ts) return -1;
    ts->mode = (mode == TERM_MODE_CONSOLE) ? TERM_MODE_CONSOLE : TERM_MODE_SHELL;
    ts->pty.master_fd = -1;

    Window *w = &s->win[s->nwin++];
    memset(w, 0, sizeof(*w));
    w->kind = WIN_TERMINAL;
    w->x = 84 + ((s->nwin - 1) % 4) * 24;
    w->y = 42 + ((s->nwin - 1) % 4) * 22;
    w->w = 8 + TERM_DEFAULT_COLS * TERM_CELL_W + 2 + SB;
    w->h = TITLE_H + INFO_H + 6 + TERM_DEFAULT_ROWS * TERM_CELL_H + 1 + SB;
    w->term = ts;
    snprintf(w->title, sizeof(w->title), "%s",
             ts->mode == TERM_MODE_CONSOLE ? "OS999 Console" : "Terminal \xe2\x80\x94 Host Shell");

    Layout lay;
    window_layout(w, &lay);
    int rows = TERM_DEFAULT_ROWS, cols = TERM_DEFAULT_COLS;
    calc_grid_dims(&lay, &rows, &cols);
    term_vt_init(&ts->vt, rows, cols);

    int launch_failed = 0;
    if (ts->mode == TERM_MODE_SHELL) {
        int rc = argv ? term_pty_open(&ts->pty, rows, cols, s->root, argv)
                      : term_pty_open_shell(&ts->pty, rows, cols, s->root);
        if (rc != 0) {
            launch_failed = 1;
            const char *err = "\x1b[1;31mCould not start host shell PTY.\x1b[0m\r\n";
            term_vt_feed(&ts->vt, err, strlen(err));
        }
    } else {
        term_console_init(&ts->console, s, &ts->vt);
    }
    s->desk_sel = -1; s->desktop_focus = 0; s->finder_hidden = 0; s->media_focused = s->browser_focused = s->guest_focused = 0; s->app_request = 1; finder_apps_note(s, 1);
    session_clamp(s);
    return launch_failed ? -1 : 0;
}

int terminal_open(Session *s, int mode) {
    int result = terminal_open_custom(s, mode, NULL);
    if (!result) native_app_note(s, mode == TERM_MODE_CONSOLE ? NATIVE_CONSOLE : NATIVE_TERMINAL);
    return result;
}

void terminal_window_close(Session *s, Window *w) {
    (void)s;
    if (!w || !w->term) return;
    TermSession *ts = w->term;
    if (ts->mode == TERM_MODE_SHELL) {
        term_pty_close(&ts->pty);
    }
    term_vt_free(&ts->vt);
    free(ts);
    w->term = NULL;
}

void terminal_sync_size(Window *w) {
    if (!w || w->kind != WIN_TERMINAL || !w->term || w->collapsed) return;
    Layout lay;
    window_layout(w, &lay);
    int rows = TERM_DEFAULT_ROWS, cols = TERM_DEFAULT_COLS;
    calc_grid_dims(&lay, &rows, &cols);
    TermSession *ts = w->term;
    if (rows != ts->vt.rows || cols != ts->vt.cols) {
        term_vt_resize(&ts->vt, rows, cols);
        if (ts->mode == TERM_MODE_SHELL && ts->pty.master_fd >= 0) {
            (void)term_pty_resize(&ts->pty, rows, cols);
        }
    }
}

int terminal_max_scroll(const Window *w) {
    if (!w || w->kind != WIN_TERMINAL || !w->term || w->collapsed) return 0;
    return w->term->vt.sb_count * TERM_CELL_H;
}

void terminal_tick(Session *s) {
    if (!s) return;
    char buf[4096];
    char reply[TERM_REPLY_MAX];
    for (int i = 0; i < s->nwin; i++) {
        Window *w = &s->win[i];
        if (w->kind != WIN_TERMINAL || !w->term) continue;
        TermSession *ts = w->term;
        ts->console.session = s;
        if (ts->mode == TERM_MODE_SHELL && ts->pty.master_fd >= 0) {
            size_t budget = 65536;
            while (budget > 0) {
                ssize_t n = term_pty_read(&ts->pty, buf, sizeof(buf));
                if (n <= 0) break;
                term_vt_feed(&ts->vt, buf, (size_t)n);
                size_t rlen = term_vt_take_reply(&ts->vt, reply, sizeof(reply));
                if (rlen > 0) (void)term_pty_write(&ts->pty, reply, rlen);
                budget = (size_t)n < budget ? budget - (size_t)n : 0;
            }
        }
        if ((ts->mode == TERM_MODE_CONSOLE && ts->console.close_requested) ||
            (ts->mode == TERM_MODE_SHELL && ts->pty.child_pid > 0 &&
             !term_pty_alive(&ts->pty) && ts->pty.exit_status == 0)) {
            close_index(s, i--); /* Only this exited utility/shell; never the desktop or VPN daemon. */
        }
    }
}

static int cell_selected(const TermSession *ts, int r, int c) {
    if (!ts || !ts->sel_active) return 0;
    int r0 = ts->sel_start_row, c0 = ts->sel_start_col;
    int r1 = ts->sel_end_row, c1 = ts->sel_end_col;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) {
        int tr = r0, tc = c0;
        r0 = r1; c0 = c1;
        r1 = tr; c1 = tc;
    }
    if (r < r0 || r > r1) return 0;
    if (r0 == r1) return c >= c0 && c <= c1;
    if (r == r0) return c >= c0;
    if (r == r1) return c <= c1;
    return 1;
}

/* Window scroll positions are measured from the oldest retained row. */
static int visible_scrollback_rows(const Window *w) {
    int count = w->term->vt.sb_count;
    if (!count || !w->scroll) return 0;
    return clamp_i((count * TERM_CELL_H - w->scroll) / TERM_CELL_H, 0, count);
}

void terminal_paint_window(Fb *fb, const Session *s, const Window *w, const Layout *lay) {
    (void)s;
    if (!fb || !w || !w->term || !lay) return;
    const TermSession *ts = w->term;
    const TermVt *vt = &ts->vt;

    if (lay->info.h > 0) {
        uint32_t bg = term_default_bg(vt->theme);
        uint32_t fg = vt->theme == TERM_THEME_DARK ? 0xFF999999u : 0xFF555555u;
        fb_fill(fb, lay->info.x, lay->info.y, lay->info.w, lay->info.h, bg);
        const char *label = ts->mode == TERM_MODE_CONSOLE ? "OS999 Console" : "Terminal";
        for (int i = 0; label[i]; i++)
            term_font_draw(fb, lay->info.x + 8 + i * TERM_CELL_W, lay->info.y - 2,
                           (unsigned char)label[i], fg, 0);
    }

    uint32_t def_fg = term_default_fg(vt->theme);
    uint32_t def_bg = term_default_bg(vt->theme);
    fb_clip(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h);
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, def_bg);

    int scroll_rows = visible_scrollback_rows(w);

    int ox = lay->content.x + 4;
    int oy = lay->content.y + 3;
    for (int r = 0; r < vt->rows; r++) {
        int cy = oy + r * TERM_CELL_H;
        if (cy + TERM_CELL_H <= lay->content.y || cy >= lay->content.y + lay->content.h) continue;
        for (int c = 0; c < vt->cols; c++) {
            int cx = ox + c * TERM_CELL_W;
            const TermCell *cell = term_vt_view_cell(vt, scroll_rows, r, c);
            if (!cell) continue;
            uint32_t fg = cell->fg_default ? def_fg : cell->fg;
            uint32_t bg = cell->bg_default ? def_bg : cell->bg;
            if (cell->attrs & TERM_ATTR_REVERSE) {
                uint32_t tmp = fg; fg = bg; bg = tmp;
            }
            if (cell_selected(ts, r, c)) {
                bg = (vt->theme == TERM_THEME_DARK) ? 0xFF2563EBu : 0xFFBFDBFEu;
                fg = (vt->theme == TERM_THEME_DARK) ? 0xFFFFFFFFu : 0xFF111827u;
            }
            if (bg != def_bg) {
                fb_fill(fb, cx, cy, TERM_CELL_W, TERM_CELL_H, bg);
            }
            if (cell->cp > ' ') {
                uint32_t ink = fg;
                if (cell->attrs & TERM_ATTR_DIM) {
                    ink = 0xff000000u | (((fg & 0xfefefeu) >> 1) + ((bg & 0xfefefeu) >> 1));
                }
                term_font_draw(fb, cx, cy, cell->cp, ink, cell->attrs & TERM_ATTR_BOLD);
            }
            if (cell->attrs & TERM_ATTR_UNDERLINE) {
                fb_hline(fb, cx, cy + TERM_CELL_H - 2, TERM_CELL_W, fg);
            }
        }
    }

    if (vt->cursor_visible && scroll_rows == 0) {
        int cur_x = ox + vt->cursor_col * TERM_CELL_W;
        int cur_y = oy + vt->cursor_row * TERM_CELL_H;
        fb_fill(fb, cur_x, cur_y + 1, TERM_CELL_W - 1, TERM_CELL_H - 2, def_fg);
        const TermCell *cc = term_vt_cell(vt, vt->cursor_row, vt->cursor_col);
        if (cc && cc->cp > ' ') {
            term_font_draw(fb, cur_x, cur_y, cc->cp, def_bg, cc->attrs & TERM_ATTR_BOLD);
        }
    }
    fb_unclip(fb);
}

static void point_to_cell(const Window *w, const Layout *lay, int x, int y, int *r, int *c) {
    int col = (x - (lay->content.x + 4)) / TERM_CELL_W;
    int row = (y - (lay->content.y + 3)) / TERM_CELL_H;
    *c = clamp_i(col, 0, w->term->vt.cols - 1);
    *r = clamp_i(row, 0, w->term->vt.rows - 1);
}

int terminal_down(Session *s, Window *w, const Layout *lay, int x, int y, int clicks) {
    (void)s; (void)clicks;
    if (!w || !w->term || !lay) return 0;
    if (rect_has(lay->info, x, y)) {
        terminal_toggle_theme(w);
        return 1;
    }
    if (!rect_has(lay->content, x, y)) return 0;
    int r = 0, c = 0;
    point_to_cell(w, lay, x, y, &r, &c);
    w->term->sel_active = 0;
    w->term->sel_dragging = 1;
    w->term->sel_start_row = w->term->sel_end_row = r;
    w->term->sel_start_col = w->term->sel_end_col = c;
    return 1;
}

void terminal_move(Session *s, Window *w, const Layout *lay, int x, int y) {
    (void)s;
    if (!w || !w->term || !lay || !w->term->sel_dragging) return;
    int r = 0, c = 0;
    point_to_cell(w, lay, x, y, &r, &c);
    w->term->sel_end_row = r;
    w->term->sel_end_col = c;
    if (r != w->term->sel_start_row || c != w->term->sel_start_col) {
        w->term->sel_active = 1;
    }
}

void terminal_up(Session *s, Window *w, const Layout *lay, int x, int y) {
    (void)s; (void)lay; (void)x; (void)y;
    if (!w || !w->term) return;
    w->term->sel_dragging = 0;
}

int terminal_key(Session *s, int key, int mods) {
    Window *w = terminal_front(s);
    if (!w || !w->term) return 0;
    if (mods & TERM_MOD_CMD) return 0;
    char enc[16];
    int n = term_vt_encode_key(&w->term->vt, key, mods, enc, sizeof(enc));
    if (n > 0) {
        w->scroll = terminal_max_scroll(w);
        send_bytes(w->term, enc, (size_t)n);
        terminal_tick(s);
        return 1;
    }
    return 0;
}

int terminal_text_input(Session *s, const char *text) {
    Window *w = terminal_front(s);
    if (!w || !w->term || !text || !*text) return 0;
    w->scroll = terminal_max_scroll(w);
    send_bytes(w->term, text, strlen(text));
    terminal_tick(s);
    return 1;
}

int terminal_copy(Session *s, Window *w) {
    if (!s || !w || !w->term || !w->term->sel_active) return -1;
    TermSession *ts = w->term;
    int r0 = ts->sel_start_row, c0 = ts->sel_start_col;
    int r1 = ts->sel_end_row, c1 = ts->sel_end_col;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) {
        int tr = r0, tc = c0;
        r0 = r1; c0 = c1;
        r1 = tr; c1 = tc;
    }
    char buf[OSPI_TEXT];
    size_t pos = 0;
    for (int r = r0; r <= r1 && pos + 2 < sizeof(buf); r++) {
        int cs = (r == r0) ? c0 : 0;
        int ce = (r == r1) ? c1 : ts->vt.cols - 1;
        size_t line_start = pos;
        for (int c = cs; c <= ce && pos + 2 < sizeof(buf); c++) {
            const TermCell *cell = term_vt_view_cell(&ts->vt, visible_scrollback_rows(w), r, c);
            uint32_t cp = cell && cell->cp >= 32 ? cell->cp : ' ';
            char utf8[4]; int n;
            if (cp < 0x80) { utf8[0] = (char)cp; n = 1; }
            else if (cp < 0x800) { utf8[0] = 0xc0 | (cp >> 6); utf8[1] = 0x80 | (cp & 63); n = 2; }
            else if (cp < 0x10000) {
                utf8[0] = 0xe0 | (cp >> 12); utf8[1] = 0x80 | ((cp >> 6) & 63);
                utf8[2] = 0x80 | (cp & 63); n = 3;
            } else {
                utf8[0] = 0xf0 | (cp >> 18); utf8[1] = 0x80 | ((cp >> 12) & 63);
                utf8[2] = 0x80 | ((cp >> 6) & 63); utf8[3] = 0x80 | (cp & 63); n = 4;
            }
            if (pos + (size_t)n >= sizeof buf) break;
            memcpy(buf + pos, utf8, (size_t)n); pos += (size_t)n;
        }
        while (pos > line_start && buf[pos - 1] == ' ') pos--;
        if (r < r1 && pos + 2 < sizeof(buf)) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    session_set_clip(s, buf);
    free(s->clipboard_large_out);
    s->clipboard_large_out = NULL;
    memcpy(s->clipboard_out, buf, pos + 1);
    s->clipboard_write_request = 1;
    return 0;
}

/* Paste safety: any control byte other than tab (newlines, ESC, DEL, C0) needs
 * confirmation, and every ESC byte is dropped from the payload so a pasted
 * escape sequence or "\x1b[201~" marker can never leave bracketed-paste mode. */
static int paste_needs_confirm(const char *text) {
    for (const unsigned char *c = (const unsigned char *)text; *c; c++)
        if ((*c < 0x20 && *c != '\t') || *c == 0x7f) return 1;
    return 0;
}

static void send_paste(TermSession *ts, const char *text) {
    if (ts->vt.bracketed_paste) send_bytes(ts, "\x1b[200~", 6);
    const char *run = text;
    for (const char *c = text;; c++) {
        if (*c == '\x1b' || *c == '\0') {
            if (c > run) send_bytes(ts, run, (size_t)(c - run));
            if (*c == '\0') break;
            run = c + 1;
        }
    }
    if (ts->vt.bracketed_paste) send_bytes(ts, "\x1b[201~", 6);
}

int terminal_paste(Session *s, Window *w) {
    if (!s || !w || !w->term || !s->clip[0]) return -1;
    TermSession *ts = w->term;
    if (paste_needs_confirm(s->clip)) {
        ts->paste_confirm_pending = 1;
        snprintf(ts->pending_paste, sizeof(ts->pending_paste), "%s", s->clip);
        s->dialog = 2;
        s->dialog_cmd = CMD_TERM_PASTE_DO;
        snprintf(s->dialog_title, sizeof(s->dialog_title), "Confirm Paste");
        snprintf(s->dialog_body, sizeof(s->dialog_body),
                 "The clipboard contains newline or control characters, which may execute commands in %s. Paste anyway?",
                 ts->mode == TERM_MODE_CONSOLE ? "OS999 Console" : "Host Shell");
        return 0;
    }
    send_paste(ts, s->clip);
    terminal_tick(s);
    return 0;
}

void terminal_confirm_paste(Session *s, Window *w) {
    if (!w || !w->term || !w->term->paste_confirm_pending) return;
    TermSession *ts = w->term;
    ts->paste_confirm_pending = 0;
    send_paste(ts, ts->pending_paste);
    ts->pending_paste[0] = '\0';
    terminal_tick(s);
}

void terminal_cancel_paste(Window *w) {
    if (!w || !w->term) return;
    w->term->paste_confirm_pending = 0;
    w->term->pending_paste[0] = '\0';
}

void terminal_clear_scrollback(Window *w) {
    if (!w || !w->term) return;
    term_vt_clear_scrollback(&w->term->vt);
    w->scroll = 0;
}

void terminal_toggle_theme(Window *w) {
    if (!w || !w->term) return;
    int next = (w->term->vt.theme == TERM_THEME_DARK) ? TERM_THEME_LIGHT : TERM_THEME_DARK;
    term_vt_set_theme(&w->term->vt, next);
}
