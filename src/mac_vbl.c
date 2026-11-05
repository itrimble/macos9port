#include "mac_runtime.h"
#include <string.h>

/* ROM-free system VBL queue. Public record and register ABI:
 * dev.os9.ca/techpubs/mac/Processes/Processes-78.html and Processes-95.html.
 * Poll only between CPU batches, never from within a CPU trap callback. */
#define VBL_RETURN 0x1ff0u
#define VBL_TICKS_PER_POLL 120u
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static int valid(MacRuntime*r,uint32_t p,size_t n){
    if((p&1)||p>mac_cpu_ram_size(r->cpu)||n>mac_cpu_ram_size(r->cpu)-p)
        return fail(r,"VBL guest range exceeds aligned RAM.");
    return 1;
}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static void unlink_task(MacRuntime*r,unsigned slot){
    uint32_t old=r->vbl_tasks[slot];
    for(unsigned j=slot;j+1<r->vbl_count;j++)r->vbl_tasks[j]=r->vbl_tasks[j+1];
    r->vbl_count--;wr(r,old,4,0);
    if(slot)wr(r,r->vbl_tasks[slot-1],4,slot<r->vbl_count?r->vbl_tasks[slot]:0);
}
int mac_vbl_service(MacRuntime*r,uint16_t opcode){
    if(opcode!=0xa033&&opcode!=0xa034)return 0;
    if(mac_cpu_halted(r->cpu))return -1;
    uint32_t p=mac_cpu_get(r->cpu,MAC_A0);
    if(!p||valid(r,p,14)<0)return fail(r,"Invalid VBL task record.");
    int result=0;
    unsigned slot=0;while(slot<r->vbl_count&&r->vbl_tasks[slot]!=p)slot++;
    if(rd(r,p+4,2)!=1)result=-2; /* vType */
    else if(opcode==0xa034){
        if(slot==r->vbl_count)result=-1;else unlink_task(r,slot);
    }else if(slot<r->vbl_count)result=-1;
    else{
        uint32_t code=rd(r,p+6,4);
        if(!code||valid(r,code,2)<0)return fail(r,"Invalid VBL callback address.");
        if(rd(r,p+12,2)||(int16_t)rd(r,p+10,2)<0)return fail(r,"VBL nonzero phase or negative countdown unsupported.");
        if(r->vbl_count==32)return fail(r,"VBL queue capacity exceeded.");
        if(r->vbl_count)wr(r,r->vbl_tasks[r->vbl_count-1],4,p);
        else if (!r->vbl_active && r->vbl_due_cursor == r->vbl_due_count) r->vbl_last_tick=r->ticks;
        wr(r,p,4,0);r->vbl_tasks[r->vbl_count++]=p;
    }
    mac_cpu_set(r->cpu,MAC_D0,(uint32_t)(int32_t)result);
    uint32_t sr=mac_cpu_get(r->cpu,MAC_SR)&~15u;
    mac_cpu_set(r->cpu,MAC_SR,sr|(result<0?8u:result==0?4u:0u));return 1;
}
static int callback(MacRuntime*r,uint32_t task,size_t *ran,size_t limit){
    uint32_t code=rd(r,task+6,4),saved[18];
    if(!code||valid(r,code,2)<0)return fail(r,"Invalid VBL callback address.");
    for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r->cpu,(MacCpuReg)i);
    uint32_t sp=saved[MAC_A7];
    if(sp<4||valid(r,sp-4,4)<0)return fail(r,"VBL callback stack exceeds RAM.");
    wr(r,sp-4,4,VBL_RETURN);mac_cpu_set(r->cpu,MAC_A7,sp-4);
    mac_cpu_set(r->cpu,MAC_A0,task);mac_cpu_set(r->cpu,MAC_PC,code);
    r->vbl_active=1;
    unsigned steps=0;
    while(steps<limit&&!mac_cpu_halted(r->cpu)&&mac_cpu_get(r->cpu,MAC_PC)!=VBL_RETURN){
        size_t n=mac_cpu_run(r->cpu,1);*ran+=n;steps++;
        if(!n)break;
    }
    r->vbl_active=0;
    if(mac_cpu_halted(r->cpu))return -1;
    if(mac_cpu_get(r->cpu,MAC_PC)!=VBL_RETURN||mac_cpu_get(r->cpu,MAC_A7)!=sp)
        return fail(r,"VBL callback failed to return within its instruction budget.");
    mac_cpu_set(r->cpu,MAC_SR,saved[MAC_SR]);
    for(unsigned i=0;i<17;i++)mac_cpu_set(r->cpu,(MacCpuReg)i,saved[i]);
    return 1;
}
size_t mac_vbl_poll(MacRuntime*r,size_t budget){
    size_t ran=0;
    unsigned ticks_processed=0;
    if(mac_cpu_halted(r->cpu))return 0;
    if(r->vbl_active){fail(r,"Recursive VBL polling is unsupported.");return 0;}
    if(budget<10000)return 0;
    if(!r->vbl_count && r->vbl_due_cursor==r->vbl_due_count){r->vbl_last_tick=r->ticks;return 0;}
    for(;;){
        if(r->vbl_due_cursor==r->vbl_due_count){
            /* last_tick advances only when its countdowns are processed. The
             * due cursor independently retains any unfinished callback list.
             * Equality and unsigned increment remain valid across TickCount
             * wrap; never discard overdue ticks after a host scheduling pause. */
            if(r->vbl_last_tick==r->ticks || ticks_processed==VBL_TICKS_PER_POLL)return ran;
            ticks_processed++;
            for(unsigned i=0;i<r->vbl_count;i++){
                uint32_t p=r->vbl_tasks[i];
                if(valid(r,p,14)<0)return ran;
                if(rd(r,p+4,2)!=1||rd(r,p+12,2)||(int16_t)rd(r,p+10,2)<0){
                    fail(r,"VBL task record changed to unsupported configuration.");return ran;
                }
            }
            r->vbl_due_count=r->vbl_due_cursor=0;r->vbl_last_tick++;
            for(unsigned i=0;i<r->vbl_count;i++){
                uint32_t p=r->vbl_tasks[i];unsigned count=rd(r,p+10,2);
                if(count){wr(r,p+10,2,count-1);if(count==1)r->vbl_due[r->vbl_due_count++]=p;}
            }
        }
        while(r->vbl_due_cursor<r->vbl_due_count){
            if(ran==10000)return ran; /* Preserve pending tasks for the next bounded poll. */
            uint32_t p=r->vbl_due[r->vbl_due_cursor++];unsigned slot=0;
            while(slot<r->vbl_count&&r->vbl_tasks[slot]!=p)slot++;
            if(slot==r->vbl_count)continue; /* Removed by an earlier task. */
            if(callback(r,p,&ran,10000-ran)<0)return ran;
            slot=0;while(slot<r->vbl_count&&r->vbl_tasks[slot]!=p)slot++;
            if(slot<r->vbl_count&&!rd(r,p+10,2))unlink_task(r,slot);
        }
    }
}
