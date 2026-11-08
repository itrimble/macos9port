#ifndef OS999_TERM_H
#define OS999_TERM_H

#include "draw.h"
#include "ospi.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    TERM_MIN_COLS = 20,
    TERM_MAX_COLS = 160,
    TERM_DEFAULT_COLS = 80,
    TERM_MIN_ROWS = 5,
    TERM_MAX_ROWS = 64,
    TERM_DEFAULT_ROWS = 24,
    TERM_SCROLLBACK_MAX = 512,
    TERM_CELL_W = 9,
    TERM_CELL_H = 20,
    TERM_CSI_MAX_PARAMS = 16,
    TERM_OSC_MAX = 256,
    TERM_REPLY_MAX = 128
};

enum {
    TERM_ATTR_BOLD = 1u << 0,
    TERM_ATTR_DIM = 1u << 1,
    TERM_ATTR_ITALIC = 1u << 2,
    TERM_ATTR_UNDERLINE = 1u << 3,
    TERM_ATTR_REVERSE = 1u << 4
};

enum {
    TERM_MODE_SHELL = 0,
    TERM_MODE_CONSOLE = 1
};

enum {
    TERM_THEME_LIGHT = 0,
    TERM_THEME_DARK = 1
};

enum {
    TERM_KEY_UP = 0x40000052,
    TERM_KEY_DOWN = 0x40000051,
    TERM_KEY_RIGHT = 0x4000004F,
    TERM_KEY_LEFT = 0x40000050,
    TERM_KEY_HOME = 0x4000004A,
    TERM_KEY_END = 0x4000004D,
    TERM_KEY_PGUP = 0x4000004B,
    TERM_KEY_PGDN = 0x4000004E,
    TERM_KEY_DELETE = 0x4000004C
};

enum {
    TERM_MOD_CMD = 1,
    TERM_MOD_SHIFT = 2,
    TERM_MOD_CTRL = 4,
    TERM_MOD_ALT = 8
};

typedef struct TermCell {
    uint32_t cp;
    uint32_t fg;
    uint32_t bg;
    uint8_t attrs;
    uint8_t fg_default;
    uint8_t bg_default;
} TermCell;

typedef struct TermLine {
    TermCell cells[TERM_MAX_COLS];
    int len;
    int wrapped;
} TermLine;

typedef struct TermVt {
    int rows;
    int cols;
    int cursor_row;
    int cursor_col;
    int saved_row;
    int saved_col;
    int alt_saved_row;
    int alt_saved_col;
    int scroll_top;
    int scroll_bottom;
    int cursor_visible;
    int app_cursor;
    int alt_screen;
    int bracketed_paste;
    int auto_wrap;
    int pending_wrap;
    int theme;
    uint32_t cur_fg;
    uint32_t cur_bg;
    uint8_t cur_attrs;
    uint8_t cur_fg_default;
    uint8_t cur_bg_default;
    uint32_t saved_fg;
    uint32_t saved_bg;
    uint8_t saved_attrs;
    uint8_t saved_fg_default;
    uint8_t saved_bg_default;

    int state;
    uint32_t utf8_cp;
    int utf8_rem;
    int csi_priv;
    int csi_interm;
    int csi_params[TERM_CSI_MAX_PARAMS];
    int csi_nparams;
    int csi_has_digit;
    char osc_buf[TERM_OSC_MAX];
    int osc_len;
    char title[OSPI_NAME];
    char reply[TERM_REPLY_MAX];
    size_t reply_len;

    TermLine primary[TERM_MAX_ROWS];
    TermLine alt[TERM_MAX_ROWS];
    TermLine *sb_lines;
    int sb_cap;
    int sb_head;
    int sb_count;
} TermVt;

typedef struct TermPty {
    int master_fd;
    pid_t child_pid;
    int alive;
    int exit_status;
    int rows;
    int cols;
} TermPty;

typedef struct TermConsole {
    Session *session;
    char line[512];
    int len;
    int cursor;
    char history[16][512];
    int history_count;
    int history_index;
} TermConsole;

typedef struct TermSession {
    int mode;
    TermVt vt;
    TermPty pty;
    TermConsole console;
    int view_scroll;
    int sel_active;
    int sel_dragging;
    int sel_start_row;
    int sel_start_col;
    int sel_end_row;
    int sel_end_col;
    int paste_confirm_pending;
    char pending_paste[OSPI_TEXT];
} TermSession;

/* Milestone 1: VT emulation engine & scrollback */
void term_vt_init(TermVt *vt, int rows, int cols);
void term_vt_free(TermVt *vt);
void term_vt_reset(TermVt *vt);
void term_vt_set_theme(TermVt *vt, int theme);
uint32_t term_default_fg(int theme);
uint32_t term_default_bg(int theme);
uint32_t term_ansi_color(int index, int theme);
void term_vt_feed(TermVt *vt, const char *data, size_t len);
void term_vt_resize(TermVt *vt, int new_rows, int new_cols);
void term_vt_clear_scrollback(TermVt *vt);
const TermCell *term_vt_cell(const TermVt *vt, int row, int col);
const TermCell *term_vt_view_cell(const TermVt *vt, int scroll_offset, int row, int col);
int term_vt_row_text(const TermVt *vt, int row, char *out, size_t out_n);
int term_vt_scrollback_row_text(const TermVt *vt, int sb_idx, char *out, size_t out_n);
size_t term_vt_take_reply(TermVt *vt, char *out, size_t out_n);
int term_vt_encode_key(const TermVt *vt, int key, int mods, char *out, size_t out_n);

/* Milestone 2: POSIX PTY backend */
int term_pty_open(TermPty *pty, int rows, int cols, const char *working_dir, char *const argv[]);
int term_pty_open_shell(TermPty *pty, int rows, int cols, const char *working_dir);
ssize_t term_pty_read(TermPty *pty, char *buf, size_t n);
ssize_t term_pty_write(TermPty *pty, const char *buf, size_t n);
int term_pty_resize(TermPty *pty, int rows, int cols);
int term_pty_alive(TermPty *pty);
int term_pty_has_foreground_job(const TermPty *pty);
void term_pty_close(TermPty *pty);

/* Milestone 3: OS999 Console backend */
void term_console_init(TermConsole *con, Session *s, TermVt *vt);
void term_console_input(TermConsole *con, TermVt *vt, const char *data, size_t len);
int term_console_exec(TermConsole *con, TermVt *vt, const char *cmd_line);

/* Milestone 4: Desktop UI, rendering, selection, clipboard & paste safety */
int terminal_tailnet_open(Session *s);
int terminal_open(Session *s, int mode);
int terminal_open_custom(Session *s, int mode, char *const argv[]);
Window *terminal_front(Session *s);
void terminal_window_close(Session *s, Window *w);
void terminal_tick(Session *s);
void terminal_paint_window(Fb *fb, const Session *s, const Window *w, const Layout *lay);
int terminal_down(Session *s, Window *w, const Layout *lay, int x, int y, int clicks);
void terminal_move(Session *s, Window *w, const Layout *lay, int x, int y);
void terminal_up(Session *s, Window *w, const Layout *lay, int x, int y);
int terminal_key(Session *s, int key, int mods);
int terminal_text_input(Session *s, const char *text);
int terminal_copy(Session *s, Window *w);
int terminal_paste(Session *s, Window *w);
void terminal_confirm_paste(Session *s, Window *w);
void terminal_cancel_paste(Window *w);
void terminal_clear_scrollback(Window *w);
void terminal_toggle_theme(Window *w);
void terminal_sync_size(Window *w);
int terminal_max_scroll(const Window *w);

#endif
