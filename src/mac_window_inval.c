#include "mac_window_int.h"
#include <string.h>

/* InvalRect ($A928), InvalRgn ($A927), ValidRect ($A92A), ValidRgn ($A929): add to or remove from the update region of
 * the window that owns the current port. The argument is in the port's local coordinates; the update region is kept in
 * global coordinates (global = local minus the pixmap's bounds origin) and clipped to the window's content.
 *
 * The update region is one rectangle here, so both directions are conservative approximations, never missing a redraw:
 * - Inval of a rectangle that does not join the current update rectangle exactly grows it to the bounding box (more is
 *   redrawn than was asked for).
 * - Valid removes only when the result is exactly a rectangle (the area is covered, or the removed part is a full-width or
 *   full-height strip); otherwise the update region is left as it was (more is redrawn than necessary).
 * Region arguments must be rectangular regions; complex ones halt. */
static int fail(MacRuntime *rt,const char *why){mac_cpu_halt(rt->cpu,why);return -1;}
static int valid(MacRuntime *rt,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(rt->cpu);return p>=0x400 && !(p&1) && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(rt->cpu,p,n,v);}
static int empty(const int b[4]){return b[2]<=b[0] || b[3]<=b[1];}

/* The rectangle of a rectangular region record, or a halt. */
static int region_box(MacRuntime *rt,uint32_t h,int out[4]) {
    if(!valid(rt,h,4))return fail(rt,"Update region argument handle is invalid.");
    uint32_t p=rd(rt,h,4);
    if(!valid(rt,p,10) || rd(rt,p,2)!=10)return fail(rt,"Update region argument must be a valid rectangular region.");
    for(unsigned i=0;i<4;i++)out[i]=(int16_t)rd(rt,p+2+i*2,2);
    return 0;
}
int mac_window_inval_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode<0xa927 || opcode>0xa92a)return 0;
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(!valid(rt,sp,4))return fail(rt,"Update region call stack is invalid.");
    uint32_t arg=rd(rt,sp,4),port=rt->current_port;
    MacWindowState *w=mac_window_find(rt,port);
    if(!w || w->address!=port)return fail(rt,"Update region calls require the current port to be an owned window.");
    if(!valid(rt,port,108) || (rd(rt,port+6,2)&0xc000)!=0xc000)return fail(rt,"Update region calls require a color window port.");
    uint32_t pm=rd(rt,rd(rt,port+2,4),4);
    if(!valid(rt,pm,14))return fail(rt,"Update region calls require a valid window pixmap.");
    int origin_v=(int16_t)rd(rt,pm+6,2),origin_h=(int16_t)rd(rt,pm+8,2);
    int local[4];
    if(opcode==0xa927 || opcode==0xa929) { if(region_box(rt,arg,local))return -1; }
    else {
        if(!valid(rt,arg,8))return fail(rt,"Update region rectangle is invalid.");
        for(unsigned i=0;i<4;i++)local[i]=(int16_t)rd(rt,arg+i*2,2);
    }
    int g[4]={local[0]-origin_v,local[1]-origin_h,local[2]-origin_v,local[3]-origin_h};
    int content[4]={w->bounds[0],w->bounds[1],w->bounds[2],w->bounds[3]},clipped[4];
    if(!mac_region_box_algebra(0xa8e4,g,content,clipped))clipped[0]=clipped[1]=clipped[2]=clipped[3]=0;
    uint32_t handle=w->handles[10];
    if(!valid(rt,handle,4))return fail(rt,"Window update region handle is invalid.");
    uint32_t p=rd(rt,handle,4);
    if(!valid(rt,p,10) || rd(rt,p,2)!=10)return fail(rt,"Window update region is not a valid rectangular region.");
    int update[4];for(unsigned i=0;i<4;i++)update[i]=(int16_t)rd(rt,p+2+i*2,2);
    if(empty(update))memset(update,0,sizeof update);
    int result[4];
    if(empty(clipped))memcpy(result,update,sizeof result);                       /* nothing of it lies in the window */
    else if(opcode==0xa927 || opcode==0xa928) {
        if(empty(update))memcpy(result,clipped,sizeof result);
        else if(!mac_region_box_algebra(0xa8e5,update,clipped,result)) {         /* not one rectangle: take the bounding box */
            result[0]=update[0]<clipped[0]?update[0]:clipped[0];result[1]=update[1]<clipped[1]?update[1]:clipped[1];
            result[2]=update[2]>clipped[2]?update[2]:clipped[2];result[3]=update[3]>clipped[3]?update[3]:clipped[3];
        }
    } else if(!mac_region_box_algebra(0xa8e6,update,clipped,result))memcpy(result,update,sizeof result); /* inexact: keep it */
    if(empty(result))memset(result,0,sizeof result);
    for(unsigned i=0;i<4;i++)wr(rt,p+2+i*2,2,(uint16_t)result[i]);
    mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
}
