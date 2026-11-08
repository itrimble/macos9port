#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#else
#include <util.h>
#endif

static int clamp_dim(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void check_child_reap(TermPty *pty) {
    if (!pty || !pty->alive || pty->child_pid <= 0) return;
    int status = 0;
    pid_t r = waitpid(pty->child_pid, &status, WNOHANG);
    if (r == pty->child_pid) {
        pty->alive = 0;
        if (WIFEXITED(status)) pty->exit_status = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) pty->exit_status = 128 + WTERMSIG(status);
    } else if (r < 0 && errno == ECHILD) {
        pty->alive = 0;
    }
}

int term_pty_open(TermPty *pty, int rows, int cols, const char *working_dir, char *const argv[]) {
    if (!pty || !argv || !argv[0]) return -1;
    memset(pty, 0, sizeof(*pty));
    pty->master_fd = -1;
    pty->rows = clamp_dim(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    pty->cols = clamp_dim(cols, TERM_MIN_COLS, TERM_MAX_COLS);

    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)pty->rows;
    ws.ws_col = (unsigned short)pty->cols;
    ws.ws_xpixel = (unsigned short)(pty->cols * TERM_CELL_W);
    ws.ws_ypixel = (unsigned short)(pty->rows * TERM_CELL_H);

    int master_fd = -1;
    pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) return -1;

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        if (working_dir && *working_dir) {
            (void)chdir(working_dir);
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "OS999_Terminal", 1);
        execvp(argv[0], argv);
        _exit(127);
    }

    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    int fdflags = fcntl(master_fd, F_GETFD, 0);
    if (fdflags >= 0) (void)fcntl(master_fd, F_SETFD, fdflags | FD_CLOEXEC);

    pty->master_fd = master_fd;
    pty->child_pid = pid;
    pty->alive = 1;
    pty->exit_status = 0;
    return 0;
}

int term_pty_open_shell(TermPty *pty, int rows, int cols, const char *working_dir) {
    const char *sh = getenv("SHELL");
    struct passwd *pw = getpwuid(getuid());
    if ((!sh || !*sh) && pw) sh = pw->pw_shell;
    if (!sh || !*sh || access(sh, X_OK) != 0) {
        if (access("/bin/zsh", X_OK) == 0) sh = "/bin/zsh";
        else if (access("/bin/bash", X_OK) == 0) sh = "/bin/bash";
        else sh = "/bin/sh";
    }
    char *const argv[] = { (char *)sh, "-l", "-i", NULL };
    return term_pty_open(pty, rows, cols, working_dir, argv);
}

ssize_t term_pty_read(TermPty *pty, char *buf, size_t n) {
    if (!pty || pty->master_fd < 0 || !buf || n == 0) return -1;
    ssize_t r = read(pty->master_fd, buf, n);
    if (r > 0) return r;
    if (r == 0) {
        check_child_reap(pty);
        return 0;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        check_child_reap(pty);
        return 0;
    }
    check_child_reap(pty);
    return -1;
}

ssize_t term_pty_write(TermPty *pty, const char *buf, size_t n) {
    if (!pty || pty->master_fd < 0 || !buf) return -1;
    size_t sent = 0;
    int retries = 0;
    while (sent < n) {
        ssize_t w = write(pty->master_fd, buf + sent, n - sent);
        if (w > 0) {
            sent += (size_t)w;
            retries = 0;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && retries < 20) {
            retries++;
            usleep(1000);
        } else {
            return sent > 0 ? (ssize_t)sent : -1;
        }
    }
    return (ssize_t)sent;
}

int term_pty_resize(TermPty *pty, int rows, int cols) {
    if (!pty || pty->master_fd < 0) return -1;
    pty->rows = clamp_dim(rows, TERM_MIN_ROWS, TERM_MAX_ROWS);
    pty->cols = clamp_dim(cols, TERM_MIN_COLS, TERM_MAX_COLS);
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)pty->rows;
    ws.ws_col = (unsigned short)pty->cols;
    ws.ws_xpixel = (unsigned short)(pty->cols * TERM_CELL_W);
    ws.ws_ypixel = (unsigned short)(pty->rows * TERM_CELL_H);
    return ioctl(pty->master_fd, TIOCSWINSZ, &ws);
}

int term_pty_alive(TermPty *pty) {
    if (!pty) return 0;
    check_child_reap(pty);
    return pty->alive;
}

int term_pty_has_foreground_job(const TermPty *pty) {
    if (!pty || pty->master_fd < 0 || !pty->alive) return 0;
    pid_t fg = tcgetpgrp(pty->master_fd);
    return (fg > 0 && fg != pty->child_pid) ? 1 : 0;
}

void term_pty_close(TermPty *pty) {
    if (!pty) return;
    if (pty->master_fd >= 0) {
        close(pty->master_fd);
        pty->master_fd = -1;
    }
    if (pty->child_pid > 0) {
        check_child_reap(pty);
        if (pty->alive) {
            (void)kill(-pty->child_pid, SIGHUP);
            (void)kill(pty->child_pid, SIGHUP);
            for (int i = 0; i < 15 && pty->alive; i++) {
                usleep(5000);
                check_child_reap(pty);
            }
        }
        if (pty->alive) {
            (void)kill(-pty->child_pid, SIGKILL);
            (void)kill(pty->child_pid, SIGKILL);
            int status = 0;
            (void)waitpid(pty->child_pid, &status, 0);
            pty->alive = 0;
        }
        pty->child_pid = -1;
    }
    pty->alive = 0;
}
