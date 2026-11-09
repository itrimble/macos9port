#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mac_geometry_service(MacRuntime *rt, uint16_t opcode);
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define SP 0xe000u
#define RECT 0x1000u
#define RAM 0x10000u

static MacRuntime fresh(void) {
    MacRuntime rt = {0};
    rt.cpu = mac_cpu_create(RAM);
    CHECK(rt.cpu);
    mac_cpu_set(rt.cpu, MAC_A7, SP);
    return rt;
}

static void point_case(MacRuntime *rt, int16_t top, int16_t left,
                       int16_t bottom, int16_t right, int16_t x, int16_t y, int yes) {
    MacCpu *cpu = rt->cpu;
    mac_cpu_set(cpu, MAC_A7, SP);
    mac_cpu_write(cpu, SP, 4, RECT);
    mac_cpu_write(cpu, SP + 4, 2, (uint16_t)y);
    mac_cpu_write(cpu, SP + 6, 2, (uint16_t)x);
    mac_cpu_write(cpu, SP + 8, 2, 0x55aa);
    mac_cpu_write(cpu, RECT, 2, (uint16_t)top);
    mac_cpu_write(cpu, RECT + 2, 2, (uint16_t)left);
    mac_cpu_write(cpu, RECT + 4, 2, (uint16_t)bottom);
    mac_cpu_write(cpu, RECT + 6, 2, (uint16_t)right);
    uint8_t before[8];
    memcpy(before, mac_cpu_ram(cpu) + RECT, sizeof before);
    CHECK(mac_geometry_service(rt, 0xa8ad) == 1);
    CHECK(!mac_cpu_halted(cpu));
    CHECK(mac_cpu_read(cpu, SP + 8, 2) == ((unsigned)yes << 8 | 0xaa));
    CHECK(mac_cpu_get(cpu, MAC_A7) == SP + 8);
    CHECK(!memcmp(before, mac_cpu_ram(cpu) + RECT, sizeof before));
}

static void invalid_case(uint32_t sp, uint32_t rect) {
    MacRuntime rt = fresh();
    memset(mac_cpu_ram(rt.cpu), 0xcc, RAM);
    mac_cpu_set(rt.cpu, MAC_A7, sp);
    if (!(sp & 1) && sp <= RAM - 4) mac_cpu_write(rt.cpu, sp, 4, rect);
    uint8_t *before = malloc(RAM);
    CHECK(before);
    memcpy(before, mac_cpu_ram(rt.cpu), RAM);
    CHECK(mac_geometry_service(&rt, 0xa8ad) == -1);
    CHECK(mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp);
    CHECK(!memcmp(before, mac_cpu_ram(rt.cpu), RAM));
    free(before);
    mac_cpu_destroy(rt.cpu);
}

