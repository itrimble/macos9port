#include "ospi.h"

#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int ends_with(const char *name, const char *ext) {
    size_t n = strlen(name);
    size_t e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

int classic_is_program(const char *name) {
    static const char *exts[] = {".sit",  ".sitx", ".hqx", ".bin", ".sea", ".cpt",
                                 ".img",  ".dsk",  ".toast", ".iso", ".cdr", ".dmg",
                                 NULL};
    if (!name || !name[0]) {
        return 0;
    }
    for (int i = 0; exts[i]; i++) {
        if (ends_with(name, exts[i])) {
            return 1;
        }
    }
    return 0;
}

static int executable(const char *path) { return path && path[0] && access(path, X_OK) == 0; }

static int find_systemless(char *out, size_t n) {
    static const char *fixed[] = {"/opt/homebrew/bin/systemless", "/usr/local/bin/systemless", NULL};
    for (int i = 0; fixed[i]; i++) {
        if (executable(fixed[i])) {
            snprintf(out, n, "%s", fixed[i]);
            return 1;
        }
    }
    const char *path = getenv("PATH");
    if (!path) {
        return 0;
    }
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf; *p;) {
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = 0;
        }
        char cand[OSPI_PATH];
        int k = snprintf(cand, sizeof cand, "%s/systemless", p);
        if (k > 0 && (size_t)k < sizeof cand && executable(cand)) {
            snprintf(out, n, "%s", cand);
            return 1;
        }
        if (!colon) {
            break;
        }
        p = colon + 1;
    }
    return 0;
}

int classic_launch(const char *path, char *err, size_t err_n) {
    if (err && err_n) {
        err[0] = 0;
    }
    char bin[OSPI_PATH];
    if (!find_systemless(bin, sizeof bin)) {
        snprintf(err, err_n,
                 "Systemless is not installed.\n\n"
                 "brew install benletchford/tap/systemless\n\n"
                 "OS Pi does not include a Toolbox.\n"
                 "Systemless is a separate program.");
        return -1;
    }
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(err, err_n, "That file is not a program OS Pi can open.");
        return -1;
    }
    pid_t mid = fork();
    if (mid < 0) {
        snprintf(err, err_n, "Could not start Systemless.");
        return -1;
    }
    if (mid == 0) {
        setsid();
        pid_t grand = fork();
        if (grand == 0) {
            char *argv[] = {bin, (char *)path, NULL};
            execve(bin, argv, environ);
            _exit(127);
        }
        _exit(grand < 0 ? 127 : 0);
    }
    int status = 0;
    if (waitpid(mid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, err_n, "Could not start Systemless.");
        return -1;
    }
    return 0;
}
