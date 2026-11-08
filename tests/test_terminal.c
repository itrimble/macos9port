#include "term.h"
#include "term_font.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"
#include "../src/session_int.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks = 0;
static int fails = 0;

#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        checks++;                                                                                        \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                              \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

static void test_vt_basic_and_colors(void) {
    TermVt vt;
    term_vt_init(&vt, 24, 80);
    CHECK(vt.rows == 24 && vt.cols == 80);
    CHECK(vt.cursor_row == 0 && vt.cursor_col == 0);
    CHECK(vt.cursor_visible == 1);

    const char *msg = "Hello, OS999!\r\nSecond line";
    term_vt_feed(&vt, msg, strlen(msg));
    char line0[128], line1[128];
    CHECK(term_vt_row_text(&vt, 0, line0, sizeof line0) == 0);
    CHECK(strcmp(line0, "Hello, OS999!") == 0);
    CHECK(term_vt_row_text(&vt, 1, line1, sizeof line1) == 0);
    CHECK(strcmp(line1, "Second line") == 0);
    CHECK(vt.cursor_row == 1 && vt.cursor_col == 11);

    /* SGR colors: bold red fg (1;31), green bg (42), 256-color (38;5;208), RGB (38;2;16;32;64) */
    const char *sgr = "\r\n\x1b[1;31;42mR\x1b[38;5;208mO\x1b[38;2;16;32;64mT\x1b[0mN";
    term_vt_feed(&vt, sgr, strlen(sgr));
    const TermCell *c_r = term_vt_cell(&vt, 2, 0);
    const TermCell *c_o = term_vt_cell(&vt, 2, 1);
    const TermCell *c_t = term_vt_cell(&vt, 2, 2);
    const TermCell *c_n = term_vt_cell(&vt, 2, 3);
    CHECK(c_r && c_r->cp == 'R' && (c_r->attrs & TERM_ATTR_BOLD));
    CHECK(c_r && !c_r->fg_default && !c_r->bg_default);
    CHECK(c_r && c_r->fg == term_ansi_color(9, vt.theme));
    CHECK(c_r && c_r->bg == term_ansi_color(2, vt.theme));
    CHECK(c_o && c_o->cp == 'O' && !c_o->fg_default);
    CHECK(c_t && c_t->cp == 'T' && c_t->fg == 0xFF102040u);
    CHECK(c_n && c_n->cp == 'N' && c_n->attrs == 0 && c_n->fg_default && c_n->bg_default);
    term_vt_free(&vt);
}

static void test_vt_cursor_clear_and_split_escapes(void) {
    TermVt vt;
    term_vt_init(&vt, 10, 40);
    /* Split escape sequence fed byte-by-byte: move to row 3, col 5 (1-based) and print "XYZ" */
    const char *seq = "\x1b[3;5H\x1b[32mXYZ\x1b[0m";
    for (size_t i = 0; seq[i]; i++) {
        term_vt_feed(&vt, &seq[i], 1);
    }
    CHECK(vt.cursor_row == 2 && vt.cursor_col == 7);
    const TermCell *cx = term_vt_cell(&vt, 2, 4);
    CHECK(cx && cx->cp == 'X' && cx->fg == term_ansi_color(2, vt.theme));

    /* Split UTF-8 character (em-dash U+2014 = E2 80 94) */
    const char utf8_em[] = "\xe2\x80\x94";
    term_vt_feed(&vt, &utf8_em[0], 1);
    term_vt_feed(&vt, &utf8_em[1], 1);
    term_vt_feed(&vt, &utf8_em[2], 1);
    const TermCell *cem = term_vt_cell(&vt, 2, 7);
    CHECK(cem && cem->cp == 0x2014u);

    /* Erase in line (EL) and Erase in display (ED) */
    const char *el = "\x1b[3;6H\x1b[K";
    term_vt_feed(&vt, el, strlen(el));
    char line2[64];
    CHECK(term_vt_row_text(&vt, 2, line2, sizeof line2) == 0);
    CHECK(strcmp(line2, "    X") == 0);

    const char *ed = "\x1b[2J\x1b[H";
    term_vt_feed(&vt, ed, strlen(ed));
    CHECK(vt.cursor_row == 0 && vt.cursor_col == 0);
    CHECK(term_vt_row_text(&vt, 2, line2, sizeof line2) == 0);
    CHECK(strcmp(line2, "") == 0);
    term_vt_free(&vt);
}

