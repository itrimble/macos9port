#include "mac_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static int call(MacRuntime*r,unsigned op,uint32_t p,int32_t d){mac_cpu_set(r->cpu,MAC_A0,p);mac_cpu_set(r->cpu,MAC_D0,(uint32_t)d);return mac_time_service(r,op);}
static int trap(MacCpu*c,uint16_t op,uint32_t pc,void*v){(void)c;(void)pc;return mac_time_service(v,op)==1?0:-1;}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);mac_cpu_set(r.cpu,MAC_A7,0x9000);wr(&r,0x3006,4,0x5000);
 /* Authored callback: increment counter; publish A1 record; RTS. */
 uint8_t code[]={0x52,0xb9,0,0,0x60,0,0x23,0xc9,0,0,0x60,4,0x4e,0x75};memcpy(mac_cpu_ram(r.cpu)+0x5000,code,sizeof code);return r;}
static void done(MacRuntime*r){mac_time_destroy(r);mac_cpu_destroy(r->cpu);}
static void timing(void){MacRuntime r=fresh();CHECK(!mac_time_set(&r,1000));CHECK(!r.time_manager);CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,17)==1);CHECK(rd(&r,0x3004,2)==0x8000);CHECK(!mac_time_set(&r,17999));CHECK(mac_time_poll(&r,10000)==0);CHECK(!mac_time_set(&r,18000));uint32_t saved[18];for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r.cpu,(MacCpuReg)i);CHECK(mac_time_poll(&r,10000)>0);for(unsigned i=0;i<18;i++)CHECK(saved[i]==mac_cpu_get(r.cpu,(MacCpuReg)i));CHECK(rd(&r,0x6000,4)==1 && rd(&r,0x6004,4)==0x3000 && rd(&r,0x3004,2)==0);
 CHECK(!mac_time_set(&r,20000));CHECK(call(&r,0xa05a,0x3000,17)==1);CHECK(!mac_time_set(&r,34999));CHECK(!mac_time_poll(&r,10000));CHECK(!mac_time_set(&r,35000));CHECK(mac_time_poll(&r,10000)>0 && rd(&r,0x6000,4)==2);done(&r);}
static void pause_resume(void){MacRuntime r=fresh();CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,-1000)==1);CHECK(!mac_time_set(&r,250));CHECK(call(&r,0xa059,0x3000,0)==1 && (int32_t)rd(&r,0x300a,4)==-750);CHECK(!mac_time_set(&r,10000));CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,0)==1);CHECK(!mac_time_set(&r,10749));CHECK(!mac_time_poll(&r,10000));CHECK(!mac_time_set(&r,10750));CHECK(mac_time_poll(&r,10000)>0);done(&r);}
static void reentrant(void){MacRuntime r=fresh();mac_cpu_set_trap(r.cpu,trap,&r);uint8_t code[]={0x52,0xb9,0,0,0x60,0,0x20,0x49,0x70,1,0xa0,0x5a,0x4e,0x75};memcpy(mac_cpu_ram(r.cpu)+0x5000,code,sizeof code);CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,1)==1);CHECK(!mac_time_set(&r,5000));CHECK(mac_time_poll(&r,10000)>0 && rd(&r,0x6000,4)==5);CHECK(rd(&r,0x3004,2)==0x8000);done(&r);}
static void failure(void){MacRuntime r=fresh();CHECK(call(&r,0xa458,MAC_RUNTIME_RAM-20,0)==-1 && !r.time_manager);done(&r);r=fresh();CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,0)==1);wr(&r,0x5000,2,0x60fe);CHECK(mac_time_poll(&r,10000)==10000 && mac_cpu_halted(r.cpu));done(&r);r=fresh();CHECK(!mac_time_set(&r,5));CHECK(mac_time_set(&r,4)==-1 && r.clock_us==5);done(&r);}
static void edges(void){MacRuntime r=fresh();CHECK(call(&r,0xa05a,0x3000,1)==1 && mac_cpu_get(r.cpu,MAC_D0)==UINT32_MAX);wr(&r,0x3006,4,0);CHECK(call(&r,0xa058,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,-1)==1);CHECK(!mac_time_set(&r,1));mac_cpu_set(r.cpu,MAC_SR,0x2700);CHECK(!mac_time_poll(&r,10000) && rd(&r,0x3004,2)==0x8000);mac_cpu_set(r.cpu,MAC_SR,0x2000);CHECK(!mac_time_poll(&r,10000) && rd(&r,0x3004,2)==0);CHECK(call(&r,0xa059,0x3000,0)==1 && rd(&r,0x300a,4)==0);done(&r);r=fresh();CHECK(call(&r,0xa458,0x3000,0)==1);CHECK(call(&r,0xa05a,0x3000,0)==1);mac_cpu_set(r.cpu,MAC_A7,2);CHECK(!mac_time_poll(&r,10000) && mac_cpu_halted(r.cpu));CHECK(mac_cpu_ram(r.cpu)[0x3004]==0x80);done(&r);}
int main(void){edges();timing();pause_resume();reentrant();failure();puts("timer tests passed");return 0;}
