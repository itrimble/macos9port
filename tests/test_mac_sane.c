#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>

int mac_sane_service(MacRuntime *rt,uint16_t opcode);
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
enum {SP=0x1000,SRC=0x200,DST=0x300};
static void put(MacRuntime*r,unsigned p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t get(MacRuntime*r,unsigned p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void ext(MacRuntime*r,unsigned p,uint16_t exponent,uint64_t sig){put(r,p,2,exponent);put(r,p+2,4,(uint32_t)(sig>>32));put(r,p+6,4,(uint32_t)sig);}
static int call(MacRuntime*r,unsigned op){put(r,SP,2,op);put(r,SP+2,4,DST);put(r,SP+6,4,SRC);mac_cpu_set(r->cpu,MAC_A7,SP);return mac_sane_service(r,0xa9eb);}
static void init(MacRuntime*r){memset(r,0,sizeof *r);r->cpu=mac_cpu_create(65536);CHECK(r->cpu!=NULL);}
static void equal_ext(MacRuntime*r,uint16_t exponent,uint64_t sig){CHECK(get(r,DST,2)==exponent);CHECK(get(r,DST+2,4)==(uint32_t)(sig>>32));CHECK(get(r,DST+6,4)==(uint32_t)sig);}
static void conversions(void) {
    MacRuntime r;init(&r);
    const struct {uint32_t single;uint16_t exponent;uint64_t sig;} cases[]={
        {0,0,0},{0x80000000,0x8000,0},{0x3f800000,0x3fff,UINT64_C(0x8000000000000000)},
        {0xbfc00000,0xbfff,UINT64_C(0xc000000000000000)},
        {1,16234,UINT64_C(0x8000000000000000)},
        {0x007fffff,16256,UINT64_C(0xfffffe0000000000)},
        {0x00800000,16257,UINT64_C(0x8000000000000000)},
        {0x7f7fffff,16510,UINT64_C(0xffffff0000000000)},
        {0x7f800000,0x7fff,UINT64_C(0x8000000000000000)},
        {0xff800000,0xffff,UINT64_C(0x8000000000000000)}};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        put(&r,SRC,4,cases[i].single);CHECK(call(&r,0x100e)==1);
        equal_ext(&r,cases[i].exponent,cases[i].sig);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+10 && !r.sane_environment);
    }
    put(&r,SRC,4,0x80000000);CHECK(call(&r,0x280e)==1);equal_ext(&r,0xc01e,UINT64_C(0x8000000000000000));
    put(&r,SRC,2,0x8000);CHECK(call(&r,0x200e)==1);equal_ext(&r,0xc00e,UINT64_C(0x8000000000000000));
    /* FC2X ($300E): comp, a signed 64-bit integer, to extended; the comp NaN has no magnitude and halts by name */
    put(&r,SRC,4,0);put(&r,SRC+4,4,1);CHECK(call(&r,0x300e)==1);equal_ext(&r,0x3fff,UINT64_C(0x8000000000000000));
    put(&r,SRC,4,0xffffffff);put(&r,SRC+4,4,0xfffffffd);CHECK(call(&r,0x300e)==1);equal_ext(&r,0xc000,UINT64_C(0xc000000000000000));   /* -3 */
    put(&r,SRC,4,0x00000001);put(&r,SRC+4,4,0);CHECK(call(&r,0x300e)==1);equal_ext(&r,0x401f,UINT64_C(0x8000000000000000));            /* 2^32 */
    put(&r,SRC,4,0x7fffffff);put(&r,SRC+4,4,0xffffffff);CHECK(call(&r,0x300e)==1);equal_ext(&r,0x403d,UINT64_C(0xfffffffffffffffe));    /* 2^63 - 1 */
    put(&r,SRC,4,0);put(&r,SRC+4,4,0);CHECK(call(&r,0x300e)==1);equal_ext(&r,0,0);
    /* arithmetic with a source that is not extended: dst := dst op src (FDIVD $0806, FMULI $2004, FSUBS $1002, FADDC $3000, FDIVL $2806) */
    ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,4,0x40000000);put(&r,SRC+4,4,0);CHECK(call(&r,0x0806)==1);equal_ext(&r,0x3fff,UINT64_C(0xc000000000000000));   /* 3.0 / 2.0 */
    CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+10);
    ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,2,0xfffe);CHECK(call(&r,0x2004)==1);equal_ext(&r,0xc001,UINT64_C(0xc000000000000000));                              /* 3.0 * -2 = -6 */
    ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,4,0x3f800000);CHECK(call(&r,0x1002)==1);equal_ext(&r,0x4000,UINT64_C(0x8000000000000000));                          /* 3.0 - 1.0f = 2.0 */
    ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,4,0);put(&r,SRC+4,4,5);CHECK(call(&r,0x3000)==1);equal_ext(&r,0x4002,UINT64_C(0x8000000000000000));   /* 3.0 + comp 5 = 8.0 */
    ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,4,2);CHECK(call(&r,0x2806)==1);equal_ext(&r,0x3fff,UINT64_C(0xc000000000000000));                                    /* 3.0 / long 2 */
    /* FCMPX ($0008) and FCPXX ($000A), and compares with another source format: the condition codes carry the order, dst is untouched, the operands are popped */
    {   const struct {uint16_t de;uint64_t ds;uint16_t se;uint64_t ss;unsigned z,n;} rel[]={
            {0x4000,UINT64_C(0xc000000000000000),0x4000,UINT64_C(0xc000000000000000),1,0},   /* 3 = 3 */
            {0x4000,UINT64_C(0xc000000000000000),0x4001,UINT64_C(0x8000000000000000),0,1},   /* 3 < 4 */
            {0x4001,UINT64_C(0x8000000000000000),0x4000,UINT64_C(0xc000000000000000),0,0},   /* 4 > 3 */
            {0x0000,0,0x8000,0,1,0},                                                         /* +0 = -0 */
            {0xc000,UINT64_C(0xc000000000000000),0x3fff,UINT64_C(0x8000000000000000),0,1},   /* -3 < 1 */
            {0x3fff,UINT64_C(0x8000000000000000),0xc000,UINT64_C(0xc000000000000000),0,0},   /* 1 > -3 */
            {0xc000,UINT64_C(0xc000000000000000),0xc001,UINT64_C(0x8000000000000000),0,0},   /* -3 > -4 */
            {0x7fff,UINT64_C(0x8000000000000000),0x7ffe,UINT64_C(0xffffffffffffffff),0,0}};  /* +inf > big */
        for(unsigned i=0;i<sizeof rel/sizeof rel[0];i++) for(unsigned op=8;op<=10;op+=2) {
            ext(&r,DST,rel[i].de,rel[i].ds);ext(&r,SRC,rel[i].se,rel[i].ss);mac_cpu_set(r.cpu,MAC_SR,0x271f);
            CHECK(call(&r,op)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
            CHECK(((mac_cpu_get(r.cpu,MAC_SR)>>2)&1)==rel[i].z && ((mac_cpu_get(r.cpu,MAC_SR)>>3)&1)==rel[i].n && (mac_cpu_get(r.cpu,MAC_SR)&0xff00)==0x2700);
            CHECK(get(&r,DST,2)==rel[i].de && get(&r,DST+2,4)==(uint32_t)(rel[i].ds>>32));
        }
        ext(&r,DST,0x4000,UINT64_C(0xc000000000000000));put(&r,SRC,4,0x40400000);mac_cpu_set(r.cpu,MAC_SR,0x2700);                  /* 3.0 against single 3.0 */
        CHECK(call(&r,0x1008)==1 && (mac_cpu_get(r.cpu,MAC_SR)&0x1f)==4);
        put(&r,SRC,2,5);CHECK(call(&r,0x2008)==1 && (mac_cpu_get(r.cpu,MAC_SR)&0x1f)==8);                                           /* 3.0 against integer 5 */
    }
    MacRuntime n;init(&n);put(&n,SRC,4,0x80000000);put(&n,SRC+4,4,0);CHECK(call(&n,0x300e)==-1 && mac_cpu_halted(n.cpu) && strstr(mac_cpu_error(n.cpu),"comp NaN"));mac_cpu_destroy(n.cpu);
    init(&n);CHECK(call(&n,0x3a0e)==-1 && strstr(mac_cpu_error(n.cpu),"selector $3A0E"));mac_cpu_destroy(n.cpu);   /* any other selector halts and names itself */
    ext(&r,SRC,0x3fff,UINT64_C(0x8000000000000001));CHECK(call(&r,0x0010)==1);
    equal_ext(&r,0x3fff,UINT64_C(0x8000000000000001)); /* Preserve full64-bit significand. */
    mac_cpu_destroy(r.cpu);
}
static void truncate_tests(void) {
    MacRuntime r;init(&r);
    const struct {uint16_t e;uint64_t sig;uint16_t out_e;uint64_t out;int inexact;} cases[]={
        {0x3fff,UINT64_C(0xe000000000000000),0x3fff,UINT64_C(0x8000000000000000),1},
        {0xbfff,UINT64_C(0xe000000000000000),0xbfff,UINT64_C(0x8000000000000000),1},
        {0xbffe,UINT64_C(0x8000000000000000),0x8000,0,1},
        {0,1,0,0,1},{0x8000,1,0x8000,0,1},{0x8000,0,0x8000,0,0},
        {0x403e,UINT64_MAX,0x403e,UINT64_MAX,0},
        {0x403d,UINT64_MAX,0x403d,UINT64_MAX-1,1},
        {0x7fff,UINT64_C(0x8000000000000000),0x7fff,UINT64_C(0x8000000000000000),0}};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
        ext(&r,DST,cases[i].e,cases[i].sig);r.sane_environment=0;
        CHECK(call(&r,0x16)==1);equal_ext(&r,cases[i].out_e,cases[i].out);
        CHECK(r.sane_environment==(cases[i].inexact?0x1000:0));CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+6);
    }
    mac_cpu_destroy(r.cpu);
}
static void integer_tests(void) {
    MacRuntime r;init(&r);
    for(unsigned negative=0;negative<2;negative++)for(unsigned mode=0;mode<4;mode++) {
        ext(&r,SRC,(uint16_t)(0x4000|(negative?0x8000:0)),UINT64_C(0xa000000000000000)); /* +/-2.5 */
        r.sane_environment=(uint16_t)(mode<<13);CHECK(call(&r,0x2810)==1);
        int expected=mode==1&&!negative?3:mode==2&&negative?3:2;
        if(negative)expected=-expected;CHECK((int32_t)get(&r,DST,4)==expected);
        CHECK(r.sane_environment==((mode<<13)|0x1000));
    }
    ext(&r,SRC,0x4000,UINT64_C(0xe000000000000000));r.sane_environment=0;
    CHECK(call(&r,0x2010)==1 && get(&r,DST,2)==4); /* 3.5 ties to even. */
    ext(&r,SRC,0xc01e,UINT64_C(0x8000000000000000));CHECK(call(&r,0x2810)==1 && get(&r,DST,4)==0x80000000);
    ext(&r,SRC,0x401d,UINT64_C(0xfffffffe00000000));CHECK(call(&r,0x2810)==1 && get(&r,DST,4)==0x7fffffff);
    ext(&r,SRC,0xbffe,UINT64_C(0x8000000000000000));r.sane_environment=0;CHECK(call(&r,0x2810)==1 && !get(&r,DST,4));
    put(&r,DST,2,0x4000);CHECK(call(&r,1)==1 && r.sane_environment==0x4000);
    put(&r,DST,2,0);CHECK(call(&r,3)==1 && get(&r,DST,2)==0x4000);
    mac_cpu_destroy(r.cpu);
}
static void failure_tests(void) {
    const unsigned selectors[]={0x100e,0x2810,0x16,0x000c};   /* the last: FREMX, not implemented */
    for(unsigned i=0;i<4;i++) {
        MacRuntime r;init(&r);memset(mac_cpu_ram(r.cpu)+DST,0xa5,10);
        if(i==0)put(&r,SRC,4,0x7fc00000);
        if(i==1)ext(&r,SRC,0x401e,UINT64_C(0x8000000000000000));
        if(i==2){ext(&r,DST,0x3fff,UINT64_C(0xc000000000000000));r.sane_environment=0x10;}
        uint8_t before[10];memcpy(before,mac_cpu_ram(r.cpu)+DST,10);uint16_t env=r.sane_environment;
        CHECK(call(&r,selectors[i])==-1 && mac_cpu_halted(r.cpu));
        CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+DST,10));CHECK(r.sane_environment==env);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);mac_cpu_destroy(r.cpu);
    }
    for(unsigned i=0;i<3;i++) {
        MacRuntime r;init(&r);put(&r,SP,2,0x100e);put(&r,SP+2,4,i==0?65530:DST);put(&r,SP+6,4,i==1?65534:SRC);
        mac_cpu_set(r.cpu,MAC_A7,i==2?UINT32_MAX-1:SP);
        CHECK(mac_sane_service(&r,0xa9eb)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    }
}
static void encoding_edges(void) {
    const struct {uint16_t exponent;uint64_t sig;} bad[]={
        {1,0},{0x3fff,0},{0xbfff,0},{0,UINT64_C(0x8000000000000000)},
        {0x8000,UINT64_C(0x8000000000000000)},{0x3fff,1}};
    const unsigned ops[]={0x0010,0x0016,0x2810};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++)for(unsigned j=0;j<3;j++) {
        MacRuntime r;init(&r);memset(mac_cpu_ram(r.cpu)+DST,0xa5,10);
        ext(&r,ops[j]==0x16?DST:SRC,bad[i].exponent,bad[i].sig);
        uint8_t before[10];memcpy(before,mac_cpu_ram(r.cpu)+DST,10);
        r.sane_environment=0x0800;
        CHECK(call(&r,ops[j])==-1 && mac_cpu_halted(r.cpu));
        CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+DST,10));
        CHECK(r.sane_environment==0x0800 && mac_cpu_get(r.cpu,MAC_A7)==SP);
        mac_cpu_destroy(r.cpu);
    }
    const unsigned nil_ops[]={0x100e,0x16,1,3};
    for(unsigned i=0;i<4;i++)for(unsigned source=0;source<(i==0?2u:1u);source++) {
        MacRuntime r;init(&r);put(&r,SP,2,nil_ops[i]);
        put(&r,SP+2,4,source?DST:0);put(&r,SP+6,4,source?0:SRC);
        memset(mac_cpu_ram(r.cpu)+DST,0xa5,10);mac_cpu_set(r.cpu,MAC_A7,SP);
        CHECK(mac_sane_service(&r,0xa9eb)==-1 && mac_cpu_halted(r.cpu));
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP && !r.sane_environment);
        for(unsigned j=0;j<10;j++)CHECK(mac_cpu_ram(r.cpu)[DST+j]==0xa5);
        mac_cpu_destroy(r.cpu);
    }
}
int main(void){encoding_edges();conversions();truncate_tests();integer_tests();failure_tests();printf("SANE checks: %s\n",failures?"FAILED":"ok");return failures!=0;}