static void test_vt_scrollback_alt_screen_and_resize(void) {
    TermVt vt;
    term_vt_init(&vt, 5, 20);
    for (int i = 0; i < 12; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "Line-%02d%s", i, i < 11 ? "\r\n" : "");
        term_vt_feed(&vt, buf, strlen(buf));
    }
    CHECK(vt.sb_count == 7);
    char sb0[32], sb6[32], vis0[32], vis4[32];
    CHECK(term_vt_scrollback_row_text(&vt, 0, sb0, sizeof sb0) == 0);
    CHECK(strcmp(sb0, "Line-00") == 0);
    CHECK(term_vt_scrollback_row_text(&vt, 6, sb6, sizeof sb6) == 0);
    CHECK(strcmp(sb6, "Line-06") == 0);
    CHECK(term_vt_row_text(&vt, 0, vis0, sizeof vis0) == 0);
    CHECK(strcmp(vis0, "Line-07") == 0);
    CHECK(term_vt_row_text(&vt, 4, vis4, sizeof vis4) == 0);
    CHECK(strcmp(vis4, "Line-11") == 0);

    /* Alternate screen buffer (?1049h / ?1049l) must preserve primary screen & scrollback */
    const char *enter_alt = "\x1b[?1049h\x1b[HAltScreen\r\nRow2";
    term_vt_feed(&vt, enter_alt, strlen(enter_alt));
    CHECK(vt.alt_screen == 1);
    CHECK(term_vt_row_text(&vt, 0, vis0, sizeof vis0) == 0);
    CHECK(strcmp(vis0, "AltScreen") == 0);
    const char *exit_alt = "\x1b[?1049l";
    term_vt_feed(&vt, exit_alt, strlen(exit_alt));
    CHECK(vt.alt_screen == 0);
    CHECK(vt.sb_count == 7);
    CHECK(term_vt_row_text(&vt, 0, vis0, sizeof vis0) == 0);
    CHECK(strcmp(vis0, "Line-07") == 0);

    /* Resize preserves visible rows and clamps safely */
    term_vt_resize(&vt, 8, 30);
    CHECK(vt.rows == 8 && vt.cols == 30);
    CHECK(term_vt_row_text(&vt, 0, vis0, sizeof vis0) == 0);
    CHECK(strcmp(vis0, "Line-07") == 0);

    /* Key encoding */
    char keybuf[16];
    CHECK(term_vt_encode_key(&vt, TERM_KEY_UP, 0, keybuf, sizeof keybuf) == 3);
    CHECK(memcmp(keybuf, "\x1b[A", 3) == 0);
    term_vt_feed(&vt, "\x1b[?1h", 5);
    CHECK(vt.app_cursor == 1);
    CHECK(term_vt_encode_key(&vt, TERM_KEY_UP, 0, keybuf, sizeof keybuf) == 3);
    CHECK(memcmp(keybuf, "\x1bOA", 3) == 0);
    CHECK(term_vt_encode_key(&vt, 'c', TERM_MOD_CTRL, keybuf, sizeof keybuf) == 1);
    CHECK(keybuf[0] == 0x03);
    term_vt_free(&vt);
}

static int vt_contains_str(const TermVt *vt, const char *needle) {
    char row[256];
    for (int r = 0; r < vt->rows; r++) {
        if (term_vt_row_text(vt, r, row, sizeof row) == 0 && strstr(row, needle)) return 1;
    }
    for (int s = 0; s < vt->sb_count; s++) {
        if (term_vt_scrollback_row_text(vt, s, row, sizeof row) == 0 && strstr(row, needle)) return 1;
    }
    return 0;
}

