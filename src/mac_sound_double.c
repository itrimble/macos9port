#include "mac_sound_int.h"
#include "mac_sound_double.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Independent implementation of public Inside Macintosh Sound ABI:
 * Sound-122.html, Sound-133.html, Sound-77.html at leopard-adc.pepas.com.
 * Supports uncompressed mono unsigned8 PCM, at rates <=48kHz. */
#define DOUBLE_RETURN 0x1fd0u
#define MAX_FRAMES (4u * 1024u * 1024u)
#define MAX_DOUBLEBACK_INSTRUCTIONS 100000u
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static int buffer(MacRuntime*r,uint32_t p,uint32_t *frames){
    if(!mac_sound_range(r,p,16,1))return mac_sound_fail(r,"Double buffer record exceeds RAM.");
    uint32_t n=rd(r,p,4),flags=rd(r,p+4,4);
    if(flags&~5u)return mac_sound_fail(r,"Unsupported double buffer flags.");
    if(!(flags&1)){*frames=0;return 0;}
    if(!n || n>MAX_FRAMES || !mac_sound_range(r,p+16,n,0))return mac_sound_fail(r,"Double buffer frame span exceeds supported bounds.");
    *frames=n;return 1;
}
void mac_sound_double_stop(MacRuntime*r,unsigned i){
    if(r->sound && i<MAC_SOUND_CHANNELS)memset(&r->sound->channels[i].double_buffer,0,sizeof(MacSoundDouble));
}
int mac_sound_double_service(MacRuntime*r,uint16_t op){
    if(op!=0xa800 || mac_cpu_get(r->cpu,MAC_D0)!=0x00200008)return 0;
    if(mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if(!mac_sound_range(r,sp,10,1))return mac_sound_fail(r,"Double buffer call stack exceeds RAM.");
    uint32_t h=rd(r,sp,4),p=rd(r,sp+4,4);MacSoundChannel*c=NULL;
    if(r->sound)for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++)if(r->sound->channels[i].address==p && p && !r->sound->channels[i].closing)c=&r->sound->channels[i];
    if(c && c->owned){
        int live=0;for(size_t i=0;i<r->handle_count;i++)if(r->handles[i].pointer==p && !r->handles[i].handle && r->handles[i].type==MAC_SOUND_OWNED && r->handles[i].size==MAC_SOUND_RECORD)live=1;
        if(!live)c=NULL;
    }
    if(!c || !mac_sound_range(r,p,36,1)){wr(r,sp+8,2,(uint16_t)-205);mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;}
    if(r->sound->callback_active)return mac_sound_fail(r,"Double buffer start at interrupt time is prohibited.");
    if(mac_sound_pump(r,c)<0)return -1; /* instantaneous queued commands (ampCmd, quietCmd, ...) are consumed first */
    if(c->playing || c->paused || c->count || c->wait_frames || c->callback_pending || c->double_buffer.active || c->double_buffer.pending)
        return mac_sound_fail(r,"Double buffer start requires an idle sound channel.");
    if(!mac_sound_range(r,h,24,1))return mac_sound_fail(r,"Double buffer header exceeds RAM.");
    MacSoundDouble d={0};d.rate=rd(r,h+8,4);d.callback=rd(r,h+20,4);
    if(rd(r,h,2)!=1 || rd(r,h+2,2)!=8 || rd(r,h+4,2) || rd(r,h+6,2) || !d.rate || d.rate>(MAC_SOUND_RATE<<16))
        return mac_sound_fail(r,"Double buffering supports only uncompressed mono8 PCM up to48kHz.");
    if(!mac_sound_range(r,d.callback,2,1))return mac_sound_fail(r,"Doubleback callback is invalid.");
    uint32_t sizes[2];
    for(unsigned i=0;i<2;i++){d.buffers[i]=rd(r,h+12+i*4,4);if(buffer(r,d.buffers[i],&sizes[i])!=1)return mac_sound_fail(r,"Both initial double buffers must be ready and nonempty.");}
    if(d.buffers[0]<d.buffers[1]+16+sizes[1] && d.buffers[1]<d.buffers[0]+16+sizes[0])return mac_sound_fail(r,"Overlapping double buffers are unsupported.");
    d.active=1;c->double_buffer=d;wr(r,sp+8,2,0);mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
}
int mac_sound_double_sample(MacRuntime*r,unsigned i,int *value){
    *value=0;if(!r->sound || i>=MAC_SOUND_CHANNELS || mac_cpu_halted(r->cpu))return 0;
    MacSoundChannel*c=&r->sound->channels[i];MacSoundDouble*d=&c->double_buffer;
    if(!d->active)return 0;
    if(c->paused)return 1;
    uint32_t p=d->buffers[d->current];
    if(!d->loaded){int valid=buffer(r,p,&d->frames);if(valid<0)return -1;
        if(!valid || (d->pending&(1u<<d->current))){
            if(!d->starved && getenv("OSPI_SOUND_NOTE_TRACE"))
                fprintf(stderr,"sound-double-starved guest-us=%llu channel=%u buffer=%u ready=%d pending=%u\n",
                        (unsigned long long)r->clock_us,i,d->current,valid,d->pending);
            d->starved=1;return 1;
        }
        d->loaded=1;d->starved=0;}
    const uint64_t unit=(uint64_t)MAC_SOUND_RATE<<16;
    uint32_t at=(uint32_t)(d->phase/unit);
    if(at>=d->frames || !mac_sound_range(r,p+16,d->frames,0))return mac_sound_fail(r,"Double buffer playback span became invalid.");
    uint8_t *bytes=mac_cpu_ram(r->cpu)+p+16;int v=((int)bytes[at]-128)*256;
    if(!(c->init&4) && at+1<d->frames){int next=((int)bytes[at+1]-128)*256;v+=(int)((int64_t)(next-v)*(int64_t)(d->phase%unit)/(int64_t)unit);}
    *value=v;d->phase+=d->rate;
    if(d->phase>=(uint64_t)d->frames*unit){
        d->phase-=(uint64_t)d->frames*unit;uint32_t flags=rd(r,p+4,4);wr(r,p+4,4,flags&~1u);
        d->loaded=0;d->pending|=1u<<d->current;
        if(getenv("OSPI_SOUND_NOTE_TRACE"))
            fprintf(stderr,"sound-double guest-us=%llu channel=%u buffer=%u frames=%u rate16=%08x flags=%x pending=%u\n",
                    (unsigned long long)r->clock_us,i,d->current,d->frames,d->rate,flags,d->pending);
        if(flags&4)d->active=0;
        else d->current^=1;
    }
    return 1;
}
size_t mac_sound_double_poll(MacRuntime*r,size_t budget){
    if(!r->sound || mac_cpu_halted(r->cpu) || budget<10000)return 0;
    if(r->sound->callback_active){mac_sound_fail(r,"Recursive doubleback callback execution is prohibited.");return 0;}
    size_t ran=0;
    for(unsigned i=0;i<MAC_SOUND_CHANNELS;i++)for(unsigned b=0;b<2;b++){
        MacSoundChannel*c=&r->sound->channels[i];MacSoundDouble*d=&c->double_buffer;
        if(!(d->pending&(1u<<b)))continue;
        if(ran>=budget || ran>=MAX_DOUBLEBACK_INSTRUCTIONS)return ran;
        if(!mac_sound_range(r,d->callback,2,1)){mac_sound_fail(r,"Doubleback code exceeds RAM.");return ran;}
        uint32_t saved[18];for(unsigned j=0;j<18;j++)saved[j]=mac_cpu_get(r->cpu,(MacCpuReg)j);
        uint32_t sp=saved[MAC_A7];if(sp<12 || !mac_sound_range(r,sp-12,12,1)){mac_sound_fail(r,"Doubleback stack exceeds RAM.");return ran;}
        uint8_t stack[12];memcpy(stack,mac_cpu_ram(r->cpu)+sp-12,12);
        wr(r,sp-12,4,DOUBLE_RETURN);wr(r,sp-8,4,d->buffers[b]);wr(r,sp-4,4,c->address);
        mac_cpu_set(r->cpu,MAC_A7,sp-12);mac_cpu_set(r->cpu,MAC_PC,d->callback);d->pending&=~(1u<<b);r->sound->callback_active=1;
        size_t callback_start=ran;
        size_t callback_budget=budget-ran;
        if(callback_budget>MAX_DOUBLEBACK_INSTRUCTIONS-ran)
            callback_budget=MAX_DOUBLEBACK_INSTRUCTIONS-ran;
        while(ran-callback_start<callback_budget && !mac_cpu_halted(r->cpu) && mac_cpu_get(r->cpu,MAC_PC)!=DOUBLE_RETURN){size_t n=mac_cpu_run(r->cpu,1);ran+=n;if(!n)break;}
        r->sound->callback_active=0;
        if(mac_cpu_halted(r->cpu))return ran;
        if(mac_cpu_get(r->cpu,MAC_PC)!=DOUBLE_RETURN || mac_cpu_get(r->cpu,MAC_A7)!=sp){
            char error[160];
            snprintf(error,sizeof error,"Doubleback return mismatch: PC=%08x A7=%08x expected=%08x after %zu instructions.",
                     mac_cpu_get(r->cpu,MAC_PC),mac_cpu_get(r->cpu,MAC_A7),sp,ran-callback_start);
            mac_sound_fail(r,error);return ran;
        }
        memcpy(mac_cpu_ram(r->cpu)+sp-12,stack,12);mac_cpu_set(r->cpu,MAC_SR,saved[MAC_SR]);for(unsigned j=0;j<17;j++)mac_cpu_set(r->cpu,(MacCpuReg)j,saved[j]);
        mac_sound_reap(r);
    }
    return ran;
}
