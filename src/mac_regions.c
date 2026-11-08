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
/* Replace the region's contents with one rectangle (all zero = empty). Capacity and validation precede modification. */
static int store_rect(MacRuntime *rt,MacHandle *m,uint32_t h,const uint16_t edges[4]) {
    uint32_t p=m->pointer;
    if(m->size<10) {
        if(m->state&0x80)return fail(rt,"RectRgn cannot grow a locked region handle.");
        p=mac_runtime_alloc(rt,10);
        if(!p)return fail(rt,"RectRgn cannot allocate the required region capacity.");
        m->pointer=p;wr(rt,h,4,p);
    }
    wr(rt,p,2,10);for(unsigned i=0;i<4;i++)wr(rt,p+2+i*2,2,edges[i]);
    m->size=10;wr(rt,0x220,2,0);return 0;
}
/* Region algebra on rectangular regions: SectRgn, UnionRgn, DiffRgn, XorRgn. A result that is a rectangle or empty is exact;
 * a result that would need a non-rectangular region halts by name (complex region scanlines are not implemented). */
typedef struct { int t,l,b,r; } Box;
static int box_empty(Box a){return a.b<=a.t || a.r<=a.l;}
static int box_in(Box a,Box b){return a.t>=b.t && a.l>=b.l && a.b<=b.b && a.r<=b.r;} /* a inside b */
static Box box_sect(Box a,Box b){Box c={a.t>b.t?a.t:b.t,a.l>b.l?a.l:b.l,a.b<b.b?a.b:b.b,a.r<b.r?a.r:b.r};return box_empty(c)?(Box){0,0,0,0}:c;}
static Box box_bound(Box a,Box b){Box c={a.t<b.t?a.t:b.t,a.l<b.l?a.l:b.l,a.b>b.b?a.b:b.b,a.r>b.r?a.r:b.r};return c;}
/* 1 and *out set when the operation's result is a rectangle (or empty), else 0. */
static int algebra(unsigned op,Box a,Box b,Box *out) {
    Box none={0,0,0,0};
    int ea=box_empty(a),eb=box_empty(b);
    if(op==0xa8e4){*out=box_sect(a,b);return 1;}
    if(op==0xa8e5) { /* union */
        if(ea){*out=eb?none:b;return 1;}
        if(eb){*out=a;return 1;}
        if(box_in(a,b)){*out=b;return 1;}
        if(box_in(b,a)){*out=a;return 1;}
        if(a.t==b.t && a.b==b.b && a.l<=b.r && b.l<=a.r){*out=box_bound(a,b);return 1;}
        if(a.l==b.l && a.r==b.r && a.t<=b.b && b.t<=a.b){*out=box_bound(a,b);return 1;}
        return 0;
    }
    if(op==0xa8e6) { /* a minus b */
        if(ea){*out=none;return 1;}
        Box c=box_sect(a,b);
        if(box_empty(c)){*out=a;return 1;}
        if(box_in(a,b)){*out=none;return 1;}
        if(b.l<=a.l && b.r>=a.r) { /* b spans a's full width: what is left is the part above or below it */
            if(b.t<=a.t){*out=(Box){b.b,a.l,a.b,a.r};return 1;}
            if(b.b>=a.b){*out=(Box){a.t,a.l,b.t,a.r};return 1;}
            return 0;
        }
        if(b.t<=a.t && b.b>=a.b) { /* b spans a's full height: the part left or right of it */
            if(b.l<=a.l){*out=(Box){a.t,b.r,a.b,a.r};return 1;}
            if(b.r>=a.r){*out=(Box){a.t,a.l,a.b,b.l};return 1;}
            return 0;
        }
        return 0;
    }
    /* xor */
    if(ea){*out=eb?none:b;return 1;}
    if(eb){*out=a;return 1;}
    if(a.t==b.t && a.l==b.l && a.b==b.b && a.r==b.r){*out=none;return 1;}
    if(box_empty(box_sect(a,b)))return algebra(0xa8e5,a,b,out);
    return 0;
}
static Box box_of(MacRuntime *rt,MacHandle *m) {
    uint32_t p=m->pointer;
    return (Box){(int16_t)rd(rt,p+2,2),(int16_t)rd(rt,p+4,2),(int16_t)rd(rt,p+6,2),(int16_t)rd(rt,p+8,2)};
}
static int region_algebra(MacRuntime *rt,uint16_t opcode,uint32_t sp) {
    if(!valid(rt,sp,12))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    uint32_t hd=rd(rt,sp,4),hb=rd(rt,sp+4,4),ha=rd(rt,sp+8,4);
    MacHandle *md=lookup(rt,hd),*mb=lookup(rt,hb),*ma=lookup(rt,ha);
    if(!md||!mb||!ma)return fail(rt,"Region operation requires valid owned, non-resource, non-code handles.");
    if(md->size!=10||mb->size!=10||ma->size!=10||rd(rt,md->pointer,2)!=10||rd(rt,mb->pointer,2)!=10||rd(rt,ma->pointer,2)!=10)
        return fail(rt,"Region algebra on complex regions is unsupported.");
    Box out;
    if(!algebra(opcode,box_of(rt,ma),box_of(rt,mb),&out))
        return fail(rt,"Region algebra whose result is not a rectangle is unsupported.");
    uint16_t edges[4]={(uint16_t)out.t,(uint16_t)out.l,(uint16_t)out.b,(uint16_t)out.r};
    if(store_rect(rt,md,hd,edges))return -1;
    mac_cpu_set(rt->cpu,MAC_A7,sp+12);return 1;
}
/* Offset/Inset/Empty/Equal/PtIn/RectIn on rectangular regions (exact). An empty region stays empty under OffsetRgn and
 * InsetRgn. Pascal Booleans are written as one byte at the start of the result slot. Edges are top/left inclusive and
 * bottom/right exclusive. */