static int pump_pty_until(TermPty *pty, TermVt *vt, const char *needle, int timeout_ms) {
    int elapsed = 0;
    char buf[1024];
    while (elapsed < timeout_ms) {
        ssize_t n = term_pty_read(pty, buf, sizeof buf);
        while (n > 0) {
            term_vt_feed(vt, buf, (size_t)n);
            n = term_pty_read(pty, buf, sizeof buf);
        }
        if (needle && vt_contains_str(vt, needle)) return 1;
        if (!needle && !term_pty_alive(pty)) return 1;
        usleep(10000);
        elapsed += 10;
    }
    return needle ? vt_contains_str(vt, needle) : !term_pty_alive(pty);
}

static void test_pty_interactive_shell_resize_and_reap(void) {
    TermVt vt;
    TermPty pty;
    term_vt_init(&vt, 24, 80);
    char *const sh_argv[] = { "/bin/sh", NULL };
    CHECK(term_pty_open(&pty, 24, 80, NULL, sh_argv) == 0);
    CHECK(pty.master_fd >= 0);
    CHECK(pty.child_pid > 0);
    CHECK(term_pty_alive(&pty) == 1);

    const char *cmd1 = "echo PTY_HELLO_$((20+22))\n";
    CHECK(term_pty_write(&pty, cmd1, strlen(cmd1)) == (ssize_t)strlen(cmd1));
    CHECK(pump_pty_until(&pty, &vt, "PTY_HELLO_42", 1500) == 1);

    /* Resize PTY and verify kernel TIOCSWINSZ via stty size */
    CHECK(term_pty_resize(&pty, 33, 105) == 0);
    term_vt_resize(&vt, 33, 105);
    const char *cmd2 = "stty size\n";
    CHECK(term_pty_write(&pty, cmd2, strlen(cmd2)) == (ssize_t)strlen(cmd2));
    CHECK(pump_pty_until(&pty, &vt, "33 105", 1500) == 1);

    /* Close while active child process is running (deliberate SIGHUP + reap) */
    const char *cmd3 = "sleep 5\n";
    CHECK(term_pty_write(&pty, cmd3, strlen(cmd3)) == (ssize_t)strlen(cmd3));
    usleep(50000);
    term_pty_close(&pty);
    CHECK(pty.master_fd == -1);
    CHECK(pty.alive == 0);
    term_vt_free(&vt);
}

static void make_temp_vol(char *out, size_t n) {
    snprintf(out, n, "/tmp/os999-term-test-XXXXXX");
    CHECK(mkdtemp(out) != NULL);
    (void)volume_seed(out);
}

static void test_console_commands_and_line_editing(void) {
    char vol[OSPI_PATH];
    make_temp_vol(vol, sizeof vol);
    desktop_theme_init(NULL);
    desktop_wallpaper_init("build/wallpapers", NULL);
    Session *s = session_create(vol);
    CHECK(s != NULL);

    TermVt vt;
    TermConsole con;
    term_vt_init(&vt, 24, 80);
    term_console_init(&con, s, &vt);
    CHECK(vt_contains_str(&vt, "OS999 Console") == 1);
    CHECK(vt_contains_str(&vt, "os999>") == 1);

    CHECK(term_console_exec(&con, &vt, "help") == 0);
    CHECK(vt_contains_str(&vt, "theme list") == 1);
    CHECK(vt_contains_str(&vt, "wallpaper set") == 1);

    CHECK(term_console_exec(&con, &vt, "about") == 0);
    CHECK(vt_contains_str(&vt, "OS999 Stylized") == 1);

    CHECK(term_console_exec(&con, &vt, "theme list") == 0);
    CHECK(vt_contains_str(&vt, "Platinum") == 1);

    CHECK(term_console_exec(&con, &vt, "theme set Titanium Night") == 0);
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Titanium Night") == 0);
    CHECK(term_console_exec(&con, &vt, "theme set platinum") == 0);
    CHECK(desktop_theme_current() == 0);

    CHECK(term_console_exec(&con, &vt, "wallpaper list") == 0);
    CHECK(vt_contains_str(&vt, "Plain Desktop") == 1);
    CHECK(term_console_exec(&con, &vt, "wallpaper set plain") == 0);
    CHECK(desktop_wallpaper_current() == -1);

    s->sound_preview_request = -1;
    CHECK(term_console_exec(&con, &vt, "sound preview alert") == 0);
    CHECK(s->sound_preview_request == 1);

    int before_win = s->nwin;
    CHECK(term_console_exec(&con, &vt, "open Documents") == 0);
    CHECK(s->nwin == before_win + 1);
    CHECK(strcmp(s->win[s->nwin - 1].title, "Documents") == 0);

    CHECK(term_console_exec(&con, &vt, "open Read Me") == 0);
    CHECK(s->win[s->nwin - 1].kind == WIN_TEXT);
    CHECK(strcmp(s->win[s->nwin - 1].title, "Read Me") == 0);

    /* Line editing: type "themx\x7fe set Blueberry\r" */
    const char *typed = "themx\x7f" "e set Blueberry\r";
    term_console_input(&con, &vt, typed, strlen(typed));
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Blueberry") == 0);

    term_vt_free(&vt);
    session_destroy(s);
}

