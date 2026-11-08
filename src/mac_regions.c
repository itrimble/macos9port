#include "mac_runtime.h"
#include "mac_rgn.h"
#include <stdlib.h>
#include <string.h>

/* Region calls on the region engine (mac_rgn.c): NewRgn $A8D8, DisposeRgn $A8D9, CopyRgn $A8DC, SetEmptyRgn $A8DD, SetRectRgn $A8DE,
 * RectRgn $A8DF, OffsetRgn $A8E0, InsetRgn $A8E1, EmptyRgn $A8E2, EqualRgn $A8E3, SectRgn $A8E4, UnionRgn $A8E5, DiffRgn $A8E6,
 * XorRgn $A8E7, PtInRgn $A8E8, RectInRgn $A8E9; OpenRgn $A8DA and CloseRgn $A8DB record an outline (see below).
 *
 * A region record is guest memory, so it is untrusted: it is decoded and validated against the runtime's own handle table (its
 * rgnSize word may not exceed the handle's real size) every time it is used. Results are encoded back in the documented format.
 * A handle's size is its capacity and never shrinks (rgnSize says how much of it is in use), so a region that alternates between
 * simple and complex does not leak heap blocks; growing allocates a new block (the development heap does not compact) and halts for a
 * locked handle. InsetRgn on a region that is not one rectangle halts by name: the edge semantics of insetting a concave shape are
 * not something I could verify. Public contracts: dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-132.html and the pages after it. */
#define REGION_OWNED UINT32_C(0x4f52474e)
#define PORT_OWNED UINT32_C(0x43505254)
#define WINDOW_OWNED UINT32_C(0x4f57494e)
static int fail(MacRuntime *rt,const char *why){mac_cpu_halt(rt->cpu,why);return -1;}
static int valid(MacRuntime *rt,uint32_t p,size_t n) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && !(p&1) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(rt->cpu,p,n,v);}
/* Is this table entry a region the guest may operate on? Ours, or a port's or a window's (whose first word is a plausible rgnSize;
 * port pattern handles share the type but their first word is below 10). */
