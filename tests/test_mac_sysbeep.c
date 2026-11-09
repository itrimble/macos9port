/* SysBeep ($A9C8): validates the stack, pops its one word, plays nothing. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    CHECK(mac_sysbeep_service(&rt,0xa9c9)==0 && mac_sysbeep_service(&rt,0xa9c7)==0);                 /* neighbours are not ours */
    mac_cpu_set(rt.cpu,MAC_A7,0x8000);mac_cpu_write(rt.cpu,0x8000,2,3);
    CHECK(mac_sysbeep_service(&rt,0xa9c8)==1 && mac_cpu_get(rt.cpu,MAC_A7)==0x8002 && !mac_cpu_halted(rt.cpu));
    mac_cpu_set(rt.cpu,MAC_A7,0x8001);CHECK(mac_sysbeep_service(&rt,0xa9c8)==-1 && mac_cpu_halted(rt.cpu));   /* odd stack */
    mac_cpu_destroy(rt.cpu);
    rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);mac_cpu_set(rt.cpu,MAC_A7,(uint32_t)MAC_RUNTIME_RAM-1);
    CHECK(mac_sysbeep_service(&rt,0xa9c8)==-1);mac_cpu_destroy(rt.cpu);                             /* no room for the argument */
    rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);mac_cpu_set(rt.cpu,MAC_A7,0x100);
    CHECK(mac_sysbeep_service(&rt,0xa9c8)==-1);mac_cpu_destroy(rt.cpu);                             /* low memory */
    puts("SysBeep checks passed");return 0;
}
