#include "mac_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
/* Independently authored scheduler from Inside Macintosh: Processes ch3
 * public ABI, pp3-8,3-18..3-26. No platform timer implementation used. */
#define TIME_TASKS 32
#define TIME_RETURN 0x1fc0u
#define MAX_DELAY_US UINT64_C(3600000000)
#define MAX_TIMER_CALLBACK_INSTRUCTIONS 100000u
typedef struct {uint32_t p,token;uint64_t due,remaining;int extended,installed,active,resume;} TimeTask;
struct MacTimeState {TimeTask tasks[TIME_TASKS];int callback;};
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static int span(MacRuntime*r,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(r->cpu);return p && !(p&1) && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
int mac_time_set(MacRuntime*r,uint64_t us){
 if(us<r->clock_us)return fail(r,"Time Manager clock moved backwards.");
 r->clock_us=us;return 0;
}
void mac_time_destroy(MacRuntime*r){free(r->time_manager);r->time_manager=NULL;}
static void links(MacRuntime*r){uint32_t prev=0;for(unsigned i=0;i<TIME_TASKS;i++){TimeTask*t=&r->time_manager->tasks[i];if(!t->installed)continue;if(prev)wr(r,prev,4,t->p);wr(r,t->p,4,0);prev=t->p;}}
int mac_time_service(MacRuntime*r,uint16_t op){
 if(op!=0xa058 && op!=0xa458 && op!=0xa059 && op!=0xa05a)return 0;
 if(mac_cpu_halted(r->cpu))return -1;
 uint32_t p=mac_cpu_get(r->cpu,MAC_A0);int install=op==0xa058||op==0xa458;
 if(!span(r,p,op==0xa458?22:14))return fail(r,"Time Manager task record exceeds RAM.");
 if(!r->time_manager){r->time_manager=calloc(1,sizeof *r->time_manager);if(!r->time_manager)return fail(r,"Cannot allocate timer state.");}
 struct MacTimeState*s=r->time_manager;TimeTask*t=NULL,*empty=NULL;
 for(unsigned i=0;i<TIME_TASKS;i++){TimeTask*v=&s->tasks[i];if(v->p==p)t=v;if(!v->p && !empty)empty=v;}
 if(install){
  if(t && t->installed)return fail(r,"Time Manager task is already installed.");
  if(!t)t=empty;if(!t)return fail(r,"Time Manager exceeds32 retained task identities.");
  uint32_t code=rd(r,p+6,4);if(code && !span(r,code,2))return fail(r,"Timer callback address is invalid.");
  int extended=op==0xa458;
  if(extended && (rd(r,p+18,4) || (rd(r,p+14,4) && (!t->p || rd(r,p+14,4)!=t->token))))return fail(r,"Unsupported external Time Manager wakeup state.");
  for(unsigned i=0;i<TIME_TASKS;i++){TimeTask*v=&s->tasks[i];if(v->installed && p<v->p+(v->extended?22:14) && v->p<p+(extended?22:14))return fail(r,"Time Manager task records overlap.");}
  if(!extended || !rd(r,p+14,4)){t->resume=0;t->token=0;t->remaining=0;}
  t->p=p;t->extended=extended;t->installed=1;t->active=0;wr(r,p+4,2,0);links(r);
 }else{
  if(!t || !t->installed){mac_cpu_set(r->cpu,MAC_D0,UINT32_MAX);return 1;}
  if(!span(r,p,t->extended?22:14))return fail(r,"Timer record span changed.");
  if(op==0xa059){
   uint64_t left=t->active && t->due>r->clock_us?t->due-r->clock_us:0;t->remaining=left;t->resume=t->extended && left;
   uint32_t count=left<=INT32_MAX?(uint32_t)-(int32_t)left:(uint32_t)((left+999)/1000);
   wr(r,p+10,4,count);wr(r,p+4,2,0);wr(r,p,4,0);t->active=0;t->installed=0;links(r);
  }else{
   if(t->active)return fail(r,"Cannot prime an already active timer; remove and reinstall first.");
   int32_t count=(int32_t)mac_cpu_get(r->cpu,MAC_D0);uint64_t delay=count<0?(uint64_t)-(int64_t)count:(uint64_t)count*1000;
   if(delay>MAX_DELAY_US)return fail(r,"Timer delays beyond one hour are not implemented.");
   uint64_t base=r->clock_us;
   if(t->extended){uint32_t wake=rd(r,p+14,4);if(wake && wake!=t->token)return fail(r,"Timer wakeup token was modified.");
    if(wake && t->resume && !count){delay=t->remaining;}else if(wake)base=t->due;
   }
   if(delay>UINT64_MAX-base)return fail(r,"Timer deadline overflows monotonic clock.");
   t->due=base+delay;t->active=1;t->resume=0;
   if(t->extended){if(++t->token==0)t->token=1;wr(r,p+14,4,t->token);}
   wr(r,p+4,2,0x8000);
  }
 }
 mac_cpu_set(r->cpu,MAC_D0,0);return 1;
}
size_t mac_time_poll(MacRuntime*r,size_t budget){
 if(!r->time_manager || budget<10000 || mac_cpu_halted(r->cpu))return 0;
 if(mac_cpu_get(r->cpu,MAC_SR)&0x0700)return 0;
 struct MacTimeState*s=r->time_manager;if(s->callback){fail(r,"Recursive timer polling is prohibited.");return 0;}
 size_t ran=0;unsigned work=0;
 if(budget>MAX_TIMER_CALLBACK_INSTRUCTIONS)budget=MAX_TIMER_CALLBACK_INSTRUCTIONS;
 size_t allowance=budget;
 while(ran<budget && work++<128){
  TimeTask*t=NULL;for(unsigned i=0;i<TIME_TASKS;i++){TimeTask*v=&s->tasks[i];if(v->installed && v->active && v->due<=r->clock_us && (!t || v->due<t->due))t=v;}
  if(!t)break;uint32_t p=t->p,code=rd(r,p+6,4);
  if(!span(r,p,t->extended?22:14) || (code && !span(r,code,2))){fail(r,"Timer callback or record exceeds RAM.");return ran;}
  uint32_t saved[18];for(unsigned i=0;i<18;i++)saved[i]=mac_cpu_get(r->cpu,(MacCpuReg)i);
  uint32_t sp=saved[MAC_A7]&~1u; /* the interrupted code may have an odd A7 (a THINK C frame of odd size): the callback runs just below it, word aligned, and A7 is restored from `saved` */
  if(code && (sp<4 || !span(r,sp-4,4))){fail(r,"Timer callback stack exceeds RAM.");return ran;}
  t->active=0;wr(r,p+4,2,0);wr(r,p+10,4,0);if(!code)continue;
  uint8_t stack[4];memcpy(stack,mac_cpu_ram(r->cpu)+sp-4,4);wr(r,sp-4,4,TIME_RETURN);
  mac_cpu_set(r->cpu,MAC_A7,sp-4);mac_cpu_set(r->cpu,MAC_A1,p);mac_cpu_set(r->cpu,MAC_PC,code);s->callback=1;
  size_t callback_start=ran;
  /* Each callback gets the poll's full budget (itself capped), not what is left of it after earlier callbacks: what is left only
   * decides whether another callback may start. A callback cut off by an earlier one's time would be abandoned in the middle of its work. */
  while(ran-callback_start<allowance && !mac_cpu_halted(r->cpu) && mac_cpu_get(r->cpu,MAC_PC)!=TIME_RETURN){size_t n=mac_cpu_run(r->cpu,1);ran+=n;if(!n)break;}
  s->callback=0;if(mac_cpu_halted(r->cpu))return ran;
  if(mac_cpu_get(r->cpu,MAC_PC)!=TIME_RETURN || mac_cpu_get(r->cpu,MAC_A7)!=sp){
   char error[160];
   snprintf(error,sizeof error,"Timer return mismatch: PC=%08x A7=%08x expected=%08x after %zu instructions.",
            mac_cpu_get(r->cpu,MAC_PC),mac_cpu_get(r->cpu,MAC_A7),sp,ran-callback_start);
   fail(r,error);return ran;
  }
  memcpy(mac_cpu_ram(r->cpu)+sp-4,stack,4);mac_cpu_set(r->cpu,MAC_SR,saved[MAC_SR]);for(unsigned i=0;i<17;i++)mac_cpu_set(r->cpu,(MacCpuReg)i,saved[i]);
 }
 return ran; /* Remaining due work retained, including callback re-primes. */
}
