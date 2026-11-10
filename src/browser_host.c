#include "browser_stage.h"
#include "browser_window_int.h"
#include "browser_overlay.h"

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

BrowserState BS = {.fd = -1, .focus_win = -1, .press_win = -1, .next_id = 1};

void bh_send_full(Os999BrowserMsg *m) {
    if (BS.fd < 0) return;
    const char *p = (const char *)m;
    size_t left = sizeof *m;
    while (left > 0) {
        ssize_t n = write(BS.fd, p, left);
        if (n <= 0) return;
        p += n;
        left -= (size_t)n;
    }
}

void bh_send(uint32_t kind, int id, int x, int y, int a, int b, const char *text) {
    if (kind == OS999B_FOCUS && BS.fd >= 0) {
        if (a) BS.input_id = id;
        else if (BS.input_id == id) BS.input_id = 0;
    }
    Os999BrowserMsg m;
    memset(&m, 0, sizeof m);
    m.kind = kind; m.id = id; m.x = x; m.y = y; m.a = a; m.b = b;
    if (text) snprintf(m.text, sizeof m.text, "%s", text);
    bh_send_full(&m);
}

static int exe_dir(char *out, size_t n) {
#ifdef __APPLE__
    uint32_t size = (uint32_t)n;
    if (_NSGetExecutablePath(out, &size) != 0) return -1;
#else
    ssize_t len = readlink("/proc/self/exe", out, n - 1);
    if (len <= 0) return -1;
    out[len] = 0;
#endif
    char *slash = strrchr(out, '/');
    if (slash) *slash = 0;
    return 0;
}

static int find_helper_raw(char *out, size_t n) {
    const char *env = getenv("OSPI_BROWSER");
    if (env && access(env, X_OK) == 0) { snprintf(out, n, "%s", env); return 0; }
    char dir[PATH_MAX];
    if (exe_dir(dir, sizeof dir) != 0) return -1;
    static const char *rel[] = {
#ifdef __APPLE__
        "/../Frameworks/OS999Browser.app/Contents/MacOS/OS999Browser",
        "/browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser",
        "/../../../browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser",
#else
        "/os999-browser/OS999Browser",
        "/browser/Release/OS999Browser",
#endif
    };
    for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++) {
        snprintf(out, n, "%s%s", dir, rel[i]);
        if (access(out, X_OK) == 0) return 0;
    }
    return -1;
}

/* Nested inside another .app the helper crashes on macOS 27, so prefer a clone
 * outside any .app; OSPI_BROWSER stays an explicit override and is not staged. */
static int find_helper(char *out, size_t n) {
    char own[OSPI_PATH + 32];
    if (find_helper_raw(out, n) != 0) return -1;
    const char *env = getenv("OSPI_BROWSER");
    if (env && !strcmp(env, out)) return 0;
    char staged[PATH_MAX];
    if (browser_stage_helper(out, os999_private_dir("Helpers", own, sizeof own) ? NULL : own, staged, sizeof staged) == 0) snprintf(out, n, "%s", staged);
    return 0;
}