static void offset_case(const uint16_t edges[4], int16_t dh, int16_t dv) {
    MacRuntime rt = fresh();
    for (unsigned i=0;i<4;i++) mac_cpu_write(rt.cpu, RECT+i*2, 2, edges[i]);
    mac_cpu_write(rt.cpu, SP, 2, (uint16_t)dv);
    mac_cpu_write(rt.cpu, SP+2, 2, (uint16_t)dh);
    mac_cpu_write(rt.cpu, SP+4, 4, RECT);
    CHECK(mac_geometry_service(&rt,0xa8a8)==1);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==SP+8);
    for (unsigned i=0;i<4;i++) CHECK(mac_cpu_read(rt.cpu,RECT+i*2,2)==
        (uint16_t)(edges[i]+(i&1?dh:dv)));
    mac_cpu_destroy(rt.cpu);
}
static void offset_checks(void) {
    const uint16_t edges[4]={0xffec,0xfff6,30,40};
    offset_case(edges,13,-7);offset_case(edges,-32768,32767);
    const uint16_t extremes[4]={32767,32768,65535,0};
    offset_case(extremes, -1, 1);offset_case(extremes,32767,-32768);
    for(int kind=0;kind<4;kind++) {
        MacRuntime rt=fresh(); uint32_t sp=kind==3?RAM-6:SP;
        mac_cpu_set(rt.cpu,MAC_A7,sp);
        uint32_t rect=kind==0?RECT+1:kind==1?RAM-6:kind==2?0xfffffffc:RECT;
        if(kind!=3)mac_cpu_write(rt.cpu,sp+4,4,rect);
        uint8_t *saved=malloc(RAM);CHECK(saved);memcpy(saved,mac_cpu_ram(rt.cpu),RAM);
        CHECK(mac_geometry_service(&rt,0xa8a8)==-1);
        CHECK(!memcmp(saved,mac_cpu_ram(rt.cpu),RAM));
        CHECK(mac_cpu_get(rt.cpu,MAC_A7)==sp);free(saved);mac_cpu_destroy(rt.cpu);
    }
    /* Destination may alias arguments: later edges must use original words. */
    MacRuntime rt=fresh();mac_cpu_write(rt.cpu,SP,2,3);mac_cpu_write(rt.cpu,SP+2,2,5);
    mac_cpu_write(rt.cpu,SP+4,4,SP);uint16_t original[4]={3,5,0,SP};
    CHECK(mac_geometry_service(&rt,0xa8a8)==1);
    for(unsigned i=0;i<4;i++)CHECK(mac_cpu_read(rt.cpu,SP+i*2,2)==original[i]+(i&1?5:3));
    mac_cpu_destroy(rt.cpu);
}
static void set_point_checks(void) {
    const uint32_t destinations[] = {RECT, SP + 2, SP + 4, RAM - 4};
    for (unsigned i = 0; i < 4; i++) {
        MacRuntime rt = fresh();
        mac_cpu_write(rt.cpu, SP, 4, 0x80007fff);
        mac_cpu_write(rt.cpu, SP + 4, 4, destinations[i]);
        CHECK(mac_geometry_service(&rt, 0xa880) == 1);
        CHECK(mac_cpu_read(rt.cpu, destinations[i], 4) == 0x80007fff);
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP + 8);
        mac_cpu_destroy(rt.cpu);
    }
    const uint32_t bad[] = {0, RECT + 1, RAM - 2, UINT32_MAX};
    for (unsigned i = 0; i < 5; i++) {
        MacRuntime rt = fresh();
        uint32_t sp = i == 4 ? RAM - 6 : SP;
        mac_cpu_set(rt.cpu, MAC_A7, sp);
        if (i < 4) mac_cpu_write(rt.cpu, SP + 4, 4, bad[i]);
        uint8_t *saved = malloc(RAM); CHECK(saved);
        memcpy(saved, mac_cpu_ram(rt.cpu), RAM);
        CHECK(mac_geometry_service(&rt, 0xa880) == -1);
        CHECK(mac_cpu_get(rt.cpu, MAC_A7) == sp);
        CHECK(!memcmp(saved, mac_cpu_ram(rt.cpu), RAM));
        free(saved); mac_cpu_destroy(rt.cpu);
    }
}
/* AddPt / SubPt: dst := dst +/- src per coordinate, modulo 2^16; the source is by value, the destination by address; refusals pop nothing and write nothing. */
static void addsub_checks(void) {
    const struct { unsigned op; uint32_t dst, src, want; } c[] = {
        {0xa87e, 0x00100020, 0x00030004, 0x00130024}, {0xa87f, 0x00100020, 0x00030004, 0x000d001c},
        {0xa87f, 0x00000000, 0x00010001, 0xffffffff}, {0xa87e, 0x7fff7fff, 0x00010001, 0x80008000}, {0xa87e, 0xffffffff, 0x00010002, 0x00000001}};
    for (unsigned i = 0; i < 5; i++) {
        MacRuntime rt = fresh(); mac_cpu_set(rt.cpu, MAC_A7, SP);
        mac_cpu_write(rt.cpu, SP, 4, RECT); mac_cpu_write(rt.cpu, SP + 4, 4, c[i].src); mac_cpu_write(rt.cpu, RECT, 4, c[i].dst);
        CHECK(mac_geometry_service(&rt, (uint16_t)c[i].op) == 1 && mac_cpu_read(rt.cpu, RECT, 4) == c[i].want && mac_cpu_get(rt.cpu, MAC_A7) == SP + 8);
        mac_cpu_destroy(rt.cpu);
    }
    const uint32_t bad[] = {0, RECT + 1, RAM - 2, UINT32_MAX};
    for (unsigned i = 0; i < 4; i++) {
        MacRuntime rt = fresh(); mac_cpu_write(rt.cpu, SP, 4, bad[i]); mac_cpu_write(rt.cpu, SP + 4, 4, 0x00010001);
        CHECK(mac_geometry_service(&rt, 0xa87f) == -1 && mac_cpu_get(rt.cpu, MAC_A7) == SP);
        mac_cpu_destroy(rt.cpu);
    }
    {   MacRuntime rt = fresh(); mac_cpu_set(rt.cpu, MAC_A7, RAM - 4); CHECK(mac_geometry_service(&rt, 0xa87e) == -1); mac_cpu_destroy(rt.cpu); }
    {   MacRuntime rt = fresh(); CHECK(mac_geometry_service(&rt, 0xa87d) == 0 && mac_geometry_service(&rt, 0xa881) == 0); mac_cpu_destroy(rt.cpu); }
}
/* EqualRect: every edge must match (two empty rectangles with different edges are different). */
static int equal_rect(MacRuntime *r,const int16_t a[4],const int16_t b[4]){
    for(unsigned i=0;i<4;i++){mac_cpu_write(r->cpu,RECT+i*2,2,(uint16_t)a[i]);mac_cpu_write(r->cpu,RECT+8+i*2,2,(uint16_t)b[i]);}
    mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_write(r->cpu,SP,4,RECT+8);mac_cpu_write(r->cpu,SP+4,4,RECT);mac_cpu_write(r->cpu,SP+8,2,0x55aa);
    CHECK(mac_geometry_service(r,0xa8a6)==1 && mac_cpu_get(r->cpu,MAC_A7)==SP+8 && mac_cpu_read(r->cpu,SP+9,1)==0xaa);
    return mac_cpu_read(r->cpu,SP+8,1);
}
static void equal_checks(void){
    MacRuntime r=fresh();
    const int16_t base[4]={-10,-20,30,40};
    CHECK(equal_rect(&r,base,base)==1);
    for(unsigned i=0;i<4;i++){int16_t other[4];memcpy(other,base,sizeof other);other[i]=(int16_t)(other[i]+1);CHECK(equal_rect(&r,base,other)==0 && equal_rect(&r,other,base)==0);}
    const int16_t e1[4]={0,0,0,0},e2[4]={5,5,5,5},e3[4]={5,5,2,2};
    CHECK(equal_rect(&r,e1,e1)==1 && equal_rect(&r,e1,e2)==0 && equal_rect(&r,e2,e3)==0);
    const int16_t lo[4]={-32768,-32768,32767,32767};CHECK(equal_rect(&r,lo,lo)==1);
    mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_write(r.cpu,SP,4,0);mac_cpu_write(r.cpu,SP+4,4,RECT);CHECK(mac_geometry_service(&r,0xa8a6)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    r=fresh();mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_write(r.cpu,SP,4,RAM-4);mac_cpu_write(r.cpu,SP+4,4,RECT);CHECK(mac_geometry_service(&r,0xa8a6)==-1 && mac_cpu_get(r.cpu,MAC_A7)==SP);mac_cpu_destroy(r.cpu);
    r=fresh();mac_cpu_set(r.cpu,MAC_A7,RAM-6);CHECK(mac_geometry_service(&r,0xa8a6)==-1);mac_cpu_destroy(r.cpu);
}
static void intersections(void) {
    const int16_t inputs[][8]={{-10,-20,30,40,0,-30,50,10},{0,0,10,10,10,0,20,10},{10,0,-10,20,-20,-20,40,40},{-32768,-32768,32767,32767,0,1,2,3}};
    const int16_t expected[][4]={{0,-20,30,10},{0,0,0,0},{0,0,0,0},{0,1,2,3}};
    for(unsigned k=0;k<4;k++)for(unsigned alias=0;alias<3;alias++) {
        MacRuntime r=fresh();uint32_t dst=RECT+alias*8;
        for(unsigned i=0;i<8;i++)mac_cpu_write(r.cpu,RECT+i*2,2,(uint16_t)inputs[k][i]);
        mac_cpu_write(r.cpu,SP,4,dst);mac_cpu_write(r.cpu,SP+4,4,RECT+8);mac_cpu_write(r.cpu,SP+8,4,RECT);mac_cpu_write(r.cpu,SP+12,2,0x55aa);
        CHECK(mac_geometry_service(&r,0xa8aa)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+12);
        for(unsigned i=0;i<4;i++)CHECK((int16_t)mac_cpu_read(r.cpu,dst+2*i,2)==expected[k][i]);
        CHECK(mac_cpu_read(r.cpu,SP+12,2)==((k==0||k==3)?0x01aa:0x00aa));mac_cpu_destroy(r.cpu);
    }
    MacRuntime r=fresh();mac_cpu_write(r.cpu,SP,4,RAM-4);mac_cpu_write(r.cpu,SP+4,4,RECT);mac_cpu_write(r.cpu,SP+8,4,RECT);
    memset(mac_cpu_ram(r.cpu)+RAM-4,0x5a,4);CHECK(mac_geometry_service(&r,0xa8aa)==-1);
    CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP && mac_cpu_ram(r.cpu)[RAM-1]==0x5a);mac_cpu_destroy(r.cpu);
}
static void unions(void) {
    const int16_t inputs[][8]={{-10,-20,30,40,0,-30,50,10},{0,0,10,10,10,0,20,10},{10,0,-10,20,-20,-20,40,40},{-32768,-32768,0,0,0,0,32767,32767},{5,5,5,5,1,1,2,2}};
    const int16_t expected[][4]={{-10,-30,50,40},{0,0,20,10},{-20,-20,40,40},{-32768,-32768,32767,32767},{1,1,5,5}};
    for(unsigned k=0;k<5;k++)for(unsigned alias=0;alias<3;alias++) {
        MacRuntime r=fresh();uint32_t dst=RECT+alias*8;
        for(unsigned i=0;i<8;i++)mac_cpu_write(r.cpu,RECT+i*2,2,(uint16_t)inputs[k][i]);
        mac_cpu_write(r.cpu,SP,4,dst);mac_cpu_write(r.cpu,SP+4,4,RECT+8);mac_cpu_write(r.cpu,SP+8,4,RECT);mac_cpu_write(r.cpu,SP+12,2,0x55aa);
        CHECK(mac_geometry_service(&r,0xa8ab)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+12);
        for(unsigned i=0;i<4;i++)CHECK((int16_t)mac_cpu_read(r.cpu,dst+2*i,2)==expected[k][i]);
        CHECK(mac_cpu_read(r.cpu,SP+12,2)==0x55aa);mac_cpu_destroy(r.cpu);               /* a procedure: nothing above its arguments changes */
    }
    for(unsigned k=0;k<5;k++) {                                                          /* NIL and out-of-RAM rectangles, or a short stack, halt and write nothing */
        MacRuntime r=fresh();mac_cpu_write(r.cpu,SP,4,k==0?0:k==3?RAM-4:RECT+16);mac_cpu_write(r.cpu,SP+4,4,k==1?0:k==4?RAM-4:RECT+8);mac_cpu_write(r.cpu,SP+8,4,k==2?0:RECT);
        memset(mac_cpu_ram(r.cpu)+RECT+16,0x5a,8);
        if(k==4)mac_cpu_write(r.cpu,SP+4,4,RAM-4);
        CHECK(mac_geometry_service(&r,0xa8ab)==-1 && mac_cpu_halted(r.cpu) && mac_cpu_get(r.cpu,MAC_A7)==SP);
        for(unsigned i=0;i<8;i++)CHECK(mac_cpu_ram(r.cpu)[RECT+16+i]==0x5a);
        mac_cpu_destroy(r.cpu);
    }
    MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,RAM-8);CHECK(mac_geometry_service(&r,0xa8ab)==-1);mac_cpu_destroy(r.cpu);
}

