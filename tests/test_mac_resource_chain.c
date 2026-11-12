#define _DARWIN_C_SOURCE 1
#include "mac_resource_chain.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {DATA=256,MAP=264,TYPES=292,REF=302,END=314,SP=0xf000,NAME=0xe000,TEXT=0x54455854};
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
static void w16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void w32(uint8_t *p,uint32_t v){w16(p,v>>16);w16(p+2,v);}
static void fixture(uint8_t p[END],const char payload[4],int id,int attributes) {
    memset(p,0,END);w32(p,DATA);w32(p+4,MAP);w32(p+8,8);w32(p+12,END-MAP);
    w32(p+DATA,4);memcpy(p+DATA+4,payload,4);
    w16(p+MAP+24,28);w16(p+MAP+26,END-MAP);w16(p+TYPES,0);
    w32(p+TYPES+2,TEXT);w16(p+TYPES+6,0);w16(p+TYPES+8,10);
    w16(p+REF,(uint16_t)id);w16(p+REF+2,0xffff);p[REF+4]=(uint8_t)attributes;
}
static void save(const char *dir,const char *name,const uint8_t *bytes,size_t n) {
    char path[512];snprintf(path,sizeof path,"%s/%s",dir,name);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    if(fd>=0){CHECK(write(fd,bytes,n)==(ssize_t)n);CHECK(close(fd)==0);}
}
static uint32_t read32(MacRuntime *r,uint32_t p){return mac_cpu_read(r->cpu,p,4);}
static void start(MacRuntime *r){mac_cpu_set(r->cpu,MAC_A7,SP);}
static int err(MacRuntime *r){return (int16_t)mac_cpu_read(r->cpu,0xa60,2);}
static int current(MacRuntime *r){start(r);CHECK(mac_resource_service(r,0xa994)==1);return (int16_t)mac_cpu_read(r->cpu,SP,2);}
static void use(MacRuntime *r,int ref){start(r);mac_cpu_write(r->cpu,SP,2,(uint16_t)ref);CHECK(mac_resource_service(r,0xa998)==1);}
static void close_res(MacRuntime *r,int ref){start(r);mac_cpu_write(r->cpu,SP,2,(uint16_t)ref);CHECK(mac_resource_service(r,0xa99a)==1);}
static int open_res(MacRuntime *r,const char *name) {
    size_t n=strlen(name);mac_cpu_ram(r->cpu)[NAME]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+NAME+1,name,n);
    start(r);mac_cpu_write(r->cpu,SP,4,NAME);mac_cpu_write(r->cpu,SP+4,2,0xabcd);
    CHECK(mac_resource_service(r,0xa997)==1);CHECK(mac_cpu_get(r->cpu,MAC_A7)==SP+4);
    return (int16_t)mac_cpu_read(r->cpu,SP+4,2);
}
/* OpenRFPerm(fileName, vRefNum, permission): the same file as OpenResFile; result above 8 bytes of arguments. */
static int open_perm(MacRuntime *r,const char *name,int vref,int perm) {
    size_t n=strlen(name);mac_cpu_ram(r->cpu)[NAME]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+NAME+1,name,n);
    start(r);mac_cpu_write(r->cpu,SP,2,(uint16_t)perm<<8);mac_cpu_write(r->cpu,SP+2,2,(uint16_t)vref);mac_cpu_write(r->cpu,SP+4,4,NAME);mac_cpu_write(r->cpu,SP+8,2,0xabcd);
    CHECK(mac_resource_service(r,0xa9c4)==1);CHECK(mac_cpu_get(r->cpu,MAC_A7)==SP+8);
    return (int16_t)mac_cpu_read(r->cpu,SP+8,2);
}
static uint32_t get(MacRuntime *r,uint16_t op,int id) {
    start(r);mac_cpu_write(r->cpu,SP,2,(uint16_t)id);mac_cpu_write(r->cpu,SP+2,4,TEXT);
    CHECK(mac_resource_service(r,op)==1);CHECK(mac_cpu_get(r->cpu,MAC_A7)==SP+6);
    return read32(r,SP+6);
}
static int home(MacRuntime *r,uint32_t h){start(r);mac_cpu_write(r->cpu,SP,4,h);CHECK(mac_resource_service(r,0xa9a4)==1);return (int16_t)mac_cpu_read(r->cpu,SP+4,2);}
static void bytes_are(MacRuntime *r,uint32_t h,const char *s){CHECK(h && !memcmp(mac_cpu_ram(r->cpu)+read32(r,h),s,4));}
int main(void) {
    uint8_t app[END],a[END],b[END],unique[END],bad[16]={0};
    fixture(app,"APP!",128,0);fixture(a,"AAA!",128,4);fixture(b,"BBB!",128,0);fixture(unique,"NEW!",129,0);
    char dir[]="/tmp/ospi-resource-chain-XXXXXX";CHECK(mkdtemp(dir)!=NULL);
    save(dir,"First.rsrc",a,END);save(dir,"Second.rsrc",b,END);save(dir,"Unique.rsrc",unique,END);save(dir,"Bad.rsrc",bad,sizeof bad);
    char link[512];snprintf(link,sizeof link,"%s/Link.rsrc",dir);CHECK(symlink("First.rsrc",link)==0);
    MacRuntime r={0};CHECK(mac_resource_open(&r.fork,app,END,NULL,0)==0);
    r.cpu=mac_cpu_create(65536);r.heap_next=0x2000;r.heap_limit=0xc000;
    CHECK(mac_files_root(&r,dir)==0);
    CHECK(current(&r)==1);uint32_t ah=get(&r,0xa9a0,128);bytes_are(&r,ah,"APP!");CHECK(home(&r,ah)==1);
    int first=open_res(&r,"fIrSt");CHECK(first>=256 && err(&r)==0 && current(&r)==first);
    CHECK(r.handle_count==2); /* resPreload genuinely loaded the new resource. */
    uint32_t fh=get(&r,0xa9a0,128);CHECK(fh!=ah);bytes_are(&r,fh,"AAA!");CHECK(home(&r,fh)==first);
    int second=open_res(&r,"Second");CHECK(second>first && current(&r)==second);
    uint32_t sh=get(&r,0xa81f,128);CHECK(sh!=fh && sh!=ah);bytes_are(&r,sh,"BBB!");
    CHECK(open_res(&r,"FIRST")==first && current(&r)==second);
    /* A full pathname that starts with the volume's own name is the same file; any other colon is still refused. */
    CHECK(open_res(&r,"OS999 HD:First")==first && err(&r)==0);
    CHECK(open_res(&r,"os999 hd:FIRST")==first && err(&r)==0);
    CHECK(open_perm(&r,"fIrSt",0,1)==first && err(&r)==0 && open_perm(&r,"First",-1,3)==first);   /* the same fork, any volume word and permission */
    CHECK(open_perm(&r,"Missing",0,1)==-1 && err(&r)==-43 && current(&r)==second);               /* a missing one: -1 and fnfErr, the current file unchanged */
    CHECK(open_perm(&r,"OS999 HD:sub:First",0,1)==-1 && err(&r)==-37);
    CHECK(open_res(&r,"OS999 HD:Missing")==-1 && err(&r)==-43);
    CHECK(open_res(&r,"OS999 HD:")==-1 && err(&r)==-37 && open_res(&r,"Other Disk:First")==-1 && err(&r)==-37);
    CHECK(open_res(&r,"OS999 HD:sub:First")==-1 && err(&r)==-37 && open_res(&r,"OS999 HD:../First")==-1 && err(&r)==-37);
    CHECK(current(&r)==second);
    use(&r,first);CHECK(err(&r)==0 && get(&r,0xa9a0,128)==fh);
    CHECK(get(&r,0xa99d,1)==sh);CHECK(get(&r,0xa99d,2)==fh);CHECK(get(&r,0xa99d,3)==ah);
    CHECK(mac_resource_chain_count(&r,TEXT,0)==3);CHECK(mac_resource_chain_count(&r,TEXT,1)==1);
    use(&r,1);CHECK(get(&r,0xa9a0,128)==ah);CHECK(mac_runtime_resource(&r,TEXT,128)==ah);
    int third=open_res(&r,"Unique");CHECK(third>second);
    CHECK(get(&r,0xa9a0,128)==sh);CHECK(get(&r,0xa81f,128)==0 && err(&r)==-192);
    uint32_t uh=get(&r,0xa81f,129);bytes_are(&r,uh,"NEW!");
    use(&r,first);uint32_t old=read32(&r,fh);mac_cpu_write(r.cpu,fh,4,0);
    CHECK(get(&r,0xa9a0,128)==fh);CHECK(read32(&r,fh)!=old);bytes_are(&r,fh,"AAA!");
    mac_cpu_write(r.cpu,fh,4,0);use(&r,1);
    start(&r);mac_cpu_write(r.cpu,SP,4,fh);CHECK(mac_resource_service(&r,0xa9a2)==1);
    CHECK(err(&r)==0);bytes_are(&r,fh,"AAA!"); /* Owner-based reload ignores current. */
    use(&r,0);CHECK(current(&r)==64);CHECK(get(&r,0xa9a0,128)==0 && err(&r)==0);
    CHECK(get(&r,0xa99d,1)==uh);CHECK(mac_resource_chain_count(&r,TEXT,0)==4);
    use(&r,12345);CHECK(err(&r)==-193 && current(&r)==64);
    CHECK(open_res(&r,"Missing")==-1 && err(&r)==-43 && current(&r)==64);
    CHECK(open_res(&r,"../First")==-1 && err(&r)==-37);
    CHECK(open_res(&r,"Link")==-1 && err(&r)==-54);
    CHECK(open_res(&r,"Bad")==-1 && err(&r)==-199);
    use(&r,third);close_res(&r,third);CHECK(err(&r)==0 && current(&r)==second && read32(&r,uh)==0);
    close_res(&r,third);CHECK(err(&r)==-193);
    CHECK(get(&r,0xa9a0,128)==sh);close_res(&r,first);CHECK(read32(&r,fh)==0 && current(&r)==second);
    /* Detached bytes survive closing the owning fork. */
    start(&r);mac_cpu_write(r.cpu,SP,4,sh);CHECK(mac_resource_service(&r,0xa992)==1);
    close_res(&r,second);CHECK(current(&r)==1);bytes_are(&r,sh,"BBB!");CHECK(home(&r,sh)==-1 && err(&r)==-192);
    CHECK(get(&r,0xa9a0,128)==ah);
    /* Preload exhaustion rolls back chain/current/heap and handle publication. */
    uint32_t heap=r.heap_next;r.heap_limit=heap;size_t handles=r.handle_count;
    CHECK(open_res(&r,"First")==-1 && err(&r)==-108);
    CHECK(current(&r)==1 && r.heap_next==heap && r.handle_count==handles);
    r.heap_limit=0xc000;first=open_res(&r,"First");CHECK(first>third);
    CHECK(get(&r,0xa9a0,128)!=fh); /* Closed resource identities cannot resurrect. */
    close_res(&r,first);
    /* The limit is an error, never an eviction or replacement of a live fork. */
    for(unsigned i=0;i<8;i++){char name[32],export_name[40];snprintf(name,sizeof name,"Bound%u",i);snprintf(export_name,sizeof export_name,"%s.rsrc",name);save(dir,export_name,b,END);CHECK(open_res(&r,name)>=256);}
    save(dir,"Overflow.rsrc",b,END);int before=current(&r);
    CHECK(open_res(&r,"Overflow")==-1 && err(&r)==-42 && current(&r)==before);
    /* A real base file without an export must not be reported as missing. */
    save(dir,"Native",b,END);start(&r);mac_cpu_ram(r.cpu)[NAME]=6;
    memcpy(mac_cpu_ram(r.cpu)+NAME+1,"Native",6);mac_cpu_write(r.cpu,SP,4,NAME);
    CHECK(mac_resource_service(&r,0xa997)==-1 && mac_cpu_halted(r.cpu));
    CHECK(strstr(mac_cpu_error(r.cpu),"Native resource fork")!=NULL);
    mac_runtime_destroy(&r);
    char native[512];snprintf(native,sizeof native,"%s/Native",dir);CHECK(unlink(native)==0);
    /* Invalid parameter/result stack fails before touching guest RAM. */
    memset(&r,0,sizeof r);r.cpu=mac_cpu_create(65536);mac_cpu_set(r.cpu,MAC_A7,65532);
    CHECK(mac_resource_service(&r,0xa997)==-1 && mac_cpu_halted(r.cpu));
    CHECK(!r.resource_chain && !r.handle_count);mac_runtime_destroy(&r);
    memset(&r,0,sizeof r);r.cpu=mac_cpu_create(65536);mac_cpu_set(r.cpu,MAC_A7,65528);CHECK(mac_resource_service(&r,0xa9c4)==-1 && mac_cpu_halted(r.cpu));mac_runtime_destroy(&r);   /* OpenRFPerm: the 8 argument bytes and the result do not fit */
    for(unsigned i=0;i<8;i++){char p[512];snprintf(p,sizeof p,"%s/Bound%u.rsrc",dir,i);CHECK(unlink(p)==0);}
    char overflow[512];snprintf(overflow,sizeof overflow,"%s/Overflow.rsrc",dir);CHECK(unlink(overflow)==0);
    const char *names[]={"First.rsrc","Second.rsrc","Unique.rsrc","Bad.rsrc","Link.rsrc"};
    for(unsigned i=0;i<sizeof names/sizeof names[0];i++){char p[512];snprintf(p,sizeof p,"%s/%s",dir,names[i]);CHECK(unlink(p)==0);}
    CHECK(rmdir(dir)==0);
    printf("Resource chain checks: %s\n",failures?"FAILED":"ok");return failures!=0;
}
