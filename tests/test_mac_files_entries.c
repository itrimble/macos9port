/* mac_files_entries: the merged volume listing a file dialog is built from. */
#include "mac_files_int.h"
#include "mac_drive_queue.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void make(const char *dir, const char *name, const char *type, const char *creator, unsigned flags, int info) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, "x", 1) == 1);
    if (info) {
        uint8_t raw[16] = {0};
        memcpy(raw, type, 4); memcpy(raw + 4, creator, 4); raw[8] = (uint8_t)(flags >> 8); raw[9] = (uint8_t)flags;
        CHECK(mac_files_finder_set(fd, raw) == 0);
    }
    CHECK(close(fd) == 0);
}
int main(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(4096); CHECK(rt.cpu);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00;
    char data[] = "/tmp/ospi-ent-data-XXXXXX", saves[] = "/tmp/ospi-ent-saves-XXXXXX";
    CHECK(mkdtemp(data) && mkdtemp(saves));
    MacFileEntry e[16]; unsigned n = 99;
    CHECK(mac_files_entries(&rt, e, 16, &n) == -35 && n == 0); /* no volume mounted */
    make(data, "zebra", "gliH", "gliR", 0, 1);
    make(data, "Apple", "TEXT", "ttxt", 0x4000, 1);   /* invisible flag set */
    make(data, "plain", "", "", 0, 0);                 /* no Finder info at all */
    make(data, "Shared", "AAAA", "BBBB", 0, 1);
    char path[1024];
    snprintf(path, sizeof path, "%s/folder", data); CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof path, "%s/link", data); CHECK(symlink("zebra", path) == 0);
    CHECK(mac_files_root(&rt, data) == 0);
    CHECK(mac_files_writable(&rt, saves, 8, 1024) == 0);
    make(saves, "Save 1", "SAVE", "gam1", 0, 1);
    make(saves, "Shared", "ZZZZ", "YYYY", 0, 1);       /* same name as a data file: listed once, and the save is what the guest opens */
    CHECK(mac_files_entries(&rt, e, 16, &n) == 0);
    const char *order[] = { "Apple", "Save 1", "Shared", "plain", "zebra" }; /* byte order; no directory or link */
    CHECK(n == 5);
    for (unsigned i = 0; i < n; i++) CHECK(!strcmp(e[i].name, order[i]));
    CHECK(e[0].type == 0x54455854 && e[0].creator == 0x74747874 && e[0].flags == 0x4000);
    CHECK(e[1].type == 0x53415645 && e[1].creator == 0x67616d31 && e[1].flags == 0);
    CHECK(e[2].type == 0x5a5a5a5a && e[2].creator == 0x59595959);  /* the save, as PBGetFInfo reports it */
    CHECK(e[3].type == 0 && e[3].creator == 0 && e[3].flags == 0); /* never invented */
    CHECK(e[4].type == 0x676c6948 && e[4].creator == 0x676c6952);
    /* A capacity too small is an error, not a silent truncation. */
    CHECK(mac_files_entries(&rt, e, 4, &n) == -50 && n == 0);
    CHECK(mac_files_entries(&rt, e, 5, &n) == 0 && n == 5);
    mac_files_destroy(&rt); mac_drive_destroy(&rt);
    puts("File entry listing checks passed");
    return 0;
}
