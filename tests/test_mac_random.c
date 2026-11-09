/* Random ($A861): Lehmer generator on QuickDraw's randSeed. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define SP 0x9000u
#define GLOBALS 0x7000u
static MacRuntime make(uint32_t seed){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.qd_globals=GLOBALS;
    CHECK(!mac_cpu_write(r.cpu,GLOBALS-126,4,seed));mac_cpu_set(r.cpu,MAC_A7,SP);return r;
}
static int call(MacRuntime*r){mac_cpu_write(r->cpu,SP,2,0xbeef);mac_cpu_write(r->cpu,SP+2,2,0x1234);mac_cpu_set(r->cpu,MAC_A7,SP);return mac_random_service(r,0xa861);}
static int16_t out(MacRuntime*r){return (int16_t)mac_cpu_read(r->cpu,SP,2);}
static uint32_t seed_raw(MacRuntime*r){const uint8_t*m=mac_cpu_ram(r->cpu)+GLOBALS-126;return (uint32_t)m[0]<<24|m[1]<<16|m[2]<<8|m[3];}
static uint32_t seed(MacRuntime*r){return mac_cpu_read(r->cpu,GLOBALS-126,4);}
int main(void){
    MacRuntime r=make(1);
    CHECK(mac_random_service(&r,0xa860)==0 && mac_random_service(&r,0xa862)==0);   /* neighbours are not ours */
    /* Park-Miller from seed 1: 16807, 282475249, 1622650073, 984943658, 1144108930, 470211272 */
    const uint32_t expect[]={16807,282475249,1622650073u,984943658,1144108930u,470211272};
    for(unsigned i=0;i<6;i++){
        CHECK(call(&r)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP);                       /* the result slot stays; nothing is popped */
        CHECK(seed(&r)==expect[i] && out(&r)==(int16_t)(expect[i]&0xffff));
        CHECK(mac_cpu_read(r.cpu,SP+2,2)==0x1234);                                  /* the caller's data above is untouched */
    }
    /* A seed of 0 stays 0. */
    MacRuntime z=make(0);CHECK(call(&z)==1 && seed(&z)==0 && out(&z)==0);
    /* A seed above 2^31-1 (the clock seeds it that way) still follows the arithmetic. */
    MacRuntime big=make(0xffffffffu);CHECK(call(&big)==1);
    uint64_t want=(uint64_t)0xffffffffu*16807u%2147483647u;CHECK(seed(&big)==(uint32_t)want && out(&big)==(int16_t)(want&0xffff));
    /* -32768 is never returned: it comes back as 0. Search for a seed that produces the low word 0x8000. */
    uint32_t found=0;for(uint32_t s=1;s<20000000u && !found;s++)if(((uint64_t)s*16807u%2147483647u&0xffff)==0x8000)found=s;
    CHECK(found);
    MacRuntime m=make(found);CHECK(call(&m)==1 && (seed(&m)&0xffff)==0x8000 && out(&m)==0);
    /* The range over many calls is -32767..32767. */
    MacRuntime w=make(12345);int lo=0,hi=0;for(unsigned i=0;i<5000;i++){CHECK(call(&w)==1);int v=out(&w);CHECK(v>=-32767);if(v<lo)lo=v;if(v>hi)hi=v;}
    CHECK(lo<-20000 && hi>20000);
    /* Refusals halt by name and leave the seed alone. */
    MacRuntime n=make(5);n.qd_globals=0;CHECK(call(&n)==-1 && mac_cpu_halted(n.cpu));
    MacRuntime o=make(5);mac_cpu_set(o.cpu,MAC_A7,SP+1);CHECK(mac_random_service(&o,0xa861)==-1 && mac_cpu_halted(o.cpu) && seed_raw(&o)==5);
    MacRuntime e=make(5);mac_cpu_set(e.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(e.cpu));CHECK(mac_random_service(&e,0xa861)==-1 && mac_cpu_halted(e.cpu) && seed_raw(&e)==5);
    MacRuntime g=make(5);g.qd_globals=100;CHECK(call(&g)==-1 && mac_cpu_halted(g.cpu));      /* too low for a randSeed */
    MacRuntime f=make(5);f.qd_globals=(uint32_t)mac_cpu_ram_size(f.cpu)+200;CHECK(call(&f)==-1 && mac_cpu_halted(f.cpu));
    MacRuntime h=make(5);mac_cpu_halt(h.cpu,"stopped");CHECK(mac_random_service(&h,0xa861)==-1 && seed_raw(&h)==5);
    mac_cpu_destroy(r.cpu);mac_cpu_destroy(z.cpu);mac_cpu_destroy(big.cpu);mac_cpu_destroy(m.cpu);mac_cpu_destroy(w.cpu);
    mac_cpu_destroy(n.cpu);mac_cpu_destroy(o.cpu);mac_cpu_destroy(e.cpu);mac_cpu_destroy(g.cpu);mac_cpu_destroy(f.cpu);mac_cpu_destroy(h.cpu);
    puts("Random checks passed");return 0;
}
