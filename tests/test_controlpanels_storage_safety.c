#include "native_settings.h"
#include "finder_files.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
int volume_join(char *o,size_t n,const char *r,const char *p) { (void)o;(void)n;(void)r;(void)p;abort(); }
static void file(const char *p, size_t size) {
    int fd=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);CHECK(ftruncate(fd,(off_t)size)==0);close(fd);
}
static void json(const char *value) {
    int fd=open("v/.os999-control-panels.json",O_WRONLY|O_CREAT|O_TRUNC,0600);
    CHECK(fd>=0);CHECK(write(fd,value,strlen(value))==(ssize_t)strlen(value));close(fd);
}
static void defaults(void) { NativeSettings s=native_settings_load("v");CHECK(!strcmp(s.heap,"8")&&s.guard&&s.isolated); }
static void cleanup(const char *p) {
    struct stat s;if(lstat(p,&s))return;
    if(!S_ISDIR(s.st_mode)){CHECK(unlink(p)==0);return;}
    DIR*d=opendir(p);CHECK(d!=NULL);struct dirent*e;
    while((e=readdir(d))) {if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        char path[4096];snprintf(path,sizeof path,"%s/%s",p,e->d_name);cleanup(path);}
    closedir(d);CHECK(rmdir(p)==0);
}
int main(void) {
    alarm(15);
    char root[]="build/cp-storage-XXXXXX";CHECK(mkdtemp(root)!=NULL);CHECK(chdir(root)==0);
    CHECK(mkdir("v",0700)==0);defaults();
    CHECK(mkfifo("v/.os999-control-panels.json",0600)==0);defaults();CHECK(unlink("v/.os999-control-panels.json")==0);
    file("outside",10);CHECK(symlink("../outside","v/.os999-control-panels.json")==0);defaults();CHECK(unlink("v/.os999-control-panels.json")==0);
    json("{\"version\":1,\"panels\":{\"memory\":{\"guest_ram_mb\":12,\"heap_guard\":false},\"network\":{\"guest_isolated\":false}}}");
    NativeSettings s=native_settings_load("v");CHECK(!strcmp(s.heap,"12")&&!s.guard&&!s.isolated);
    json("{\"version\":1,\"panels\":{\"network\":{\"guest_isolated\":falsejunk}}}");defaults();
    json("{\"version\":1,\"panels\":{\"memory\":{\"guest_ram_mb\":\"12\",\"heap_guard\":0},\"network\":{\"guest_isolated\":\"false\"}}}");defaults();
    json("{\"version\":1,\"panels\":{\"memory\":{\"guest_ram_mb\":999}}}");defaults();
    CHECK(volume_ram_disk_sync("v",1,1,0)==0);CHECK(mkdir("v/Docs",0700)==0);
    CHECK(mkdir("v/Docs/Folder",0700)==0);file("v/Docs/Folder/large",1048577);
    CHECK(finder_move("v","Docs","Folder","RAM Disk")==-1);CHECK(access("v/Docs/Folder/large",F_OK)==0);
    file("v/Docs/App",1);file("v/Docs/App.rsrc",1048576);
    CHECK(finder_move("v","Docs","App","RAM Disk")==-1);CHECK(access("v/Docs/App.rsrc",F_OK)==0);
    CHECK(symlink("../../outside","v/Docs/link")==0);CHECK(finder_move("v","Docs","link","RAM Disk")==-1);
    CHECK(mkdir("v/Docs/Small",0700)==0);file("v/Docs/Small/data",10);file("v/Docs/Small/data.rsrc",12);
    CHECK(finder_move("v","Docs","Small","RAM Disk")==0);
    uint64_t used;int quota;CHECK(volume_ram_disk_usage("v",&used,&quota)==0&&used==22);
    CHECK(mkdir("v/RAM Disk/Sub",0700)==0);
    CHECK(finder_move("v","RAM Disk","Small","RAM Disk/Sub")==0);
#ifdef __APPLE__
    file("v/Docs/Native",0);char *fork=calloc(1048576,1);CHECK(fork!=NULL);
    CHECK(setxattr("v/Docs/Native","com.apple.ResourceFork",fork,1048576,0,0)==0);free(fork);
    CHECK(finder_move("v","Docs","Native","RAM Disk")==-1);
    char out[OSPI_NAME];CHECK(finder_copy_to("v","Docs","Native","RAM Disk",out)==-1);
    CHECK(removexattr("v/Docs/Native","com.apple.ResourceFork",0)==0);
    CHECK(setxattr("v/Docs/Native","com.apple.ResourceFork","fork",4,0,0)==0);
    CHECK(finder_move("v","Docs","Native","RAM Disk")==0);
    CHECK(volume_ram_disk_usage("v",&used,&quota)==0&&used==26);
#endif
    CHECK(unlink("v/RAM Disk/.os999-ramdisk-mb")==0);CHECK(mkfifo("v/RAM Disk/.os999-ramdisk-mb",0600)==0);
    CHECK(finder_move("v","Docs","App","RAM Disk")==-1);
    char copied[OSPI_NAME];CHECK(finder_copy_to("v","Docs","App","RAM Disk",copied)==-1);
    cleanup("v");cleanup("outside");CHECK(chdir("../..")==0);CHECK(rmdir(root)==0);
    printf("%d storage/settings safety checks passed\n",checks);return 0;
}
