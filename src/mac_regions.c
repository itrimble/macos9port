#include "mac_runtime.h"
#include <string.h>

/* Public region ABI, independently implemented:
 * dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-132.html (NewRgn)
 * QuickDraw-135.html (DisposeRgn), QuickDraw-139.html (RectRgn).
 * This module creates/replaces rectangles only; it never interprets complex
 * region scanlines or claims OpenRgn/CloseRgn drawing support. */
#define REGION_OWNED UINT32_C(0x4f52474e)
static int fail(MacRuntime *rt,const char *why){mac_cpu_halt(rt->cpu,why);return -1;}
static int valid(MacRuntime *rt,uint32_t p,size_t n) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && !(p&1) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(rt->cpu,p,n,v);}
static MacHandle *lookup(MacRuntime *rt,uint32_t h) {
    if(!valid(rt,h,4))return NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle!=h || !m->pointer || m->resource || m->loaded_code ||
           !valid(rt,m->pointer,m->size) || rd(rt,h,4)!=m->pointer)continue;
        int window_region=0;
        for(unsigned j=0;j<8;j++) {
            MacWindowState *w=j?&rt->parked_windows[j-1]:&rt->window;
            if(w->address && m->size==10 && (h==w->handles[8] || h==w->handles[9] || h==w->handles[10]))window_region=1;
        }
        if(m->type && m->type!=REGION_OWNED &&
           !(m->type==0x43505254 && m->size==10) &&
           !(m->type==0x4f57494e && window_region))return NULL;
        if((uint64_t)m->pointer<m->handle+UINT64_C(4) &&
           (uint64_t)m->pointer+m->size>m->handle)return NULL;
        return m;
    }
    return NULL;
}
static int new_region(MacRuntime *rt,uint32_t sp) {
    size_t slot=0;while(slot<MAC_RUNTIME_HANDLES && (rt->handles[slot].handle || rt->handles[slot].pointer))slot++;
    uint32_t block=slot<MAC_RUNTIME_HANDLES?mac_runtime_alloc(rt,16):0;
    if(!block){wr(rt,sp,4,0);wr(rt,0x220,2,(uint16_t)-108);return 1;}
    uint32_t p=block+4;wr(rt,block,4,p);wr(rt,p,2,10);
    rt->handles[slot]=(MacHandle){.handle=block,.pointer=p,.size=10,.type=REGION_OWNED};
    if(rt->handle_count<=slot)rt->handle_count=slot+1;
    wr(rt,sp,4,block);wr(rt,0x220,2,0);return 1;
}
int mac_regions_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xa8d8 && opcode!=0xa8d9 && opcode!=0xa8df)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    unsigned stack=opcode==0xa8df?8:4;
    if(!valid(rt,sp,stack))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    if(opcode==0xa8d8)return new_region(rt,sp);
    uint32_t h=rd(rt,sp+(opcode==0xa8df?4:0),4);
    MacHandle *m=lookup(rt,h);
    if(!m)return fail(rt,"Region operation requires a valid owned, non-resource, non-code handle.");
    if(opcode==0xa8d9) {
        if(m->type!=REGION_OWNED)return fail(rt,"DisposeRgn cannot dispose a port or window owned region.");
        wr(rt,h,4,0);memset(m,0,sizeof(*m));wr(rt,0x220,2,0);
        mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
    }
    uint32_t r=rd(rt,sp,4);uint16_t edges[4];
    if(!valid(rt,r,8))return fail(rt,"RectRgn rectangle exceeds aligned guest RAM.");
    for(unsigned i=0;i<4;i++)edges[i]=(uint16_t)rd(rt,r+i*2,2);
    if((int16_t)edges[2]<=(int16_t)edges[0] || (int16_t)edges[3]<=(int16_t)edges[1])memset(edges,0,sizeof edges);
    uint32_t p=m->pointer;
    if(m->size<10) {
        if(m->state&0x80)return fail(rt,"RectRgn cannot grow a locked region handle.");
        p=mac_runtime_alloc(rt,10);
        if(!p)return fail(rt,"RectRgn cannot allocate the required region capacity.");
        m->pointer=p;wr(rt,h,4,p);
    }
    /* Full capacity and source validation precede modification. Input may
     * overlap the old region, including a region relocated by this call. */
    wr(rt,p,2,10);for(unsigned i=0;i<4;i++)wr(rt,p+2+i*2,2,edges[i]);
    m->size=10;wr(rt,0x220,2,0);mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;
}
