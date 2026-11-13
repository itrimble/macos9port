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
        setenv("HOME", home, 1);                                    /* keep the legacy carry-over away from the real Mac home */
        CHECK(quake_data_import_demo(home, vol) == -1);
    }
    { /* The full retail set: nine paks in the real table; here a two-file synthetic table with injected hashes. */
        static const char b0[] = "retail fixture pak0", b1[] = "retail fixture pak1 longer";
        const QuakePak fake[] = {
            {"pak0.pk3", 19, "70e31c53f2d55f25f8f20c8465935d39693986575cdbaf3de0f754f4e3b73b76"},
            {"pak1.pk3", 26, "0c63e4d72d5a2b211b60cf1c3572e41764fc1897211db967d91d2e6d934ad4f5"}};
        int cnt = 0; const QuakePak *real = quake_data_retail_table(&cnt);
        CHECK(cnt == 9 && !strcmp(real[0].name, "pak0.pk3") && !strcmp(real[8].name, "pak8.pk3"));
        for (int i = 0; i < cnt; i++) CHECK(real[i].size > 0 && strlen(real[i].sha256) == 64);
        char src[512], nest[512], base[512], h2[512], inst[512], root2[512];
        snprintf(src, sizeof src, "%s/chosen", root); mkdir(src, 0755);
        snprintf(nest, sizeof nest, "%s/chosen/Quake III Arena (Complete)", root); mkdir(nest, 0755);
        snprintf(base, sizeof base, "%s/baseq3", nest); mkdir(base, 0755);
        snprintf(p, sizeof p, "%s/pak0.pk3", base); FILE *f = fopen(p, "wb"); fwrite(b0, 1, 19, f); fclose(f);
        const char *const dirs[] = {src};
        snprintf(h2, sizeof h2, "%s/home2", root); mkdir(h2, 0755);
        snprintf(root2, sizeof root2, "%s/Library/Application Support/OS999/Quake3-Full", h2);
        CHECK(quake_data_import_set(h2, dirs, 1, fake, 2) == -1);        /* only pak0: incomplete set */
        snprintf(inst, sizeof inst, "%s/baseq3/pak0.pk3", root2);
        CHECK(access(inst, F_OK) != 0);
        snprintf(p, sizeof p, "%s/pak1.pk3", base); f = fopen(p, "wb"); fputs("retail fixture pak1 TAMPER!", f); fclose(f);
        CHECK(quake_data_import_set(h2, dirs, 1, fake, 2) == -1);        /* tampered, same size */
        CHECK(access(inst, F_OK) != 0);                                  /* nothing installed from a bad set */
        f = fopen(p, "wb"); fwrite(b1, 1, 26, f); fclose(f);
        CHECK(quake_data_import_set(h2, dirs, 1, fake, 2) == 0);         /* found inside a subfolder */
        CHECK(quake_data_retail_set_ok(root2, fake, 2) && !quake_data_retail_set_ok(root2, real, cnt));
        struct stat st; CHECK(!stat(inst, &st) && (st.st_mode & 077) == 0);
        CHECK(quake_data_import_set(h2, dirs, 1, fake, 2) == 1);         /* already installed */
        snprintf(p, sizeof p, "%s.part", inst); CHECK(access(p, F_OK) != 0);
        setenv("OS999_QUAKE3_DATA", root2, 1);
        CHECK(quake_launch_resolve_retail(&q, exe, h2) == -3);           /* the real table does not match fixtures */
        unsetenv("OS999_QUAKE3_DATA");
        CHECK(quake_launch_resolve_retail(&q, exe, h2) == -3);           /* nothing installed under the real name */
        CHECK(quake_launch_resolve_retail(&q, "/nonexistent", h2) == -1);
        const char *rd = getenv("OS999_TEST_RETAIL_DATA"); /* optional: a folder holding the real baseq3, kept outside Git */
        if (rd) {
            setenv("OS999_QUAKE3_DATA", rd, 1);
            int before = failures;
            CHECK(quake_launch_resolve_retail(&q, exe, h2) == 0 && !q.arena_demo && !strcmp(q.game, "baseq3"));
            CHECK(strstr(q.homepath, "Quake3-Full-Home") != NULL);
            unsetenv("OS999_QUAKE3_DATA");
            if (failures == before) printf("retail data at %s: accepted, game=%s\n", rd, q.game);
            else printf("retail data at %s: REJECTED by the real-data check\n", rd);
        }
        memset(&q, 0, sizeof q); strcpy(q.game, "baseq3");
        strcpy(q.basepath, "/tmp/engine"); strcpy(q.datapath, "/tmp/full"); strcpy(q.homepath, "/tmp/full-home");
        char *ra[20];
        CHECK(quake_launch_argv(&q, ra, 16) == 0 /* too small */);
        CHECK(quake_launch_argv(&q, ra, 19) == 16 && ra[16] == NULL);
        CHECK(!strcmp(ra[8], "fs_homepath") && !strcmp(ra[9], "\"/tmp/full-home\""));
        CHECK(!strcmp(ra[11], "sv_pure") && !strcmp(ra[12], "0"));   /* retail local play needs sv_pure 0 */
        CHECK(!strcmp(ra[14], "net_noudp") && !strcmp(ra[15], "1")); /* local only, like the demo */
        setenv("OS999_QUAKE3_MAP", "q3dm17", 1);                     /* test hook: start in a map */
        CHECK(quake_launch_argv(&q, ra, 19) == 18 && !strcmp(ra[16], "+map") && !strcmp(ra[17], "q3dm17") && !ra[18]);
        setenv("OS999_QUAKE3_MAP", "q3dm17; quit", 1);              /* anything but a plain map name is ignored */
        CHECK(quake_launch_argv(&q, ra, 19) == 16 && ra[16] == NULL);
        unsetenv("OS999_QUAKE3_MAP");
        CHECK(quake_launch_argv(&q, ra, 19) == 16);
        for (int i = 1; i < 16; i++) CHECK(strcmp(ra[i], "fs_game") && strcmp(ra[i], "os999_demoProfile"));
    }
#endif
    unsetenv("OS999_QUAKE3_DATA");
    printf("quake launch: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
