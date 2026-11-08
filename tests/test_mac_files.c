#include "mac_runtime.h"
#include "mac_drive_queue.h"
#include "mac_system_volume.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
enum { PB = 0x100, NAME = 0x200, DATA = 0x400 };
static void put(MacRuntime *rt, unsigned offset, unsigned width, uint32_t v) {
    CHECK(!mac_cpu_write(rt->cpu, PB + offset, width, v));
}
static int call(MacRuntime *rt, uint16_t op) {
    mac_cpu_set(rt->cpu, MAC_A0, PB);
    CHECK(mac_file_service(rt, op) == 1);
    int result = (int16_t)mac_cpu_read(rt->cpu, PB + 16, 2);
    CHECK((int32_t)mac_cpu_get(rt->cpu, MAC_D0) == result);
    CHECK((mac_cpu_get(rt->cpu, MAC_SR) & 15) == (result < 0 ? 8u : 4u));
    return result;
}
static int open_name(MacRuntime *rt, const char *name, int permission) {
    size_t n = strlen(name);
    mac_cpu_ram(rt->cpu)[NAME] = (uint8_t)n;
    memcpy(mac_cpu_ram(rt->cpu)+NAME+1,name,n);
    put(rt,18,4,NAME); put(rt,22,2,0); put(rt,27,1,permission);
    return call(rt,0xa000);
}
static int read_at(MacRuntime *rt, int mode, int offset, int count) {
    put(rt,32,4,DATA); put(rt,36,4,(uint32_t)count);
    put(rt,44,2,(uint16_t)mode); put(rt,46,4,(uint32_t)offset);
    return call(rt,0xa002);
}
static int wd(MacRuntime *rt, int volume, int index, uint32_t process, uint32_t name, int async) {
    put(rt,22,2,(uint16_t)volume); put(rt,26,2,(uint16_t)index);
    put(rt,28,4,process); put(rt,18,4,name); mac_cpu_set(rt->cpu,MAC_D0,7);
    return call(rt,async ? 0xa660 : 0xa260);
}

static int cat(MacRuntime *rt, int volume, int index, uint32_t id, uint32_t name) {
    put(rt,22,2,(uint16_t)volume);put(rt,28,2,(uint16_t)index);
    put(rt,48,4,id);put(rt,18,4,name);mac_cpu_set(rt->cpu,MAC_D0,9);
    return call(rt,0xa260);
}

static void flush_checks(MacRuntime *r) {
    const int refs[]={0,-1,1,-2,2,-3,99};
    const int errors[]={0,0,0,-35,-56,-35,-56};
    for(unsigned i=0;i<7;i++) {
        memset(mac_cpu_ram(r->cpu)+PB,0x5a,50);
        put(r,18,4,0);put(r,22,2,(uint16_t)refs[i]);
        uint8_t before[50];memcpy(before,mac_cpu_ram(r->cpu)+PB,50);
        CHECK(call(r,0xa013)==errors[i]);
        for(unsigned j=0;j<50;j++)if(j!=16 && j!=17)CHECK(before[j]==mac_cpu_ram(r->cpu)[PB+j]);
    }
    const char *names[]={"os pi data:","Unknown:","OS Pi Data:Game Data","not a volume",""};
    const int named_errors[]={0,-35,0,0,0};
    for(unsigned i=0;i<5;i++) {
        unsigned n=(unsigned)strlen(names[i]);mac_cpu_ram(r->cpu)[NAME]=(uint8_t)n;
        memcpy(mac_cpu_ram(r->cpu)+NAME+1,names[i],n);
        put(r,18,4,NAME);put(r,22,2,0xffff);CHECK(call(r,0xa013)==named_errors[i]);
    }
    r->system_volume=mac_system_volume_create(r);CHECK(r->system_volume);
    put(r,18,4,0);put(r,22,2,0xfffe);CHECK(call(r,0xa013)==0);
    memcpy(mac_cpu_ram(r->cpu)+NAME,"\x0dos pi system:",14);
    put(r,18,4,NAME);put(r,22,2,99);CHECK(call(r,0xa013)==0);
    mac_system_volume_destroy(r->system_volume);r->system_volume=NULL;
    CHECK(call(r,0xa013)==-35);
    put(r,18,4,4095);mac_cpu_ram(r->cpu)[4095]=2;CHECK(call(r,0xa013)==-50);
    put(r,18,4,0);put(r,22,2,0);CHECK(call(r,0xa413)==-50);
    mac_cpu_set(r->cpu,MAC_A0,4072);mac_cpu_write(r->cpu,4090,4,0);mac_cpu_write(r->cpu,4094,2,0xffff);
    CHECK(mac_file_service(r,0xa013)==1 && (int32_t)mac_cpu_get(r->cpu,MAC_D0)==0);
}

