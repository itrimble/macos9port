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
/* Palette activation on the single 8-bit main device. The device table returns to its default for the current
 * type (color or gray), then each entry is satisfied by its usage: courteous entries use the nearest existing color
 * and change nothing; tolerant entries use an existing exact match or take the unclaimed device index (never 0 or
 * 255) whose current color is nearest, and are set to the exact RGB. A tolerant entry's tolerance only ever relaxes
 * that, so supplying the exact color always satisfies it. Entries inhibited for this device type (pmInhibitC8 on a
 * color device, pmInhibitG8 on a gray one) are skipped. Explicit entries (pmExplicit, alone or with the tolerant and animated bits) change
 * nothing - the interface says "no reserve, no render, no record; stuff index into port" - so they are skipped and map to their
 * own index. The interface does not say what explicit combined with animated means (no reserve versus reserve): when the explicit
 * bit is set it wins here, the entry reserves and renders nothing, and the animated bit's reservation is not honoured, so
 * animating such entries (AnimatePalette and the like) is separate and still halts by name; this is an interpretation, not
 * verified on real hardware. Dithered, plain animated, and explicit combined with dithered are refused before anything changes. The window is invalidated if the table changed. */
static unsigned nearest_index(const MacRuntime*r,uint32_t rgb,const uint8_t*claimed,unsigned lo,unsigned hi){
    unsigned best=lo;long bd=-1;
    for(unsigned i=lo;i<=hi;i++){
        if(claimed && claimed[i])continue;
        uint32_t c=r->palette[i];
        long dr=(long)((c>>16)&255)-(long)((rgb>>16)&255),dg=(long)((c>>8)&255)-(long)((rgb>>8)&255),db=(long)(c&255)-(long)(rgb&255);
        long d=dr*dr+dg*dg+db*db;if(bd<0 || d<bd){bd=d;best=i;}
    }
    return best;
}
/* Everything activation can refuse, checked before anything is changed (ShowWindow calls this first so a refused
 * palette leaves the window hidden). 0 ok, -1 with the CPU halted. */