static void test_desktop_window_chrome_focus_selection_and_paste_safety(void) {
    char vol[OSPI_PATH];
    make_temp_vol(vol, sizeof vol);
    desktop_theme_init(NULL);
    desktop_wallpaper_init("build/wallpapers", NULL);
    Session *s = session_create(vol);
    CHECK(s != NULL);

    /* Open OS999 Console window */
    session_command(s, CMD_TERMINAL_CONSOLE);
    Window *w_con = terminal_front(s);
    CHECK(w_con != NULL);
    if (!w_con || !w_con->term) {
        session_destroy(s);
        return;
    }
    CHECK(w_con->kind == WIN_TERMINAL);
    CHECK(strcmp(w_con->title, "OS999 Console") == 0);
    CHECK(w_con->term != NULL && w_con->term->mode == TERM_MODE_CONSOLE);

    /* Open Host Shell window with explicit /bin/sh */
    char *const sh_argv[] = { "/bin/sh", NULL };
    CHECK(terminal_open_custom(s, TERM_MODE_SHELL, sh_argv) == 0);
    Window *w_sh = terminal_front(s);
    CHECK(w_sh != NULL && w_sh->kind == WIN_TERMINAL);
    CHECK(strcmp(w_sh->title, "Terminal \xe2\x80\x94 Host Shell") == 0);
    CHECK(w_sh->term != NULL && w_sh->term->mode == TERM_MODE_SHELL);

    /* Z-order safety with bring_front */
    bring_front(s, 0);
    CHECK(terminal_front(s) == NULL);
    bring_front(s, s->nwin - 2);
    w_sh = terminal_front(s);
    CHECK(w_sh != NULL && w_sh->term->mode == TERM_MODE_SHELL);

    /* Resize updates both VT and PTY dimensions */
    w_sh->w = 700;
    w_sh->h = 540;
    terminal_sync_size(w_sh);
    CHECK(w_sh->term->vt.cols > 60 && w_sh->term->vt.rows > 20);
    CHECK(w_sh->term->pty.cols == w_sh->term->vt.cols);
    CHECK(w_sh->term->pty.rows == w_sh->term->vt.rows);

    /* Paint desktop with terminal window in Light and Dark themes */
    uint32_t *px = calloc((size_t)OSPI_W * OSPI_H, sizeof(uint32_t));
    CHECK(px != NULL);
    session_paint(s, px, OSPI_W, OSPI_H);
    Layout lay;
    window_layout(w_sh, &lay);
    uint32_t sample_light = px[(lay.content.y + 4) * OSPI_W + (lay.content.x + 4)];
    session_command(s, CMD_TERM_THEME);
    CHECK(w_sh->term->vt.theme == TERM_THEME_LIGHT);
    session_paint(s, px, OSPI_W, OSPI_H);
    uint32_t sample_dark = px[(lay.content.y + 4) * OSPI_W + (lay.content.x + 4)];
    CHECK(sample_light != sample_dark);
    free(px);

    /* Selection and Copy (Cmd-C) */
    bring_front(s, 0); /* Bring OS999 Console to front */
    w_con = terminal_front(s);
    CHECK(w_con != NULL && w_con->term->mode == TERM_MODE_CONSOLE);
    w_con->term->sel_active = 1;
    w_con->term->sel_start_row = 0;
    w_con->term->sel_start_col = 0;
    w_con->term->sel_end_row = 0;
    w_con->term->sel_end_col = 12;
    session_command(s, CMD_COPY);
    CHECK(strncmp(s->clip, "OS999 Console", 13) == 0);

    /* Paste safety: single-line paste does NOT execute automatically */
    session_set_clip(s, "theme set Graphite");
    session_command(s, CMD_PASTE);
    CHECK(w_con->term->paste_confirm_pending == 0);
    CHECK(strcmp(w_con->term->console.line, "theme set Graphite") == 0);
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Graphite") != 0);
    /* Pressing Return now executes it */
    session_key(s, '\r', 0);
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Graphite") == 0);

    /* Multiline paste requires confirmation before sending */
    session_set_clip(s, "theme set Lime\ntheme set Grape\n");
    session_command(s, CMD_PASTE);
    CHECK(w_con->term->paste_confirm_pending == 1);
    CHECK(s->dialog == 2 && s->dialog_cmd == CMD_TERM_PASTE_DO);
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Graphite") == 0);
    dialog_accept(s);
    CHECK(w_con->term->paste_confirm_pending == 0);
    CHECK(strcmp(desktop_theme_name(desktop_theme_current()), "Grape") == 0);

    /* Close terminal windows cleanly */
    session_command(s, CMD_CLOSE);
    session_command(s, CMD_CLOSE);
    session_destroy(s);
}

