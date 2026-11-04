#include "mac_sound_int.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SP 0x8000u
static unsigned calls;
static uint32_t target;
static int trap(MacCpu *cpu,uint16_t op,uint32_t pc,void *opaque){
    (void)pc;MacRuntime*r=opaque;assert(op==0xa801);
    int result=mac_sound_service(r,op);assert(result==1);calls++;
    assert(r->sound->channels[0].closing && r->sound->channels[0].address==target);
    assert(mac_cpu_read(cpu,target+12,4)==0x12345678);
    assert(!r->sound->channels[0].playing && !r->sound->channels[0].count);
    /* A subsequent SoundDispatch must see the logical close before validating
     * its otherwise invalid header or rejecting interrupt-time start. */
    uint32_t sp=mac_cpu_get(cpu,MAC_A7),d0=mac_cpu_get(cpu,MAC_D0);
    assert(!mac_cpu_write(cpu,0x7800,4,0));assert(!mac_cpu_write(cpu,0x7804,4,target));
    mac_cpu_set(cpu,MAC_A7,0x7800);mac_cpu_set(cpu,MAC_D0,0x00200008);
    assert(mac_sound_double_service(r,0xa800)==1 && (int16_t)mac_cpu_read(cpu,0x7808,2)==-205);
    mac_cpu_set(cpu,MAC_A7,sp);mac_cpu_set(cpu,MAC_D0,d0);
    return 0;
}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){assert(!mac_cpu_write(r->cpu,p,n,v));}
static void run(int supplied,int doubleback){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);assert(r.cpu);r.heap_next=0x100000;r.heap_limit=0x200000;
    if(supplied){wr(&r,0x7000,4,0x20000);wr(&r,0x2001e,2,1500);}
    wr(&r,SP,4,0x5000);wr(&r,SP+4,4,0x8c);wr(&r,SP+8,2,5);wr(&r,SP+10,4,0x7000);mac_cpu_set(r.cpu,MAC_A7,SP);
    assert(mac_sound_service(&r,0xa807)==1);target=mac_cpu_read(r.cpu,0x7000,4);wr(&r,target+12,4,0x12345678);
    /* Dispose twice from a real Pascal callback. The second call must reject
     * the logically closed channel; its borrowed record must remain readable. */
    const uint8_t code[]={
      0x55,0x8f,0x2f,0x2f,0,10,0x3f,0x3c,1,0,0xa8,1,0x30,0x1f,
      0x55,0x8f,0x2f,0x2f,0,10,0x3f,0x3c,1,0,0xa8,1,0x30,0x1f,
      0x33,0xc0,0,0,0x60,4,0x20,0x6f,0,8,0x20,0x28,0,12,
      0x23,0xc0,0,0,0x60,0,0x20,0x5f,0x50,0x8f,0x4e,0xd0};
    memcpy(mac_cpu_ram(r.cpu)+0x5000,code,sizeof code);
    if(doubleback){r.sound->channels[0].double_buffer=(MacSoundDouble){.callback=0x5000,.buffers={0x4100,0x4200},.active=1,.pending=1};}
    else assert(mac_sound_command(&r,&r.sound->channels[0],(MacSoundCommand){13,0,0})==1);
    uint8_t before[12036];size_t bytes=supplied?sizeof before:MAC_SOUND_RECORD;
    memcpy(before,mac_cpu_ram(r.cpu)+target,bytes);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,0x5500);
    mac_cpu_set_trap(r.cpu,trap,&r);calls=0;
    size_t ran=doubleback?mac_sound_double_poll(&r,10000):mac_sound_poll(&r,10000);
    assert(ran>0 && !mac_cpu_halted(r.cpu) && calls==2);
    assert(mac_cpu_read(r.cpu,0x6000,4)==0x12345678 && (int16_t)mac_cpu_read(r.cpu,0x6004,2)==-205);
    assert(mac_cpu_get(r.cpu,MAC_PC)==0x5500 && mac_cpu_get(r.cpu,MAC_A7)==SP);
    assert(!r.sound->channels[0].address && !r.sound->channels[0].closing);
    if(supplied)assert(!memcmp(before,mac_cpu_ram(r.cpu)+target,bytes));
    else{assert(!r.handles[0].pointer);for(size_t i=0;i<bytes;i++)assert(!mac_cpu_ram(r.cpu)[target+i]);}
    mac_sound_destroy(&r);mac_cpu_destroy(r.cpu);
}
static void busy_disposal(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);assert(r.cpu);r.heap_next=0x100000;r.heap_limit=0x200000;
    wr(&r,SP,4,0);wr(&r,SP+4,4,0x8c);wr(&r,SP+8,2,5);wr(&r,SP+10,4,0x7000);mac_cpu_set(r.cpu,MAC_A7,SP);
    assert(mac_sound_service(&r,0xa807)==1);uint32_t p=mac_cpu_read(r.cpu,0x7000,4);
    r.sound->channels[0].playing=1;wr(&r,SP,2,0);wr(&r,SP+2,4,p);mac_cpu_set(r.cpu,MAC_A7,SP);
    assert(mac_sound_service(&r,0xa801)==-1 && r.sound->channels[0].playing && !r.sound->channels[0].closing);
    assert(r.handles[0].pointer==p);mac_sound_destroy(&r);mac_cpu_destroy(r.cpu);
}
int main(void){run(0,0);run(1,0);run(0,1);run(1,1);busy_disposal();puts("deferred callback channel disposal and caller storage tests passed");return 0;}
