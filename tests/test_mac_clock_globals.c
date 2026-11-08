/* Low-memory Ticks ($16A) and Time ($20C), published from rt->ticks for programs that read them directly. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint32_t rd(MacRuntime*r,uint32_t p){return mac_cpu_read(r->cpu,p,4);}
int main(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    mac_clock_publish(NULL);                                   /* harmless without a runtime or a CPU */
    MacRuntime none={0};mac_clock_publish(&none);
    r.clock_base=0xa0000000u;
    r.ticks=0;mac_clock_publish(&r);CHECK(rd(&r,0x16a)==0 && rd(&r,0x20c)==0xa0000000u);
    r.ticks=59;mac_clock_publish(&r);CHECK(rd(&r,0x16a)==59 && rd(&r,0x20c)==0xa0000000u);          /* not yet a second */
    r.ticks=60;mac_clock_publish(&r);CHECK(rd(&r,0x16a)==60 && rd(&r,0x20c)==0xa0000001u);
    r.ticks=12345;mac_clock_publish(&r);CHECK(rd(&r,0x16a)==12345 && rd(&r,0x20c)==0xa0000000u+205u);
    r.ticks=0xfffffffeu;mac_clock_publish(&r);CHECK(rd(&r,0x16a)==0xfffffffeu);                     /* the long wraps like the real counter */
    CHECK(mac_cpu_read(r.cpu,0x169,1)==0 && mac_cpu_read(r.cpu,0x16e,1)==0 && mac_cpu_read(r.cpu,0x20b,1)==0 && mac_cpu_read(r.cpu,0x210,1)==0);                  /* neighbouring low memory is untouched */
    /* With no base given, the host clock is used once and then held (seconds since 1904, long after 2020). */
    MacRuntime h={0};h.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(h.cpu);h.ticks=0;
    uint64_t t0=(uint64_t)time(NULL);mac_clock_publish(&h);uint64_t t1=(uint64_t)time(NULL);
    uint32_t base=rd(&h,0x20c);CHECK(base>3700000000u && h.clock_base==base);
    CHECK(base>=t0+2082844800ull && base<=t1+2082844800ull);   /* seconds since 1 Jan 1904 */
    h.ticks=600;mac_clock_publish(&h);CHECK(rd(&h,0x20c)==base+10 && h.clock_base==base);
    /* Both run entry points publish before the guest runs. */
    MacRuntime a={0};a.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(a.cpu);a.clock_base=0xb0000000u;a.ticks=777;
    mac_cpu_set(a.cpu,MAC_A7,0x8000);mac_cpu_set(a.cpu,MAC_PC,0x10000);
    mac_runtime_run(&a,100);CHECK(rd(&a,0x16a)==777 && rd(&a,0x20c)==0xb0000000u+12);
    a.ticks=1800;mac_runtime_run_cycles(&a,400);CHECK(rd(&a,0x16a)==1800 && rd(&a,0x20c)==0xb0000000u+30);
    mac_cpu_destroy(a.cpu);mac_cpu_destroy(h.cpu);mac_cpu_destroy(r.cpu);
    puts("clock globals checks passed");return 0;
}