static void test_utilities_launcher_and_login_zsh(void) {
    char vol[OSPI_PATH]; make_temp_vol(vol, sizeof vol);
    CHECK(volume_builtin_terminal(vol, "Applications/Utilities/Terminal"));
    CHECK(!volume_builtin_terminal(vol, "Documents/Terminal"));
    Item items[8]; int count = volume_list(vol, "Applications/Utilities", items, 8);
    CHECK(count == 2 && items[0].kind == ITEM_APPLICATION);
    CHECK(volume_builtin_tailnet(vol, "Applications/Utilities/Tailscale"));
    /* Isolate zsh startup from user configuration while testing rc loading. */
    if (access("/bin/zsh", X_OK)) return;
    char zd[] = "/tmp/os999-zsh-test-XXXXXX"; CHECK(mkdtemp(zd) != NULL);
    char rc[512]; snprintf(rc, sizeof rc, "%s/.zshrc", zd);
    FILE *f = fopen(rc, "w"); CHECK(f != NULL); if (!f) return;
    fputs("print -r -- OS999_RC_LOADED\n", f); fclose(f);
    char *old_shell = getenv("SHELL") ? strdup(getenv("SHELL")) : NULL;
    char *old_zd = getenv("ZDOTDIR") ? strdup(getenv("ZDOTDIR")) : NULL;
    setenv("SHELL", "/bin/zsh", 1); setenv("ZDOTDIR", zd, 1);
    Session *session = session_create(vol);
    CHECK(session != NULL);
    if (session) {
        CHECK(open_folder(session, "Applications/Utilities") == 0);
        Window *folder = &session->win[session->nwin - 1];
        for (int i = 0; i < folder->nitems; i++)
            if (!strcmp(folder->items[i].name, "Terminal")) { open_item(session, folder, i); break; }
        CHECK(terminal_front(session) != NULL);
        session_destroy(session);
    }
    TermPty pty; TermVt vt; term_vt_init(&vt, 24, 80);
    CHECK(term_pty_open_shell(&pty, 24, 80, zd) == 0);
    if (old_shell) setenv("SHELL", old_shell, 1); else unsetenv("SHELL");
    if (old_zd) setenv("ZDOTDIR", old_zd, 1); else unsetenv("ZDOTDIR");
    free(old_shell); free(old_zd);
    CHECK(pump_pty_until(&pty, &vt, "OS999_RC_LOADED", 3000));
    const char *cmd = "print -r -- LOGIN=$options[login],INTERACTIVE=$options[interactive]\n";
    term_pty_write(&pty, cmd, strlen(cmd));
    CHECK(pump_pty_until(&pty, &vt, "LOGIN=on,INTERACTIVE=on", 3000));
    term_pty_close(&pty); term_vt_free(&vt); unlink(rc); rmdir(zd);
}

