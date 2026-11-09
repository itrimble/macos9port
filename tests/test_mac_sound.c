#include "mac_sound_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x100000;r.heap_limit=0x200000;return r;}
static void destroy(MacRuntime*r){mac_sound_destroy(r);mac_cpu_destroy(r->cpu);}
static uint32_t create(MacRuntime*r,uint32_t callback){
 wr(r,SP,4,callback);wr(r,SP+4,4,0x8c);wr(r,SP+8,2,5);wr(r,SP+10,4,0x7000);mac_cpu_set(r->cpu,MAC_A7,SP);
 CHECK(mac_sound_service(r,0xa807)==1 && rd(r,SP+14,2)==0 && mac_cpu_get(r->cpu,MAC_A7)==SP+14);return rd(r,0x7000,4);
}
static int command(MacRuntime*r,uint32_t chan,unsigned cmd,unsigned one,uint32_t two,int immediate){
 wr(r,0x4000,2,cmd);wr(r,0x4002,2,one);wr(r,0x4004,4,two);mac_cpu_set(r->cpu,MAC_A7,SP);
 if(immediate){wr(r,SP,4,0x4000);wr(r,SP+4,4,chan);}else{wr(r,SP,2,0x01aa);wr(r,SP+2,4,0x4000);wr(r,SP+6,4,chan);}
 return mac_sound_service(r,immediate?0xa804:0xa803);
}
static void sample(MacRuntime*r){wr(r,0x4100,4,0);wr(r,0x4104,4,3);wr(r,0x4108,4,24000u<<16);wr(r,0x4114,1,0);uint8_t b[3]={0,128,255};memcpy(mac_cpu_ram(r->cpu)+0x4116,b,3);}
static void audio_callback(void){
 MacRuntime r=fresh();uint32_t chan=create(&r,0x5000);CHECK(chan && rd(&r,chan+8,4)==0x5000 && rd(&r,chan+30,2)==128);wr(&r,chan+12,4,0x12345678);
 uint8_t callback[]={0x20,0x6f,0,8,0x20,0x28,0,12,0x22,0x6f,0,4,0x22,0x29,0,4,0x23,0xc0,0,0,0x60,0,0x23,0xc1,0,0,0x60,4,0x20,0x5f,0x50,0x8f,0x4e,0xd0};
 memcpy(mac_cpu_ram(r.cpu)+0x5000,callback,sizeof callback);sample(&r);
 CHECK(command(&r,chan,81,0,0x4100,0)==1);CHECK(command(&r,chan,13,7,0xabcdef01,0)==1);
 int16_t pcm[16];CHECK(mac_sound_render(&r,pcm,6)==6);
 int16_t expected[]={-32768,-32768,0,0,32512,32512};for(unsigned i=0;i<6;i++)CHECK(pcm[2*i]==expected[i] && pcm[2*i+1]==expected[i]);
 CHECK(rd(&r,0x6000,4)==0 && mac_sound_poll(&r,10000)==0);CHECK(mac_sound_render(&r,pcm,1)==1 && pcm[0]==0);
 mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,0x5500);mac_cpu_set(r.cpu,MAC_D0,0x55555555);mac_cpu_set(r.cpu,MAC_A5,0x800000);
 uint32_t regs[18];for(unsigned i=0;i<18;i++)regs[i]=mac_cpu_get(r.cpu,(MacCpuReg)i);
 CHECK(mac_sound_poll(&r,10000)>0 && !mac_cpu_halted(r.cpu));CHECK(rd(&r,0x6000,4)==0x12345678 && rd(&r,0x6004,4)==0xabcdef01);
 for(unsigned i=0;i<18;i++)CHECK(mac_cpu_get(r.cpu,(MacCpuReg)i)==regs[i]);
 CHECK(command(&r,chan,43,128,0,1)==1 && command(&r,chan,81,0,0x4100,1)==1);CHECK(mac_sound_render(&r,pcm,1)==1 && pcm[0]==-16448);
 CHECK(command(&r,chan,3,0,0,1)==1);CHECK(mac_sound_render(&r,pcm,1)==1 && pcm[0]==0);
 wr(&r,SP,2,0x01aa);wr(&r,SP+2,4,chan);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_sound_service(&r,0xa801)==1 && rd(&r,SP+6,2)==0);CHECK(rd(&r,chan+30,2)==0);destroy(&r);
}
static void failures(void){
 MacRuntime r=fresh();uint32_t chan=create(&r,0);for(unsigned i=0;i<128;i++)CHECK(command(&r,chan,0,0,0,0)==1);
 CHECK(command(&r,chan,0,0,0,0)==1 && (int16_t)rd(&r,SP+10,2)==-203);CHECK(r.sound->channels[0].count==128);
 CHECK(command(&r,chan,4,0,0,1)==1 && r.sound->channels[0].count==0);
 CHECK(command(&r,chan,4,0,0,0)==1);CHECK(command(&r,chan,0,0,0,0)==1);int16_t pcm[2];CHECK(mac_sound_render(&r,pcm,1)==1 && r.sound->channels[0].count==0);sample(&r);wr(&r,0x4100,4,MAC_RUNTIME_RAM-1);
 CHECK(command(&r,chan,81,0,0x4100,1)==-1 && !r.sound->channels[0].playing);destroy(&r);
 r=fresh();chan=create(&r,0x5000);wr(&r,0x5000,2,0x60fe);CHECK(command(&r,chan,13,0,0,1)==1);CHECK(mac_sound_poll(&r,10000)==10000 && mac_cpu_halted(r.cpu));destroy(&r);
}
static void supplied(void){
 MacRuntime r=fresh();wr(&r,0x7000,4,0x20000);wr(&r,0x2001e,2,1500);wr(&r,0x2000c,4,0x12345678);
 uint32_t chan=create(&r,0);CHECK(chan==0x20000 && !r.sound->channels[0].owned && r.sound->channels[0].capacity==1500);
 for(unsigned i=0;i<1500;i++)CHECK(command(&r,chan,0,0,0,0)==1);
 CHECK(command(&r,chan,0,0,0,0)==1 && (int16_t)rd(&r,SP+10,2)==-203);
 CHECK(rd(&r,chan+12,4)==0x12345678);uint8_t saved[12036];memcpy(saved,mac_cpu_ram(r.cpu)+chan,sizeof saved);
 wr(&r,SP,2,0x0100);wr(&r,SP+2,4,chan);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_sound_service(&r,0xa801)==1);
 CHECK(!memcmp(saved,mac_cpu_ram(r.cpu)+chan,sizeof saved));destroy(&r);
 r=fresh();wr(&r,0x7000,4,MAC_RUNTIME_RAM-36);wr(&r,MAC_RUNTIME_RAM-6,2,1500);
 wr(&r,SP,4,0);wr(&r,SP+4,4,0x8c);wr(&r,SP+8,2,5);wr(&r,SP+10,4,0x7000);mac_cpu_set(r.cpu,MAC_A7,SP);
 CHECK(mac_sound_service(&r,0xa807)==-1 && !r.sound);destroy(&r);
}
/* SndNewChannel init options: sampled synth only, mono (or unspecified), noInterp/noDrop and the sample-rate field accepted. */
static void init_options(void){
 static const struct{unsigned synth;uint32_t init;int ok;}cases[]={
  {5,0x8c,1},{5,0x80,1},{5,0x00,1},{5,0x84,1},{5,0x88,1},
  {5,0xac,1},{5,0x9c,1},{5,0xbc,1},{5,0xa0,1},{5,0x90,1},{5,0x30,1},{5,0x20,1},   /* sample-rate field values */
  {5,0xc0,0},{5,0x40,0},{5,0xcc,0},{5,0x02,0},{5,0x03,0},{5,0x01,0},{5,0x100,0},{5,0x300,0},{5,0x8000,0},{5,0xff00,0},{5,0x1ac,0},
  {1,0x8c,0},{3,0x8c,0},{4,0x8c,0},{0,0x8c,0},{6,0x8c,0},
 };
 for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
  MacRuntime r=fresh();wr(&r,SP,4,0);wr(&r,SP+4,4,cases[i].init);wr(&r,SP+8,2,cases[i].synth);wr(&r,SP+10,4,0x7000);mac_cpu_set(r.cpu,MAC_A7,SP);
  int rc=mac_sound_service(&r,0xa807);
  if(cases[i].ok)CHECK(rc==1 && r.sound && !mac_cpu_halted(r.cpu));else CHECK(rc==-1 && mac_cpu_halted(r.cpu) && !r.sound);
  destroy(&r);
 }
}
static void notes(void){
 MacRuntime r=fresh();uint32_t chan=create(&r,0);sample(&r);wr(&r,0x410c,4,1);wr(&r,0x4110,4,3);wr(&r,0x4115,1,60);
 CHECK(command(&r,chan,80,0,0x4100,1)==1 && !r.sound->channels[0].playing);
 int16_t pcm[128];CHECK(mac_sound_render(&r,pcm,1)==1 && pcm[0]==0);
 CHECK(command(&r,chan,42,0,72,1)==1);CHECK(command(&r,chan,80,0,0x4100,1)==1 && r.sound->channels[0].playing);CHECK(mac_sound_render(&r,pcm,7)==7);
 int16_t expect[7]={-32768,0,32512,0,32512,0,32512};for(unsigned i=0;i<7;i++)CHECK(pcm[2*i]==expect[i]);
 CHECK(command(&r,chan,3,0,0,1)==1);
 CHECK(command(&r,chan,42,0,0x8f000048,1)==1);CHECK(mac_sound_render(&r,pcm,1)==1 && pcm[0]==-18375 && r.sound->channels[0].amp==143);
 CHECK(command(&r,chan,3,0,0,1)==1);CHECK(command(&r,chan,43,255,0,1)==1);
 CHECK(command(&r,chan,40,1,60,0)==1);CHECK(command(&r,chan,3,0,0,0)==1);
 CHECK(mac_sound_render(&r,pcm,24)==24 && r.sound->channels[0].playing && r.sound->channels[0].wait_frames==0);
 CHECK(mac_sound_render(&r,pcm,1)==1 && !r.sound->channels[0].playing && pcm[0]==0);
 CHECK(command(&r,chan,42,0,60,1)==1);CHECK(command(&r,chan,11,0,0,1)==1);CHECK(command(&r,chan,3,0,0,0)==1);
 CHECK(mac_sound_render(&r,pcm,1)==1 && r.sound->channels[0].playing);
 CHECK(command(&r,chan,12,0,0,1)==1);CHECK(mac_sound_render(&r,pcm,1)==1 && !r.sound->channels[0].playing);
 CHECK(command(&r,chan,40,1,60,0)==1);CHECK(mac_sound_render(&r,pcm,48)==48);
 CHECK(r.sound->channels[0].playing && !r.sound->channels[0].wait_frames); /* quiet is required after duration. */
 CHECK(command(&r,chan,3,0,0,1)==1);
 CHECK(command(&r,chan,42,0,127,1)==-1 && !r.sound->channels[0].playing);destroy(&r);
 r=fresh();chan=create(&r,0);sample(&r);wr(&r,0x4115,1,60);wr(&r,0x4110,4,4);
 CHECK(command(&r,chan,80,0,0x4100,1)==-1 && !r.sound->channels[0].voice);destroy(&r);
}
static void loop_boundaries(void){
 MacRuntime r=fresh();uint32_t chan=create(&r,0);sample(&r);wr(&r,0x4104,4,5);wr(&r,0x4108,4,48000u<<16);
 wr(&r,0x410c,4,1);wr(&r,0x4110,4,4);wr(&r,0x4115,1,60);
 for(unsigned i=0;i<5;i++)wr(&r,0x4116+i,1,128+i);wr(&r,0x411b,1,255);
 CHECK(command(&r,chan,80,0,0x4100,1)==1 && command(&r,chan,42,0,60,1)==1);
 int16_t pcm[24];CHECK(mac_sound_render(&r,pcm,9)==9);
 int expected[]={0,256,512,768,1024,256,512,768,256};for(unsigned i=0;i<9;i++)CHECK(pcm[i*2]==expected[i]);
 CHECK(command(&r,chan,3,0,0,1)==1);wr(&r,0x410c,4,0);wr(&r,0x4110,4,1);
 CHECK(command(&r,chan,80,0,0x4100,1)==1 && command(&r,chan,42,0,60,1)==1);
 CHECK(mac_sound_render(&r,pcm,9)==9);for(unsigned i=5;i<9;i++)CHECK(pcm[i*2]==0);
 CHECK(command(&r,chan,3,0,0,1)==1);wr(&r,0x410c,4,2);wr(&r,0x4110,4,2);
 CHECK(command(&r,chan,80,0,0x4100,1)==-1 && r.sound->channels[0].voice_loop_end==1);destroy(&r);
}
int main(void){audio_callback();failures();supplied();init_options();notes();loop_boundaries();puts("mac sound PCM, queue, callback, and bounds tests passed");return 0;}
