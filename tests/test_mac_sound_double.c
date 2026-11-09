#include "mac_sound_int.h"
#include "mac_sound_double.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x9000u
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.sound=calloc(1,sizeof *r.sound);CHECK(r.sound);r.sound->channels[0].address=0x2000;r.sound->channels[0].init=4;
 wr(&r,0x3000,2,1);wr(&r,0x3002,2,8);wr(&r,0x3008,4,24000u<<16);wr(&r,0x300c,4,0x4000);wr(&r,0x3010,4,0x4100);wr(&r,0x3014,4,0x5000);
 for(unsigned p=0x4000;p<=0x4100;p+=0x100){wr(&r,p,4,2);wr(&r,p+4,4,1);wr(&r,p+16,1,0);wr(&r,p+17,1,255);}
 uint8_t code[]={0x20,0x6f,0,4,0x22,0x6f,0,8,0x23,0xc8,0,0,0x60,0,0x23,0xc9,0,0,0x60,4,0x21,0x7c,0,0,0,5,0,4,0x20,0x5f,0x50,0x8f,0x4e,0xd0};
 memcpy(mac_cpu_ram(r.cpu)+0x5000,code,sizeof code);return r;}
static int start(MacRuntime*r){wr(r,SP,4,0x3000);wr(r,SP+4,4,0x2000);wr(r,SP+8,2,0xbeef);mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_D0,0x00200008);return mac_sound_double_service(r,0xa800);}
static void done(MacRuntime*r){mac_sound_destroy(r);mac_cpu_destroy(r->cpu);}
static void playback(void){MacRuntime r=fresh();CHECK(start(&r)==1 && rd(&r,SP+8,2)==0 && mac_cpu_get(r.cpu,MAC_A7)==SP+8);int v;int exp[]={-32768,-32768,32512,32512};
 for(unsigned i=0;i<4;i++){CHECK(mac_sound_double_sample(&r,0,&v)==1);CHECK(v==exp[i]);}CHECK(rd(&r,0x4004,4)==0 && rd(&r,0x6000,4)==0);
 mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,0x7000);uint32_t saved[18];for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r.cpu,(MacCpuReg)i);
 CHECK(mac_sound_double_poll(&r,9999)==0);CHECK(mac_sound_double_poll(&r,10000)>0 && !mac_cpu_halted(r.cpu));CHECK(rd(&r,0x6000,4)==0x4000 && rd(&r,0x6004,4)==0x2000 && rd(&r,0x4004,4)==5);
 for(unsigned i=0;i<18;i++)CHECK(saved[i]==mac_cpu_get(r.cpu,(MacCpuReg)i));
 for(unsigned i=0;i<4;i++){CHECK(mac_sound_double_sample(&r,0,&v)==1 && v==exp[i]);}CHECK(mac_sound_double_poll(&r,10000)>0);
 for(unsigned i=0;i<4;i++){CHECK(mac_sound_double_sample(&r,0,&v)==1 && v==exp[i]);}CHECK(!r.sound->channels[0].double_buffer.active && rd(&r,0x4004,4)==4);CHECK(mac_sound_double_sample(&r,0,&v)==0);CHECK(mac_sound_double_poll(&r,10000)>0);CHECK(!r.sound->channels[0].double_buffer.active);done(&r);}
/* The interrupted code has an odd A7: the callback still runs and everything comes back, including the odd A7. */
static void odd_stack(void){MacRuntime r=fresh();CHECK(start(&r)==1);int v;for(unsigned i=0;i<4;i++)CHECK(mac_sound_double_sample(&r,0,&v)==1);
 mac_cpu_set(r.cpu,MAC_A7,SP+1);mac_cpu_set(r.cpu,MAC_PC,0x7000);uint32_t saved[18];for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r.cpu,(MacCpuReg)i);
 CHECK(mac_sound_double_poll(&r,10000)>0 && !mac_cpu_halted(r.cpu) && rd(&r,0x6000,4)==0x4000);
 for(unsigned i=0;i<18;i++)CHECK(saved[i]==mac_cpu_get(r.cpu,(MacCpuReg)i));done(&r);}