/* `history` must show this volume's commands, never the host user's own. */
static void test_shell_history_is_per_volume(void) {
    if (access("/bin/zsh", X_OK)) return;
    char home[] = "/tmp/os999-hist-home-XXXXXX"; CHECK(mkdtemp(home) != NULL);
    char vol[] = "/tmp/os999-hist-vol-XXXXXX"; CHECK(mkdtemp(vol) != NULL);
    char path[600];
    snprintf(path, sizeof path, "%s/.zsh_history", home);
    FILE *f = fopen(path, "w"); CHECK(f != NULL); if (!f) return;
    fputs(": 1700000000:0;echo HOST_SECRET_COMMAND\n", f); fclose(f);
    snprintf(path, sizeof path, "%s/.zshrc", home);
    f = fopen(path, "w"); CHECK(f != NULL); if (!f) return;
    fputs("HISTFILE=$HOME/.zsh_history; HISTSIZE=1000; SAVEHIST=1000\nsetopt SHARE_HISTORY\n", f); fclose(f);
    char *old_shell = getenv("SHELL") ? strdup(getenv("SHELL")) : NULL;
    char *old_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;
    char *old_zd = getenv("ZDOTDIR") ? strdup(getenv("ZDOTDIR")) : NULL;
    setenv("SHELL", "/bin/zsh", 1); setenv("HOME", home, 1); unsetenv("ZDOTDIR");
    TermPty pty; TermVt vt; term_vt_init(&vt, 24, 200);
    CHECK(term_pty_open_shell(&pty, 24, 200, vol) == 0);
    if (old_shell) setenv("SHELL", old_shell, 1); else unsetenv("SHELL");
    if (old_home) setenv("HOME", old_home, 1); else unsetenv("HOME");
    if (old_zd) setenv("ZDOTDIR", old_zd, 1);
    free(old_shell); free(old_home); free(old_zd);
    const char *cmd = "print -r -- HFD=${${HISTFILE:h}:t} HFN=${HISTFILE:t}; fc -l 1 > fc-out.txt 2>&1; print -r -- DONE_FC\n";
    term_pty_write(&pty, cmd, strlen(cmd));
    CHECK(pump_pty_until(&pty, &vt, "HFD=.os999-terminal HFN=history", 4000));
    CHECK(pump_pty_until(&pty, &vt, "DONE_FC", 4000));
    char outp[700]; snprintf(outp, sizeof outp, "%s/fc-out.txt", vol);
    char got[2048] = {0}; FILE *o = fopen(outp, "r");
    if (o) { size_t r = fread(got, 1, sizeof got - 1, o); got[r] = 0; fclose(o); }
    if (strstr(got, "HOST_SECRET")) fprintf(stderr, "host history leaked: [%s]\n", got);
    CHECK(strstr(got, "HOST_SECRET") == NULL);
    term_pty_close(&pty); term_vt_free(&vt);
    char rm[1300]; snprintf(rm, sizeof rm, "rm -rf '%s' '%s'", home, vol); (void)system(rm);
}

