/* Terminal shell history stays inside the volume: a host ~/.zsh_history never shows up in the OS999 shell. */
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int main(void) {
    test_shell_history_is_per_volume();
    printf("test_terminal_history: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