static void waiting(void){MacRuntime r=fresh();CHECK(start(&r)==1);int v;for(unsigned i=0;i<8;i++)CHECK(mac_sound_double_sample(&r,0,&v)==1);CHECK(r.sound->channels[0].double_buffer.pending==3);CHECK(mac_sound_double_sample(&r,0,&v)==1 && v==0);CHECK(mac_sound_double_poll(&r,10000)>0 && rd(&r,0x4104,4)==5);done(&r);}
static void long_callback(void){MacRuntime r=fresh();CHECK(start(&r)==1);int v;for(unsigned i=0;i<4;i++)CHECK(mac_sound_double_sample(&r,0,&v)==1);uint8_t code[35];memcpy(code,mac_cpu_ram(r.cpu)+0x5000,sizeof code);memcpy(mac_cpu_ram(r.cpu)+0x10000,code,sizeof code);memset(mac_cpu_ram(r.cpu)+0x5000,0x4e,24000);for(unsigned i=0;i<24000;i+=2)mac_cpu_ram(r.cpu)[0x5000+i+1]=0x71;wr(&r,0x5000+24000,2,0x4ef9);wr(&r,0x5000+24002,4,0x10000);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,0x7000);size_t ran=mac_sound_double_poll(&r,100000);if(ran<=10000 || mac_cpu_halted(r.cpu))fprintf(stderr,"long callback ran=%zu halted=%d reason=%s\n",ran,mac_cpu_halted(r.cpu),mac_cpu_error(r.cpu));CHECK(ran>10000 && !mac_cpu_halted(r.cpu));done(&r);}
static void invalid(void){for(unsigned test=0;test<5;test++){MacRuntime r=fresh();if(test==0)wr(&r,0x3002,2,16);if(test==1)wr(&r,0x4000,4,0xffffffff);if(test==2)wr(&r,0x3010,4,0x4000);if(test==3)wr(&r,0x3014,4,0);if(test==4)wr(&r,0x4104,4,0);CHECK(start(&r)==-1 && !r.sound->channels[0].double_buffer.active);CHECK(mac_cpu_ram(r.cpu)[SP+8]==0xbe);done(&r);}
 MacRuntime r=fresh();CHECK(start(&r)==1);int v;for(unsigned i=0;i<4;i++)mac_sound_double_sample(&r,0,&v);wr(&r,0x5000,2,0x60fe);CHECK(mac_sound_double_poll(&r,10000)==10000 && mac_cpu_halted(r.cpu));done(&r);}
/* A callback that returns with a bare RTS (C calling convention) leaves its two arguments on the stack: still a clean return.
 * Any other stack depth at the return stub is refused. */
