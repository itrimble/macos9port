#include "browser_stage.h"
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#include <removefile.h>
#endif

static const char kExeTail[] = "/Contents/MacOS/OS999Browser";
static const char kCef[] = "/Contents/Frameworks/Chromium Embedded Framework.framework/Versions/A/Chromium Embedded Framework";

/* Writes the helper bundle root (".../OS999Browser.app") into `bundle`; 0 on success. */
static int bundle_of(const char *exe, char *bundle, size_t n) {
    size_t len = exe ? strlen(exe) : 0, tail = sizeof kExeTail - 1;
    if (len <= tail || len - tail >= n || strcmp(exe + len - tail, kExeTail)) return -1;
    memcpy(bundle, exe, len - tail); bundle[len - tail] = 0;
    return 0;
}

int browser_stage_is_nested(const char *exe) {
    char bundle[PATH_MAX];
    if (bundle_of(exe, bundle, sizeof bundle)) return 0;
    char *leaf = strrchr(bundle, '/');
    if (!leaf) return 0;
    for (const char *p = bundle; p < leaf; p++)
        if (!strncmp(p, ".app/", 5) || (p[0] == '.' && !strncmp(p, ".app", 4) && p + 4 == leaf)) return 1;
    return 0;
}

#ifdef __APPLE__
static int stamp_of(const char *bundle, char *out, size_t n) {
    char exe[PATH_MAX], cef[PATH_MAX]; struct stat a, b;
    snprintf(exe, sizeof exe, "%s%s", bundle, kExeTail);
    snprintf(cef, sizeof cef, "%s%s", bundle, kCef);
    if (stat(exe, &a) || stat(cef, &b)) return -1;
    snprintf(out, n, "%lld:%ld.%ld:%lld:%ld.%ld\n", (long long)a.st_size, (long)a.st_mtimespec.tv_sec,
             (long)a.st_mtimespec.tv_nsec, (long long)b.st_size, (long)b.st_mtimespec.tv_sec,
             (long)b.st_mtimespec.tv_nsec);
    return 0;
}
static int read_file(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "r"); if (!f) return -1;
    size_t r = fread(out, 1, n - 1, f); out[r] = 0; fclose(f); return 0;
}
static int mkdirs(const char *path) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; if (mkdir(tmp, 0700) && errno != EEXIST) return -1; *p = '/'; }
    return mkdir(tmp, 0700) && errno != EEXIST ? -1 : 0;
}
#endif

int browser_stage_helper(const char *exe, const char *support_dir, char *out, size_t n) {
    snprintf(out, n, "%s", exe ? exe : "");
#ifdef __APPLE__
    char bundle[PATH_MAX];
    if (!exe || bundle_of(exe, bundle, sizeof bundle)) return -1;
    if (!browser_stage_is_nested(exe)) return 0;
    char dir[PATH_MAX];
    if (support_dir && *support_dir) snprintf(dir, sizeof dir, "%s", support_dir);
    else {
        const char *home = getenv("HOME"); struct passwd *pw = getpwuid(getuid());
        if (!home || !*home) home = pw ? pw->pw_dir : NULL;
        if (!home || snprintf(dir, sizeof dir, "%s/Library/Application Support/OS999/Helpers", home) >= (int)sizeof dir) return -1;
    }
    char dest[PATH_MAX], stamp_path[PATH_MAX], want[128], have[128] = "", fresh[PATH_MAX], staged[PATH_MAX];
    if (mkdirs(dir) || snprintf(dest, sizeof dest, "%s/OS999Browser.app", dir) >= (int)sizeof dest ||
        snprintf(stamp_path, sizeof stamp_path, "%s/OS999Browser.stamp", dir) >= (int)sizeof stamp_path ||
        snprintf(staged, sizeof staged, "%s%s", dest, kExeTail) >= (int)sizeof staged ||
        stamp_of(bundle, want, sizeof want)) return -1;
    if (!read_file(stamp_path, have, sizeof have) && !strcmp(have, want) && !access(staged, X_OK)) {
        snprintf(out, n, "%s", staged); return 0;
    }
    if (snprintf(fresh, sizeof fresh, "%s.new-%ld", dest, (long)getpid()) >= (int)sizeof fresh) return -1;
    removefile(fresh, NULL, REMOVEFILE_RECURSIVE);
    if (copyfile(bundle, fresh, NULL, COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE) != 0) {
        removefile(fresh, NULL, REMOVEFILE_RECURSIVE); return -1;
    }
    unlink(stamp_path);
    removefile(dest, NULL, REMOVEFILE_RECURSIVE);
    FILE *f = NULL;
    if (rename(fresh, dest) != 0 || !(f = fopen(stamp_path, "w"))) { removefile(fresh, NULL, REMOVEFILE_RECURSIVE); return -1; }
    int ok = fputs(want, f) >= 0; ok = fclose(f) == 0 && ok;
    if (!ok || access(staged, X_OK)) return -1;
    snprintf(out, n, "%s", staged);
    return 0;
#else
    (void)support_dir; return browser_stage_is_nested(exe) ? -1 : 0;
#endif
}
