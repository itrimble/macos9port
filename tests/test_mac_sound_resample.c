#include "mac_sound_int.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define UNIT ((uint64_t)MAC_SOUND_RATE << 16)
static MacSoundChannel fixture(MacRuntime *r, uint64_t rate, unsigned flags) {
    uint8_t values[]={0,128,255,64};
    memcpy(mac_cpu_ram(r->cpu)+0x4000,values,sizeof values);
    return (MacSoundChannel){.data=0x4000,.length=4,.rate=rate,.init=flags,.playing=1};
}
int main(void) {
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);assert(r.cpu);
    MacSoundChannel c=fixture(&r,UNIT*2,12);
    assert(mac_sound_sample(&r,&c)==-16384); /* Both source cells contribute. */
    assert(mac_sound_sample(&r,&c)==8064 && !c.playing);
    c=fixture(&r,UNIT*2,4);assert(mac_sound_sample(&r,&c)==-32768); /* Drop conversion allowed. */
    c=fixture(&r,UNIT*2,8);assert(mac_sound_sample(&r,&c)==-64); /* Linear ramp area. */
    c=fixture(&r,UNIT*3/2,12);
    assert(mac_sound_sample(&r,&c)==-21845);
    assert(mac_sound_sample(&r,&c)==21674);
    assert(mac_sound_sample(&r,&c)==-10922 && !c.playing); /* Partial last frame padded with silence. */
    c=fixture(&r,UNIT*2,12);c.length=3;c.note_playback=1;c.loop_start=1;c.loop_end=3;
    assert(mac_sound_sample(&r,&c)==-16384);
    assert(mac_sound_sample(&r,&c)==16256 && c.looping);
    assert(mac_sound_sample(&r,&c)==16256);
    c=fixture(&r,UNIT*16,12);c.length=3;c.note_playback=1;c.loop_start=2;c.loop_end=3;
    assert(mac_sound_sample(&r,&c)==26400 && c.playing);
    assert(mac_sound_sample(&r,&c)==32512);
    c.phase+=UNIT/2;assert(mac_sound_sample(&r,&c)==32512 && !mac_cpu_halted(r.cpu));
    c=fixture(&r,UNIT*2,12);c.data=MAC_RUNTIME_RAM-2;c.length=2;
    mac_cpu_ram(r.cpu)[c.data]=0;mac_cpu_ram(r.cpu)[c.data+1]=255;
    assert(mac_sound_sample(&r,&c)==-128 && !c.playing); /* Exact RAM boundary. */
    c=fixture(&r,UNIT,12);c.voice=1;c.playing=0;c.voice_data=0x4000;c.voice_length=4;
    c.voice_rate=48000u<<16;c.voice_base=60;
    assert(mac_sound_command(&r,&c,(MacSoundCommand){42,0,72})==1 && c.rate==UNIT*2);
    assert(mac_sound_sample(&r,&c)==-16384);
    c.playing=0;assert(mac_sound_command(&r,&c,(MacSoundCommand){42,0,108})==1 && c.rate==UNIT*16);
    c.playing=0;assert(mac_sound_command(&r,&c,(MacSoundCommand){42,0,109})==-1 && c.rate==UNIT*16);
    mac_cpu_destroy(r.cpu);
    puts("bounded area resampling, interpolation, loop, and endpoint tests passed");
    return 0;
}
