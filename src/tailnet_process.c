#include "tailnet.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *tailnet_cli(void) {
    const char *override = getenv("OS999_TAILSCALE_CLI");
    if (override && override[0] == '/' && !access(override, X_OK)) return override;
    const char *paths[] = {"/usr/local/bin/tailscale", "/opt/homebrew/bin/tailscale",
        "/usr/bin/tailscale", "/Applications/Tailscale.app/Contents/MacOS/Tailscale"};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) if (!access(paths[i], X_OK)) return paths[i];
    return NULL;
}
static long long millis(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int tailnet_capture(char *const argv[], char *out, size_t size) {
    if (!argv || !argv[0] || !out || size < 2) return -1;
    int fd[2]; if (pipe(fd)) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (!pid) {
        close(fd[0]); dup2(fd[1], STDOUT_FILENO); close(fd[1]);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, STDERR_FILENO); dup2(null, STDIN_FILENO); close(null); }
        execv(argv[0], argv); _exit(127);
    }
    close(fd[1]); fcntl(fd[0], F_SETFL, O_NONBLOCK);
    size_t used = 0; long long deadline = millis() + 8000;
    int status = 0, exited = 0, eof = 0, bad = 0;
    while (!eof || !exited) {
        if (millis() >= deadline) { bad = 1; break; }
        struct pollfd pollfd = {fd[0], POLLIN, 0}; poll(&pollfd, 1, 50);
        ssize_t n = read(fd[0], out + used, size - 1 - used);
        if (n > 0) {
            used += (size_t)n;
            if (used >= size - 1) { bad = 1; break; }
        } else if (!n) eof = 1;
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { bad = 1; break; }
        if (!exited) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) exited = 1;
            else if (r < 0 && errno != EINTR) { bad = 1; break; }
        }
    }
    close(fd[0]);
    if (!exited) { kill(pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    out[used] = 0;
    return bad || !WIFEXITED(status) || WEXITSTATUS(status) ? -1 : 0;
}
int tailnet_run(char *const argv[]) {
    /* The child shares this utility's PTY; SSH owns its normal password/raw UI. */
    struct sigaction ignore = {0}, old;
    ignore.sa_handler = SIG_IGN; sigaction(SIGINT, &ignore, &old);
    pid_t pid = fork();
    if (!pid) {
        signal(SIGINT, SIG_DFL); signal(SIGQUIT, SIG_DFL);
        execv(argv[0], argv); perror("Could not launch command"); _exit(127);
    }
    int status = 0; pid_t waited = -1;
    if (pid > 0) do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    sigaction(SIGINT, &old, NULL);
    return waited < 0 || !WIFEXITED(status) ? -1 : WEXITSTATUS(status);
}
