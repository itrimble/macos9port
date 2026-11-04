#define _DARWIN_C_SOURCE 1

#define _POSIX_C_SOURCE 200809L
#include "mac_runtime.h"
#include "mac_drive_queue.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int mac_volume_blocks(int directory_fd, uint32_t *blocks);
int mac_volume_info(MacRuntime *rt, uint32_t pb, int directory_fd);
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void put(MacCpu *cpu, uint32_t p, unsigned n, uint32_t v) { CHECK(mac_cpu_write(cpu,p,n,v) == 0); }
static uint32_t get(MacCpu *cpu, uint32_t p, unsigned n) { return mac_cpu_read(cpu,p,n); }
static void prepare(MacCpu *cpu, int index, int volume, uint32_t name) {
    memset(mac_cpu_ram(cpu) + 256, 0x5a, 122);
    put(cpu, 274, 4, name); put(cpu, 278, 2, (uint16_t)volume);
    put(cpu, 284, 2, (uint16_t)index);
}
static void file(int directory, const char *name, off_t length) {
    int fd = openat(directory,name,O_RDWR|O_CREAT|O_EXCL,0600);
    CHECK(fd >= 0); if (fd >= 0) { CHECK(ftruncate(fd,length) == 0); CHECK(close(fd) == 0); }
}
int main(void) {
    char path[] = "/tmp/ospi-volume-test-XXXXXX";
    CHECK(mkdtemp(path) != NULL);
    int fd = open(path,O_RDONLY|O_DIRECTORY); CHECK(fd >= 0);
    if (fd < 0) return 1;
    file(fd,"one",513); file(fd,"zero",0); file(fd,"two",512);
    file(fd,"bad:name",8192); file(fd,"bad\\name",8192);
    CHECK(mkdirat(fd,"folder",0700) == 0);
    CHECK(symlinkat("one",fd,"link") == 0);
    CHECK(mkfifoat(fd,"fifo",0600) == 0);
    uint32_t blocks = 0x12345678;
    CHECK(mac_volume_blocks(fd, &blocks) == 0 && blocks == 3);
    CHECK(mac_volume_blocks(fd, &blocks) == 0 && blocks == 3);
    CHECK(mac_volume_blocks(-1, &blocks) == -35 && blocks == 3);
    CHECK(mac_volume_blocks(fd, NULL) == -50);
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(65536); CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    rt.heap_next = 0x1000; rt.heap_limit = 0x8000;
    MacCpu *cpu = rt.cpu; uint8_t *ram = mac_cpu_ram(cpu);
    prepare(cpu,-1,-1,0);
    CHECK(mac_volume_info(&rt,256,fd) == 0);
    CHECK(get(cpu,278,2) == 65535 && get(cpu,294,2) == 0x8000);
    CHECK(get(cpu,296,2) == 3 && get(cpu,302,2) == 3);
    CHECK(get(cpu,304,4) == 512 && get(cpu,308,4) == 512);
    CHECK(get(cpu,318,2) == 0 && get(cpu,320,2) == 0x4244);
    CHECK(get(cpu,338,4) == 3 && get(cpu,342,4) == 1);
    CHECK(get(cpu,322,2) == 1);
    uint32_t drive = get(cpu,0x30a,4);
    CHECK(drive && get(cpu,drive+6,2) == 1 && get(cpu,drive+12,2) == 3);
    CHECK(get(cpu,drive-3,1) == 8);
    CHECK(get(cpu,272,2) == 0x5a5a); /* Caller sets ioResult. */
    CHECK(get(cpu,280,4) == 0x5a5a5a5a && get(cpu,284,2) == 65535);
    CHECK(get(cpu,290,4) != 0 && get(cpu,328,4) == 0);
    for (unsigned i = 346; i < 378; i++) CHECK(ram[i] == 0);
    prepare(cpu,1,123,1024); memset(ram+1024,0xcc,20);
    CHECK(mac_volume_info(&rt,256,fd) == 0);
    CHECK(ram[1024] == 10 && memcmp(ram+1025,"OS Pi Data",10) == 0 && ram[1035] == 0xcc);
    prepare(cpu,-1,123,1024); ram[1024] = 11; memcpy(ram+1025,"os pi data:",11);
    CHECK(mac_volume_info(&rt,256,fd) == 0);
    prepare(cpu,0,0,0); CHECK(mac_volume_info(&rt,256,fd) == 0);
    CHECK(get(cpu,338,4) == 3); /* Independent enumeration starts at beginning. */
    uint8_t saved[122];
    const int bad[][3] = {{2,-1,0},{0,123,0},{-1,123,0}};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        prepare(cpu,bad[i][0],bad[i][1],(uint32_t)bad[i][2]); memcpy(saved,ram+256,122);
        CHECK(mac_volume_info(&rt,256,fd) == -35); CHECK(memcmp(saved,ram+256,122) == 0);
    }
    prepare(cpu,-1,-1,1024); ram[1024]=3; memcpy(ram+1025,"bad",3);
    memcpy(saved,ram+256,122); CHECK(mac_volume_info(&rt,256,fd) == -35);
    CHECK(memcmp(saved,ram+256,122) == 0);
    prepare(cpu,1,-1,65530); memcpy(saved,ram+256,122);
    CHECK(mac_volume_info(&rt,256,fd) == -50); CHECK(memcmp(saved,ram+256,122) == 0);
    prepare(cpu,-1,-1,65520); ram[65520]=255; memcpy(saved,ram+256,122);
    CHECK(mac_volume_info(&rt,256,fd) == -50); CHECK(memcmp(saved,ram+256,122) == 0);
    prepare(cpu,1,-1,300); memcpy(saved,ram+256,122);
    CHECK(mac_volume_info(&rt,256,fd) == -50); CHECK(memcmp(saved,ram+256,122) == 0);
    CHECK(mac_volume_info(&rt,UINT32_MAX-1,fd) == -50);
    CHECK(mac_volume_info(&rt,65500,fd) == -50);
    CHECK(mac_volume_info(&rt,257,fd) == -50);
    CHECK(mac_volume_info(&rt,256,-1) == -35);
    file(fd,"too-large",33554432); prepare(cpu,-1,-1,0); memcpy(saved,ram+256,122);
    CHECK(mac_volume_info(&rt,256,fd) == -108); CHECK(memcmp(saved,ram+256,122) == 0);
    blocks = 77;
    CHECK(mac_volume_blocks(fd, &blocks) == -108 && blocks == 77);
    CHECK(get(cpu,drive+12,2) == 3); /* Failed scans preserve mounted capacity. */
    CHECK(!mac_cpu_halted(cpu)); mac_drive_destroy(&rt); mac_cpu_destroy(cpu);
    const char *names[]={"one","zero","two","bad:name","bad\\name","link","fifo","too-large"};
    for (unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) CHECK(unlinkat(fd,names[i],0)==0);
    CHECK(unlinkat(fd,"folder",AT_REMOVEDIR)==0); CHECK(close(fd)==0); CHECK(rmdir(path)==0);
    if (failures) return 1;
    puts("virtual volume tests passed"); return 0;
}
