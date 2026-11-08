#include <stdio.h>
#include "mac_window_int.h"
#include <string.h>

/* Independently implemented public WIND layout and Pascal calling convention:
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-225.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-293.html
 * The existing native NewCWindow implementation owns drawing and lifecycle.
 * Custom WDEFs and automatic positioning remain unsupported. A 'wctb' with the WIND's ID is honoured for the parts
 * the native chrome can show (public layout and part codes:
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-295.html): 0 content background, 1 frame, 2 title text. Parts 3 and
 * 4 are documented as reserved and have no effect; parts 5-12 (title-bar stripes, dialog frame, tinge) are not
 * rendered, so a table using them halts instead of being silently drawn wrongly. */
static uint16_t be16(const uint8_t *p){return (uint16_t)(p[0]*256u+p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)be16(p)<<16|be16(p+2);}
static void put16(uint8_t*p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t*p,uint32_t v){put16(p,(uint16_t)(v>>16));put16(p+2,(uint16_t)v);}
static int valid(MacRuntime *r,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(r->cpu);return p<=z && n<=z-p;}
static int overlaps(uint32_t p,size_t n,uint32_t q,size_t m){return m && (uint64_t)p+n>q && (uint64_t)q+m>p;}
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
/* A compiled 'wctb': 6 unused bytes, entry count minus one, then {part, r, g, b} records of four 16-bit words.
 * Any inconsistency rejects the whole table; nothing is partially applied. */
typedef struct { uint8_t set;uint16_t rgb[3][3]; } Colors;
static const char *parse_colors(const MacResource *t,Colors *out) {
    if(t->size<8)return "WIND window color table is truncated.";
    int16_t last=(int16_t)be16(t->data+6);
    if(last<0 || last>12)return "WIND window color table has an invalid entry count.";
    size_t entries=(size_t)last+1;
    if(t->size!=8+8*entries)return "WIND window color table size does not match its entry count.";
    unsigned seen=0;memset(out,0,sizeof *out);
    for(size_t i=0;i<entries;i++) {
        const uint8_t *e=t->data+8+8*i;unsigned id=be16(e);
        if(id>12)return "WIND window color table has an unknown part identifier.";
        if(seen&(1u<<id))return "WIND window color table repeats a part identifier.";
        seen|=1u<<id;
        if(id<=2){out->set|=(uint8_t)(1u<<id);for(unsigned k=0;k<3;k++)out->rgb[id][k]=be16(e+2+2*k);}
        else if(id>4)return "WIND window color table uses title-bar stripe, dialog-frame or tinge colors, which are unsupported.";
    }
    return NULL; /* parts 3 and 4 are reserved by the public description: accepted, no effect */
}
int mac_window_resource_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xaa46)return 0;
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if((sp&1) || !valid(rt,sp,14) || !valid(rt,0xa60,2))return fail(rt,"GetNewCWindow stack is invalid.");
    uint8_t *ram=mac_cpu_ram(rt->cpu);
    uint32_t behind=be32(ram+sp),storage=be32(ram+sp+4);
    int16_t id=(int16_t)be16(ram+sp+8);
    MacResource wind,colors;
    if(mac_resource_find(&rt->fork,UINT32_C(0x57494e44),id,&wind)!=0) {
        put32(ram+sp+10,0);put16(ram+0xa60,(uint16_t)-192);
        mac_cpu_set(rt->cpu,MAC_A7,sp+10);return 1;
    }
    const uint8_t *data=wind.data;size_t size=wind.size;
    /* Respect caller edits to an already-loaded WIND handle. A purged handle
     * falls back to its immutable resource backing, as loading it would. */
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *h=&rt->handles[i];
        if(!h->resource || h->type!=wind.type || h->id!=id)continue;
        if(!valid(rt,h->handle,4))return fail(rt,"WIND master pointer is invalid.");
        uint32_t p=be32(ram+h->handle);
        if(p) {
            if(p!=h->pointer || !valid(rt,p,h->size))return fail(rt,"WIND loaded record is invalid.");
            data=ram+p;size=h->size;
        }
        break;
    }
    if(size<19 || size<19u+data[18])return fail(rt,"WIND resource is truncated.");
    size_t end=19u+data[18],aligned=(end+1)&~(size_t)1;
    /* Bytes after the title: exactly the System 7 positioning word (read) or nothing is the documented layout. Longer resources exist (Room
     * Editor's WIND 128 has 250 stray bytes after its Pascal title); the title is length-prefixed, so they cannot change what is read here, and
     * they are ignored, positioning word included. That is my reading of the format, not something I verified against a real Mac. */
    if(size==aligned+2 && be16(data+aligned))return fail(rt,"WIND automatic positioning is unsupported.");
    Colors tint={0};
    if(mac_resource_find(&rt->fork,UINT32_C(0x77637462),id,&colors)==0) {
        const char *why=parse_colors(&colors,&tint);
        if(why)return fail(rt,why);
    }
    /* A temporary stack frame invokes the real NewCWindow path. All borrowed
     * bytes are restored even on a guest halt, and caller arguments survive. */
    enum { SCRATCH=294 }; /* NewCWindow30 + rectangle8 + Str255256. */
    if(sp<0x1000+SCRATCH || !valid(rt,sp-SCRATCH,SCRATCH+14))return fail(rt,"GetNewCWindow scratch stack is invalid.");
    uint32_t base=sp-SCRATCH;
    if((storage && overlaps(storage,156,base,SCRATCH+14)) ||
       overlaps(base,SCRATCH+14,rt->screen_base,(size_t)rt->screen_row_bytes*rt->screen_height) ||
       overlaps(base,SCRATCH+14,rt->qd_globals,4) ||
       (rt->heap_limit>rt->heap_next && overlaps(base,SCRATCH+14,rt->heap_next,rt->heap_limit-rt->heap_next)))
        return fail(rt,"GetNewCWindow stack aliases active guest storage.");
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *h=&rt->handles[i];
        if((h->handle && overlaps(base,SCRATCH+14,h->handle,4)) ||
           (h->pointer && overlaps(base,SCRATCH+14,h->pointer,h->size)))
            return fail(rt,"GetNewCWindow stack aliases an owned record.");
    }
    uint8_t saved[SCRATCH],frame[SCRATCH]={0};memcpy(saved,ram+base,SCRATCH);
    put32(frame,be32(data+14));frame[4]=data[12];put32(frame+6,behind);
    put16(frame+10,be16(data+8));frame[12]=data[10];
    put32(frame+14,base+38);put32(frame+18,base+30);put32(frame+22,storage);
    memcpy(frame+30,data,8);memcpy(frame+38,data+18,(size_t)data[18]+1);
    memcpy(ram+base,frame,SCRATCH);mac_cpu_set(rt->cpu,MAC_A7,base);
    int handled=mac_window_service(rt,0xaa45);
    uint32_t window=be32(ram+base+26);
    memcpy(ram+base,saved,SCRATCH);mac_cpu_set(rt->cpu,MAC_A7,sp);
    if(handled!=1 || mac_cpu_halted(rt->cpu))return -1;
    if(tint.set) { /* the window was just drawn with native colors: apply its table and repaint if it is showing */
        MacWindowState *state=mac_window_find(rt,window);
        if(state){state->color_set=tint.set;memcpy(state->color_rgb,tint.rgb,sizeof tint.rgb);}
        if(state && state==&rt->window && state->visible)mac_window_draw(rt,1);
    }
    put32(ram+sp+10,window);put16(ram+0xa60,0);
    mac_cpu_set(rt->cpu,MAC_A7,sp+10);return 1;
}