static MacHandle *lookup(MacRuntime *rt,uint32_t h) {
    if(!valid(rt,h,4))return NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle!=h || !m->pointer || m->resource || m->loaded_code ||
           !valid(rt,m->pointer,m->size) || rd(rt,h,4)!=m->pointer)continue;
        unsigned rgn_size=m->size>=2?rd(rt,m->pointer,2):0;
        int window_region=0;
        for(unsigned j=0;j<8;j++) {
            MacWindowState *w=j?&rt->parked_windows[j-1]:&rt->window;
            if(w->address && (h==w->handles[8] || h==w->handles[9] || h==w->handles[10]))window_region=1;
        }
        if(m->type && m->type!=REGION_OWNED &&
           !(m->type==PORT_OWNED && rgn_size>=10 && rgn_size<=m->size) &&
           !(m->type==WINDOW_OWNED && window_region))return NULL;
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
/* Decode a region record into the engine. The record's own size must fit its handle; any malformation halts by name. */
static int load(MacRuntime *rt,MacHandle *m,MacRgn *g) {
    uint32_t p=m->pointer;
    if(m->size<10 || !valid(rt,p,10))return fail(rt,"Region record is too small or outside guest RAM.");
    unsigned size=rd(rt,p,2);
    if(size<10 || size>m->size || !valid(rt,p,size))return fail(rt,"Region record's size exceeds its handle.");
    const char *why;
    if(mac_rgn_decode(mac_cpu_ram(rt->cpu)+p,size,g,&why))return fail(rt,why?why:"Region record is malformed.");
    return 0;
}
/* Encode the engine's region into the handle, growing it if it does not fit. */
static int store(MacRuntime *rt,MacHandle *m,uint32_t h,const MacRgn *g) {
    size_t need=mac_rgn_encoded_size(g);
    if(!need)return fail(rt,"Region is too complex to store (over 2048 rectangles, 256 in one row, or 32760 bytes).");
    if(need>m->size) {
        if(m->state&0x80)return fail(rt,"A region operation cannot grow a locked region handle.");
        uint32_t p=mac_runtime_alloc(rt,(uint32_t)need);
        if(!p || !valid(rt,p,need))return fail(rt,"A region operation cannot allocate the required region capacity.");
        m->pointer=p;m->size=(uint32_t)need;wr(rt,h,4,p);
    }
    if(!valid(rt,m->pointer,need) || !mac_rgn_encode(g,mac_cpu_ram(rt->cpu)+m->pointer,need))return fail(rt,"Region could not be encoded.");
    wr(rt,0x220,2,0);return 0;
}
static MacRect rect_at(MacRuntime *rt,uint32_t r){return (MacRect){(int16_t)rd(rt,r,2),(int16_t)rd(rt,r+2,2),(int16_t)rd(rt,r+4,2),(int16_t)rd(rt,r+6,2)};}
static MacHandle *need(MacRuntime *rt,uint32_t h) {
    MacHandle *m=lookup(rt,h);
    if(!m)fail(rt,"Region operation requires a valid owned, non-resource, non-code handle.");
    return m;
}
/* SectRgn, UnionRgn, DiffRgn, XorRgn: dst, srcB, srcA from the top of the stack; the result is a OP b. The destination may be an operand. */
static int region_algebra(MacRuntime *rt,uint16_t opcode,uint32_t sp) {
    if(!valid(rt,sp,12))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    uint32_t hd=rd(rt,sp,4),hb=rd(rt,sp+4,4),ha=rd(rt,sp+8,4);
    MacHandle *md=lookup(rt,hd),*mb=lookup(rt,hb),*ma=lookup(rt,ha);
    if(!md||!mb||!ma)return fail(rt,"Region operation requires valid owned, non-resource, non-code handles.");
    MacRgn a,b,r;mac_rgn_init(&a);mac_rgn_init(&b);mac_rgn_init(&r);
    int rc=-1;const char *why=NULL;
    if(!load(rt,ma,&a) && !load(rt,mb,&b)) {
        if(mac_rgn_op((int)(opcode-0xa8e4),&a,&b,&r,&why))fail(rt,why?why:"Region operation failed.");
        else if(!store(rt,md,hd,&r)){mac_cpu_set(rt->cpu,MAC_A7,sp+12);rc=1;}
    }
    mac_rgn_free(&a);mac_rgn_free(&b);mac_rgn_free(&r);return rc;
}
/* OffsetRgn, InsetRgn, EmptyRgn, EqualRgn, PtInRgn, RectInRgn. Pascal Booleans are written as one byte at the start of the result
 * slot. Edges are top/left inclusive and bottom/right exclusive. An empty region stays empty under OffsetRgn and InsetRgn. */
