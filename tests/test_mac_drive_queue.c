#include "mac_drive_queue.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
static uint32_t read_raw(MacRuntime *r,uint32_t p,unsigned n) {
    const uint8_t*b=mac_cpu_ram(r->cpu)+p;uint32_t v=0;
    while(n--)v=v*256+*b++;
    return v;
}
static void check_entry(MacRuntime*r,uint32_t e,unsigned drive,uint32_t blocks,uint32_t next) {
    CHECK(read_raw(r,e,4)==next);CHECK(read_raw(r,e+4,2)==1);
    CHECK(read_raw(r,e+6,2)==drive);CHECK(read_raw(r,e+8,4)==0);
    CHECK((read_raw(r,e+14,2)<<16|read_raw(r,e+12,2))==blocks);
    CHECK(read_raw(r,e-4,1)==0x80 && read_raw(r,e-3,1)==8);
    CHECK(read_raw(r,e-2,2)==0);
}
int main(void) {
    MacRuntime r={0};r.cpu=mac_cpu_create_model(8192,MAC_CPU_68020);CHECK(r.cpu!=NULL);
    if(!r.cpu)return 1;
    memset(mac_cpu_ram(r.cpu),0x5a,8192);
    CHECK(mac_drive_mount(&r,2,1)==-1 && r.drive_queue==NULL && r.heap_next==0);
    r.heap_next=0x30a;r.heap_limit=8192;
    CHECK(mac_drive_mount(&r,2,1)==-1 && r.drive_queue==NULL && r.heap_next==0x30a);
    for(unsigned i=0;i<8192;i++)CHECK(mac_cpu_ram(r.cpu)[i]==0x5a);
    CHECK(mac_drive_number(&r,1)==0 && mac_drive_number(&r,2)==0);
    r.heap_next=0x1000;r.heap_limit=0x1010;
    CHECK(mac_drive_mount(&r,2,1)==-1 && r.drive_queue==NULL && r.heap_next==0x1000);
    r.heap_limit=8192;
    CHECK(mac_drive_mount(&r,0,1)==-1 && r.heap_next==0x1000);
    CHECK(mac_drive_mount(&r,2,1)==0);
    uint32_t system=read_raw(&r,0x30a,4),end=r.heap_next;
    CHECK(system>=0x1000 && read_raw(&r,0x30e,4)==system);
    check_entry(&r,system,2,1,0);
    CHECK(mac_drive_number(&r,1)==0 && mac_drive_number(&r,2)==2);
    CHECK(mac_drive_mount(&r,1,0x12345)==0 && r.heap_next==end);
    uint32_t data=read_raw(&r,0x30a,4);
    CHECK(data!=system && read_raw(&r,0x30e,4)==system);
    check_entry(&r,data,1,0x12345,system);check_entry(&r,system,2,1,0);
    CHECK(mac_drive_mount(&r,1,0)==0 && r.heap_next==end);check_entry(&r,data,1,0,system);
    mac_drive_unmount(&r,2);CHECK(read_raw(&r,0x30a,4)==data && read_raw(&r,0x30e,4)==data);
    check_entry(&r,data,1,0,0);CHECK(read_raw(&r,system,4)==0);
    CHECK(mac_drive_mount(&r,2,1)==0 && r.heap_next==end);
    mac_drive_unmount(&r,1);CHECK(read_raw(&r,0x30a,4)==system);check_entry(&r,system,2,1,0);
    mac_cpu_halt(r.cpu,"test halt before cleanup");
    CHECK(mac_drive_mount(&r,1,4)==-1);
    mac_drive_destroy(&r);CHECK(r.drive_queue==NULL && mac_drive_number(&r,2)==0);
    CHECK(read_raw(&r,0x308,2)==0 && read_raw(&r,0x30a,4)==0 && read_raw(&r,0x30e,4)==0);
    for(uint32_t i=0x1000;i<end;i++)CHECK(mac_cpu_ram(r.cpu)[i]==0);
    mac_drive_destroy(&r);mac_cpu_destroy(r.cpu);
    printf("Logical drive queue: %s\n",failures?"FAILED":"ok");return failures!=0;
}
