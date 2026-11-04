#include "mac_runtime.h"
#include <string.h>

/* Independent old-pattern CGrafPort pen state from public QuickDraw contracts:
 * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-68.html
 * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-269.html
 * Color PixPat conversion and unsupported transfer modes remain explicit errors. */
#define PORT_HANDLE_TYPE 0x43505254u
static int fail(MacRuntime *rt, const char *why) {
    mac_cpu_halt(rt->cpu, why); return -1;
}
static int range(MacRuntime *rt, uint32_t p, size_t n, int aligned) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    if ((aligned && (p&1)) || p>size || n>size-p)
        return fail(rt,"Color pen address is outside guest RAM or misaligned.");
    return 1;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n) { return mac_cpu_read(rt->cpu,p,n); }
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v) { mac_cpu_write(rt->cpu,p,n,v); }
static int pattern(MacRuntime *rt,uint32_t port,unsigned offset,uint32_t *data) {
    uint32_t h=rd(rt,port+offset,4);
    if (!h || range(rt,h,4,1)<0) return fail(rt,"Invalid color pen pattern handle.");
    MacHandle *owned=NULL;
    for(size_t i=0;i<rt->handle_count;i++) if(rt->handles[i].handle==h) { owned=&rt->handles[i];break; }
    if(!owned || owned->type!=PORT_HANDLE_TYPE || owned->resource || owned->loaded_code || owned->size!=28)
        return fail(rt,"Color pen requires an owned plain port pattern.");
    *data=rd(rt,h,4);
    if(!*data || *data!=owned->pointer || range(rt,*data,28,1)<0)
        return fail(rt,"Color pen pattern master pointer is invalid.");
    if(rd(rt,*data,2)!=0) return fail(rt,"Color PixPat conversion is not implemented.");
    return 1;
}
int mac_color_pen_service(MacRuntime *rt,uint16_t opcode) {
    unsigned args;
    switch(opcode) {
    case 0xa87c: case 0xa89d: case 0xa89b: case 0xa893: case 0xa89a: args=4;break;
    case 0xa89c: args=2;break;
    case 0xa896: case 0xa897: case 0xa89e: args=0;break;
    default:return 0;
    }
    if(mac_cpu_halted(rt->cpu))return -1;
    if(!rt->qd_globals || range(rt,rt->qd_globals,4,1)<0)
        return fail(rt,"Color pen requires initialized QuickDraw globals.");
    uint32_t port=rd(rt,rt->qd_globals,4);
    if(!port || range(rt,port,108,1)<0)return fail(rt,"Invalid current graphics port.");
    if((rd(rt,port+6,2)&0xc000)!=0xc000)return 0;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7),data=0;
    if(range(rt,sp,args,1)<0)return -1;
    if(opcode==0xa87c || opcode==0xa89d || opcode==0xa89e) {
        uint8_t bits[8];
        if(opcode==0xa89e)memset(bits,255,sizeof bits);
        else {
            uint32_t source=rd(rt,sp,4);
            if(!source || range(rt,source,8,0)<0)return fail(rt,"Invalid old-style pattern address.");
            memcpy(bits,mac_cpu_ram(rt->cpu)+source,8);
        }
        if(pattern(rt,port,opcode==0xa87c?32:58,&data)<0)return -1;
        memcpy(mac_cpu_ram(rt->cpu)+data+20,bits,8);
        if(opcode==0xa89e) {
            wr(rt,port+52,2,1);wr(rt,port+54,2,1);wr(rt,port+56,2,8);
        }
    } else if(opcode==0xa89b || opcode==0xa893) {
        uint16_t v=(uint16_t)rd(rt,sp,2),h=(uint16_t)rd(rt,sp+2,2);
        if(opcode==0xa89b && ((int16_t)v<0 || (int16_t)h<0))
            return fail(rt,"Negative color pen size is unsupported.");
        unsigned offset=opcode==0xa89b?52:48;
        wr(rt,port+offset,2,v);wr(rt,port+offset+2,2,h);
    } else if(opcode==0xa89c) {
        uint16_t mode=(uint16_t)rd(rt,sp,2);
        if(mode<8 || mode>15)return fail(rt,"Color pen transfer mode is unsupported.");
        wr(rt,port+56,2,mode);
    } else if(opcode==0xa89a) {
        uint32_t out=rd(rt,sp,4),point=rd(rt,port+48,4);
        if(!out || range(rt,out,4,1)<0)return fail(rt,"Invalid GetPen output pointer.");
        wr(rt,out,4,point);
    } else {
        int visibility=(int16_t)rd(rt,port+66,2)+(opcode==0xa896?-1:1);
        if(visibility< -32768 || visibility>32767)return fail(rt,"Color pen visibility nesting overflow.");
        wr(rt,port+66,2,(uint16_t)visibility);
    }
    rt->current_port=port;
    mac_cpu_set(rt->cpu,MAC_A7,sp+args);return 1;
}
