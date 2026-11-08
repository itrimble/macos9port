#include "ospi.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
/* Unrelated special-folder/cache helpers share the source unit. RAM disk
 * operations must never use their path-based join routine. */
int volume_join(char *out, size_t n, const char *root, const char *rel) {
    (void)out; (void)n; (void)root; (void)rel; abort();
}
static void put(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0); CHECK(write(fd,"keep",4) == 4); CHECK(close(fd) == 0);
}
static void kept(const char *path) {
    char buf[8] = {0}; int fd = open(path,O_RDONLY);
    CHECK(fd >= 0); CHECK(read(fd,buf,sizeof buf) == 4); CHECK(!strcmp(buf,"keep")); close(fd);
}
int main(void) {
    char root[] = "build/ramdisk-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL); CHECK(chdir(root) == 0);
    CHECK(mkdir("volume",0700) == 0); put("outside");
    CHECK(mkdir("volume/RAM Disk",0700) == 0); put("volume/RAM Disk/user");
    CHECK(volume_ram_disk_sync("volume",1,4,0) == -1);
    CHECK(volume_ram_disk_sync("volume",1,4,1) == -1);
    CHECK(volume_ram_disk_sync("volume",0,4,1) == -1); kept("volume/RAM Disk/user");
    CHECK(unlink("volume/RAM Disk/user") == 0); CHECK(rmdir("volume/RAM Disk") == 0);
    CHECK(symlink("..","volume/RAM Disk") == 0);
    CHECK(volume_ram_disk_sync("volume",1,4,1) == -1); kept("outside"); CHECK(unlink("volume/RAM Disk") == 0);
    CHECK(volume_ram_disk_sync("volume",1,4,0) == 0);
    uint64_t used; int quota;
    CHECK(volume_ram_disk_usage("volume",&used,&quota) == 0 && !used && quota == 4);
    CHECK(unlink("volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(symlink("../../outside","volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(volume_ram_disk_sync("volume",1,8,1) == -1);
    CHECK(volume_ram_disk_usage("volume",&used,&quota) == -1); kept("outside");
    CHECK(unlink("volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(link("outside","volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(volume_ram_disk_sync("volume",1,8,1) == -1); kept("outside");
    CHECK(unlink("volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(mkfifo("volume/RAM Disk/.os999-ramdisk-mb",0600) == 0);
    CHECK(volume_ram_disk_sync("volume",1,8,1) == -1);
    CHECK(volume_ram_disk_usage("volume",&used,&quota) == -1);
    CHECK(unlink("volume/RAM Disk/.os999-ramdisk-mb") == 0);
    CHECK(volume_ram_disk_sync("volume",1,8,0) == 0);
    CHECK(mkdir("volume/RAM Disk/nested",0700) == 0); put("volume/RAM Disk/nested/data");
    CHECK(symlink("../../../outside","volume/RAM Disk/nested/link") == 0);
    CHECK(volume_ram_disk_usage("volume",&used,&quota) == 0 && used == 4 && quota == 8);
    CHECK(volume_ram_disk_sync("volume",0,8,0) == -1); kept("volume/RAM Disk/nested/data");
    CHECK(volume_ram_disk_sync("volume",1,8,1) == 0); kept("outside");
    CHECK(volume_ram_disk_usage("volume",&used,&quota) == 0 && used == 0);
    CHECK(volume_ram_disk_sync("volume",0,8,0) == 0);
    CHECK(access("volume/RAM Disk",F_OK) != 0); CHECK(rmdir("volume") == 0);
    CHECK(unlink("outside") == 0); CHECK(chdir("../..") == 0); CHECK(rmdir(root) == 0);
    printf("%d RAM disk safety checks passed\n",checks); return 0;
}