int main(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(4096);
    rt.heap_next = 0x800; rt.heap_limit = 0xe00;
    CHECK(rt.cpu != NULL);
    if (!rt.cpu) return 1;
    char root[]="/tmp/ospi-files-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512], link[512], fifo[512];
    snprintf(path,sizeof path,"%s/Game Data",root);
    int fd = open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    CHECK(fd >= 0);
    if(fd>=0) { CHECK(write(fd,"abcdef",6)==6); close(fd); }
    snprintf(link,sizeof link,"%s/alias",root);
    CHECK(symlink("/etc/passwd",link)==0);
    snprintf(fifo,sizeof fifo,"%s/fifo",root); CHECK(mkfifo(fifo,0600)==0);
    CHECK(open_name(&rt,"Game Data",1)==-35);
    put(&rt,18,4,0);put(&rt,22,2,0);CHECK(call(&rt,0xa013)==-50);
    put(&rt,18,4,NAME);
    CHECK(call(&rt,0xa014)==-35);
    CHECK(wd(&rt,0,0,0,NAME,0)==-35);
    CHECK(mac_files_root(&rt,root)==0);
    flush_checks(&rt);
    CHECK(wd(&rt,0,0,0,NAME,0)==0);
    CHECK(mac_cpu_read(rt.cpu,PB+22,2)==0xffff);
    CHECK(mac_cpu_read(rt.cpu,PB+32,2)==0xffff);
    CHECK(mac_cpu_read(rt.cpu,PB+48,4)==2);
    CHECK(mac_cpu_ram(rt.cpu)[NAME]==10);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+NAME+1,"OS Pi Data",10));
    CHECK(wd(&rt,-1,0,123,0,0)==0); CHECK(mac_cpu_read(rt.cpu,PB+28,4)==0);
    CHECK(wd(&rt,-32767,0,0,0,0)==-51);
    CHECK(wd(&rt,0,1,0,0,0)==-51); CHECK(wd(&rt,-1,1,0,0,0)==-51);
    CHECK(wd(&rt,0,2,0,0,0)==-51); CHECK(wd(&rt,0,-1,0,0,0)==-51);
    CHECK(wd(&rt,0,1,123,0,0)==-51); CHECK(wd(&rt,-2,0,0,0,0)==-35);
    CHECK(wd(&rt,-32766,0,0,0,0)==-51); CHECK(wd(&rt,-32767,1,0,0,0)==-51);
    put(&rt,32,2,0x1234);put(&rt,48,4,0x12345678);
    CHECK(wd(&rt,0,0,0,4090,0)==-50); CHECK(mac_cpu_read(rt.cpu,PB+32,2)==0x1234);
    CHECK(mac_cpu_read(rt.cpu,PB+48,4)==0x12345678);
    CHECK(wd(&rt,0,0,0,0,1)==-50);
    mac_cpu_set(rt.cpu,MAC_D0,8); CHECK(mac_file_service(&rt,0xa260)==0);
    struct stat root_st;CHECK(stat(root,&root_st)==0);
    CHECK(cat(&rt,-1,-1,2,NAME)==0);
    CHECK(mac_cpu_read(rt.cpu,PB+30,1)==0x10);
    CHECK(mac_cpu_read(rt.cpu,PB+52,2)==1); /* Regular file; link/FIFO excluded. */
    CHECK(mac_cpu_read(rt.cpu,PB+100,4)==1);
    CHECK(mac_cpu_read(rt.cpu,PB+76,4)==(uint32_t)((int64_t)root_st.st_mtime+2082844800));