int main(void) {
    addsub_checks();
    equal_checks();
    intersections();
    unions();
    set_point_checks();
    offset_checks();
    MacRuntime rt = fresh();
    CHECK(mac_geometry_service(&rt, 0xafff) == 0);
    CHECK(mac_cpu_get(rt.cpu, MAC_A7) == SP && !mac_cpu_halted(rt.cpu));
    point_case(&rt, -20, -10, 30, 40, -10, -20, 1);
    point_case(&rt, -20, -10, 30, 40, 39, 29, 1);
    point_case(&rt, -20, -10, 30, 40, 40, 29, 0);
    point_case(&rt, -20, -10, 30, 40, 39, 30, 0);
    point_case(&rt, -20, -10, 30, 40, -11, 0, 0);
    point_case(&rt, -20, -10, 30, 40, 0, -21, 0);
    point_case(&rt, 0, 0, 0, 1, 0, 0, 0); /* Empty height. */
    point_case(&rt, 0, 0, 1, 0, 0, 0, 0); /* Empty width. */
    point_case(&rt, 10, 10, -10, -10, 0, 0, 0); /* Inverted. */
    point_case(&rt, -32768, -32768, 32767, 32767, -32768, -32768, 1);
    point_case(&rt, -32768, -32768, 32767, 32767, 32767, 0, 0);
    point_case(&rt, -32768, -32768, 32767, 32767, 0, 32767, 0);
    /* Exhaust boundaries with intentionally asymmetric coordinates. */
    for (int y = -4; y <= 5; y++) for (int x = -7; x <= 8; x++)
        point_case(&rt, -3, -6, 4, 7, x, y, x >= -6 && x < 7 && y >= -3 && y < 4);
    mac_cpu_destroy(rt.cpu);
    invalid_case(SP, RECT + 1);
    invalid_case(SP, RAM - 6);
    invalid_case(SP, 0xfffffffc);
    invalid_case(SP + 1, RECT);
    invalid_case(RAM - 8, RECT); /* Valid arguments, missing result slot. */
    invalid_case(0xfffffffc, RECT);
    puts("mac geometry ABI tests passed");
    return 0;
}
