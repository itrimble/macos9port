#include "mac_runtime.h"
#include "mac_system_volume.h"
#include <stdio.h>
#include <string.h>

static int environment_checks(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create_model(4096, MAC_CPU_68020);
    if (!rt.cpu) return 1;
    int failed = 0;
    const int requests[] = {1,2,3,0,-1};
    for (unsigned i=0;i<5;i++) {
        memset(mac_cpu_ram(rt.cpu)+0x100,0x5a,16);
        mac_cpu_set(rt.cpu,MAC_A0,0x100);mac_cpu_set(rt.cpu,MAC_A7,0x800);
        mac_cpu_set(rt.cpu,MAC_D0,(uint16_t)requests[i]);mac_cpu_set(rt.cpu,MAC_SR,0x10);
        int error=requests[i]<=0?-5501:requests[i]>2?-5502:-5500;
        if(mac_system_service(&rt,0xa090)!=1 || (int32_t)mac_cpu_get(rt.cpu,MAC_D0)!=error ||
           mac_cpu_get(rt.cpu,MAC_A0)!=0x100 || mac_cpu_get(rt.cpu,MAC_A7)!=0x800 ||
           mac_cpu_get(rt.cpu,MAC_SR)!=(unsigned)(0x10|(error?8:4))) failed=1;
        if(requests[i]<=0) {
            for(unsigned k=0;k<16;k++)if(mac_cpu_ram(rt.cpu)[0x100+k]!=0x5a)failed=1;
        } else if(mac_cpu_read(rt.cpu,0x100,2)!=(unsigned)(requests[i]>2?2:requests[i]) ||
                  mac_cpu_read(rt.cpu,0x102,2)!=0 || mac_cpu_read(rt.cpu,0x104,2)!=0 ||
                  mac_cpu_read(rt.cpu,0x106,2)!=3 || mac_cpu_read(rt.cpu,0x108,4)!=0 ||
                  mac_cpu_read(rt.cpu,0x10c,4)!=0)failed=1;
    }
    /* Presence only; the environment service must never dereference file_state. */
    rt.file_state=&rt;rt.main_device=1;rt.screen_depth=8;
    mac_cpu_set(rt.cpu,MAC_D0,2);if(mac_system_service(&rt,0xa090)!=1)failed=1;
    if(mac_cpu_read(rt.cpu,0x109,1)!=1 || mac_cpu_read(rt.cpu,0x10e,2)!=0)failed=1;
    rt.file_state=NULL;
    rt.system_volume=mac_system_volume_create(&rt);
    if(!rt.system_volume)failed=1;
    for(unsigned version=1;version<=3;version++) {
        mac_cpu_set(rt.cpu,MAC_A0,0x100);mac_cpu_set(rt.cpu,MAC_A7,0x800);
        mac_cpu_set(rt.cpu,MAC_D0,version);
        if(mac_system_service(&rt,0xa090)!=1 ||
           (int32_t)mac_cpu_get(rt.cpu,MAC_D0)!=(version>2?-5502:0) ||
           mac_cpu_get(rt.cpu,MAC_A0)!=0x100 || mac_cpu_get(rt.cpu,MAC_A7)!=0x800 ||
           mac_cpu_read(rt.cpu,0x100,2)!=(version>2?2:version) ||
           mac_cpu_read(rt.cpu,0x102,2)!=MAC_PROFILE_SYSENV_MACHINE || mac_cpu_read(rt.cpu,0x104,2)!=MAC_PROFILE_SYSTEM_VERSION || mac_cpu_read(rt.cpu,0x106,2)!=3 ||
           mac_cpu_read(rt.cpu,0x108,1)!=0 || mac_cpu_read(rt.cpu,0x109,1)!=1 ||
           mac_cpu_read(rt.cpu,0x10a,4)!=0 || mac_cpu_read(rt.cpu,0x10e,2)!=0xfffe)failed=1;
    }
    /* The returned identity resolves to the genuinely registered System FCB. */
    memset(mac_cpu_ram(rt.cpu)+0x200,0,64);
    mac_cpu_write(rt.cpu,0x218,2,mac_cpu_read(rt.cpu,0xa58,2));
    mac_cpu_set(rt.cpu,MAC_A0,0x200);mac_cpu_set(rt.cpu,MAC_D0,8);
    if(mac_system_volume_service(&rt,rt.system_volume,0xa260)!=1 ||
       mac_cpu_get(rt.cpu,MAC_D0)!=0 || mac_cpu_read(rt.cpu,0x234,2)!=0xfffe ||
       mac_cpu_read(rt.cpu,0x228,4)!=286)failed=1;
    mac_system_volume_destroy(rt.system_volume);rt.system_volume=NULL;
    mac_cpu_set(rt.cpu,MAC_A0,0x100);mac_cpu_set(rt.cpu,MAC_D0,1);
    if(mac_system_service(&rt,0xa090)!=1 || (int32_t)mac_cpu_get(rt.cpu,MAC_D0)!=-5500 ||
       mac_cpu_read(rt.cpu,0x10e,2)!=0 || mac_cpu_read(rt.cpu,0x102,2)!=0 || mac_cpu_read(rt.cpu,0x104,2)!=0)failed=1;   /* no System, no identity */
    mac_cpu_destroy(rt.cpu);
    const uint32_t bad[]={0,0x101,4090,0xffffffff};
    for(unsigned i=0;i<4;i++) {
        rt.cpu=mac_cpu_create(4096);memset(mac_cpu_ram(rt.cpu),0x5a,4096);
        mac_cpu_set(rt.cpu,MAC_A0,bad[i]);mac_cpu_set(rt.cpu,MAC_D0,2);
        if(mac_system_service(&rt,0xa090)!=-1 || !mac_cpu_halted(rt.cpu))failed=1;
        for(unsigned k=0;k<4096;k++)if(mac_cpu_ram(rt.cpu)[k]!=0x5a)failed=1;
        mac_cpu_destroy(rt.cpu);
    }
    return failed;
}

