#include "mac_runtime.h"
#include <stdio.h>
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
    case 0xa87c: case 0xa89d: case 0xa89b: case 0xa893: case 0xa89a: case 0xa891: case 0xa892: case 0xa894: case 0xa898: case 0xa899: args=4;break;
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
    } else if(opcode==0xa891 || opcode==0xa892 || opcode==0xa894) {
        /* LineTo(h, v), Line(dh, dv), Move(dh, dv): v is on top. A line paints the rectangle the pen sweeps (pen size at each point);
         * only horizontal and vertical lines are supported. A hidden pen (pnVis < 0) moves without drawing. Move never draws. */
        int v=(int16_t)rd(rt,sp,2),h=(int16_t)rd(rt,sp+2,2);
        int cv=(int16_t)rd(rt,port+48,2),ch=(int16_t)rd(rt,port+50,2);
        int nv=opcode==0xa891?v:cv+v,nh=opcode==0xa891?h:ch+h;
        if(opcode!=0xa894 && (int16_t)rd(rt,port+66,2)>=0) {
            int ph=(int16_t)rd(rt,port+52,2),pw=(int16_t)rd(rt,port+54,2);
            if(cv!=nv && ch!=nh)return fail(rt,"Diagonal lines on color ports are unsupported.");
            if(ph>0 && pw>0) {
                int t=cv<nv?cv:nv,l=ch<nh?ch:nh,b=(cv>nv?cv:nv)+ph,r=(ch>nh?ch:nh)+pw;
                if(mac_color_paint_box(rt,port,t,l,b,r)<0)return -1;
            }
        }
        wr(rt,port+48,2,(uint16_t)nv);wr(rt,port+50,2,(uint16_t)nh);
    } else if(opcode==0xa89b || opcode==0xa893) {
        uint16_t v=(uint16_t)rd(rt,sp,2),h=(uint16_t)rd(rt,sp+2,2);
        if(opcode==0xa89b && ((int16_t)v<0 || (int16_t)h<0))
            return fail(rt,"Negative color pen size is unsupported.");
        unsigned offset=opcode==0xa89b?52:48;
        wr(rt,port+offset,2,v);wr(rt,port+offset+2,2,h);
    } else if(opcode==0xa89c) {
        uint16_t mode=(uint16_t)rd(rt,sp,2);
        if(mode>15 && mode!=44) {   /* 0-7 act as 8-15 (srcOr = patOr and so on, see mac_color_raster.c) and 44 is transparent + patCopy; the other arithmetic modes ($20-$27), ditherCopy, hilite and anything else are refused */
            char why[96];snprintf(why,sizeof why,"Color pen transfer mode %u is unsupported (only 0 to 15, and transparent + patCopy, 44).",mode);
            return fail(rt,why);
        }
        wr(rt,port+56,2,mode);
    } else if(opcode==0xa898 || opcode==0xa899) {
        /* GetPenState / SetPenState (pnState): pnLoc (4), pnSize (4), pnMode (2), pnPat (8) = 18 bytes; the pattern is the old-style 8 bytes of the pen's PixPat. */
        uint32_t state=rd(rt,sp,4);
        if(!state || range(rt,state,18,0)<0)return fail(rt,"Invalid PenState address.");
        if(pattern(rt,port,58,&data)<0)return -1;
        uint8_t *ram=mac_cpu_ram(rt->cpu);
        if(opcode==0xa898) {
            wr(rt,state,4,rd(rt,port+48,4));wr(rt,state+4,4,rd(rt,port+52,4));wr(rt,state+8,2,rd(rt,port+56,2));
            memcpy(ram+state+10,ram+data+20,8);
        } else {
            uint32_t size=rd(rt,state+4,4);
            if((int16_t)(size>>16)<0 || (int16_t)(size&0xffff)<0)return fail(rt,"Negative color pen size is unsupported.");
            unsigned mode=rd(rt,state+8,2);
            if(mode>15 && mode!=44)return fail(rt,"Color pen transfer mode in SetPenState is unsupported.");
            wr(rt,port+48,4,rd(rt,state,4));wr(rt,port+52,4,size);wr(rt,port+56,2,mode);
            memcpy(ram+data+20,ram+state+10,8);
        }
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
