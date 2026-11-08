/* SetStdCProcs: the standard routine pointers, and its refusals. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define SP 0x9000u
#define REC 0x6000u
static MacRuntime make(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);mac_cpu_set(r.cpu,MAC_A7,SP);return r;}
int main(void){
    static const uint16_t std[13]={0xa882,0xa890,0xa8a0,0xa8af,0xa8b6,0xa8bd,0xa8c5,0xa8d1,0xa8eb,0xa8f1,0xa8ed,0xa8ee,0xa8f0};
    MacRuntime r=make();
    CHECK(mac_stdprocs_service(&r,0xaa4d)==0 && mac_stdprocs_service(&r,0xaa4f)==0);          /* neighbours are not ours */
    memset(mac_cpu_ram(r.cpu)+REC-8,0x5a,96);mac_cpu_write(r.cpu,SP,4,REC);mac_cpu_write(r.cpu,SP+4,4,0x1234);
    CHECK(mac_stdprocs_service(&r,0xaa4e)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    for(unsigned i=0;i<13;i++)CHECK(mac_cpu_read(r.cpu,REC+4*i,4)==0x2000u+(unsigned)(std[i]&0xfff)*8u);   /* each is the stub of its Std trap */
    for(unsigned i=13;i<20;i++)CHECK(mac_cpu_read(r.cpu,REC+4*i,4)==0);                      /* opcodeProc and the reserved entries are NIL */
    CHECK(mac_cpu_ram(r.cpu)[REC-1]==0x5a && mac_cpu_ram(r.cpu)[REC+80]==0x5a);              /* nothing outside the 80 bytes is touched */
    CHECK(mac_cpu_read(r.cpu,SP+4,4)==0x1234);
    mac_cpu_destroy(r.cpu);
    for(unsigned k=0;k<6;k++){
        r=make();uint32_t sp=SP,p=REC;
        if(k==0)p=0;if(k==1)p=REC+1;if(k==2)p=(uint32_t)mac_cpu_ram_size(r.cpu)-76;if(k==3)sp=SP+1;if(k==4)sp=(uint32_t)mac_cpu_ram_size(r.cpu)-2;if(k==5)mac_cpu_halt(r.cpu,"stopped");
        mac_cpu_set(r.cpu,MAC_A7,sp);if(k!=3&&k!=4)mac_cpu_write(r.cpu,SP,4,p);
        CHECK(mac_stdprocs_service(&r,0xaa4e)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    }
    puts("SetStdCProcs checks passed");return 0;
}
