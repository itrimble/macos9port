#include "quake_launch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void touch(const char *p, int exec) { FILE *f = fopen(p, "w"); if (f) { fputs("x", f); fclose(f); } chmod(p, exec ? 0755 : 0644); }

int main(void) {
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
    printf("quake launch: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
