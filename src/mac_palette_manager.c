#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>

/* Independent public Palette/ColorInfo layout and NewPalette contract:
 * dev.os9.ca/techpubs/mac/ACI/ACI-28.html
 * dev.os9.ca/techpubs/mac/ACIReference/ACIReference-23.html
 * Object creation does not imply activation or hardware palette allocation. */
#define PALETTE_OWNED UINT32_C(0x504d504c)
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static int valid(MacRuntime*r,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(r->cpu);return p && !(p&1) && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static MacHandle *owned(MacRuntime*r,uint32_t h,size_t minimum){
    if(!valid(r,h,4))return NULL;
    for(size_t i=0;i<r->handle_count;i++){
        MacHandle *m=&r->handles[i];
        if(m->handle==h && !m->loaded_code && m->size>=minimum &&
           valid(r,m->pointer,m->size) && rd(r,h,4)==m->pointer)return m;
    }
    return NULL;
}
static MacHandle *palette(MacRuntime*r,uint32_t h) {
    MacHandle *m=owned(r,h,16);
    if(!m || m->resource || m->type!=PALETTE_OWNED)return NULL;
    unsigned n=rd(r,m->pointer,2);
    return n<=256 && m->size==16+n*16?m:NULL;
}
static int association(MacRuntime*r,uint16_t opcode,uint32_t sp){
    if(opcode==0xaa93){
        uint32_t h=rd(r,sp,4);MacHandle *m=palette(r,h);
        if(!m)return fail(r,"DisposePalette requires an owned Palette Manager object.");
        for(unsigned i=0;i<r->window_count;i++){
            MacWindowState *w=mac_window_find(r,r->window_order[i]);
            if(w && w->palette_handle==h && w->visible)return fail(r,"Disposing an activated palette requires device allocation release.");
        }
        for(unsigned i=0;i<r->window_count;i++){
            MacWindowState *w=mac_window_find(r,r->window_order[i]);
            if(w && w->palette_handle==h){w->palette_handle=0;w->palette_updates=0;}
        }
        wr(r,h,4,0);memset(m,0,sizeof *m);mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;
    }
    uint32_t address=rd(r,sp+(opcode==0xaa95?6:0),4);
    MacWindowState *w=mac_window_find(r,address);
    if(!w)return fail(r,"Palette association requires a registered window; default application palettes are unsupported.");
    if(opcode==0xaa96){
        if(w->palette_handle && !palette(r,w->palette_handle))return fail(r,"Window palette association is invalid.");
        wr(r,sp+4,4,w->palette_handle);mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;
    }
    uint32_t h=rd(r,sp+2,4);
    if(!palette(r,h))return fail(r,"SetPalette requires an owned Palette Manager object.");
    if(w->visible)return fail(r,"SetPalette for a visible window requires unimplemented Palette Manager activation.");
    w->palette_handle=h;w->palette_updates=rd(r,sp,1)?0xe000:0xc000;
    mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
}
/* _PaletteDispatch ($AAA2): the selector is the low word of D0, high byte = argument bytes, low byte = routine
 * (e.g. $0A14 = HasDepth, $0A13 = SetDepth; declaration pairs in the public MPW interface files). The only
 * display depth is the main device's 8-bit indexed mode, which has a color table and a gray table (the device's
 * "Grays" mode), so a request is supported at exactly that depth for either device type. whichFlags selects which
 * flag bits matter (bit 0 = gdDevType: 1 color, 0 gray); any other bit is not defined here and is refused. */
static int palette_dispatch(MacRuntime *r){
    uint32_t sel=mac_cpu_get(r->cpu,MAC_D0)&0xffff,sp=mac_cpu_get(r->cpu,MAC_A7);
    unsigned routine=sel&0xff,args=sel>>8;
    if((routine!=0x13 && routine!=0x14) || args!=10){
        static char message[80];
        snprintf(message,sizeof message,"PaletteDispatch selector 0x%04x is unsupported.",(unsigned)sel);
        return fail(r,message);
    }
    if(!valid(r,sp,12))return fail(r,"PaletteDispatch stack exceeds guest RAM.");
    uint32_t flags=rd(r,sp,2),which=rd(r,sp+2,2),depth=rd(r,sp+4,2),gd=rd(r,sp+6,4);
    int supported=gd && gd==r->main_device && r->screen_depth==depth && !(which&~1u);
    if(supported && routine==0x13 && (which&1) && mac_color_set_gray(r,!(flags&1)))supported=0;
    if(routine==0x14)wr(r,sp+10,2,supported?1:0);                  /* HasDepth: nonzero when available */
    else wr(r,sp+10,2,supported?0:(uint16_t)-50);                  /* SetDepth: noErr, or paramErr when unavailable */
    mac_cpu_set(r->cpu,MAC_A7,sp+10);
    return 1;
}
/* A palette object: Palette header (entry count + private words, zero) then 16-byte ColorInfo records
 * {RGB 6, usage 2, tolerance 2, private 6 (zero)}. Returns its handle, or 0 with nothing allocated. */
static uint32_t alloc_palette(MacRuntime*r,unsigned count,uint8_t colors[][6],const uint16_t*usage,const uint16_t*tolerance){
    unsigned slot=0;while(slot<MAC_RUNTIME_HANDLES && (r->handles[slot].handle || r->handles[slot].pointer))slot++;
    uint32_t size=16+count*16;
    uint32_t h=slot<MAC_RUNTIME_HANDLES?mac_runtime_alloc(r,size+4):0;
    if(h){
        uint32_t p=h+4;memset(mac_cpu_ram(r->cpu)+p,0,size);wr(r,h,4,p);wr(r,p,2,count);
        for(unsigned i=0;i<count;i++){
            memcpy(mac_cpu_ram(r->cpu)+p+16+i*16,colors[i],6);
            wr(r,p+22+i*16,2,usage[i]);wr(r,p+24+i*16,2,tolerance[i]);
        }
        r->handles[slot]=(MacHandle){.handle=h,.pointer=p,.size=size,.type=PALETTE_OWNED};
        if(r->handle_count<=slot)r->handle_count=slot+1;
    }
    return h;
}
/* GetNewPalette(paletteID): build a palette from a 'pltt' resource. The resource is "a series of ColorInfo structures
 * without the private fields" (public description); real resources are 16 + 16*count bytes, count first, with
 * non-zero leftover bytes in the header and private slots, so only the count and each entry's RGB, usage and
 * tolerance are read. The resource is detached by construction: it is read from the fork and never left loaded. */
static int get_new_palette(MacRuntime*r,uint32_t sp){
    if(!valid(r,sp,6))return fail(r,"GetNewPalette arguments or result exceed guest stack.");
    int16_t id=(int16_t)rd(r,sp,2),err=0;uint32_t h=0;MacResource res;
    if(mac_resource_find(&r->fork,MAC_RESOURCE_TYPE('p','l','t','t'),id,&res))err=-192;
    else {
        mac_resedit_apply(r,0,&res);
        unsigned count=res.size>=2?(unsigned)(res.data[0]<<8|res.data[1]):0;
        if(res.size<16 || res.size!=16u+16u*count)err=-199;
        else if(count>256)return fail(r,"GetNewPalette exceeds the supported 256-entry indexed palette.");
        else {
            uint8_t colors[256][6];uint16_t usage[256],tolerance[256];
            for(unsigned i=0;i<count;i++){
                const uint8_t*e=res.data+16+16*i;memcpy(colors[i],e,6);
                usage[i]=(uint16_t)(e[6]<<8|e[7]);tolerance[i]=(uint16_t)(e[8]<<8|e[9]);
            }
            h=alloc_palette(r,count,colors,usage,tolerance);
            wr(r,0x220,2,(uint16_t)(h?0:-108));
        }
    }
    wr(r,0xa60,2,(uint16_t)err);
    wr(r,sp+2,4,h);mac_cpu_set(r->cpu,MAC_A7,sp+2);return 1;
}
int mac_palette_manager_service(MacRuntime *r,uint16_t opcode){
    if(opcode==0xaaa2){
        if(mac_cpu_halted(r->cpu))return -1;
        return palette_dispatch(r);
    }
    if(opcode!=0xaa91 && opcode!=0xaa92 && opcode!=0xaa93 && opcode!=0xaa95 && opcode!=0xaa96)return 0;
    if(mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if(opcode==0xaa92)return get_new_palette(r,sp);
    if(opcode!=0xaa91){
        unsigned n=opcode==0xaa93?4:opcode==0xaa95?10:8;
        if(!valid(r,sp,n))return fail(r,"Palette Manager stack exceeds guest RAM.");
        return association(r,opcode,sp);
    }
    if(!valid(r,sp,14))return fail(r,"NewPalette arguments or result exceed guest stack.");
    int count=(int16_t)rd(r,sp+8,2);
    uint32_t source=rd(r,sp+4,4);uint16_t usage=(uint16_t)rd(r,sp+2,2),tolerance=(uint16_t)rd(r,sp,2);
    if(count<0 || count>256)return fail(r,"NewPalette exceeds the supported 256-entry indexed palette.");
    uint8_t colors[256][6]={{0}};
    if(source){
        MacHandle *table=owned(r,source,8);
        if(!table)return fail(r,"NewPalette source color table is not a valid owned handle.");
        unsigned last=rd(r,table->pointer+6,2),entries=last==65535?0:last+1;
        if(entries>256 || table->size<8+entries*8)return fail(r,"NewPalette source color table is truncated or oversized.");
        unsigned copy=entries<(unsigned)count?entries:(unsigned)count;
        for(unsigned i=0;i<copy;i++)memcpy(colors[i],mac_cpu_ram(r->cpu)+table->pointer+10+i*8,6);
    }
    uint16_t usages[256],tolerances[256];
    for(unsigned i=0;i<(unsigned)count;i++){usages[i]=usage;tolerances[i]=tolerance;}
    uint32_t h=alloc_palette(r,(unsigned)count,colors,usages,tolerances);
    wr(r,sp+10,4,h);wr(r,0x220,2,(uint16_t)(h?0:-108));
    mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
}
