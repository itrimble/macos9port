#include "mac_sound_int.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

static uint64_t note_trace_count;

static void trace_note(MacRuntime *r, MacSoundChannel *c, MacSoundCommand cmd,
                       unsigned note, unsigned amplitude) {
 if (!getenv("OSPI_SOUND_NOTE_TRACE") || note_trace_count >= 4096) return;
 unsigned slot=(unsigned)(c-r->sound->channels);
 fprintf(stderr,"sound-note guest-us=%llu event=%llu channel=%u command=%u duration-half-ms=%u note=%u amplitude=%u playing=%d phase=%llu rate=%llu wait=%u queued=%u base=%u length=%u loop=%u..%u init=%x\n",
         (unsigned long long)r->clock_us,(unsigned long long)note_trace_count++,
         slot,cmd.command,cmd.param1,note,amplitude,
         c->playing,(unsigned long long)c->phase,(unsigned long long)c->rate,
         c->wait_frames,c->count,c->base_note,c->length,c->loop_start,c->loop_end,c->init);
}
static void trace_voice(MacRuntime *r, MacSoundChannel *c, unsigned command) {
 if (!getenv("OSPI_SOUND_NOTE_TRACE") || note_trace_count >= 4096) return;
 fprintf(stderr,"sound-voice guest-us=%llu event=%llu channel=%u command=%u data=%08x length=%u rate16=%08x loop=%u..%u base=%u init=%x\n",
         (unsigned long long)r->clock_us,(unsigned long long)note_trace_count++,
         (unsigned)(c-r->sound->channels),command,
         command==80?c->voice_data:c->data,command==80?c->voice_length:c->length,
         command==80?c->voice_rate:c->source_rate,
         command==80?c->voice_loop_start:c->loop_start,command==80?c->voice_loop_end:c->loop_end,
         command==80?c->voice_base:c->base_note,c->init);
 uint32_t data=command==80?c->voice_data:c->data,length=command==80?c->voice_length:c->length;
 if(length<=32 && mac_sound_range(r,data,length,0)){
  fprintf(stderr,"sound-voice-bytes");
  for(uint32_t i=0;i<length;i++)fprintf(stderr," %02x",mac_cpu_ram(r->cpu)[data+i]);
  fputc('\n',stderr);
 }
}
/* Independent sampled Sound Manager ABI: public Inside Macintosh Sound
 * Sound-73.html, Sound-74.html, Sound-97.html, Sound-100.html, Sound-131.html
 * at leopard-adc.pepas.com/documentation/mac/Sound/. No Apple implementation. */
int mac_sound_fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
int mac_sound_range(MacRuntime*r,uint32_t p,size_t n,int aligned){size_t z=mac_cpu_ram_size(r->cpu);return p && (!aligned || !(p&1)) && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static int finish(MacRuntime*r,uint32_t sp,unsigned args,int error){wr(r,sp+args,2,(uint16_t)error);mac_cpu_set(r->cpu,MAC_A7,sp+args);return 1;}
static MacSoundChannel *channel(MacRuntime*r,uint32_t p){
 if(!r->sound || !mac_sound_range(r,p,36,1))return NULL;
 for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++)if(r->sound->channels[i].address==p && !r->sound->channels[i].closing){
  if(!r->sound->channels[i].owned)return &r->sound->channels[i];
  for(size_t j=0;j<r->handle_count;j++)if(r->handles[j].pointer==p && !r->handles[j].handle && r->handles[j].type==MAC_SOUND_OWNED && r->handles[j].size==MAC_SOUND_RECORD)return &r->sound->channels[i];
 }
 return NULL;
}
void mac_sound_queue_fields(MacRuntime*r,MacSoundChannel*c){wr(r,c->address+32,2,c->head);wr(r,c->address+34,2,(c->head+c->count)%c->capacity);}
static void links(MacRuntime*r){uint32_t previous=0;for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++){
 uint32_t p=r->sound->channels[i].address;if(!p || r->sound->channels[i].closing)continue;if(previous)wr(r,previous,4,p);wr(r,p,4,0);previous=p;
}}
/* Logical close happens synchronously; storage borrowed by a guest callback
 * remains valid until its Pascal return. No allocation or CPU reentry occurs. */