#if defined(__APPLE__)
    CHECK(mac_cpu_read(rt.cpu,PB+72,4)==(uint32_t)((int64_t)root_st.st_birthtimespec.tv_sec+2082844800));
#endif
    CHECK(mac_cpu_read(rt.cpu,PB+80,4)==0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+NAME+1,"OS Pi Data",10));
    char child[512];snprintf(child,sizeof child,"%s/Child",root);CHECK(mkdir(child,0700)==0);
    CHECK(cat(&rt,0,-1,2,0)==0);CHECK(mac_cpu_read(rt.cpu,PB+52,2)==2);CHECK(rmdir(child)==0);
    put(&rt,52,2,1234);put(&rt,100,4,12345678);
    CHECK(cat(&rt,0,-1,2,4090)==-50);
    CHECK(mac_cpu_read(rt.cpu,PB+52,2)==1234 && mac_cpu_read(rt.cpu,PB+100,4)==12345678);
    CHECK(cat(&rt,0,0,2,0)==-50);CHECK(cat(&rt,0,1,2,0)==-50);
    CHECK(cat(&rt,0,-1,99,0)==-120);CHECK(cat(&rt,-2,-1,2,0)==-35);
    put(&rt,22,2,0);put(&rt,28,2,0xffff);put(&rt,48,4,2);mac_cpu_set(rt.cpu,MAC_D0,9);
    CHECK(call(&rt,0xa660)==-50);
    char moved[512];snprintf(moved,sizeof moved,"%s-moved",root);CHECK(rename(root,moved)==0);
    CHECK(cat(&rt,0,-1,2,0)==0);CHECK(mac_cpu_read(rt.cpu,PB+52,2)==1);CHECK(rename(moved,root)==0);

    put(&rt,18,4,0);put(&rt,22,2,0xffff);put(&rt,28,2,0xffff);
    CHECK(call(&rt,0xa207)==0);
    CHECK(mac_cpu_read(rt.cpu,PB+38,2)==0x8000);
    CHECK(mac_cpu_read(rt.cpu,PB+82,4)==1);
    CHECK(mac_cpu_read(rt.cpu,PB+62,2)==0); /* Read-only virtual capacity. */
    CHECK(call(&rt,0xa607)==-50); /* Async request has no completion adapter. */

    put(&rt,18,4,NAME); put(&rt,22,2,123);
    CHECK(call(&rt,0xa014)==0);
    CHECK(mac_cpu_read(rt.cpu,PB+22,2)==0xffff);
    CHECK(mac_cpu_ram(rt.cpu)[NAME]==10);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+NAME+1,"OS Pi Data",10));
    put(&rt,18,4,0); CHECK(call(&rt,0xa014)==0);
    put(&rt,18,4,4090); put(&rt,22,2,123);
    CHECK(call(&rt,0xa014)==-50); CHECK(mac_cpu_read(rt.cpu,PB+22,2)==123);
    put(&rt,18,4,NAME); CHECK(call(&rt,0xa414)==-50);

    CHECK(mac_files_root(&rt,root)==-1);
    CHECK(open_name(&rt,"Game Data",3)==-54);
    CHECK(open_name(&rt,"missing",1)==-43);
    CHECK(open_name(&rt,"../escape",1)==-50);
    CHECK(open_name(&rt,"..",1)==-50);
    CHECK(open_name(&rt,":Game Data",1)==-50);
    CHECK(open_name(&rt,"\200",1)==-50);
    CHECK(open_name(&rt,"alias",1)==-54);
    CHECK(open_name(&rt,"fifo",1)==-50);
    CHECK(open_name(&rt,"Game Data",1)==0);
    CHECK(call(&rt,0xa001)==0);
    put(&rt,22,2,0xffff); CHECK(call(&rt,0xa000)==0);
    CHECK(call(&rt,0xa001)==0);
    put(&rt,22,2,0xfffe); CHECK(call(&rt,0xa000)==-35);
    put(&rt,22,2,0xffff); CHECK(call(&rt,0xa000)==0);
    int ref = (int)mac_cpu_read(rt.cpu,PB+24,2);
    CHECK(ref>=128);
    put(&rt,28,4,0x12345678);
    CHECK(call(&rt,0xa011)==0 && mac_cpu_read(rt.cpu,PB+28,4)==6);
    put(&rt,28,4,0x12345678);
    CHECK(call(&rt,0xa411)==-50 && mac_cpu_read(rt.cpu,PB+28,4)==0x12345678);
    put(&rt,24,2,127);
    CHECK(call(&rt,0xa011)==-51 && mac_cpu_read(rt.cpu,PB+28,4)==0x12345678);
    put(&rt,24,2,(uint16_t)ref);
    int writer = open(path,O_WRONLY); CHECK(writer >= 0);
    if(writer >= 0) {
        CHECK(ftruncate(writer,(off_t)INT32_MAX+1)==0);
        CHECK(call(&rt,0xa011)==-36 && mac_cpu_read(rt.cpu,PB+28,4)==0x12345678);
        CHECK(ftruncate(writer,9)==0);
        CHECK(call(&rt,0xa011)==0 && mac_cpu_read(rt.cpu,PB+28,4)==9);
        CHECK(ftruncate(writer,6)==0); CHECK(close(writer)==0);
    }
    char renamed[512]; snprintf(renamed,sizeof renamed,"%s/Original",root);
    CHECK(rename(path,renamed)==0);
    int replacement=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); CHECK(replacement>=0);
    if(replacement>=0) { CHECK(write(replacement,"other",5)==5); CHECK(close(replacement)==0); }
    CHECK(call(&rt,0xa011)==0 && mac_cpu_read(rt.cpu,PB+28,4)==6);
    CHECK(unlink(path)==0); CHECK(rename(renamed,path)==0);
    /* Only the 32 bytes actually used by GetEOF are required. */
    mac_cpu_set(rt.cpu,MAC_A0,4064); mac_cpu_write(rt.cpu,4088,2,(uint16_t)ref);
    CHECK(mac_file_service(&rt,0xa011)==1 && mac_cpu_read(rt.cpu,4092,4)==6);
    CHECK(read_at(&rt,0,999,2)==0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+DATA,"ab",2));
    CHECK(mac_cpu_read(rt.cpu,PB+46,4)==2);
    CHECK(read_at(&rt,3,-1,2)==0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+DATA,"bc",2));
    put(&rt,46,4,0x12345678);
    CHECK(call(&rt,0xa011)==0 && mac_cpu_read(rt.cpu,PB+28,4)==6);
    CHECK(mac_cpu_read(rt.cpu,PB+46,4)==0x12345678);
    CHECK(read_at(&rt,0,0,1)==0 && mac_cpu_ram(rt.cpu)[DATA]=='d');
    CHECK(read_at(&rt,2,-2,5)==-39);
    CHECK(mac_cpu_read(rt.cpu,PB+40,4)==2);
    CHECK(mac_cpu_read(rt.cpu,PB+46,4)==6);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+DATA,"ef",2));
    CHECK(read_at(&rt,0,0,0)==0);
    CHECK(read_at(&rt,1,0,6)==0);
    CHECK(read_at(&rt,1,-1,1)==-40);
    CHECK(read_at(&rt,0,0,-1)==-50);
    CHECK(read_at(&rt,0x80,0,1)==-50);
    CHECK(read_at(&rt,1,99,1)==-39);
    CHECK(mac_cpu_read(rt.cpu,PB+46,4)==6);
    put(&rt,32,4,4095); put(&rt,36,4,2); put(&rt,44,2,0);
    CHECK(call(&rt,0xa002)==-50);
    CHECK(call(&rt,0xa402)==-50);
    CHECK(call(&rt,0xa001)==0);
    CHECK(call(&rt,0xa001)==-51);
    put(&rt,28,4,0x12345678);
    CHECK(call(&rt,0xa011)==-51 && mac_cpu_read(rt.cpu,PB+28,4)==0x12345678);
    CHECK(read_at(&rt,0,0,1)==-51);
    for(int i=0;i<32;i++) CHECK(open_name(&rt,"Game Data",0)==0);
    CHECK(open_name(&rt,"Game Data",0)==-42);
    CHECK(call(&rt,0xa003)==-61); /* wrPermErr: data volume refs are read-only */
    mac_files_destroy(&rt); CHECK(rt.file_state==NULL);
    mac_files_destroy(&rt);
    CHECK(mac_files_root(&rt,root)==0);
    CHECK(open_name(&rt,"Game Data",1)==0);
    mac_files_destroy(&rt);
    fd=open(path,O_RDONLY); char data[6]; CHECK(fd>=0);
    if(fd>=0) { CHECK(read(fd,data,6)==6); CHECK(!memcmp(data,"abcdef",6)); close(fd); }
    CHECK(unlink(path)==0); CHECK(unlink(link)==0); CHECK(unlink(fifo)==0); CHECK(rmdir(root)==0);
    mac_cpu_set(rt.cpu,MAC_A0,4090);
    CHECK(mac_file_service(&rt,0xa000)==-1); CHECK(mac_cpu_halted(rt.cpu));
    mac_drive_destroy(&rt);
    mac_cpu_destroy(rt.cpu);
    rt.cpu=mac_cpu_create(4096);mac_cpu_set(rt.cpu,MAC_A0,4000);mac_cpu_set(rt.cpu,MAC_D0,9);
    memset(mac_cpu_ram(rt.cpu)+4000,0x5a,96);
    CHECK(mac_file_service(&rt,0xa260)==-1);CHECK(mac_cpu_halted(rt.cpu));
    for(int i=0;i<96;i++)CHECK(mac_cpu_ram(rt.cpu)[4000+i]==0x5a);
    mac_cpu_destroy(rt.cpu);
    rt.cpu=mac_cpu_create(4096);mac_cpu_set(rt.cpu,MAC_A0,3980);
    memset(mac_cpu_ram(rt.cpu)+3980,0x5a,116);
    CHECK(mac_file_service(&rt,0xa207)==-1);CHECK(mac_cpu_halted(rt.cpu));
    for(int i=0;i<116;i++)CHECK(mac_cpu_ram(rt.cpu)[3980+i]==0x5a);
    mac_cpu_destroy(rt.cpu);
    const uint32_t bad_pb[] = {4066, 257, UINT32_MAX-1};
    for(unsigned i=0;i<sizeof(bad_pb)/sizeof(bad_pb[0]);i++) {
        rt.cpu=mac_cpu_create(4096); CHECK(rt.cpu != NULL);
        memset(mac_cpu_ram(rt.cpu),0x5a,4096);
        mac_cpu_set(rt.cpu,MAC_A0,bad_pb[i]);
        CHECK(mac_file_service(&rt,0xa011)==-1 && mac_cpu_halted(rt.cpu));
        for(unsigned j=0;j<4096;j++) CHECK(mac_cpu_ram(rt.cpu)[j]==0x5a);
        mac_cpu_destroy(rt.cpu);
    }
    printf("File Manager checks: %s\n",failures?"FAILED":"ok");
    return failures != 0;
}
