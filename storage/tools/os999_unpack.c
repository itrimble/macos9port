/* os999-unpack: expand classic Mac archives and disk images into an OS999
 * guest volume, preserving resource forks and Finder metadata.
 *   os999-unpack -l FILE                 list what FILE holds
 *   os999-unpack [-x] [-q BYTES] FILE VOLUME [FOLDER]
 * -x allows the external unar fallback for StuffIt methods 13/15 and .sitx;
 * -q sets a byte quota for the volume. */
#include "os999_unpack.h"
#include "os999_volume.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: os999-unpack -l [-x] FILE\n       os999-unpack [-x] [-q BYTES] FILE VOLUME [FOLDER]\n");
}

int main(int argc, char **argv) {
    int list = 0, ext = 0, c;
    unsigned long long quota = 0;
    while ((c = getopt(argc, argv, "lxq:h")) != -1) {
        if (c == 'l') list = 1;
        else if (c == 'x') ext = 1;
        else if (c == 'q') quota = strtoull(optarg, NULL, 10);
        else { usage(); return 2; }
    }
    if (optind >= argc || (!list && optind + 2 > argc)) { usage(); return 2; }
    const char *file = argv[optind];
    StArchive a = {0};
    StUnpackInfo info;
    StErr err = {0};
    int rc = st_unpack_file(file, ext, &a, &info, NULL, &err);
    if (rc) {
        fprintf(stderr, "os999-unpack: %s: %s (%s)\n", file, err.msg, st_status_text(rc));
        return 1;
    }
    printf("%s: %s%s%s, %zu entries\n", file, info.chain[0] ? info.chain : "plain file",
           info.volume[0] ? ", volume " : "", info.volume, a.n);
    if (list) {
        for (size_t i = 0; i < a.n; i++) {
            const StMacFile *f = &a.v[i].file;
            if (a.v[i].is_dir) printf("  %s/\n", a.v[i].path);
            else printf("  %s  [%.4s/%.4s] data %zu, resource %zu\n", a.v[i].path, (const char *)f->finfo,
                        (const char *)f->finfo + 4, f->data.n, f->rsrc.n);
        }
        st_archive_free(&a);
        return 0;
    }
    StVolumeOptions opt = {quota, 0, 0};
    StVolume *v;
    rc = st_vol_open(argv[optind + 1], &opt, &v, &err);
    if (rc == ST_OK) {
        rc = st_vol_install(v, optind + 2 < argc ? argv[optind + 2] : "", &a, &err);
        st_vol_close(v);
    }
    st_archive_free(&a);
    if (rc) {
        fprintf(stderr, "os999-unpack: %s (%s)\n", err.msg, st_status_text(rc));
        return 1;
    }
    printf("installed into %s\n", argv[optind + 1]);
    return 0;
}