int mac_palette_check(MacRuntime*r,const MacWindowState*w){
    if(!w || !w->palette_handle)return 0;
    MacHandle*m=palette(r,w->palette_handle);
    if(!m)return fail(r,"Window palette association is invalid.");
    if(!r->main_device || r->screen_depth!=8)return fail(r,"Palette activation is implemented for the 8-bit indexed device only.");
    uint32_t p=m->pointer,gd=rd(r,r->main_device,4);unsigned n=rd(r,p,2);
    if(!valid(r,gd,62))return fail(r,"Palette activation needs a valid main device record.");
    int gray=!(rd(r,gd+20,2)&1);
    uint32_t distinct[256];unsigned needed=0;
    for(unsigned i=0;i<n;i++){
        uint32_t e=p+16+16*i,usage=rd(r,e+6,2);
        if(usage&~0x3f3fu)return fail(r,"Palette entry has unsupported usage modifiers.");
        unsigned mode=usage&0xf;
        if(mode!=0 && mode!=2 && !((mode&8) && !(mode&1)))return fail(r,"Palette usage dithered, plain animated, or explicit combined with dithered is unsupported.");
        /* Only tolerant entries that are not inhibited for this device type take a device index. Activation never touches
         * index 0 (white) or 255 (black), so every other distinct color needs one of the 254 indices 1-254, whether it is
         * newly written or already present; repeats of a color share an index, and white and black need none. */
        if((usage&(gray?0x1000u:0x2000u)) || (usage&0xf)==0 || (usage&8))continue;
        uint32_t rgb=(rd(r,e,2)>>8)<<16|(rd(r,e+2,2)>>8)<<8|(rd(r,e+4,2)>>8);
        if(rgb==0xffffffu || rgb==0)continue;
        unsigned k=0;while(k<needed && distinct[k]!=rgb)k++;
        if(k==needed)distinct[needed++]=rgb;
    }
    if(needed>254)return fail(r,"Palette needs more device entries than the 8-bit device has free.");
    return 0;
}
int mac_palette_activate(MacRuntime*r,MacWindowState*w){
    if(!w || !w->palette_handle || w!=&r->window || !w->visible)return 0; /* only the frontmost visible window drives the device */
    if(mac_palette_check(r,w))return -1;
    MacHandle*m=palette(r,w->palette_handle);
    uint32_t p=m->pointer,gd=rd(r,r->main_device,4);unsigned n=rd(r,p,2);
    int gray=!(rd(r,gd+20,2)&1);
    uint32_t before[256];memcpy(before,r->palette,sizeof before);
    if(mac_color_reset_table(r))return fail(r,"Palette activation could not reset the device table.");
    uint8_t claimed[256]={0}; /* allocation searches indices 1-254 only, so white (0) and black (255) are never reallocated */
    for(unsigned i=0;i<n;i++){
        uint32_t e=p+16+16*i,usage=rd(r,e+6,2);
        uint16_t c16[3]={(uint16_t)rd(r,e,2),(uint16_t)rd(r,e+2,2),(uint16_t)rd(r,e+4,2)};
        uint32_t rgb=(uint32_t)(c16[0]>>8)<<16|(uint32_t)(c16[1]>>8)<<8|(c16[2]>>8);
        if(usage&(gray?0x1000u:0x2000u)){r->palette_map[i]=(uint8_t)nearest_index(r,rgb,NULL,0,255);continue;}
        if(usage&8){r->palette_map[i]=(uint8_t)i;continue;} /* explicit: the palette index is the pixel value; the device is untouched */
        if((usage&0xf)==0){r->palette_map[i]=(uint8_t)nearest_index(r,rgb,NULL,0,255);continue;}
        unsigned j=256;
        for(unsigned k=0;k<256;k++)if(r->palette[k]==rgb){j=k;break;}
        if(j==256){
            j=nearest_index(r,rgb,claimed,1,254);
            if(mac_color_set_entry(r,j,c16[0],c16[1],c16[2]))return fail(r,"Palette activation could not write a device entry.");
        }
        claimed[j]=1;r->palette_map[i]=(uint8_t)j;
    }
    r->palette_active=w->palette_handle;
    if(memcmp(before,r->palette,sizeof before) && w->handles[10]){ /* colors changed: the window must redraw */
        uint32_t region=rd(r,w->handles[10],4);
        if(region && valid(r,region,10)){wr(r,region,2,10);for(unsigned k=0;k<4;k++)wr(r,region+2+2*k,2,(uint16_t)w->bounds[k]);}
    }
    return 0;
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
    w->palette_handle=h;w->palette_updates=rd(r,sp,1)?0xe000:0xc000;
    if(mac_palette_activate(r,w))return -1; /* a visible frontmost window gets its palette realized now; others on show */
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
/* AnimatePalette(window, srcCTab, srcIndex, dstEntry, dstLength) ($AA9A): the colors of srcCTab entries srcIndex.. replace the
 * device colors of the window palette's entries dstEntry.., at once, with no redraw. Stack from SP: dstLength, dstEntry,
 * srcIndex, srcCTab (4), window (4). Only animated entries can change; an entry that is not animated halts by name rather than
 * being skipped or changed (the interface text here does not say which). The device index of an entry is the one activation
 * chose (palette_map), which for the explicit and animated entries Castle of Dr. Brain uses is the entry's own index. The
 * palette's stored colors are left as they are, so activating the palette again restores them: an interpretation, not verified
 * on real hardware. The window's palette must be the active one. Everything is checked before the first device entry changes. */
static int animate_palette(MacRuntime*r,uint32_t sp){
    if(!valid(r,sp,14))return fail(r,"AnimatePalette stack exceeds guest RAM.");
    int length=(int16_t)rd(r,sp,2),entry=(int16_t)rd(r,sp+2,2),first=(int16_t)rd(r,sp+4,2);
    uint32_t table=rd(r,sp+6,4);
    MacWindowState*w=mac_window_find(r,rd(r,sp+10,4));
    if(!w)return fail(r,"AnimatePalette requires a registered window.");
    if(!w->palette_handle)return fail(r,"AnimatePalette needs a window that has a palette.");
    if(r->palette_active!=w->palette_handle)return fail(r,"AnimatePalette needs the window's palette to be the active one.");
    MacHandle*m=palette(r,w->palette_handle);
    if(!m)return fail(r,"Window palette association is invalid.");
    if(length<0 || entry<0 || first<0)return fail(r,"AnimatePalette has a negative index or length.");
    unsigned n=rd(r,m->pointer,2);
    if((unsigned)entry+(unsigned)length>n)return fail(r,"AnimatePalette entries exceed the window's palette.");
    if(length==0){mac_cpu_set(r->cpu,MAC_A7,sp+14);return 1;}
    MacHandle*t=owned(r,table,8);
    if(!t)return fail(r,"AnimatePalette source color table is not a valid owned handle.");
    unsigned last=rd(r,t->pointer+6,2),have=last==65535?0:last+1;
    if(have>256 || t->size<8+have*8 || (unsigned)first+(unsigned)length>have)return fail(r,"AnimatePalette source entries exceed the color table.");
    for(int k=0;k<length;k++){
        uint32_t usage=rd(r,m->pointer+16+16*(entry+k)+6,2);
        if(!(usage&4))return fail(r,"AnimatePalette on an entry that is not animated is unsupported.");
    }
    for(int k=0;k<length;k++){
        uint32_t c=t->pointer+10+8*(first+k);
        if(mac_color_set_entry(r,r->palette_map[entry+k],(uint16_t)rd(r,c,2),(uint16_t)rd(r,c+2,2),(uint16_t)rd(r,c+4,2)))
            return fail(r,"AnimatePalette could not write a device entry.");
    }
    mac_cpu_set(r->cpu,MAC_A7,sp+14);return 1;
}
int mac_palette_manager_service(MacRuntime *r,uint16_t opcode){
    if(opcode==0xaa9a){
        if(mac_cpu_halted(r->cpu))return -1;
        return animate_palette(r,mac_cpu_get(r->cpu,MAC_A7));
    }
    if(opcode==0xaaa2){
        if(mac_cpu_halted(r->cpu))return -1;
        return palette_dispatch(r);
    }
    if(opcode!=0xaa91 && opcode!=0xaa92 && opcode!=0xaa93 && opcode!=0xaa94 && opcode!=0xaa95 && opcode!=0xaa96)return 0;
    if(mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if(opcode==0xaa92)return get_new_palette(r,sp);
    if(opcode==0xaa94){ /* ActivatePalette(window) */
        if(!valid(r,sp,4))return fail(r,"ActivatePalette stack exceeds guest RAM.");
        MacWindowState*w=mac_window_find(r,rd(r,sp,4));
        if(!w)return fail(r,"ActivatePalette requires a registered window.");
        if(mac_palette_activate(r,w))return -1;
        mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;
    }
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
