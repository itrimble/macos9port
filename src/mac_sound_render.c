#include "mac_sound_int.h"
#include <stdlib.h>
#include <string.h>
/* CPU reference PCM mixer. Rendering is explicitly pulled by a consumer; it
 * never claims host playback or advances sound merely because CPU time passed. */
static int queued(MacRuntime*r,MacSoundChannel*c){
 for(unsigned work=0;work<MAC_SOUND_QUEUE && !c->buffer_block && !c->wait_frames && !c->paused && !c->callback_pending && c->count;work++){
  MacSoundCommand cmd=c->queue[c->head];
  c->head=(c->head+1)%c->capacity;c->count--;mac_sound_queue_fields(r,c);
  if(mac_sound_command(r,c,cmd)<0)return -1;
 }
 return 1;
}
static int16_t clamp(int value){return (int16_t)(value>32767?32767:value< -32768?-32768:value);}
size_t mac_sound_render(MacRuntime*r,int16_t *out,size_t frames){
 if(!out || frames>4096){mac_sound_fail(r,"Audio pull exceeds the4096-frame work bound.");return 0;}
 memset(out,0,frames*2*sizeof *out);if(!r->sound || mac_cpu_halted(r->cpu))return 0;
 int trace=getenv("OSPI_AUDIO_TRACE")!=NULL;
 /* Diagnostic headroom: OSPI_SOUND_MIX_SHIFT=n halves the mix n times before
  * the clamp so clipping can be compared by ear. Unset in normal runs. */
 const char *shift_env=getenv("OSPI_SOUND_MIX_SHIFT");int shift=shift_env?atoi(shift_env):0;if(shift<0||shift>4)shift=0;
 for(size_t f=0;f<frames;f++){
  int left=0,right=0;
  for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++){
   MacSoundChannel*c=&r->sound->channels[i];if(!c->address || c->closing)continue;
   int streamed=0,stream=mac_sound_double_sample(r,i,&streamed);
   if(stream<0)return f;
   if(stream){
    streamed=streamed*(int)c->amp/255;
    left+=streamed*(int)c->left/256;right+=streamed*(int)c->right/256;
    continue;
   }
   if(c->double_buffer.pending)continue;
   if(queued(r,c)<0)return f;
   if(c->wait_frames)c->wait_frames--;
   if(!c->playing)continue;
   int value=mac_sound_sample(r,c);
   if(mac_cpu_halted(r->cpu))return f;
   value=value*(int)c->amp/255;left+=value*(int)c->left/256;right+=value*(int)c->right/256;
  }
  if(trace){
   unsigned peak=(unsigned)(left<0?-left:left),right_peak=(unsigned)(right<0?-right:right);
   if(right_peak>peak)peak=right_peak;
   r->sound->rendered_frames++;
   if(left>32767 || left< -32768 || right>32767 || right< -32768)r->sound->clipped_frames++;
   if(peak>r->sound->peak_absolute)r->sound->peak_absolute=peak;
  }
  out[f*2]=clamp(left>>shift);out[f*2+1]=clamp(right>>shift);
 }
 return frames;
}
#define SOUND_RETURN 0x1fe0u
size_t mac_sound_poll(MacRuntime*r,size_t budget){
 if(!r->sound || mac_cpu_halted(r->cpu) || budget<10000)return 0;
 if(r->sound->callback_active){mac_sound_fail(r,"Recursive Sound Manager callback execution is prohibited.");return 0;}
 size_t ran=0;
 for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++){
  MacSoundChannel*c=&r->sound->channels[i];if(!c->address || c->closing || !c->callback_pending)continue;
  uint32_t code=mac_cpu_read(r->cpu,c->address+8,4);if(!code){c->callback_pending=0;continue;}
  if(!mac_sound_range(r,code,2,1)){mac_sound_fail(r,"Invalid sound callback address.");return ran;}
  uint32_t saved[18];for(unsigned j=0;j<18;j++)saved[j]=mac_cpu_get(r->cpu,(MacCpuReg)j);
  uint32_t sp=saved[MAC_A7];if(sp<12 || !mac_sound_range(r,sp-12,12,1)){mac_sound_fail(r,"Sound callback stack exceeds RAM.");return ran;}
  uint8_t stack[12];memcpy(stack,mac_cpu_ram(r->cpu)+sp-12,12);
  mac_cpu_write(r->cpu,sp-12,4,SOUND_RETURN);mac_cpu_write(r->cpu,sp-8,4,c->address+20);mac_cpu_write(r->cpu,sp-4,4,c->address);
  mac_cpu_set(r->cpu,MAC_A7,sp-12);mac_cpu_set(r->cpu,MAC_PC,code);c->callback_pending=0;r->sound->callback_active=1;
  size_t limit=10000-ran;
  while(limit-- && !mac_cpu_halted(r->cpu) && mac_cpu_get(r->cpu,MAC_PC)!=SOUND_RETURN){size_t n=mac_cpu_run(r->cpu,1);ran+=n;if(!n)break;}
  r->sound->callback_active=0;
  if(mac_cpu_halted(r->cpu))return ran;
  if(mac_cpu_get(r->cpu,MAC_PC)!=SOUND_RETURN || mac_cpu_get(r->cpu,MAC_A7)!=sp){mac_sound_fail(r,"Sound callback did not return with the Pascal stack within budget.");return ran;}
  memcpy(mac_cpu_ram(r->cpu)+sp-12,stack,12);mac_cpu_set(r->cpu,MAC_SR,saved[MAC_SR]);
  for(unsigned j=0;j<17;j++)mac_cpu_set(r->cpu,(MacCpuReg)j,saved[j]);
  mac_sound_reap(r);
  if(ran>=10000)break;
 }
 return ran;
}
