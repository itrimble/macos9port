#include "mac_runtime.h"
#include <string.h>

/* Independent CGrafPort layout from the public Color QuickDraw summary:
 * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-269.html
 * Owned plain patterns and rectangular regions only; no rendering callbacks. */
#define PORT_HANDLE_TYPE 0x43505254u
static const unsigned offsets[8] = {2, 8, 24, 28, 32, 58, 62, 0};
static const unsigned sizes[8] = {50, 26, 10, 10, 28, 28, 28, 2056};
static int valid(MacRuntime *rt, uint32_t p, size_t n) {
    if ((p & 1) || p > mac_cpu_ram_size(rt->cpu) || n > mac_cpu_ram_size(rt->cpu) - p) {
        mac_cpu_halt(rt->cpu, "Color port range exceeds aligned guest RAM."); return 0;
    }
    return 1;
}
static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }
static int fail(MacRuntime *rt, const char *why) { mac_cpu_halt(rt->cpu, why); return -1; }
static void rect(MacRuntime *rt, uint32_t p, int t, int l, int b, int r) {
    wr(rt,p,2,(uint16_t)t);wr(rt,p+2,2,(uint16_t)l);
    wr(rt,p+4,2,(uint16_t)b);wr(rt,p+6,2,(uint16_t)r);
}

int mac_color_port_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xaa00 && opcode != 0xaa01 && opcode != 0xaa02) return 0;
    if (mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!valid(rt, sp, 4)) return -1;
    uint32_t port = rd(rt, sp, 4);
    if (!port || !valid(rt, port, 108) || !rt->qd_globals || !valid(rt, rt->qd_globals, 4))
        return fail(rt, "Color port requires initialized QuickDraw globals and valid storage.");
    uint32_t h[8], p[8]; unsigned slots[8];
    if (opcode == 0xaa00) {
        if (mac_color_init(rt) < 0) return -1;
        unsigned found = 0;
        for (unsigned i=0;i<MAC_RUNTIME_HANDLES && found<8;i++)
            if (!rt->handles[i].handle && !rt->handles[i].pointer) slots[found++]=i;
        if (found != 8) return fail(rt, "No handles for color port.");
        uint32_t block = mac_runtime_alloc(rt, 32+50+26+10+10+28+28+28+2056);
        if (!block) return fail(rt, "No memory for color port.");
        uint32_t next = block+32;
        for (unsigned i=0;i<8;i++) {
            h[i]=block+i*4;p[i]=next;next+=sizes[i];
            wr(rt,h[i],4,p[i]);
            rt->handles[slots[i]]=(MacHandle){.handle=h[i],.pointer=p[i],.size=sizes[i],.type=PORT_HANDLE_TYPE};
            if(rt->handle_count<=slots[i])rt->handle_count=slots[i]+1;
        }
    } else {
        if ((rd(rt,port+6,2)&0xc000)!=0xc000) return fail(rt,"Expected an open color port.");
        for(unsigned i=0;i<8;i++) {
            h[i]=i==7?rd(rt,p[0]+42,4):rd(rt,port+offsets[i],4);
            unsigned j=0;
            while(j<rt->handle_count && rt->handles[j].handle!=h[i])j++;
            if(j==rt->handle_count || rt->handles[j].type!=PORT_HANDLE_TYPE || rt->handles[j].size!=sizes[i])
                return fail(rt,"Unsupported color port handle ownership.");
            slots[i]=j;p[i]=rt->handles[j].pointer;
            if (!valid(rt,h[i],4) || !p[i] || !valid(rt,p[i],sizes[i]) || rd(rt,h[i],4)!=p[i])
                return fail(rt,"Invalid owned color port handle.");
            for(unsigned k=0;k<i;k++)if(h[k]==h[i])return fail(rt,"Aliased color port handles.");
        }
    }
    if(opcode==0xaa02) {
        for(unsigned i=0;i<8;i++) { wr(rt,h[i],4,0);memset(&rt->handles[slots[i]],0,sizeof(MacHandle)); }
        /* The port storage belongs to the caller; disposal is a separate call. */
        for(unsigned i=0;i<7;i++)wr(rt,port+offsets[i],4,0);
        wr(rt,port+6,2,0);
    } else {
        uint32_t pm=rd(rt,rt->screen_pixmap,4), ct=rd(rt,rt->screen_colors,4);
        if(!valid(rt,pm,50)||!valid(rt,ct,2056))return -1;
        memset(mac_cpu_ram(rt->cpu)+port,0,108);
        for(unsigned i=0;i<8;i++)memset(mac_cpu_ram(rt->cpu)+p[i],0,sizes[i]);
        memcpy(mac_cpu_ram(rt->cpu)+p[0],mac_cpu_ram(rt->cpu)+pm,50);
        memcpy(mac_cpu_ram(rt->cpu)+p[7],mac_cpu_ram(rt->cpu)+ct,2056);
        wr(rt,p[0]+42,4,h[7]);wr(rt,p[7]+4,2,0); /* Non-device table. */
        for(unsigned i=0;i<256;i++)wr(rt,p[7]+8+i*8,2,i);
        for(unsigned i=0;i<7;i++)wr(rt,port+offsets[i],4,h[i]);
        wr(rt,port+6,2,0xc000);
        rect(rt,port+16,0,0,rt->screen_height,rt->screen_width);
        wr(rt,p[2],2,10);rect(rt,p[2]+2,0,0,rt->screen_height,rt->screen_width);
        wr(rt,p[3],2,10);rect(rt,p[3]+2,-32767,-32767,32767,32767);
        memset(mac_cpu_ram(rt->cpu)+p[5]+20,255,8);
        memset(mac_cpu_ram(rt->cpu)+p[6]+20,255,8);
        memset(mac_cpu_ram(rt->cpu)+port+42,255,6); /* White background RGB. */
        wr(rt,port+52,2,1);wr(rt,port+54,2,1);wr(rt,port+56,2,8);
        wr(rt,port+72,2,1);wr(rt,port+80,4,255);wr(rt,port+84,4,0);
        rt->current_port=port;wr(rt,rt->qd_globals,4,port);
    }
    mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
}
