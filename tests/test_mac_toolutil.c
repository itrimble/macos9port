/* Toolbox utilities: BitAnd/BitXor/BitNot/BitOr/BitShift/BitTst/BitSet/BitClr/HiWord/LoWord. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fails++;}}while(0)
#define SP 0x8000u
static int fails;
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static int call(MacRuntime*r,unsigned op){mac_cpu_set(r->cpu,MAC_A7,SP);return mac_toolutil_service(r,(uint16_t)op);}
int main(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    /* two-long ops: value1 below value2 on the stack, result above both */
    wr(&r,SP,4,0x0ff00ff0);wr(&r,SP+4,4,0x3c3c3c3c);wr(&r,SP+8,4,0xdead);CHECK(call(&r,0xa858)==1 && rd(&r,SP+8,4)==0x0c300c30 && mac_cpu_get(r.cpu,MAC_A7)==SP+8);
    wr(&r,SP,4,0x0ff00ff0);wr(&r,SP+4,4,0x3c3c3c3c);CHECK(call(&r,0xa859)==1 && rd(&r,SP+8,4)==0x33cc33cc);
    wr(&r,SP,4,0x0ff00ff0);wr(&r,SP+4,4,0x3c3c3c3c);CHECK(call(&r,0xa85b)==1 && rd(&r,SP+8,4)==0x3ffc3ffc);
    wr(&r,SP,4,0x0000ffff);wr(&r,SP+4,4,0xdead);CHECK(call(&r,0xa85a)==1 && rd(&r,SP+4,4)==0xffff0000 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    wr(&r,SP,4,0x12345678);wr(&r,SP+4,2,0);CHECK(call(&r,0xa86a)==1 && rd(&r,SP+4,2)==0x1234 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    wr(&r,SP,4,0x12345678);CHECK(call(&r,0xa86b)==1 && rd(&r,SP+4,2)==0x5678);
    /* BitShift(value, count): count on top */
    struct{uint32_t v;int c;uint32_t e;}sh[]={{1,4,16},{0x80000001,1,2},{0x80000000,-31,1},{0xf0,-4,0xf},{0xff,0,0xff},{0xff,32,0},{0xff,-32,0},{0xff,100,0},{0xff,-100,0},{0xffffffff,31,0x80000000}};
    for(unsigned i=0;i<10;i++){wr(&r,SP,2,(uint16_t)sh[i].c);wr(&r,SP+2,4,sh[i].v);wr(&r,SP+6,4,0xdead);CHECK(call(&r,0xa85c)==1 && rd(&r,SP+6,4)==sh[i].e && mac_cpu_get(r.cpu,MAC_A7)==SP+6);}
    /* bit tests count from the high bit; they run on into following bytes */
    uint8_t *m=mac_cpu_ram(r.cpu)+0x6000;memset(m,0,4);m[0]=0x80;m[1]=0x01;
    #define BIT(op,n) (wr(&r,SP,4,(uint32_t)(n)),wr(&r,SP+4,4,0x6000),wr(&r,SP+8,2,0xffff),call(&r,op))
    CHECK(BIT(0xa85d,0)==1 && rd(&r,SP+8,1)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+8);                           /* Boolean result: the high byte of the 2-byte slot */
    CHECK(BIT(0xa85d,1)==1 && rd(&r,SP+8,1)==0);CHECK(BIT(0xa85d,15)==1 && rd(&r,SP+8,1)==1);
    CHECK(BIT(0xa85e,2)==1 && m[0]==0xa0 && mac_cpu_get(r.cpu,MAC_A7)==SP+8);CHECK(BIT(0xa85f,0)==1 && m[0]==0x20);
    CHECK(BIT(0xa85e,24)==1 && m[3]==0x80);
    /* refusals: negative bit, address outside RAM or low memory, bad stack, other opcodes, halted CPU */
    CHECK(BIT(0xa85d,-1)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);wr(&r,SP,4,0x10);wr(&r,SP+4,4,0xfffffff0u);CHECK(call(&r,0xa85d)==-1);mac_cpu_destroy(r.cpu);
    r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);wr(&r,SP,4,1);wr(&r,SP+4,4,0x100);CHECK(call(&r,0xa85e)==-1);mac_cpu_destroy(r.cpu);
    r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);mac_cpu_set(r.cpu,MAC_A7,SP+1);CHECK(mac_toolutil_service(&r,0xa858)==-1);mac_cpu_destroy(r.cpu);
    r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-2);CHECK(mac_toolutil_service(&r,0xa86a)==-1);mac_cpu_destroy(r.cpu);
    r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(mac_toolutil_service(&r,0xa857)==0 && mac_toolutil_service(&r,0xa860)==0 && mac_toolutil_service(&r,0xa86c)==0);
    mac_cpu_halt(r.cpu,"x");CHECK(mac_toolutil_service(&r,0xa858)==-1);mac_cpu_destroy(r.cpu);
    if(!fails)puts("Toolbox utility checks passed");
    return fails?1:0;
}