static int region_query(MacRuntime *rt,uint16_t opcode,uint32_t sp) {
    unsigned stack; /* arguments plus the Boolean result slot where there is one */
    switch(opcode){case 0xa8e0:case 0xa8e1:stack=8;break;case 0xa8e2:stack=6;break;default:stack=10;break;}
    if(!valid(rt,sp,stack))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    MacRgn g,o;mac_rgn_init(&g);mac_rgn_init(&o);int rc=-1;
    if(opcode==0xa8e0||opcode==0xa8e1) {
        int dv=(int16_t)rd(rt,sp,2),dh=(int16_t)rd(rt,sp+2,2);uint32_t h=rd(rt,sp+4,4);
        MacHandle *m=need(rt,h);
        if(m && !load(rt,m,&g)) {
            int ok=1;
            if(opcode==0xa8e0)mac_rgn_offset(&g,dh,dv);
            else if(!mac_rgn_is_empty(&g)) {
                MacRect b;
                if(!mac_rgn_is_rect(&g,&b)){fail(rt,"InsetRgn on a region that is not one rectangle is unsupported.");ok=0;}
                else mac_rgn_set_rect(&g,(MacRect){b.t+dv,b.l+dh,b.b-dv,b.r-dh});
            }
            if(ok && !store(rt,m,h,&g)){mac_cpu_set(rt->cpu,MAC_A7,sp+8);rc=1;}
        }
        mac_rgn_free(&g);return rc;
    }
    int answer=0;unsigned pop=8;
    MacHandle *m=need(rt,rd(rt,sp,4));
    if(m && !load(rt,m,&g)) {
        rc=1;
        if(opcode==0xa8e2){answer=mac_rgn_is_empty(&g);pop=4;}
        else if(opcode==0xa8e3) {
            MacHandle *mo=need(rt,rd(rt,sp+4,4));
            if(!mo || load(rt,mo,&o))rc=-1;else answer=mac_rgn_equal(&g,&o);
        } else if(opcode==0xa8e8) {
            answer=mac_rgn_contains(&g,(int16_t)rd(rt,sp+6,2),(int16_t)rd(rt,sp+4,2));
        } else {
            uint32_t r=rd(rt,sp+4,4);
            if(!valid(rt,r,8)){fail(rt,"RectInRgn rectangle exceeds aligned guest RAM.");rc=-1;}
            else answer=mac_rgn_rect_in(&g,rect_at(rt,r));
        }
    }
    if(rc==1){mac_cpu_write(rt->cpu,sp+pop,1,(unsigned)answer);wr(rt,0x220,2,0);mac_cpu_set(rt->cpu,MAC_A7,sp+pop);}
    mac_rgn_free(&g);mac_rgn_free(&o);return rc;
}
static int open_rgn(MacRuntime *rt);
static int close_rgn(MacRuntime *rt,uint32_t sp);
int mac_regions_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xa8d8 && opcode!=0xa8d9 && opcode!=0xa8da && opcode!=0xa8db && opcode!=0xa8dc && opcode!=0xa8dd && opcode!=0xa8de && opcode!=0xa8df && (opcode<0xa8e0 || opcode>0xa8e7) && opcode!=0xa8e8 && opcode!=0xa8e9)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(opcode==0xa8da)return open_rgn(rt);
    if(opcode==0xa8db)return close_rgn(rt,sp);
    if(opcode>=0xa8e4 && opcode<=0xa8e7)return region_algebra(rt,opcode,sp);
    if((opcode>=0xa8e0 && opcode<=0xa8e3) || opcode==0xa8e8 || opcode==0xa8e9)return region_query(rt,opcode,sp);
    unsigned stack=opcode==0xa8de?12:(opcode==0xa8df||opcode==0xa8dc)?8:4;
    if(!valid(rt,sp,stack))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    if(opcode==0xa8d8)return new_region(rt,sp);
    if(opcode==0xa8dc) { /* CopyRgn(src, dst): dst is on top */
        uint32_t hd=rd(rt,sp,4),hs=rd(rt,sp+4,4);
        MacHandle *md=lookup(rt,hd),*ms=lookup(rt,hs);
        if(!md||!ms)return fail(rt,"Region operation requires valid owned, non-resource, non-code handles.");
        MacRgn g;mac_rgn_init(&g);int rc=-1;
        if(!load(rt,ms,&g) && !store(rt,md,hd,&g)){mac_cpu_set(rt->cpu,MAC_A7,sp+8);rc=1;}
        mac_rgn_free(&g);return rc;
    }
    uint32_t h=rd(rt,sp+(opcode==0xa8df?4:opcode==0xa8de?8:0),4);
    MacHandle *m=lookup(rt,h);
    if(!m)return fail(rt,"Region operation requires a valid owned, non-resource, non-code handle.");
    if(opcode==0xa8d9) {
        if(m->type!=REGION_OWNED)return fail(rt,"DisposeRgn cannot dispose a port or window owned region.");
        wr(rt,h,4,0);memset(m,0,sizeof(*m));wr(rt,0x220,2,0);
        mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
    }
    /* RectRgn and SetRectRgn take a rectangle; SetEmptyRgn is the same replacement with none. An inverted or empty rectangle is empty. */
    MacRect box={0,0,0,0};
    if(opcode==0xa8df) {
        uint32_t r=rd(rt,sp,4);
        if(!valid(rt,r,8))return fail(rt,"RectRgn rectangle exceeds aligned guest RAM.");
        box=rect_at(rt,r);
    } else if(opcode==0xa8de) /* bottom, right, top, left, then the region */
        box=(MacRect){(int16_t)rd(rt,sp+4,2),(int16_t)rd(rt,sp+6,2),(int16_t)rd(rt,sp,2),(int16_t)rd(rt,sp+2,2)};
    MacRgn g;mac_rgn_init(&g);mac_rgn_set_rect(&g,box);
    int rc=store(rt,m,h,&g);
    mac_rgn_free(&g);
    if(rc)return -1;
    mac_cpu_set(rt->cpu,MAC_A7,sp+stack);return 1;
}