static void test_nerd_glyphs_and_clipping(void) {
#ifdef OSPI_FREETYPE
    const uint32_t glyphs[] = {0xe0b0, 0xe0a0, 0xf120, 0x256d, 0x2713};
    uint32_t pixels[40 * 30]; Fb fb;
    fb_init(&fb, pixels, 40, 30);
    for (size_t k = 0; k < sizeof glyphs / sizeof glyphs[0]; k++) {
        CHECK(term_font_has_glyph(glyphs[k]));
        fb_fill(&fb, 0, 0, 40, 30, 0xff141414u);
        fb_clip(&fb, 10, 5, TERM_CELL_W, TERM_CELL_H);
        term_font_draw(&fb, 10, 5, glyphs[k], 0xffeeeeeeu, 0);
        int ink = 0, antialias = 0, outside = 0;
        for (int y = 0; y < 30; y++) for (int x = 0; x < 40; x++) {
            uint32_t c = pixels[y * 40 + x];
            if (c != 0xff141414u) {
                ink++;
                if (c != 0xffeeeeeeu) antialias++;
                if (x < 10 || x >= 10 + TERM_CELL_W || y < 5 || y >= 5 + TERM_CELL_H) outside++;
            }
        }
        CHECK(ink > 0); CHECK(antialias > 0); CHECK(outside == 0);
        fb_unclip(&fb);
    }
#endif
    TermVt vt; term_vt_init(&vt, 24, 80);
    CHECK(vt.theme == TERM_THEME_DARK);
    const char *prompt = "\xe2\x95\xad\xe2\x94\x80 \xee\x82\xb0 \xef\x84\xa0";
    term_vt_feed(&vt, prompt, strlen(prompt));
    CHECK(term_vt_cell(&vt, 0, 0)->cp == 0x256d);
    CHECK(term_vt_cell(&vt, 0, 3)->cp == 0xe0b0);
    CHECK(term_vt_cell(&vt, 0, 5)->cp == 0xf120);
    term_vt_free(&vt);
}

static void test_copy_visible_scrollback(void) {
    char vol[OSPI_PATH]; make_temp_vol(vol, sizeof vol);
    Session *s = session_create(vol);
    CHECK(s != NULL);
    if (!s) return;
    session_command(s, CMD_TERMINAL_CONSOLE);
    Window *w = terminal_front(s);
    CHECK(w != NULL);
    if (!w) { session_destroy(s); return; }
    TermSession *ts = w->term;
    term_vt_free(&ts->vt);
    term_vt_init(&ts->vt, 5, 20);
    for (int i = 0; i < 12; i++) {
        char line[32];
        snprintf(line, sizeof line, "Line-%02d%s", i, i < 11 ? "\r\n" : "");
        term_vt_feed(&ts->vt, line, strlen(line));
    }
    ts->sel_active = 1;
    ts->sel_start_row = ts->sel_start_col = 0;
    ts->sel_end_row = 1; ts->sel_end_col = 6;
    w->scroll = TERM_CELL_H;
    session_command(s, CMD_COPY);
    CHECK(strcmp(s->clip, "Line-01\nLine-02") == 0);
    CHECK(s->clipboard_write_request == 1);
    CHECK(strcmp(session_clipboard_output(s), "Line-01\nLine-02") == 0);
    w->scroll = terminal_max_scroll(w);
    session_command(s, CMD_COPY);
    CHECK(strcmp(s->clip, "Line-07\nLine-08") == 0);
    session_destroy(s);
}

int main(void) {
    test_copy_visible_scrollback();
    test_utilities_launcher_and_login_zsh();
    test_shell_history_is_per_volume();
    test_nerd_glyphs_and_clipping();
    test_vt_basic_and_colors();
    test_vt_cursor_clear_and_split_escapes();
    test_vt_scrollback_alt_screen_and_resize();
    test_pty_interactive_shell_resize_and_reap();
    test_console_commands_and_line_editing();
    test_desktop_window_chrome_focus_selection_and_paste_safety();
    if (fails) {
        fprintf(stderr, "test_terminal: %d/%d checks failed\n", fails, checks);
        return 1;
    }
    printf("test_terminal: %d checks passed\n", checks);
    return 0;
}