static void calling_conventions(void){
 for(unsigned k=0;k<4;k++){MacRuntime r=fresh();CHECK(start(&r)==1);int v;for(unsigned i=0;i<4;i++)CHECK(mac_sound_double_sample(&r,0,&v)==1);
  static const uint8_t c_style[]={0x4e,0x75};                        /* rts */
  static const uint8_t pops_extra[]={0x58,0x8f,0x4e,0x75};          /* addq.l #4,sp ; rts : A7 == sp-4 */
  static const uint8_t pops_twelve[]={0x20,0x5f,0x4f,0xef,0,4,0x4e,0xd0}; /* movea.l (sp)+,a0 ; lea 4(sp),sp ; jmp (a0) : A7 == sp+4-... */
  memset(mac_cpu_ram(r.cpu)+0x5000,0,64);
  if(k==0)memcpy(mac_cpu_ram(r.cpu)+0x5000,c_style,sizeof c_style);
  if(k==1)memcpy(mac_cpu_ram(r.cpu)+0x5000,pops_extra,sizeof pops_extra);
  if(k==2)memcpy(mac_cpu_ram(r.cpu)+0x5000,pops_twelve,sizeof pops_twelve);
  if(k==3){static const uint8_t pascal_style[]={0x20,0x5f,0x50,0x8f,0x4e,0xd0};memcpy(mac_cpu_ram(r.cpu)+0x5000,pascal_style,sizeof pascal_style);}
  mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,0x7000);uint32_t saved[18];for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r.cpu,(MacCpuReg)i);
  size_t ran=mac_sound_double_poll(&r,10000);
  if(k==0||k==3){CHECK(ran>0 && !mac_cpu_halted(r.cpu));for(unsigned i=0;i<18;i++)CHECK(saved[i]==mac_cpu_get(r.cpu,(MacCpuReg)i));CHECK(r.sound->channels[0].double_buffer.pending==0);}
  else CHECK(mac_cpu_halted(r.cpu));
  done(&r);}
}
static void interpolation(void){MacRuntime r=fresh();r.sound->channels[0].init=0;CHECK(start(&r)==1);int v;CHECK(mac_sound_double_sample(&r,0,&v)==1 && v==-32768);CHECK(mac_sound_double_sample(&r,0,&v)==1 && v==-128);mac_sound_double_stop(&r,0);CHECK(!r.sound->channels[0].double_buffer.active && !r.sound->channels[0].double_buffer.pending);done(&r);}
/* SndPlayDoubleBuffer after instantaneous commands starts; on a channel that is really busy it still halts. */
static void queued_commands(void) {
    static const struct { MacSoundCommand cmd; int ok; } cases[] = {
        { {43, 200, 0}, 1 },   /* ampCmd: consumed at once */
        { {3, 0, 0}, 1 },      /* quietCmd on an idle channel */
        { {0, 0, 0}, 1 },      /* nullCmd */
        { {10, 100, 0}, 0 },   /* waitCmd (duration in param1): the channel really is busy */
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        MacRuntime r = fresh(); MacSoundChannel *c = &r.sound->channels[0];
        c->capacity = 128; c->amp = 255; c->left = c->right = 256;
        c->queue[0] = cases[i].cmd; c->count = 1;
        int rc = start(&r);
        if (cases[i].ok) { CHECK(rc == 1 && c->count == 0 && c->double_buffer.active); CHECK(i != 0 || c->amp == 200); }
        else CHECK(rc == -1 && mac_cpu_halted(r.cpu) && !c->double_buffer.active);
        done(&r);
    }
    /* An instantaneous command ahead of a wait is consumed, and the wait still stops the start. */
    { MacRuntime r = fresh(); MacSoundChannel *c = &r.sound->channels[0]; c->capacity = 128; c->amp = 255;
      c->queue[0] = (MacSoundCommand){43, 77, 0}; c->queue[1] = (MacSoundCommand){10, 100, 0}; c->count = 2;
      CHECK(start(&r) == -1 && c->amp == 77 && c->count == 0 && c->wait_frames != 0); done(&r); }
    /* A queued command the runtime cannot honour halts the guest instead of being skipped. */
    { MacRuntime r = fresh(); MacSoundChannel *c = &r.sound->channels[0]; c->capacity = 128; c->amp = 255;
      c->queue[0] = (MacSoundCommand){127, 0, 0}; c->count = 1;
      CHECK(start(&r) == -1 && mac_cpu_halted(r.cpu) && !c->double_buffer.active); done(&r); }
    /* Every other busy state is refused too. */
    for (unsigned s = 0; s < 6; s++) {
        MacRuntime r = fresh(); MacSoundChannel *c = &r.sound->channels[0]; c->capacity = 128; c->amp = 255;
        if (s == 0) c->playing = 1;
        if (s == 1) c->paused = 1;
        if (s == 2) c->wait_frames = 5;
        if (s == 3) c->callback_pending = 1;
        if (s == 4) c->double_buffer.active = 1;
        if (s == 5) c->double_buffer.pending = 1;
        CHECK(start(&r) == -1 && mac_cpu_halted(r.cpu)); done(&r);
    }
}
static void mixer_and_commands(void) {
    MacRuntime r = fresh();
    MacSoundChannel *c = &r.sound->channels[0];
    c->capacity = 128; c->amp = 255; c->left = 256; c->right = 128;
    CHECK(start(&r) == 1);
    int16_t pcm[8];
    CHECK(mac_sound_render(&r, pcm, 4) == 4);
    CHECK(pcm[0] == -32768 && pcm[1] == -16384);
    CHECK(pcm[6] == 32512 && pcm[7] == 16256);
    CHECK(c->double_buffer.active && c->double_buffer.pending);
    CHECK(mac_sound_command(&r, c, (MacSoundCommand){4,0,0}) == 1);
    CHECK(c->double_buffer.active && c->double_buffer.pending);
    CHECK(mac_sound_command(&r, c, (MacSoundCommand){3,0,0}) == 1);
    CHECK(!c->double_buffer.active && !c->double_buffer.pending);
    CHECK(mac_sound_double_poll(&r, 10000) == 0);
    done(&r);
    r = fresh(); r.sound->channels[0].paused = 1;
    CHECK(start(&r) == -1 && !r.sound->channels[0].double_buffer.active);
    done(&r);
}
int main(void){odd_stack();calling_conventions();playback();waiting();long_callback();invalid();interpolation();mixer_and_commands();queued_commands();puts("doublebuffer tests passed");return 0;}
