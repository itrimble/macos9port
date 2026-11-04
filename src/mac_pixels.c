#include "mac_runtime.h"

/* Independently implemented public _QDExtensions selectors:
 * dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-323.html (LockPixels)
 * QuickDraw-324.html (UnlockPixels), QuickDraw-326.html (NoPurgePixels),
 * QuickDraw-329.html (GetPixBaseAddr).
 * Only NewPixMap records backed by an owned NewPtr allocation are supported.
 * Such storage cannot move or purge in this runtime; state bits record the
 * requested constraints. GWorld handle-to-pointer transitions are unsupported. */
#define PIXMAP_OWNED UINT32_C(0x4f504d50)
static int fail(MacRuntime *rt,const char *message) {
    mac_cpu_halt(rt->cpu,message);return -1;
}
static int range(MacRuntime *rt,uint32_t p,size_t n,int aligned) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && (!aligned || !(p&1)) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n) {return mac_cpu_read(rt->cpu,p,n);}
static MacHandle *pixel_buffer(MacRuntime *rt,uint32_t h) {
    if(!range(rt,h,4,1))return NULL;
    uint32_t p=rd(rt,h,4);
    MacHandle *map=NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle==h && m->pointer==p && m->size>=50 &&
           m->type==PIXMAP_OWNED && !m->resource) {map=m;break;}
    }
    if(!map || !range(rt,p,50,1))return NULL;
    uint32_t base=rd(rt,p,4),row=rd(rt,p+4,2),depth=rd(rt,p+32,2);
    int top=(int16_t)rd(rt,p+6,2),left=(int16_t)rd(rt,p+8,2);
    int bottom=(int16_t)rd(rt,p+10,2),right=(int16_t)rd(rt,p+12,2);
    if((row&0xc000)!=0x8000 || (depth!=1 && depth!=2 && depth!=4 && depth!=8) ||
       rd(rt,p+30,2)!=0 || rd(rt,p+34,2)!=1 || rd(rt,p+36,2)!=depth ||
       bottom<=top || right<=left)return NULL;
    uint32_t stride=row&0x3fff;
    uint64_t minimum=((uint64_t)(right-left)*depth+7)/8;
    uint64_t bytes=(uint64_t)stride*(unsigned)(bottom-top);
    if(stride<minimum || bytes>SIZE_MAX || !range(rt,base,(size_t)bytes,0))return NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(!m->handle && m->pointer==base && !m->resource && !m->type && m->size>=bytes)
            return m;
    }
    return NULL;
}
int mac_pixels_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xab1d)return 0;
    uint32_t selector=mac_cpu_get(rt->cpu,MAC_D0);
    if(selector!=0x00040001 && selector!=0x00040002 && selector!=0x0004000c &&
       selector!=0x0004000f)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    /* Pascal Boolean occupies a two-byte stack slot, with its byte at SP+4. */
    if(!range(rt,sp,selector==0x0004000f?8:selector==0x00040001?6:4,1))
        return fail(rt,"Pixel-memory call has an invalid argument/result stack range.");
    MacHandle *buffer=pixel_buffer(rt,rd(rt,sp,4));
    if(!buffer)return fail(rt,"Pixel-memory operation requires an owned indexed PixMap and complete NewPtr buffer; GWorld handle-backed storage is unsupported.");
    if(selector==0x0004000c)buffer->state&=(uint8_t)~0x40;
    else if(selector==0x00040002)buffer->state&=(uint8_t)~0x80;
    else if(selector==0x0004000f)mac_cpu_write(rt->cpu,sp+4,4,buffer->pointer);
    else {
        buffer->state|=0x80;
        mac_cpu_write(rt->cpu,sp+4,1,1);
    }
    mac_cpu_set(rt->cpu,MAC_A7,sp+4);
    return 1;
}
