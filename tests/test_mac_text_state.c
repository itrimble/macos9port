/* TextFont, TextFace, TextMode, TextSize: one field each of the current port, argument popped. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define SP 0x9000u
#define PORT 0x6000u
static MacRuntime make(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.current_port=PORT;
    memset(mac_cpu_ram(r.cpu)+PORT,0x5a,108);                   /* a sentinel pattern to see any stray write */
    mac_cpu_set(r.cpu,MAC_A7,SP);return r;
}
static int call(MacRuntime*r,uint16_t op,uint32_t word){mac_cpu_write(r->cpu,SP,2,word);mac_cpu_write(r->cpu,SP+2,2,0x7777);mac_cpu_set(r->cpu,MAC_A7,SP);return mac_text_state_service(r,op);}
static unsigned fieldbyte(MacRuntime*r,unsigned off){return mac_cpu_ram(r->cpu)[PORT+off];}
/* Every byte of the record other than [lo,hi) still holds the sentinel. */
static int only(MacRuntime*r,unsigned lo,unsigned hi){for(unsigned i=0;i<108;i++)if((i<lo||i>=hi)&&fieldbyte(r,i)!=0x5a)return 0;return 1;}
int main(void){
    MacRuntime r=make();
    CHECK(mac_text_state_service(&r,0xa886)==0 && mac_text_state_service(&r,0xa88b)==0);   /* neighbours are not ours */
    /* TextFont: integer at +68. */
    CHECK(call(&r,0xa887,0x0014)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2 && mac_cpu_read(r.cpu,PORT+68,2)==0x14 && only(&r,68,70));
    CHECK(call(&r,0xa887,0x8000)==1 && mac_cpu_read(r.cpu,PORT+68,2)==0x8000 && only(&r,68,70));
    /* TextFace: a Style byte, taken from the high half of the stack word, at +70 (the pad byte at +71 is not touched). */
    CHECK(call(&r,0xa888,0x0100)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2 && fieldbyte(&r,70)==0x01 && only(&r,68,71));
    CHECK(call(&r,0xa888,0x4201)==1 && fieldbyte(&r,70)==0x42 && fieldbyte(&r,71)==0x5a);                 /* low byte ignored */
    CHECK(call(&r,0xa888,0x0000)==1 && fieldbyte(&r,70)==0 && only(&r,68,71));                           /* plain text */
    /* TextMode: integer at +72. */
    CHECK(call(&r,0xa889,0x0001)==1 && mac_cpu_read(r.cpu,PORT+72,2)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2);
    CHECK(call(&r,0xa889,0x0007)==1 && mac_cpu_read(r.cpu,PORT+72,2)==7);
    /* TextSize: integer at +74. */
    CHECK(call(&r,0xa88a,0x000c)==1 && mac_cpu_read(r.cpu,PORT+74,2)==12 && mac_cpu_get(r.cpu,MAC_A7)==SP+2);
    CHECK(call(&r,0xa88a,0x0000)==1 && mac_cpu_read(r.cpu,PORT+74,2)==0);                               /* 0 = default size */
    /* All four fields held independently, and nothing outside 68..74 changed. */
    call(&r,0xa887,3);call(&r,0xa888,0x0b00);call(&r,0xa889,2);call(&r,0xa88a,9);
    CHECK(mac_cpu_read(r.cpu,PORT+68,2)==3 && fieldbyte(&r,70)==0x0b && mac_cpu_read(r.cpu,PORT+72,2)==2 && mac_cpu_read(r.cpu,PORT+74,2)==9 && only(&r,68,76));
    CHECK(mac_cpu_read(r.cpu,SP+2,2)==0x7777);                                                        /* the caller's data above is untouched */
    /* Refusals halt by name and change nothing. */
    for(unsigned k=0;k<7;k++){
        MacRuntime q=make();uint32_t sp=SP;
        if(k==0)q.current_port=0;                                    /* no port */
        if(k==1)q.current_port=PORT+1;                               /* odd port */
        if(k==2)q.current_port=(uint32_t)mac_cpu_ram_size(q.cpu)-70; /* the text fields would run off RAM */
        if(k==3)sp=SP+1;                                             /* odd stack */
        if(k==4)sp=(uint32_t)mac_cpu_ram_size(q.cpu);                /* no room for the argument */
        if(k==5)sp=0x100;                                            /* low memory */
        if(k==6)mac_cpu_halt(q.cpu,"stopped");
        mac_cpu_set(q.cpu,MAC_A7,sp);
        CHECK(mac_text_state_service(&q,0xa887)==-1 && mac_cpu_halted(q.cpu));
        {const uint8_t*m=mac_cpu_ram(q.cpu);int same=1;for(unsigned i=0;i<108;i++)if(m[PORT+i]!=0x5a)same=0;CHECK(same);}   /* the real port record is untouched */
        mac_cpu_destroy(q.cpu);
    }
    mac_cpu_destroy(r.cpu);
    puts("Text state checks passed");return 0;
}