/* ---- OpenRgn .. CloseRgn ----
 * OpenRgn ($A8DA) starts recording the current port's outline calls; CloseRgn ($A8DB, destination region on the stack) ends it and stores
 * the region the outline encloses. Recorded here: MoveTo, LineTo, Move, Line and FrameRect, as axis-aligned closed paths filled even-odd
 * (each MoveTo starts a new sub-path, and every sub-path is closed back to its start, as QuickDraw closes the outline). The calls are
 * only observed. OpenRgn hides the pen (decrements pnVis, +66) and CloseRgn shows it again, so that nothing is drawn while the outline is
 * recorded: this is how I remember the QuickDraw documentation, not something the interface listing here states, and it is supported by
 * what Castle of Dr. Brain does (without it the region outlines it records appeared on screen). A diagonal segment, a second OpenRgn, a CloseRgn without OpenRgn, more than 4096 segments, or
 * a frame of an oval, round rectangle, arc, polygon or region while recording (outlines this module cannot capture) halts by name. */
typedef struct { int h0,v0,h1,v1; } Seg;
typedef struct MacRgnRec { int n,cur_v,cur_h,start_v,start_h; uint32_t port; Seg seg[4096]; } Rec;
void mac_rgn_record_destroy(MacRuntime *rt){free(rt->rgn_rec);rt->rgn_rec=NULL;}
static int add_seg(MacRuntime *rt,Rec *r,int h0,int v0,int h1,int v1) {
    if(h0==h1 && v0==v1)return 0;
    if(r->n>=4096)return fail(rt,"OpenRgn recorded more than 4096 outline segments.");
    r->seg[r->n++]=(Seg){h0,v0,h1,v1};return 0;
}
static int close_sub(MacRuntime *rt,Rec *r){return add_seg(rt,r,r->cur_h,r->cur_v,r->start_h,r->start_v);}
void mac_rgn_record_observe(MacRuntime *rt,uint16_t opcode) {
    Rec *r=rt->rgn_rec;
    if(!r || !rt->cpu || mac_cpu_halted(rt->cpu))return;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    switch(opcode) {
    case 0xa893: case 0xa891: case 0xa894: case 0xa892: {                      /* MoveTo, LineTo, Move, Line: (h, v) or (dh, dv); v is on top */
        if(!valid(rt,sp,4)){fail(rt,"Outline call stack exceeds guest RAM.");return;}
        int v=(int16_t)rd(rt,sp,2),h=(int16_t)rd(rt,sp+2,2);
        int rel=opcode==0xa894||opcode==0xa892,nh=rel?r->cur_h+h:h,nv=rel?r->cur_v+v:v;
        if(opcode==0xa893){if(close_sub(rt,r))return;r->start_h=nh;r->start_v=nv;}   /* a new sub-path */
        else if(opcode==0xa891 || opcode==0xa892){if(add_seg(rt,r,r->cur_h,r->cur_v,nh,nv))return;}
        r->cur_h=nh;r->cur_v=nv;return; }
    case 0xa8a1: {                                                              /* FrameRect(r): a closed rectangle of its own */
        if(!valid(rt,sp,4)){fail(rt,"Outline call stack exceeds guest RAM.");return;}
        uint32_t p=rd(rt,sp,4);
        if(!valid(rt,p,8)){fail(rt,"FrameRect rectangle exceeds guest RAM while recording a region.");return;}
        int t=(int16_t)rd(rt,p,2),l=(int16_t)rd(rt,p+2,2),b=(int16_t)rd(rt,p+4,2),rr=(int16_t)rd(rt,p+6,2);
        if(b<=t || rr<=l)return;
        if(add_seg(rt,r,l,t,rr,t)||add_seg(rt,r,rr,t,rr,b)||add_seg(rt,r,rr,b,l,b)||add_seg(rt,r,l,b,l,t))return;
        return; }
    case 0xa8b7: case 0xa8b0: case 0xa8be: case 0xa8c6: case 0xa8d2:
        fail(rt,"Framing an oval, round rectangle, arc, polygon or region while recording a region (OpenRgn) is unsupported.");return;
    default: return;
    }
}
static int open_rgn(MacRuntime *rt) {
    if(rt->rgn_rec)return fail(rt,"OpenRgn while a region is already being recorded is unsupported.");
    uint32_t port=rt->current_port;
    if(!valid(rt,port,108))return fail(rt,"OpenRgn needs a current port.");
    Rec *r=calloc(1,sizeof *r);
    if(!r)return fail(rt,"OpenRgn could not allocate its recording.");
    r->cur_v=r->start_v=(int16_t)rd(rt,port+48,2);r->cur_h=r->start_h=(int16_t)rd(rt,port+50,2);
    r->port=port;wr(rt,port+66,2,(uint16_t)((int16_t)rd(rt,port+66,2)-1));   /* the pen is hidden while the outline is recorded (see the comment above) */
    rt->rgn_rec=r;return 1;
}
static int close_rgn(MacRuntime *rt,uint32_t sp) {
    Rec *r=rt->rgn_rec;
    if(!r)return fail(rt,"CloseRgn without OpenRgn.");
    if(!valid(rt,sp,4))return fail(rt,"Region call stack exceeds aligned guest RAM.");
    uint32_t h=rd(rt,sp,4);MacHandle *m=lookup(rt,h);
    if(!m)return fail(rt,"CloseRgn requires a valid owned, non-resource, non-code handle.");
    if(close_sub(rt,r))return -1;
    static int edges[4096][3];int n=0;
    for(int i=0;i<r->n;i++) {
        Seg s=r->seg[i];
        if(s.h0!=s.h1 && s.v0!=s.v1)return fail(rt,"A diagonal line was recorded while building a region; only horizontal and vertical outlines are supported.");
        if(s.h0==s.h1){edges[n][0]=s.h0;edges[n][1]=s.v0<s.v1?s.v0:s.v1;edges[n][2]=s.v0<s.v1?s.v1:s.v0;n++;}
    }
    MacRgn g;mac_rgn_init(&g);const char *why=NULL;int rc=-1;
    if(mac_rgn_from_vedges(&g,(const int (*)[3])edges,n,&why))fail(rt,why?why:"The recorded outline could not be filled.");
    else if(!store(rt,m,h,&g)){
        if(valid(rt,r->port,108))wr(rt,r->port+66,2,(uint16_t)((int16_t)rd(rt,r->port+66,2)+1));   /* show the pen again */
        mac_cpu_set(rt->cpu,MAC_A7,sp+4);mac_rgn_record_destroy(rt);rc=1;
    }
    mac_rgn_free(&g);return rc;
}

/* Rectangle algebra for other modules (the window update region): op is $A8E4 sect, $A8E5 union, $A8E6 diff, $A8E7 xor; boxes are
 * {top,left,bottom,right}. Returns 1 with *out set when the result is one rectangle or empty, else 0. */
int mac_region_box_algebra(unsigned op,const int a[4],const int b[4],int out[4]) {
    MacRgn x,y,r;mac_rgn_init(&x);mac_rgn_init(&y);mac_rgn_init(&r);
    const char *why;int ok=0;
    if(!mac_rgn_set_rect(&x,(MacRect){a[0],a[1],a[2],a[3]}) && !mac_rgn_set_rect(&y,(MacRect){b[0],b[1],b[2],b[3]}) &&
       !mac_rgn_op((int)(op-0xa8e4),&x,&y,&r,&why) && r.n<=1) {
        MacRect bb;mac_rgn_bbox(&r,&bb);out[0]=bb.t;out[1]=bb.l;out[2]=bb.b;out[3]=bb.r;ok=1;
    }
    mac_rgn_free(&x);mac_rgn_free(&y);mac_rgn_free(&r);return ok;
}
