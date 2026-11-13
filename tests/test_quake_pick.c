/* Headless checks for the Quake III "Choose Folder..." flow (src/quake_pick.c): the in-OS999 file dialog points at a
 * folder, the folder is verified by size and SHA-256 (synthetic tables are injected), and a folder that does not
 * verify is refused with nothing copied. */
#include "quake_pick.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *path, const char *data) { FILE *f = fopen(path, "wb"); if (f) { fputs(data, f); fclose(f); } }
static void mk(char *out, size_t n, const char *a, const char *b) { snprintf(out, n, "%s/%s", a, b); mkdir(out, 0755); }

int main(void) {
#ifdef __APPLE__
    char tmpl[] = "/tmp/os999-qpick-XXXXXX", top[PATH_MAX];
    CHECK(mkdtemp(tmpl) && realpath(tmpl, top));
    char vol[PATH_MAX], host[PATH_MAX], priv[PATH_MAX], dl[PATH_MAX], p[PATH_MAX], retail[PATH_MAX], base[PATH_MAX];
    mk(vol, sizeof vol, top, "volume"); mk(host, sizeof host, top, "home"); mk(priv, sizeof priv, vol, ".os999");
    mk(dl, sizeof dl, host, "Downloads"); mk(p, sizeof p, host, "Desktop"); mk(p, sizeof p, host, "Documents");

    FdRoot roots[4]; char paths[3][OSPI_PATH];
    int n = quake_pick_roots(vol, host, roots, paths);
    CHECK(n == 4 && !strcmp(roots[0].label, "OS999 Disk") && !strcmp(roots[1].label, "Downloads"));

    /* Synthetic two-pak retail table and a one-file "demo", all with injected hashes (fixtures, not Quake data). */
    const QuakePak fake[] = {
        {"pak0.pk3", 19, "70e31c53f2d55f25f8f20c8465935d39693986575cdbaf3de0f754f4e3b73b76"},
        {"pak1.pk3", 26, "0c63e4d72d5a2b211b60cf1c3572e41764fc1897211db967d91d2e6d934ad4f5"}};
    const char *dsha = "475815b56dc74d836c50d50e2752a29de9f90be76eaa8ecbfc474dbd34553445";
    off_t dsize = 24;

    FileDialog fd;
    CHECK(fd_open(&fd, FD_OPEN, "Choose", roots, n, roots[1].path, NULL) == 0);
    CHECK(!strcmp(fd.cwd, dl));

    /* An empty or unrelated folder is refused, with a message, and nothing is installed. */
    mk(p, sizeof p, dl, "Random Stuff"); snprintf(p, sizeof p, "%s/Random Stuff/pak0.pk3", dl); put(p, "not quake data at all");
    CHECK(fd_goto(&fd, "Random Stuff") == 0);
    CHECK(quake_pick_commit(&fd, priv, fake, 2, dsize, dsha) == -1 && fd.msg[0]);
    snprintf(p, sizeof p, "%s/Library/Application Support/OS999/Quake3-Full/baseq3/pak0.pk3", priv);
    CHECK(access(p, F_OK) != 0);

    /* The retail layout the RAR extracts to: Downloads/Quake III Arena (Complete)/baseq3/pak*.pk3. */
    mk(retail, sizeof retail, dl, "Quake III Arena (Complete)"); mk(base, sizeof base, retail, "baseq3");
    snprintf(p, sizeof p, "%s/pak0.pk3", base); put(p, "retail fixture pak0");
    CHECK(fd_goto(&fd, dl) == 0);
    CHECK(quake_pick_commit(&fd, priv, fake, 2, dsize, dsha) == -1);     /* incomplete: pak1 missing */
    snprintf(p, sizeof p, "%s/pak1.pk3", base); put(p, "retail fixture pak1 TAMPER!");
    CHECK(quake_pick_commit(&fd, priv, fake, 2, dsize, dsha) == -1);     /* same size, wrong hash */
    snprintf(p, sizeof p, "%s/Library/Application Support/OS999/Quake3-Full/baseq3/pak0.pk3", priv);
    CHECK(access(p, F_OK) != 0);                                          /* all-or-nothing: no partial install */
    snprintf(p, sizeof p, "%s/pak1.pk3", base); put(p, "retail fixture pak1 longer");
    CHECK(fd_goto(&fd, dl) == 0);
    CHECK(quake_pick_commit(&fd, priv, fake, 2, dsize, dsha) == 1 && !fd.msg[0]);   /* full game via the picker */
    char inst[PATH_MAX]; snprintf(inst, sizeof inst, "%s/Library/Application Support/OS999/Quake3-Full", priv);
    CHECK(quake_data_retail_set_ok(inst, fake, 2));

    /* A folder holding only the verified demo installs the demo. */
    char dem[PATH_MAX]; mk(dem, sizeof dem, top, "demo-only");
    snprintf(p, sizeof p, "%s/pak0.pk3", dem); put(p, "os999 quake data fixture");
    char priv2[PATH_MAX]; mk(priv2, sizeof priv2, top, "priv2");
    FdRoot r2[1] = {{"Demo", dem}};
    FileDialog f2;
    CHECK(fd_open(&f2, FD_OPEN, "Choose", r2, 1, dem, NULL) == 0);
    CHECK(quake_pick_commit(&f2, priv2, fake, 2, dsize, dsha) == 2);
    snprintf(p, sizeof p, "%s/Library/Application Support/OS999/Quake3/demoq3/pak0.pk3", priv2);
    CHECK(quake_data_check(p, dsize, dsha));

    /* The real tables never accept fixtures (NULL table = built-in). */
    CHECK(quake_pick_commit(&fd, priv, NULL, 0, 0, NULL) == -1);

    /* The dialog cannot be pointed outside its roots. */
    CHECK(fd_goto(&fd, "/etc") == -1);
#else
    printf("quake picker checks need macOS hashing; skipped\n");
#endif
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("quake picker checks passed\n");
    return 0;
}
