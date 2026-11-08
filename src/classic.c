#include "ospi.h"
#include "native_launch.h"

#include <dirent.h>
#include <fcntl.h>
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
    static const char *exts[] = {".sit", ".sitx", ".hqx", ".bin", ".sea", ".cpt", ".zip",
                                 ".img", ".dsk",  ".toast", ".iso", ".cdr", ".dmg", NULL};
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

static int under_root(const char *root, const char *path) {
    size_t n = root ? strlen(root) : 0;
    if (!n || !path || strncmp(path, root, n) != 0) {
        return 0;
    }
    return path[n] == 0 || path[n] == '/';
}

static int archive_rank(const char *name) {
    if (ends_with(name, ".sit") || ends_with(name, ".sitx")) {
        return 3;
    }
    if (ends_with(name, ".zip") || ends_with(name, ".hqx") || ends_with(name, ".bin") ||
        ends_with(name, ".sea") || ends_with(name, ".cpt")) {
        return 2;
    }
    if (ends_with(name, ".img") || ends_with(name, ".dsk") || ends_with(name, ".toast") ||
        ends_with(name, ".iso") || ends_with(name, ".cdr") || ends_with(name, ".dmg")) {
        return 1;
    }
    return 0;
}

static int best_archive(const char *dir, char *out, size_t n) {
    DIR *d = opendir(dir);
    struct dirent *de;
    char name[OSPI_NAME];
    int best = 0;
    if (!d) {
        return 0;
    }
    name[0] = 0;
    while ((de = readdir(d)) != NULL) {
        char path[OSPI_PATH];
        struct stat st;
        int rank;
        int k;
        if (de->d_name[0] == '.' || strlen(de->d_name) >= OSPI_NAME) {
            continue;
        }
        rank = archive_rank(de->d_name);
        if (rank < best || (rank == best && strcasecmp(de->d_name, name) >= 0)) {
            continue;
        }
        k = snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (k < 0 || (size_t)k >= sizeof path || lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        best = rank;
        snprintf(name, sizeof name, "%s", de->d_name);
    }
    closedir(d);
    if (!best) {
        return 0;
    }
    int k = snprintf(out, n, "%s/%s", dir, name);
    return k > 0 && (size_t)k < n;
}

static int looks_binary(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char buf[4096];
    size_t got;
    if (!f) {
        return 0;
    }
    got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    for (size_t i = 0; i < got; i++) {
        if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t' && buf[i] < 32) {
            return 1;
        }
    }
    return 0;
}

static void copy_hint(char *hint, size_t n, const char *name) {
    size_t keep = 31;
    size_t len;
    if (!hint || n < 2) {
        return;
    }
    hint[0] = 0;
    if (!name) {
        return;
    }
    if (n - 1 < keep) {
        keep = n - 1;
    }
    len = strlen(name);
    if (len > keep) {
        len = keep;
    }
    memcpy(hint, name, len);
    hint[len] = 0;
}

int classic_bundle(const char *root, const char *full, const char *name, char *launch, size_t launch_n,
                   char *hint, size_t hint_n) {
    char dir[OSPI_PATH];
    if (launch && launch_n) {
        launch[0] = 0;
    }
    if (hint && hint_n) {
        hint[0] = 0;
    }
    if (!root || !full || !name || !launch || !launch_n || !under_root(root, full)) {
        return 1;
    }
    if (classic_is_program(name)) {
        int k = snprintf(launch, launch_n, "%s", full);
        return k > 0 && (size_t)k < launch_n ? 0 : 1;
    }
    if (!looks_binary(full)) {
        return 1;
    }
    snprintf(dir, sizeof dir, "%s", full);
    for (int depth = 0; depth < 16; depth++) {
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) {
            break;
        }
        *slash = 0;
        if (!under_root(root, dir)) {
            break;
        }
        if (best_archive(dir, launch, launch_n)) {
            copy_hint(hint, hint_n, name);
            return 0;
        }
        if (strcmp(dir, root) == 0) {
            break;
        }
    }
    return 1;
}

static void silence(void) {
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        return;
    }
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2) {
        close(fd);
    }
}

static int wait_grand(pid_t grand) {
    for (int i = 0; i < 8; i++) {
        int st = 0;
        pid_t got = waitpid(grand, &st, WNOHANG);
        if (got == grand) {
            if (WIFEXITED(st)) {
                return WEXITSTATUS(st);
            }
            return 1;
        }
        if (got < 0) {
            return 0;
        }
        usleep(100000);
    }
    return 0;
}

int classic_launch(const char *path, const char *hint, char *err, size_t err_n) {
    if (err && err_n) {
        err[0] = 0;
    }
    char bin[OSPI_PATH];
    if (!find_systemless(bin, sizeof bin)) {
        snprintf(err, err_n,
                 "Systemless is not installed.\n\n"
                 "brew install benletchford/tap/systemless\n\n"
                 "OS999 Stylized does not include a Toolbox.\n"
                 "Systemless is a separate program.");
        return -1;
    }
    struct stat st;
    if (!path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(err, err_n, "That file is not a program OS999 Stylized can open.");
        return -1;
    }
    pid_t mid = fork();
    if (mid < 0) {
        snprintf(err, err_n, "Could not start Systemless.");
        return -1;
    }
    if (mid == 0) {
        pid_t grand;
        setsid();
        grand = fork();
        if (grand == 0) {
            char *argv[] = {bin, (char *)path, NULL};
            silence();
            if (hint && hint[0]) {
                setenv("SYSTEMLESS_LOAD_EXECUTABLE", hint, 1);
            }
            execve(bin, argv, environ);
            _exit(127);
        }
        _exit(grand < 0 ? 127 : wait_grand(grand));
    }
    int status = 0;
    if (waitpid(mid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, err_n, "Systemless could not open that program.");
        return -1;
    }
    return 0;
}

int classic_open(const char *root, const char *full, const char *name, char *err, size_t err_n) {
    int native = native_open(root, full, err, err_n);
    if (native != 1) return native;
    /* A selected non-archive must never launch an unrelated sibling archive.
     * Explicit archive callers retain the separate legacy fallback. */
    if (!classic_is_program(name)) return 1;
    char launch[OSPI_PATH];
    char hint[40];
    int picked = classic_bundle(root, full, name, launch, sizeof launch, hint, sizeof hint);
    if (picked != 0) {
        return picked;
    }
    return classic_launch(launch, hint[0] ? hint : NULL, err, err_n);
}
