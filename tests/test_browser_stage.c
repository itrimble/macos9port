/* Staging keeps Chromium out of any enclosing .app (macOS 27 crashes there). */
#include "browser_stage.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void mkp(const char *path) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}
static void put(const char *path, const char *text, int exec) {
    FILE *f = fopen(path, "w"); CHECK(f != NULL); if (!f) return;
    fputs(text, f); fclose(f); chmod(path, exec ? 0755 : 0644);
}
/* Builds <root>/<where>/OS999Browser.app with a fake exe and CEF binary. */
static void make_helper(const char *root, const char *where, const char *exe_text, char *exe_out, size_t n) {
    char dir[PATH_MAX], file[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/%s/OS999Browser.app/Contents/MacOS", root, where); mkp(dir);
    snprintf(exe_out, n, "%s/OS999Browser", dir); put(exe_out, exe_text, 1);
    snprintf(dir, sizeof dir, "%s/%s/OS999Browser.app/Contents/Frameworks/Chromium Embedded Framework.framework/Versions/A", root, where);
    mkp(dir);
    snprintf(file, sizeof file, "%s/Chromium Embedded Framework", dir); put(file, "cef-binary", 0);
}
static long size_of(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_size; }
static long ino_of(const char *p) { struct stat st; return stat(p, &st) ? -1 : (long)st.st_ino; }

int main(void) {
    char root[] = "/tmp/os999-stage-XXXXXX"; CHECK(mkdtemp(root) != NULL);
    char nested[PATH_MAX], plain[PATH_MAX], out[PATH_MAX], support[PATH_MAX];
    make_helper(root, "Outer.app/Contents/Frameworks", "v1", nested, sizeof nested);
    make_helper(root, "PlainDir/Contents/Frameworks", "v1", plain, sizeof plain);
    snprintf(support, sizeof support, "%s/support/Helpers", root);

    CHECK(browser_stage_is_nested(nested));
    CHECK(!browser_stage_is_nested(plain));
    CHECK(!browser_stage_is_nested("/tmp/not-a-helper"));
    CHECK(!browser_stage_is_nested(NULL));

    CHECK(browser_stage_helper(plain, support, out, sizeof out) == 0 && !strcmp(out, plain));
    CHECK(access(support, F_OK) != 0);   /* nothing is staged for a standalone helper */

#ifdef __APPLE__
    CHECK(browser_stage_helper(nested, support, out, sizeof out) == 0);
    CHECK(strcmp(out, nested) != 0 && !strncmp(out, support, strlen(support)));
    CHECK(access(out, X_OK) == 0 && !browser_stage_is_nested(out));
    CHECK(size_of(out) == 2);
    char first[PATH_MAX]; snprintf(first, sizeof first, "%s", out);
    long ino = ino_of(out);

    /* Unchanged source: the same clone is reused, not rebuilt. */
    CHECK(browser_stage_helper(nested, support, out, sizeof out) == 0 && !strcmp(out, first));
    CHECK(ino_of(out) == ino);

    /* A new browser build replaces the clone. */
    sleep(1); put(nested, "version-two", 1);
    CHECK(browser_stage_helper(nested, support, out, sizeof out) == 0 && !strcmp(out, first));
    CHECK(size_of(out) == 11);

    /* A failure falls back to the nested original rather than losing the browser. */
    char blocked[PATH_MAX]; snprintf(blocked, sizeof blocked, "%s/support-file", root); put(blocked, "x", 0);
    char under[PATH_MAX]; snprintf(under, sizeof under, "%s/sub", blocked);
    CHECK(browser_stage_helper(nested, under, out, sizeof out) == -1 && !strcmp(out, nested));
#endif
    char rm[PATH_MAX + 16]; snprintf(rm, sizeof rm, "rm -rf '%s'", root); (void)system(rm);
    printf("browser stage: %s\n", failures ? "FAILED" : "ok");
    return !!failures;
}