void mac_sound_reap(MacRuntime*r){
 if(!r->sound || r->sound->callback_active)return;
 for(unsigned slot=0;slot<MAC_SOUND_CHANNELS;slot++){
  MacSoundChannel*c=&r->sound->channels[slot];if(!c->closing)continue;
  uint32_t p=c->address;
  if(c->owned){
   for(size_t i=0;i<r->handle_count;i++)if(r->handles[i].pointer==p && r->handles[i].type==MAC_SOUND_OWNED)memset(&r->handles[i],0,sizeof r->handles[i]);
   memset(mac_cpu_ram(r->cpu)+p,0,MAC_SOUND_RECORD);
  }
  memset(c,0,sizeof *c);
 }
}
static int new_channel(MacRuntime*r,uint32_t sp){
 uint32_t callback=rd(r,sp,4),init=rd(r,sp+4,4),out=rd(r,sp+10,4);unsigned synth=rd(r,sp+8,2);
 if(!mac_sound_range(r,out,4,1) || (callback && !mac_sound_range(r,callback,2,1)))return mac_sound_fail(r,"Sound channel output or callback address is invalid.");
 if(r->sound && r->sound->callback_active)return mac_sound_fail(r,"Sound channel allocation at callback interrupt time is prohibited.");
 uint32_t supplied=rd(r,out,4);unsigned capacity=128;
 if(supplied){
  if(!mac_sound_range(r,supplied,36,1))return mac_sound_fail(r,"Supplied sound channel header exceeds RAM.");
  capacity=rd(r,supplied+30,2);
  if(!capacity || capacity>MAC_SOUND_QUEUE || !mac_sound_range(r,supplied,36+capacity*8,1))return mac_sound_fail(r,"Supplied sound queue capacity exceeds its bounded RAM span.");
  if(r->sound)for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++){
   MacSoundChannel *other=&r->sound->channels[i];
   if(other->address && supplied<other->address+36+other->capacity*8 && other->address<supplied+36+capacity*8)return mac_sound_fail(r,"Supplied sound channel overlaps an active channel.");
  }
 }
 if(synth!=5 || (init&~0x8cu) || ((init&0xc0)!=0 && (init&0xc0)!=0x80))return mac_sound_fail(r,"Only sampled mono channels with explicit mono and optional noInterp/noDrop are implemented.");
 if(!r->sound){r->sound=calloc(1,sizeof *r->sound);if(!r->sound)return finish(r,sp,14,-108);}
 unsigned slot=0,handle=0;while(slot<MAC_SOUND_CHANNELS && r->sound->channels[slot].address)slot++;
 while(handle<MAC_RUNTIME_HANDLES && (r->handles[handle].handle || r->handles[handle].pointer))handle++;
 if(slot==MAC_SOUND_CHANNELS || (!supplied && handle==MAC_RUNTIME_HANDLES))return finish(r,sp,14,-108);
 uint32_t p=supplied?supplied:mac_runtime_alloc(r,MAC_SOUND_RECORD);if(!p)return finish(r,sp,14,-108);
 if(!supplied)memset(mac_cpu_ram(r->cpu)+p,0,MAC_SOUND_RECORD);
 wr(r,p+4,4,0);wr(r,p+8,4,callback);wr(r,p+16,4,0);wr(r,p+28,2,0);wr(r,p+30,2,capacity);wr(r,p+32,2,0);wr(r,p+34,2,0);
 if(!supplied){r->handles[handle]=(MacHandle){.pointer=p,.size=MAC_SOUND_RECORD,.type=MAC_SOUND_OWNED};if(r->handle_count<=handle)r->handle_count=handle+1;}
 r->sound->channels[slot]=(MacSoundChannel){.address=p,.init=init,.capacity=capacity,.owned=!supplied,.amp=255,.left=256,.right=256};links(r);wr(r,out,4,p);return finish(r,sp,14,0);
}
int mac_sound_command(MacRuntime*r,MacSoundChannel*c,MacSoundCommand cmd){
 uint32_t p=cmd.param2;
 if((c->double_buffer.active || c->double_buffer.pending) && cmd.command!=0 && cmd.command!=3 &&
    cmd.command!=4 && cmd.command!=43 && cmd.command!=45 && cmd.command!=46 && cmd.command!=47)
  return mac_sound_fail(r,"This immediate command is unsupported during double-buffer playback.");
 switch(cmd.command){
 case 0:return 1;
 case 3:mac_sound_double_stop(r,(unsigned)(c-r->sound->channels));c->playing=0;c->buffer_block=0;return 1;
 case 10:c->wait_frames=(uint32_t)cmd.param1*24;return 1;
 case 41:c->playing=0;c->buffer_block=0;c->wait_frames=(uint32_t)cmd.param1*24;return 1;
 case 4:c->count=0;mac_sound_queue_fields(r,c);return 1;
 case 11:c->paused=1;return 1;
 case 12:c->paused=0;return 1;
 case 13:
  if(c->callback_pending)return mac_sound_fail(r,"A sound callback is already pending.");
  wr(r,c->address+20,2,cmd.command);wr(r,c->address+22,2,cmd.param1);wr(r,c->address+24,4,p);
  c->callback_pending=1;return 1;
 case 40: case 42:{
  /* Legacy noteCmd packs optional amplitude in bits31..24. Public API
   * example: Jim Reekes, Apr9 1992, comp.sys.mac.programmer/oirtJekZKTQ.
   * Untagged MIDI commands retain the separately configured amplitude. */
  unsigned note=p&255, amplitude=p>>24;
  if((p&0x00ffff00u) || note>127)return mac_sound_fail(r,"Sampled note contains unsupported frequency bits.");
  if(c->voice){
   int restart=cmd.command==40 || !c->playing;
   uint32_t source=restart?c->voice_rate:c->source_rate;
   unsigned base=restart?c->voice_base:c->base_note;
   double rate=(double)source*exp2(((double)note-base)/12.0);
   if(rate<1 || rate>((double)MAC_SOUND_RATE*65536.0*16.0)){char reason[192];snprintf(reason,sizeof reason,"Note conversion unsupported: note=%u base=%u source16.16=%08x derivedHz=%.6f init=%08x.",note,base,source,rate/65536.0,c->init);return mac_sound_fail(r,reason);}
   if(restart){
    c->data=c->voice_data;c->length=c->voice_length;c->source_rate=c->voice_rate;
    c->base_note=c->voice_base;c->loop_start=c->voice_loop_start;c->loop_end=c->voice_loop_end;
    c->phase=0;c->looping=0;
   }
   c->rate=(uint64_t)llround(rate);c->playing=1;c->buffer_block=0;c->note_playback=1;
   trace_note(r,c,cmd,note,amplitude?amplitude:c->amp);
  }
  if(amplitude)c->amp=amplitude;
  if(cmd.command==40)c->wait_frames=(uint32_t)cmd.param1*24;
  return 1;
 }
 case 43:if(cmd.param1>255)return mac_sound_fail(r,"Sound amplitude exceeds255.");c->amp=cmd.param1;return 1;
 case 45:if(!mac_sound_range(r,p,2,1))return mac_sound_fail(r,"GetAmp output is invalid.");wr(r,p,2,c->amp);return 1;
 case 46:if((p&65535)>512 || (p>>16)>512)return mac_sound_fail(r,"Sound volume exceeds supported2x gain.");c->left=p&65535;c->right=p>>16;return 1;
 case 47:if(!mac_sound_range(r,p,4,1))return mac_sound_fail(r,"GetVolume output is invalid.");wr(r,p,4,c->left|(c->right<<16));return 1;
 case 80: case 81:{
  if(!mac_sound_range(r,p,22,1))return mac_sound_fail(r,"SoundHeader exceeds guest RAM.");
  uint32_t data=rd(r,p,4),length=rd(r,p+4,4),rate=rd(r,p+8,4);if(!data)data=p+22;
  if(rd(r,p+20,1)!=0)return mac_sound_fail(r,"Compressed and extended sound headers are not implemented.");
  if(!length || length>4u*1024u*1024u || !mac_sound_range(r,data,length,0) || !rate)
   return mac_sound_fail(r,"Sound sample data or rate exceeds supported mono PCM range.");
  /* Legacy sampled voices use half-open loop boundaries. Inside Macintosh
   * Volume V, V-493/V-495: length3000/end2200 leaves800 decay samples.
   * Accept the exact end boundary; never clamp or read sample[length]. */
  uint32_t start=rd(r,p+12,4),end=rd(r,p+16,4);unsigned base=rd(r,p+21,1);
  if(cmd.command==80 && (base<1 || base>127 || ((start || end) && (start>=end || end>length)))){char reason[180];snprintf(reason,sizeof reason,"Invalid voice header%08x length%u loop%u..%u base%u.",p,length,start,end,base);return mac_sound_fail(r,reason); }
  if(cmd.command==80){
   c->voice_data=data;c->voice_length=length;c->voice_rate=rate;c->voice_base=base;
   c->voice_loop_start=start;c->voice_loop_end=end;c->voice=1;trace_voice(r,c,80);return 1;
  }
  c->data=data;c->length=length;c->rate=rate;c->source_rate=rate;c->phase=0;c->looping=0;
  c->note_playback=0;c->base_note=base;c->loop_start=start;c->loop_end=end;
  c->playing=cmd.command==81;c->buffer_block=c->playing;trace_voice(r,c,81);return 1;
 }
 default:{char reason[128];snprintf(reason,sizeof reason,"Unsupported sampled Sound Manager command %u (param1=%u param2=%08x).",cmd.command,cmd.param1,cmd.param2);return mac_sound_fail(r,reason); }
 }
}
int mac_sound_service(MacRuntime*r,uint16_t opcode){
 if(opcode!=0xa807 && opcode!=0xa801 && opcode!=0xa803 && opcode!=0xa804)return 0;
 if(mac_cpu_halted(r->cpu))return -1;
 unsigned args=opcode==0xa807?14:opcode==0xa801?6:opcode==0xa803?10:8;uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
 if(!mac_sound_range(r,sp,args+2,1))return mac_sound_fail(r,"Sound call stack exceeds guest RAM.");
 if(opcode==0xa807)return new_channel(r,sp);
 uint32_t p=rd(r,sp+(opcode==0xa801?2:opcode==0xa803?6:4),4);MacSoundChannel*c=channel(r,p);
 if(!c)return finish(r,sp,args,-205);
 if(opcode==0xa801){
  if(!rd(r,sp,1) && (c->playing || c->count || c->callback_pending || c->wait_frames || c->paused || c->buffer_block || c->double_buffer.active || c->double_buffer.pending))return mac_sound_fail(r,"Synchronous sound disposal requires an unimplemented wait continuation.");
  /* Sound-98 discourages interrupt disposal because it can free memory.
   * We support immediate logical closure with bounded deferred reclamation. */
  c->closing=1;c->playing=0;c->count=0;c->wait_frames=0;c->callback_pending=0;c->buffer_block=0;
  mac_sound_double_stop(r,(unsigned)(c-r->sound->channels));links(r);
  int result=finish(r,sp,args,0);mac_sound_reap(r);return result;
 }
 uint32_t command=rd(r,sp+(opcode==0xa803?2:0),4);
 if(!mac_sound_range(r,command,8,1))return mac_sound_fail(r,"Sound command record exceeds guest RAM.");
 MacSoundCommand cmd={(uint16_t)rd(r,command,2),(uint16_t)rd(r,command+2,2),rd(r,command+4,4)};
 if(opcode==0xa804){if(mac_sound_command(r,c,cmd)<0)return -1;}
 else {
  if(c->count==c->capacity){if(!rd(r,sp,1))return mac_sound_fail(r,"Blocking on a full sound queue requires a wait continuation.");return finish(r,sp,args,-203);}
  unsigned tail=(c->head+c->count)%c->capacity;c->queue[tail]=cmd;
  memcpy(mac_cpu_ram(r->cpu)+p+36+tail*8,mac_cpu_ram(r->cpu)+command,8);c->count++;mac_sound_queue_fields(r,c);
 }
 return finish(r,sp,args,0);
}
void mac_sound_destroy(MacRuntime*r){free(r->sound);r->sound=NULL;}
