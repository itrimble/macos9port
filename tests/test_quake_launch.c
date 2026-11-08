#include "quake_launch.h"
#include "quake_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void touch(const char *p, int exec) { FILE *f = fopen(p, "w"); if (f) { fputs("x", f); fclose(f); } chmod(p, exec ? 0755 : 0644); }

int main(int argc, char **args) {
    char root[] = "/tmp/os999-quake-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char p[512], exe[512], home[512];
    unsetenv("OS999_QUAKE3"); unsetenv("OS999_QUAKE3_DATA");
    snprintf(exe, sizeof exe, "%s/Contents/MacOS", root);
    snprintf(p, sizeof p, "%s/Contents", root); mkdir(p, 0755); mkdir(exe, 0755);
    snprintf(home, sizeof home, "%s/home", root); mkdir(home, 0755);
    QuakeLaunch q;
    CHECK(quake_launch_resolve(&q, exe, home) == -1);           /* no engine */
    snprintf(p, sizeof p, "%s/Contents/Resources", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Contents/Resources/Quake3", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Contents/Resources/Quake3/quake3", root); touch(p, 0);
    CHECK(quake_launch_resolve(&q, exe, home) == -1);           /* not executable */
    chmod(p, 0755);
    CHECK(quake_launch_resolve(&q, exe, home) == -2);           /* engine, no pak0 */
#ifdef __APPLE__
    const char *rel[] = {"/Library", "/Library/Application Support", "/Library/Application Support/OS999",
                         "/Library/Application Support/OS999/Quake3", "/Library/Application Support/OS999/Quake3/demota"};
#else
    const char *rel[] = {"/.local", "/.local/share", "/.local/share/os999", "/.local/share/os999/quake3",
                         "/.local/share/os999/quake3/demota"};
#endif
    for (int i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s%s", home, rel[i]); mkdir(p, 0755); }
    snprintf(p, sizeof p, "%s%s/pak0.pk3", home, rel[4]); touch(p, 0);
    CHECK(quake_launch_resolve(&q, exe, home) == 0);
    CHECK(!strcmp(q.game, "demota"));
    char *argv[16];
    int n = quake_launch_argv(&q, argv, 16);
    CHECK(n == 10 && !strcmp(argv[1], "+set") && !strcmp(argv[2], "fs_basepath") &&
          !strcmp(argv[8], "fs_game") && !strcmp(argv[9], "demota") && argv[10] == NULL);
    CHECK(strstr(argv[3], "Resources/Quake3") != NULL);
    setenv("OS999_QUAKE3", "/nonexistent/quake3", 1);
    CHECK(quake_launch_resolve(&q, exe, home) == 0);            /* bad override falls back */
    unsetenv("OS999_QUAKE3");
    CHECK(quake_launch_resolve_demo(&q, exe, home) == -3);
    char arena[512]; snprintf(arena, sizeof arena, "%s/arena", root); mkdir(arena, 0755);
    snprintf(p, sizeof p, "%s/demoq3", arena); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/demoq3/pak0.pk3", arena); touch(p, 0);
    setenv("OS999_QUAKE3_DATA", arena, 1);
    CHECK(quake_launch_resolve_demo(&q, exe, home) == -3); /* modified/short original */
    setenv("OS999_QUAKE3_DATA", "/nonexistent", 1);
    CHECK(quake_launch_resolve_demo(&q, exe, home) == -3); /* no fallback */
    memset(&q, 0, sizeof q); q.arena_demo = 1;
    strcpy(q.game, "demoq3"); strcpy(q.homepath, "/tmp/arena-home");
    char *demo_argv[24];
    CHECK(quake_launch_argv(&q, demo_argv, 22) == 0);
    CHECK(quake_launch_argv(&q, demo_argv, 24) == 22 && demo_argv[22] == NULL);
    CHECK(!strcmp(demo_argv[11], "os999_demoProfile") && !strcmp(demo_argv[12], "arena-1.11"));
    CHECK(!strcmp(demo_argv[14], "fs_homepath") && !strcmp(demo_argv[17], "sv_pure"));
    CHECK(!strcmp(demo_argv[20], "net_noudp") && !strcmp(demo_argv[21], "1"));
    strcpy(q.basepath, "/tmp/engine with spaces");
    strcpy(q.datapath, "/tmp/data with spaces");
    CHECK(quake_launch_argv(&q, demo_argv, 24) == 22);
    CHECK(!strcmp(demo_argv[3], "\"/tmp/engine with spaces\"") &&
          !strcmp(demo_argv[6], "\"/tmp/data with spaces\"") &&
          !strcmp(demo_argv[15], "\"/tmp/arena-home\""));
    strcpy(q.basepath, "/tmp/bad\"path");
    CHECK(quake_launch_argv(&q, demo_argv, 24) == 0);
    strcpy(q.basepath, "/tmp/bad\npath");
    CHECK(quake_launch_argv(&q, demo_argv, 24) == 0);
    if (argc > 1) { /* Optional authentic fixture stays outside Git. */
        setenv("OS999_QUAKE3_DATA", args[1], 1);
        CHECK(quake_launch_resolve_demo(&q, exe, home) == 0);
        CHECK(q.arena_demo && !strcmp(q.game, "demoq3"));
    }
#ifdef __APPLE__
    { /* The demo is found in the volume, verified, and installed; wrong or tampered files never are. */
        static const char body[] = "os999 quake data fixture";
        static const char sha[] = "475815b56dc74d836c50d50e2752a29de9f90be76eaa8ecbfc474dbd34553445";
        char vol[512], dl[512], inst[512], bad[512];
        snprintf(vol, sizeof vol, "%s/volume", root); mkdir(vol, 0755);
        snprintf(dl, sizeof dl, "%s/Downloads", vol); mkdir(dl, 0755);
        snprintf(inst, sizeof inst, "%s/Library/Application Support/OS999/Quake3/demoq3/pak0.pk3", home);
        snprintf(bad, sizeof bad, "%s/pak0.pk3", dl);
        FILE *f = fopen(bad, "wb"); fputs("os999 quake data tampered", f); fclose(f);
        const char *const dirs[] = {dl};
        off_t size = (off_t)(sizeof body - 1);
        CHECK(quake_data_import(home, dirs, 1, size, sha) == -1);   /* tampered, same size */
        CHECK(access(inst, F_OK) != 0);
        f = fopen(bad, "wb"); fwrite(body, 1, size, f); fclose(f);
        CHECK(quake_data_check(bad, size, sha) && !quake_data_check(bad, size + 1, sha));
        CHECK(quake_data_import(home, dirs, 1, size, sha) == 0);    /* installed */
        CHECK(quake_data_check(inst, size, sha));
        struct stat st; CHECK(!stat(inst, &st) && (st.st_mode & 077) == 0);
        CHECK(quake_data_import(home, dirs, 1, size, sha) == 1);    /* already installed */
        unlink(inst);
        snprintf(p, sizeof p, "%s.part", inst); CHECK(access(p, F_OK) != 0);
        CHECK(!quake_data_demo_ok(bad));                            /* not the real demo */
        CHECK(quake_data_import_demo(home, vol) == -1);
    }
#endif
    unsetenv("OS999_QUAKE3_DATA");
    printf("quake launch: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
