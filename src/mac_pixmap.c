#include "mac_runtime.h"
#include <string.h>

/* Independent implementation of the published NewPixMap/DisposePixMap ABI.
 * QuickDraw Reference, 2007-06-29, pp.195,297: the device record is copied,
 * but the color table is newly allocated and must be initialized by the app.
 * Pixel storage is borrowed; this module never allocates or disposes it. */
#define PIXMAP_OWNED UINT32_C(0x4f504d50)
#define CTABLE_OWNED UINT32_C(0x4f435442)
static int fail(MacRuntime *rt,const char *why){mac_cpu_halt(rt->cpu,why);return -1;}
static int valid(MacRuntime *rt,uint32_t p,uint32_t n) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && !(p&1) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(rt->cpu,p,n,v);}
static MacHandle *lookup(MacRuntime *rt,uint32_t h,uint32_t size) {
    if(!valid(rt,h,4))return NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle==h && m->size>=size && valid(rt,m->pointer,m->size) && rd(rt,h,4)==m->pointer)return m;
    }
    return NULL;
}
static int disposable_table(MacRuntime *rt,uint32_t h,MacHandle **out) {
    MacHandle *m=lookup(rt,h,8);
    if(!m || h==rt->screen_colors || m->resource || m->state&0x80 ||
       (m->type && m->type!=CTABLE_OWNED))return 0;
    int count=(int16_t)rd(rt,m->pointer+6,2);
    if(count< -1 || count>255 || (uint32_t)(8+(count+1)*8)>m->size)return 0;
    *out=m;return 1;
}
static void dispose(MacRuntime *rt,MacHandle *m) {
    wr(rt,m->handle,4,0);memset(m,0,sizeof(*m));
}
static int new_pixmap(MacRuntime *rt,uint32_t sp) {
    MacHandle *gd=lookup(rt,rt->current_device,62),*source;
    if(!gd || rt->current_device!=rt->main_device ||
       !(source=lookup(rt,rd(rt,gd->pointer+22,4),50)))
        return fail(rt,"NewPixMap requires a valid current screen device.");
    unsigned slots[2],found=0;
    for(unsigned i=0;i<MAC_RUNTIME_HANDLES && found<2;i++)
        if(!rt->handles[i].handle && !rt->handles[i].pointer)slots[found++]=i;
    uint32_t block=found==2?mac_runtime_alloc(rt,68):0;
    if(!block) {wr(rt,sp,4,0);wr(rt,0x220,2,(uint16_t)-108);return 1;}
    uint32_t pm=block+8,ct=block+60;
    /* 50-byte PixMap padded to a 4-byte allocation boundary. */
    memcpy(mac_cpu_ram(rt->cpu)+pm,mac_cpu_ram(rt->cpu)+source->pointer,50);
    wr(rt,block,4,pm);wr(rt,block+4,4,ct);wr(rt,pm+42,4,block+4);
    /* Deterministic empty header, rather than exposing uninitialized host data. */
    wr(rt,ct+6,2,0xffff);
    rt->handles[slots[0]]=(MacHandle){.handle=block,.pointer=pm,.size=50,.type=PIXMAP_OWNED};
    rt->handles[slots[1]]=(MacHandle){.handle=block+4,.pointer=ct,.size=8,.type=CTABLE_OWNED};
    for(unsigned i=0;i<2;i++)if(rt->handle_count<=slots[i])rt->handle_count=slots[i]+1;
    wr(rt,sp,4,block);wr(rt,0x220,2,0);return 1;
}
int mac_pixmap_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xaa03 && opcode!=0xaa04 && opcode!=0xaa24)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(!valid(rt,sp,4))return fail(rt,"PixMap call requires an aligned guest stack.");
    if(opcode==0xaa03)return new_pixmap(rt,sp);
    uint32_t h=rd(rt,sp,4);MacHandle *pm=NULL,*ct=NULL;
    if(opcode==0xaa04) {
        pm=lookup(rt,h,50);
        if(!pm || pm->type!=PIXMAP_OWNED || pm->resource || pm->state&0x80)
            return fail(rt,"DisposePixMap requires an owned unlocked PixMap.");
        uint32_t table=rd(rt,pm->pointer+42,4);
        if(table && !disposable_table(rt,table,&ct))
            return fail(rt,"DisposePixMap refuses an invalid, shared, or protected color table.");
        if(ct==pm)return fail(rt,"DisposePixMap has aliased record handles.");
        if(ct)for(size_t i=0;i<rt->handle_count;i++) {
            MacHandle *other=&rt->handles[i];
            if(other!=pm && other->handle && other->type==PIXMAP_OWNED &&
               other->size>=50 && valid(rt,other->pointer,50) &&
               rd(rt,other->pointer+42,4)==ct->handle)
                return fail(rt,"DisposePixMap color table is shared by another owned PixMap.");
        }
    } else if(!disposable_table(rt,h,&ct))
        return fail(rt,"DisposeCTable requires an owned unprotected color table.");
    /* All validation precedes changes; framebuffer memory is never touched. */
    if(ct)dispose(rt,ct);
    if(pm)dispose(rt,pm);
    wr(rt,0x220,2,0);mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
}