int bh_ensure(const char *root) {
    if (BS.fd >= 0) return 0;
    BS.error[0] = 0;
    char path[PATH_MAX], port[128];
    if (find_helper(path, sizeof path) != 0) {
        snprintf(BS.error, sizeof BS.error, "OS999 Browser helper not found. Run sh browser/scripts/build.sh");
        return -1;
    }
    if (overlay_listen(port, sizeof port) != 0) {
        snprintf(BS.error, sizeof BS.error, "Could not open the browser frame channel.");
        return -1;
    }
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(sv[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    /* Downloads land only in the volume's Downloads folder (checklist B04). */
    char downloads[OSPI_PATH + 16];
    snprintf(BS.root, sizeof BS.root, "%s", root);
    snprintf(downloads, sizeof downloads, "%s/Downloads", root);
    mkdir(downloads, 0755);
    char a_fd[32], a_port[192], a_dl[OSPI_PATH + 40], a_log[PATH_MAX + 24];
    snprintf(a_fd, sizeof a_fd, "--os999-embed-fd=3");
    snprintf(a_port, sizeof a_port, "--os999-embed-port=%s", port);
    snprintf(a_dl, sizeof a_dl, "--os999-download-dir=%s", downloads);
    char *argv[9] = {path, a_fd, a_port, a_dl, NULL};
    int argc = 4;
    /* The browser's profile, cookies and cache stay inside the OS999 disk. */
    char own[OSPI_PATH + 32], a_profile[OSPI_PATH + 64];
    if (!os999_private_dir("Browser", own, sizeof own)) {
        snprintf(a_profile, sizeof a_profile, "--os999-profile=%s", own); argv[argc++] = a_profile;
    }
    const char *log = getenv("OSPI_BROWSER_LOG");
    if (log) { snprintf(a_log, sizeof a_log, "--os999-stats-log=%s", log); argv[argc++] = a_log; }
#ifdef OS999_ENABLE_TEST_HOOKS
    if (getenv("OSPI_BROWSER_TEST")) argv[argc++] = "--os999-test";
#endif
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 3);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, path, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(sv[1]);
    if (rc != 0) {
        close(sv[0]);
        snprintf(BS.error, sizeof BS.error, "Could not start Chromium (%d).", rc);
        return -1;
    }
    BS.pid = pid;
    BS.fd = sv[0];
    BS.ready = 0;
    BS.input_id = 0;
    BS.in_len = 0;
    return 0;
}

void bh_stop(void) {
    if (BS.pid > 0) {
        bh_send(OS999B_SHUTDOWN, 0, 0, 0, 0, 0, NULL);
        for (int i = 0; i < 60 && waitpid(BS.pid, NULL, WNOHANG) == 0; i++) usleep(50000);
        if (waitpid(BS.pid, NULL, WNOHANG) == 0) kill(BS.pid, SIGTERM);
    }
    if (BS.fd >= 0) close(BS.fd);
    BS.fd = -1;
    BS.pid = 0;
}

static void notice(int w, const char *text) {
    snprintf(BS.win[w].notice, sizeof BS.win[w].notice, "%s", text);
    BS.win[w].notice_ms = SDL_GetTicks();
}

static void handle(Os999BrowserMsg *m) {
    if (m->kind == OS999B_STATUS) { snprintf(BS.status, sizeof BS.status, "%s", m->text); return; }
    if (m->kind == OS999B_READY) { BS.ready = 1; return; }
    if (m->kind == OS999B_SECURITY) {
        snprintf(BS.security, sizeof BS.security, "%s", m->text);
        BS.security_level = m->a;
        BS.security_block = m->b;
        BS.security_ms = SDL_GetTicks();
        return;
    }
    int t = 0, w = bw_find_tab(m->id, &t);
    if (w < 0) return;
    BrowserTab *tab = &BS.win[w].tabs[t];
    char line[260];
    switch (m->kind) {
    case OS999B_APP_RESULT: notice(w, m->text); break;
    case OS999B_TLS:
        tab->verified = m->a == 1;
        snprintf(tab->tls_message, sizeof tab->tls_message, "%s", m->text);
        break;
    case OS999B_TITLE: snprintf(tab->title, sizeof tab->title, "%s", m->text); break;
    case OS999B_ADDRESS: tab->verified = 0; snprintf(tab->url, sizeof tab->url, "%s", m->text); break;
    case OS999B_LOADING:
        tab->loading = m->a; tab->back = m->b & 1; tab->forward = (m->b >> 1) & 1;
        break;
    case OS999B_CURSOR: tab->cursor = m->a; break;
    case OS999B_FULLSCREEN: if (t == BS.win[w].active) bw_set_fullscreen(w, m->a); break;
    case OS999B_OPEN_URL:
        if (m->a) {
            int nw = bw_open_window(m->text);
            if (nw >= 0) break;
        }
        bw_new_tab(w, m->text, 1);
        break;
    case OS999B_DOWNLOAD:
        if (m->b == 1) snprintf(line, sizeof line, "Saved %s to Downloads", m->text);
        else if (m->b == 2) snprintf(line, sizeof line, "Download failed: %s", m->text);
        else snprintf(line, sizeof line, "Downloading %s  %d%%", m->text, m->a < 0 ? 0 : m->a);
        notice(w, line);
        break;
    case OS999B_IME_RECT: {
        Rect c = bw_content(&BS.win[w]);
        SDL_Window *win = SDL_GetMouseFocus() ? SDL_GetMouseFocus() : SDL_GetKeyboardFocus();
        int ww = OSPI_W, wh = OSPI_H;
        if (win) SDL_GetWindowSize(win, &ww, &wh);
        SDL_Rect r = {(c.x + m->x) * ww / OSPI_W, (c.y + m->y) * wh / OSPI_H,
                      m->a * ww / OSPI_W + 1, m->b * wh / OSPI_H + 1};
        SDL_SetTextInputRect(&r);
        break;
    }
    case OS999B_CLOSED: break;
    default: break;
    }
}

void bh_drain(void) {
    if (BS.fd < 0) return;
    for (;;) {
        ssize_t n = recv(BS.fd, (char *)&BS.in + BS.in_len, sizeof BS.in - BS.in_len, MSG_DONTWAIT);
        if (n == 0) {
            close(BS.fd);
            BS.fd = -1;
            snprintf(BS.error, sizeof BS.error, "Chromium closed.");
            return;
        }
        if (n < 0) break;
        BS.in_len += (size_t)n;
        if (BS.in_len < sizeof BS.in) continue;
        BS.in_len = 0;
        BS.in.text[OS999B_TEXT - 1] = 0;
        handle(&BS.in);
    }
    if (BS.pid > 0 && waitpid(BS.pid, NULL, WNOHANG) == BS.pid) {
        BS.pid = 0;
        if (BS.fd >= 0) close(BS.fd);
        BS.fd = -1;
        snprintf(BS.error, sizeof BS.error, "Chromium exited.");
    }
}
