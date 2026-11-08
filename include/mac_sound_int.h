#ifndef MAC_SOUND_INT_H
#define MAC_SOUND_INT_H
#include "mac_sound.h"
#include "mac_sound_double.h"
#define MAC_SOUND_CHANNELS 8
#define MAC_SOUND_QUEUE 2048
#define MAC_SOUND_RECORD (36+128*8)
#define MAC_SOUND_OWNED UINT32_C(0x534e4420)
typedef struct { uint16_t command,param1;uint32_t param2; } MacSoundCommand;
typedef struct {
 uint32_t address,init,data,length;
 uint64_t rate;
 uint64_t phase;
 uint32_t source_rate,loop_start,loop_end,wait_frames;
 unsigned base_note;
 uint32_t voice_data,voice_length,voice_rate,voice_loop_start,voice_loop_end;
 unsigned voice_base;
 int note_playback;
 int voice,buffer_block,looping;
 unsigned head,count,capacity,amp,left,right;
 int owned,closing;
 int playing,paused,callback_pending;
 MacSoundDouble double_buffer;
 MacSoundCommand queue[MAC_SOUND_QUEUE];
} MacSoundChannel;
int mac_sound_pump(MacRuntime*r,MacSoundChannel*c); /* run instantaneous queued commands (mac_sound_render.c) */
struct MacSoundState {
 MacSoundChannel channels[MAC_SOUND_CHANNELS];
 uint64_t rendered_frames,clipped_frames;
 uint32_t peak_absolute;
 int callback_active;
};
int mac_sound_fail(MacRuntime *rt,const char *reason);
int mac_sound_range(MacRuntime *rt,uint32_t p,size_t n,int aligned);
int mac_sound_command(MacRuntime *rt,MacSoundChannel *channel,MacSoundCommand command);
int mac_sound_sample(MacRuntime *rt,MacSoundChannel *channel);
void mac_sound_reap(MacRuntime *rt);
void mac_sound_queue_fields(MacRuntime *rt,MacSoundChannel *channel);
#endif
