#include "ospi.h"
#include "../src/session_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static int write_bytes(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int rc = n && fwrite(data, 1, n, f) != n ? -1 : 0;
    if (fclose(f) != 0) rc = -1;
    return rc;
}

static int find_win(const Session *s, const char *title) {
    for (int i = 0; i < s->nwin; i++) if (strcmp(s->win[i].title, title) == 0) return i;
    return -1;
}

static int find_item(const Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) if (strcmp(w->items[i].name, name) == 0) return i;
    return -1;
}

static int write_tool(const char *path, const char *script) {
    return write_bytes(path, script, strlen(script)) || chmod(path, 0755) != 0 ? -1 : 0;
}

static void test_sit_install(const char *root) {
    char tool_dir[] = "/tmp/ospi-tools-XXXXXX";
    char *tools = mkdtemp(tool_dir);
    CHECK(tools != NULL);
    if (!tools) return;
    char curl[OSPI_PATH], unar[OSPI_PATH], systemless[OSPI_PATH], sentinel[OSPI_PATH];
    snprintf(curl, sizeof curl, "%s/curl", tools);
    snprintf(unar, sizeof unar, "%s/unar", tools);
    snprintf(systemless, sizeof systemless, "%s/systemless", tools);
    snprintf(sentinel, sizeof sentinel, "%s/systemless-called", tools);
    CHECK(write_tool(curl,
        "#!/bin/sh\nout=\nurl=\nwhile [ $# -gt 0 ]; do\n"
        "if [ \"$1\" = -o ]; then out=$2; shift 2; else url=$1; shift; fi\ndone\n"
        "case $url in *macintoshgarden.org/apps/sit-probe) "
        "printf '%s' '<a href=\"https://download.macintoshgarden.org/apps/Sample.SIT?token=x\">x</a>' >\"$out\";; "
        "*download.macintoshgarden.org/apps/Sample.SIT*) printf 'fixture-sit' >\"$out\";; *) exit 9;; esac\n") == 0);
    CHECK(write_tool(unar,
        "#!/bin/sh\nout=\nwhile [ $# -gt 0 ]; do if [ \"$1\" = -o ]; then out=$2; shift 2; else shift; fi; done\n"
        "[ \"$OSPI_TEST_UNAR_MODE\" = fail ] && exit 17\n"
        "if [ \"$OSPI_TEST_UNAR_MODE\" = deep ]; then d=$out; i=0; while [ $i -lt 10 ]; do d=$d/l$i; /bin/mkdir \"$d\" || exit 18; i=$((i+1)); done; printf deep >\"$d/file\"; exit 0; fi\n"
        "/bin/mkdir -p \"$out/Fixture App\" || exit 19\n"
        "printf DATA >\"$out/Fixture App/Fixture App\"\nprintf RSRC >\"$out/Fixture App/Fixture App.rsrc\"\n") == 0);
    char sys_script[OSPI_PATH + 80];
    snprintf(sys_script, sizeof sys_script, "#!/bin/sh\n/bin/touch '%s'\n", sentinel);
    CHECK(write_tool(systemless, sys_script) == 0);
    const char *old_path = getenv("PATH");
    char saved_path[OSPI_PATH];
    snprintf(saved_path, sizeof saved_path, "%s", old_path ? old_path : "");
    char path[OSPI_PATH];
    snprintf(path, sizeof path, "%s:/usr/bin:/bin", tools);
    CHECK(setenv("PATH", path, 1) == 0);
    char appdir[OSPI_PATH], archive[OSPI_PATH], rel[OSPI_PATH], err[240], extracted[OSPI_PATH];
    snprintf(appdir, sizeof appdir, "%s/Applications", root);
    snprintf(archive, sizeof archive, "%s/Local.SIT", appdir);
    CHECK(write_bytes(archive, "fixture", 7) == 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "success", 1) == 0);
    CHECK(install_sit(root, "Applications/../Local.SIT", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(install_sit(root, "Applications/Local.SIT", rel, sizeof rel, err, sizeof err) == 0);
    CHECK(strcmp(rel, "Applications/Local") == 0);
    char extracted_full[OSPI_PATH];
    snprintf(extracted_full, sizeof extracted_full, "%s/%s", root, rel);
    snprintf(extracted, sizeof extracted, "%s/Fixture App/Fixture App.rsrc", extracted_full);
    CHECK(access(extracted, F_OK) == 0 && access(archive, F_OK) == 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "fail", 1) == 0);
    CHECK(install_sit(root, "Applications/Local.SIT", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "Could not expand") != NULL && access(archive, F_OK) == 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "deep", 1) == 0);
    CHECK(install_sit(root, "Applications/Local.SIT", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "partial files") != NULL && rel[0] != 0 && access(archive, F_OK) == 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "success", 1) == 0);
    char gui_archive[OSPI_PATH];
    snprintf(gui_archive, sizeof gui_archive, "%s/Click.SIT", appdir);
    CHECK(write_bytes(gui_archive, "fixture", 7) == 0);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (s) {
        CHECK(open_folder(s, "Applications") == 0);
        Window *w = &s->win[s->nwin - 1];
        int idx = find_item(w, "Click.SIT");
        CHECK(idx >= 0 && session_is_sit(w->items[idx].name));
        if (idx >= 0) open_item(s, w, idx);
        CHECK(find_win(s, "Click") >= 0);
        session_destroy(s);
    }
    CHECK(access(sentinel, F_OK) != 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "fail", 1) == 0);
    CHECK(install_garden(root, "https://macintoshgarden.org/apps/sit-probe", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "Archive saved") != NULL && rel[0] != 0);
    char garden_dir[OSPI_PATH], garden_archive[OSPI_PATH];
    snprintf(garden_dir, sizeof garden_dir, "%s/%s", root, rel);
    snprintf(garden_archive, sizeof garden_archive, "%s/Sample.SIT", garden_dir);
    CHECK(access(garden_archive, F_OK) == 0);
    CHECK(setenv("OSPI_TEST_UNAR_MODE", "success", 1) == 0);
    CHECK(install_garden(root, "https://macintoshgarden.org/apps/sit-probe", rel, sizeof rel, err, sizeof err) == 0);
    snprintf(garden_dir, sizeof garden_dir, "%s/%s", root, rel);
    snprintf(extracted, sizeof extracted, "%s/Fixture App/Fixture App.rsrc", garden_dir);
    snprintf(garden_archive, sizeof garden_archive, "%s/Sample.SIT", garden_dir);
    CHECK(access(extracted, F_OK) == 0 && access(garden_archive, F_OK) == 0);
    CHECK(setenv("PATH", saved_path, 1) == 0);
    unsetenv("OSPI_TEST_UNAR_MODE");
    unlink(curl); unlink(unar); unlink(systemless);
    rmdir(tools);
}


int main(void) {
    char root_template[] = "/tmp/ospi-sit-test-XXXXXX";
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (!root) return 1;
    char appdir[OSPI_PATH];
    snprintf(appdir, sizeof appdir, "%s/Applications", root);
    CHECK(volume_mkdir_p(appdir) == 0);
    test_sit_install(root);
    char cmd[OSPI_PATH + 32];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (fails == 0) (void)system(cmd); else fprintf(stderr, "left fixture at %s\n", root);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("archive install ok");
    return 0;
}