static MacHandle *rect_region(MacRuntime *rt,uint32_t h) {
    MacHandle *m=lookup(rt,h);
    if(!m){fail(rt,"Region operation requires a valid owned, non-resource, non-code handle.");return NULL;}
    if(m->size!=10||rd(rt,m->pointer,2)!=10){fail(rt,"Region operation on complex regions is unsupported.");return NULL;}
    return m;
}
static int region_query(MacRuntime *rt,uint16_t opcode,uint32_t sp) {
    unsigned stack; /* arguments plus the Boolean result slot where there is one */
    switch(opcode){case 0xa8e0:case 0xa8e1:stack=8;break;case 0xa8e2:stack=6;break;default:stack=10;break;}
    if(!valid(rt,sp,stack))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    if(opcode==0xa8e0||opcode==0xa8e1) {
        int dv=(int16_t)rd(rt,sp,2),dh=(int16_t)rd(rt,sp+2,2);uint32_t h=rd(rt,sp+4,4);
        MacHandle *m=rect_region(rt,h);if(!m)return -1;
        Box b=box_of(rt,m);uint16_t edges[4]={0,0,0,0};
        if(!box_empty(b)) {
            Box o=opcode==0xa8e0?(Box){b.t+dv,b.l+dh,b.b+dv,b.r+dh}:(Box){b.t+dv,b.l+dh,b.b-dv,b.r-dh};
            if(!box_empty(o)){edges[0]=(uint16_t)o.t;edges[1]=(uint16_t)o.l;edges[2]=(uint16_t)o.b;edges[3]=(uint16_t)o.r;}
        }
        if(store_rect(rt,m,h,edges))return -1;
        mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;
    }
    int answer=0;unsigned pop=8;
    if(opcode==0xa8e2) {
        MacHandle *m=rect_region(rt,rd(rt,sp,4));if(!m)return -1;
        answer=box_empty(box_of(rt,m));pop=4;
    } else {
        MacHandle *m=rect_region(rt,rd(rt,sp,4));if(!m)return -1;
        Box b=box_of(rt,m);
        if(opcode==0xa8e3) {
            MacHandle *o=rect_region(rt,rd(rt,sp+4,4));if(!o)return -1;
            Box c=box_of(rt,o);
            answer=(box_empty(b)&&box_empty(c))||(!box_empty(b)&&!box_empty(c)&&b.t==c.t&&b.l==c.l&&b.b==c.b&&b.r==c.r);
        } else if(opcode==0xa8e8) {
            int v=(int16_t)rd(rt,sp+4,2),hh=(int16_t)rd(rt,sp+6,2);
            answer=!box_empty(b)&&v>=b.t&&v<b.b&&hh>=b.l&&hh<b.r;
        } else {
            uint32_t r=rd(rt,sp+4,4);
            if(!valid(rt,r,8))return fail(rt,"RectInRgn rectangle exceeds aligned guest RAM.");
            Box q={(int16_t)rd(rt,r,2),(int16_t)rd(rt,r+2,2),(int16_t)rd(rt,r+4,2),(int16_t)rd(rt,r+6,2)};
            answer=!box_empty(b)&&!box_empty(box_sect(q,b));
        }
    }
    mac_cpu_write(rt->cpu,sp+pop,1,(unsigned)answer); /* the result slot sits just above the arguments */
    wr(rt,0x220,2,0);mac_cpu_set(rt->cpu,MAC_A7,sp+pop);return 1;
}
int mac_regions_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xa8d8 && opcode!=0xa8d9 && opcode!=0xa8df && opcode!=0xa8dd && (opcode<0xa8e0 || opcode>0xa8e7) && opcode!=0xa8e8 && opcode!=0xa8e9)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(opcode>=0xa8e4 && opcode<=0xa8e7)return region_algebra(rt,opcode,sp);
    if((opcode>=0xa8e0 && opcode<=0xa8e3) || opcode==0xa8e8 || opcode==0xa8e9)return region_query(rt,opcode,sp);
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
    /* RectRgn takes its rectangle from the stack; SetEmptyRgn ($A8DD) is the same replacement with the empty rectangle. */
    uint16_t edges[4]={0,0,0,0};
    if(opcode==0xa8df) {
        uint32_t r=rd(rt,sp,4);
        if(!valid(rt,r,8))return fail(rt,"RectRgn rectangle exceeds aligned guest RAM.");
        for(unsigned i=0;i<4;i++)edges[i]=(uint16_t)rd(rt,r+i*2,2);
        if((int16_t)edges[2]<=(int16_t)edges[0] || (int16_t)edges[3]<=(int16_t)edges[1])memset(edges,0,sizeof edges);
    }
    if(store_rect(rt,m,h,edges))return -1;
    mac_cpu_set(rt->cpu,MAC_A7,sp+stack);return 1;
}
