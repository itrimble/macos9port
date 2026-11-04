#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static int trap(MacCpu*c,uint16_t op,uint32_t pc,void*u){(void)c;(void)pc;return mac_vbl_service(u,op)>0?0:-1;}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(65536);CHECK(r.cpu);mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_PC,0x4000);return r;}
static void task(MacRuntime*r,uint32_t p,uint32_t code,unsigned count){wr(r,p+4,2,1);wr(r,p+6,4,code);wr(r,p+10,2,count);wr(r,p+12,2,0);}
static void install(MacRuntime*r,uint32_t p){mac_cpu_set(r->cpu,MAC_A0,p);CHECK(mac_vbl_service(r,0xa033)==1);CHECK(mac_cpu_get(r->cpu,MAC_D0)==0);}
static void countdown(void){
 MacRuntime r=fresh();mac_cpu_set_trap(r.cpu,trap,&r);
 /* addq.w #1,14(A0); move.w #2,10(A0); moveq #7,D0; RTS */
 const uint16_t words[]={0x5268,14,0x317c,2,10,0x7007,0x4e75};
 for(unsigned i=0;i<7;i++)wr(&r,0x2000+i*2,2,words[i]);
 task(&r,0x3000,0x2000,2);install(&r,0x3000);
 mac_cpu_set(r.cpu,MAC_D0,0x12345678);mac_cpu_set(r.cpu,MAC_A0,0x5550);
 r.ticks=1;CHECK(mac_vbl_poll(&r,10000)==0&&rd(&r,0x300a,2)==1);
 r.ticks=2;CHECK(mac_vbl_poll(&r,9999)==0&&rd(&r,0x300a,2)==1);
 CHECK(mac_vbl_poll(&r,10000)==4&&rd(&r,0x300e,2)==1&&rd(&r,0x300a,2)==2);
 CHECK(mac_cpu_get(r.cpu,MAC_D0)==0x12345678&&mac_cpu_get(r.cpu,MAC_A0)==0x5550);
 CHECK(mac_cpu_get(r.cpu,MAC_PC)==0x4000&&mac_cpu_get(r.cpu,MAC_A7)==0x8000);
 r.ticks=4;CHECK(mac_vbl_poll(&r,10000)==4&&rd(&r,0x300e,2)==2);
 mac_cpu_set(r.cpu,MAC_A0,0x3000);CHECK(mac_vbl_service(&r,0xa034)==1&&!r.vbl_count);
 CHECK(mac_vbl_service(&r,0xa034)==1&&(int32_t)mac_cpu_get(r.cpu,MAC_D0)==-1);
 mac_cpu_destroy(r.cpu);
}
static void queue_and_reentrant(void){
 MacRuntime r=fresh();mac_cpu_set_trap(r.cpu,trap,&r);
 /* Current callback removes itself, installs a prepared second task, returns. */
 const uint16_t words[]={0xa034,0x207c,0,0x3020,0xa033,0x4e75};
 for(unsigned i=0;i<6;i++)wr(&r,0x2000+i*2,2,words[i]);
 wr(&r,0x2100,2,0x5268);wr(&r,0x2102,2,14);wr(&r,0x2104,2,0x4e75);
 task(&r,0x3000,0x2000,1);task(&r,0x3020,0x2100,1);install(&r,0x3000);
 r.ticks=1;CHECK(mac_vbl_poll(&r,10000)==4);
 CHECK(r.vbl_count==1&&r.vbl_tasks[0]==0x3020&&rd(&r,0x302a,2)==1);
 r.ticks=2;CHECK(mac_vbl_poll(&r,10000)==2&&rd(&r,0x302e,2)==1&&!r.vbl_count);
 /* Two simultaneously due callbacks both execute within the same tick. */
 task(&r,0x3000,0x2100,1);task(&r,0x3020,0x2100,1);install(&r,0x3000);install(&r,0x3020);
 CHECK(rd(&r,0x3000,4)==0x3020);r.ticks=3;
 CHECK(mac_vbl_poll(&r,10000)==4&&!r.vbl_count);
 mac_cpu_destroy(r.cpu);
}
static void faults(void){
 MacRuntime r=fresh();task(&r,0x3000,0x2000,1);wr(&r,0x3004,2,2);mac_cpu_set(r.cpu,MAC_A0,0x3000);
 CHECK(mac_vbl_service(&r,0xa033)==1&&(int32_t)mac_cpu_get(r.cpu,MAC_D0)==-2&&!r.vbl_count);mac_cpu_destroy(r.cpu);
 r=fresh();mac_cpu_set(r.cpu,MAC_A0,65530);CHECK(mac_vbl_service(&r,0xa033)==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 r=fresh();task(&r,0x3000,0x2000,1);wr(&r,0x2000,2,0x60fe);install(&r,0x3000);r.ticks=1;
 CHECK(mac_vbl_poll(&r,10000)==10000&&mac_cpu_halted(r.cpu));CHECK(mac_cpu_get(r.cpu,MAC_PC)==0x2000);mac_cpu_destroy(r.cpu);
}
int main(void){countdown();queue_and_reentrant();faults();puts("mac VBL callback tests passed");return 0;}