int main(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create_model(MAC_RUNTIME_RAM, MAC_CPU_68020);
    if (!rt.cpu) return 1;
    const uint32_t selectors[] = {0x73797376,0x70726f63,0x72616d20,0x66707520,0x71642020,0x6d616368,0x71647277,0x62616421};
    const uint32_t responses[] = {MAC_PROFILE_SYSTEM_VERSION,3,0x800000,0,0,MAC_PROFILE_GESTALT_MACHINE,0,0};
    int failed = environment_checks();
    for (unsigned i=0;i<8;i++) {
        mac_cpu_set(rt.cpu,MAC_D0,selectors[i]);
        mac_cpu_set(rt.cpu,MAC_A7,0x50000);
        if (mac_system_service(&rt,0xa1ad)!=1 ||
            mac_cpu_get(rt.cpu,MAC_A0)!=responses[i] ||
            mac_cpu_get(rt.cpu,MAC_D0)!=(i==7?(uint32_t)-5551:0) ||
            mac_cpu_get(rt.cpu,MAC_A7)!=0x50000) failed=1;
    }
    /* NewGestalt records a selector; Gestalt then reports it present (response 0). */
    mac_cpu_set(rt.cpu,MAC_D0,0x62616421);mac_cpu_set(rt.cpu,MAC_A0,0x1000);
    if (mac_system_service(&rt,0xa3ad)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=0 || rt.gestalt_count!=1) failed=1;
    mac_cpu_set(rt.cpu,MAC_D0,0x62616421);
    if (mac_system_service(&rt,0xa1ad)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=0) failed=1;
    mac_cpu_set(rt.cpu,MAC_D0,0x62616421);mac_cpu_set(rt.cpu,MAC_A0,0x1000);
    if (mac_system_service(&rt,0xa3ad)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=(uint32_t)-5550) failed=1; /* duplicate */
    mac_cpu_set(rt.cpu,MAC_D0,0x73797376);
    if (mac_system_service(&rt,0xa3ad)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=(uint32_t)-5550) failed=1; /* built in */
    mac_cpu_set(rt.cpu,MAC_D0,0x73746466);mac_cpu_set(rt.cpu,MAC_A7,0x50000);                  /* 'stdf': exactly bit 0, StandardFile58 */
    if (mac_system_service(&rt,0xa1ad)!=1 || mac_cpu_get(rt.cpu,MAC_A0)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=0) failed=1;
    rt.main_device=1;rt.screen_depth=8;                         /* the indexed color profile is up */
    mac_cpu_set(rt.cpu,MAC_D0,0x71647277);mac_cpu_set(rt.cpu,MAC_A7,0x50000);
    if (mac_system_service(&rt,0xa1ad)!=1 || mac_cpu_get(rt.cpu,MAC_A0)!=1 || mac_cpu_get(rt.cpu,MAC_D0)!=0) failed=1;
    rt.screen_depth=1;                                          /* any other depth: no color claim */
    mac_cpu_set(rt.cpu,MAC_D0,0x71647277);
    if (mac_system_service(&rt,0xa1ad)!=1 || mac_cpu_get(rt.cpu,MAC_A0)!=0) failed=1;
    rt.main_device=0;rt.screen_depth=0;
    mac_cpu_write(rt.cpu,0x100,2,0x7001); /* MOVEQ #1,D0 */
    mac_cpu_set(rt.cpu,MAC_PC,0x100);
    mac_cpu_run(rt.cpu,1);
    mac_cpu_write(rt.cpu,0x100,2,0x7002); /* Modified code must execute immediately. */
    if (mac_system_service(&rt,0xa0bd)!=1) failed=1;
    mac_cpu_set(rt.cpu,MAC_PC,0x100);
    mac_cpu_run(rt.cpu,1);
    if (mac_cpu_get(rt.cpu,MAC_D0)!=2 || mac_cpu_halted(rt.cpu)) failed=1;
    mac_cpu_destroy(rt.cpu);
    printf("Environment queries: %s\n",failed?"FAILED":"ok");
    return failed;
}
