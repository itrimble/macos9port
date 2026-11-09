/* SANE FP68K FADDX/FSUBX/FMULX/FDIVX on extended operands: vectors from an independent exact reference, then refusals. */
#include "mac_runtime.h"
#include "mac_sane_arith.h"
#include <stdio.h>
#include <string.h>
#include "sane_arith_vectors.h"
#include "sane_float_vectors.h"
int mac_sane_service(MacRuntime*r,uint16_t opcode);
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
enum {SP=0x1000,SRC=0x200,DST=0x300};
static void put(MacRuntime*r,unsigned p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static uint32_t get(MacRuntime*r,unsigned p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void ext(MacRuntime*r,unsigned p,uint16_t e,uint64_t s){put(r,p,2,e);put(r,p+2,4,(uint32_t)(s>>32));put(r,p+6,4,(uint32_t)s);}
static int call(MacRuntime*r,unsigned op){put(r,SP,2,op);put(r,SP+2,4,DST);put(r,SP+6,4,SRC);mac_cpu_set(r->cpu,MAC_A7,SP);return mac_sane_service(r,0xa9eb);}
static void init(MacRuntime*r){memset(r,0,sizeof *r);r->cpu=mac_cpu_create(65536);CHECK(r->cpu!=NULL);}
static void vectors(void){
    for(unsigned i=0;i<sizeof VEC/sizeof VEC[0];i++){
        MacRuntime r;init(&r);r.sane_environment=(uint16_t)(VEC[i].mode<<13);
        ext(&r,DST,VEC[i].ae,VEC[i].as);ext(&r,SRC,VEC[i].be,VEC[i].bs);
        int rc=call(&r,VEC[i].op);
        uint16_t e=(uint16_t)get(&r,DST,2);uint64_t s=(uint64_t)get(&r,DST+2,4)<<32|get(&r,DST+6,4);
        if(rc!=1||e!=VEC[i].re||s!=VEC[i].rs||((r.sane_environment&0x1000)!=0)!=(VEC[i].inexact!=0)||mac_cpu_get(r.cpu,MAC_A7)!=SP+10)
            fprintf(stderr,"vector %u (op %u mode %u): got %04x %016llx env %04x rc %d\n",i,VEC[i].op,VEC[i].mode,e,(unsigned long long)s,r.sane_environment,rc);
        CHECK(rc==1&&e==VEC[i].re&&s==VEC[i].rs);
        CHECK(((r.sane_environment&0x1000)!=0)==(VEC[i].inexact!=0));
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+10);
        CHECK(get(&r,SRC,2)==VEC[i].be&&((uint64_t)get(&r,SRC+2,4)<<32|get(&r,SRC+6,4))==VEC[i].bs);   /* the source is untouched */
        mac_cpu_destroy(r.cpu);
    }
}
static void floats(void){
    for(unsigned i=0;i<sizeof FV/sizeof FV[0];i++){
        MacRuntime r;init(&r);r.sane_environment=(uint16_t)(FV[i].mode<<13);ext(&r,SRC,FV[i].xe,FV[i].xs);
        unsigned op=FV[i].fmt=='S'?0x1010:0x0810,size=FV[i].fmt=='S'?4:8;
        int rc=call(&r,op);uint64_t bits=0;for(unsigned k=0;k<size;k++)bits=bits<<8|mac_cpu_ram(r.cpu)[DST+k];
        if(rc!=1||bits!=FV[i].bits)fprintf(stderr,"float vector %u (%c mode %u): got %016llx want %016llx rc %d\n",i,FV[i].fmt,FV[i].mode,(unsigned long long)bits,(unsigned long long)FV[i].bits,rc);
        CHECK(rc==1&&bits==FV[i].bits&&((r.sane_environment&0x1000)!=0)==(FV[i].inexact!=0)&&mac_cpu_get(r.cpu,MAC_A7)==SP+10);
        mac_cpu_destroy(r.cpu);
    }
    for(unsigned i=0;i<sizeof DV/sizeof DV[0];i++){
        MacRuntime r;init(&r);put(&r,SRC,4,(uint32_t)(DV[i].bits>>32));put(&r,SRC+4,4,(uint32_t)DV[i].bits);
        int rc=call(&r,0x080e);uint16_t e=(uint16_t)get(&r,DST,2);uint64_t s=(uint64_t)get(&r,DST+2,4)<<32|get(&r,DST+6,4);
        CHECK(rc==1&&e==DV[i].xe&&s==DV[i].xs&&!r.sane_environment&&mac_cpu_get(r.cpu,MAC_A7)==SP+10);
        mac_cpu_destroy(r.cpu);
    }
    struct{unsigned op;uint16_t e;uint64_t s;uint64_t d;}bad[]={
        {0x1010,0x7fff,UINT64_C(0xc000000000000000),0},{0x1010,0x3fff+128,UINT64_C(0x8000000000000000),0},{0x1010,0x3fff-127,UINT64_C(0x8000000000000000),0},
        {0x1010,0,7,0},{0x0810,0x3fff+1024,UINT64_C(0x8000000000000000),0},{0x0810,0x3fff-1023,UINT64_C(0x8000000000000000),0},
        {0x1010,0x3fff,UINT64_C(0x4000000000000000),0},{0x080e,0,0,UINT64_C(0x7ff8000000000000)},{0x080e,0,0,UINT64_C(0xfff0000000000001)},
        {0x1010,0x3fff+127,UINT64_MAX,0}};   /* rounds up into overflow */
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){
        MacRuntime r;init(&r);r.sane_environment=0;
        if(bad[i].op==0x080e){put(&r,SRC,4,(uint32_t)(bad[i].d>>32));put(&r,SRC+4,4,(uint32_t)bad[i].d);}else ext(&r,SRC,bad[i].e,bad[i].s);
        memset(mac_cpu_ram(r.cpu)+DST,0xa5,10);
        int rc=call(&r,bad[i].op);if(!(rc==-1&&mac_cpu_halted(r.cpu)))fprintf(stderr,"float refusal %u rc=%d\n",i,rc);
        CHECK(rc==-1&&mac_cpu_halted(r.cpu)&&mac_cpu_get(r.cpu,MAC_A7)==SP);
        for(unsigned k=0;k<10;k++)CHECK(mac_cpu_ram(r.cpu)[DST+k]==0xa5);
        mac_cpu_destroy(r.cpu);
    }
}
static void refusals(void){
    struct{unsigned op;uint16_t ae;uint64_t as;uint16_t be;uint64_t bs;uint16_t env;}c[]={
        {0,0x7fff,UINT64_C(0x8000000000000000),0x3fff,UINT64_C(0x8000000000000000),0},          /* infinity */
        {0,0x3fff,UINT64_C(0x8000000000000000),0x7fff,UINT64_C(0xc000000000000000),0},          /* NaN */
        {2,0,5,0x3fff,UINT64_C(0x8000000000000000),0},                                          /* denormal */
        {4,0x3fff,UINT64_C(0x4000000000000000),0x3fff,UINT64_C(0x8000000000000000),0},          /* unnormalized */
        {6,0x3fff,UINT64_C(0x8000000000000000),0,0,0},                                          /* divide by zero */
        {4,0x7ffe,UINT64_C(0x8000000000000000),0x7ffe,UINT64_C(0x8000000000000000),0},          /* overflow */
        {4,0x0001,UINT64_C(0x8000000000000000),0x0001,UINT64_C(0x8000000000000000),0},          /* underflow */
        {0,0x3fff,UINT64_C(0x8000000000000000),0x3fff,UINT64_C(0x8000000000000000),0x0020},     /* double precision rounding */
        {0,0x3fff,UINT64_C(0x8000000000000000),0x3fb9,UINT64_C(0x8000000000000000),0x0010},     /* inexact halt enabled */
        {8,0x3fff,UINT64_C(0x8000000000000000),0x3fff,UINT64_C(0x8000000000000000),0},          /* FCMPX is not here */
    };
    for(unsigned i=0;i<sizeof c/sizeof c[0];i++){
        MacRuntime r;init(&r);ext(&r,DST,c[i].ae,c[i].as);ext(&r,SRC,c[i].be,c[i].bs);r.sane_environment=c[i].env;
        uint8_t before[10];memcpy(before,mac_cpu_ram(r.cpu)+DST,10);
        int rc=call(&r,c[i].op);
        if(!(rc==-1&&mac_cpu_halted(r.cpu)))fprintf(stderr,"refusal %u: rc=%d\n",i,rc);
        CHECK(rc==-1&&mac_cpu_halted(r.cpu));
        CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+DST,10)&&r.sane_environment==c[i].env&&mac_cpu_get(r.cpu,MAC_A7)==SP);
        mac_cpu_destroy(r.cpu);
    }
    /* bad operand addresses and stacks: nothing is written */
    for(unsigned k=0;k<3;k++){
        MacRuntime r;init(&r);ext(&r,DST,0x3fff,UINT64_C(0x8000000000000000));ext(&r,SRC,0x3fff,UINT64_C(0x8000000000000000));
        put(&r,SP,2,0);put(&r,SP+2,4,k==0?65530:DST);put(&r,SP+6,4,k==1?65534:SRC);mac_cpu_set(r.cpu,MAC_A7,k==2?UINT32_MAX-1:SP);
        CHECK(mac_sane_service(&r,0xa9eb)==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    }
}
static void direct(void){
    Extended out;int inexact;const char*why="";
    Extended one={0x3fff,UINT64_C(0x8000000000000000)};
    CHECK(mac_sane_arith(0,one,one,0,&out,&inexact,&why)==0&&out.sign_exp==0x4000&&out.significand==UINT64_C(0x8000000000000000)&&!inexact);   /* 1+1=2 */
    CHECK(mac_sane_arith(2,one,one,0,&out,&inexact,&why)==0&&out.sign_exp==0&&out.significand==0&&!inexact);                                       /* 1-1=+0 */
    CHECK(mac_sane_arith(2,one,one,2,&out,&inexact,&why)==0&&out.sign_exp==0x8000&&out.significand==0);                                          /* round down: -0 */
    Extended neg_one={0xbfff,UINT64_C(0x8000000000000000)};
    for(unsigned mode=0;mode<4;mode++){                                       /* x - x and x + (-x): +0, except -0 when rounding down */
        CHECK(mac_sane_arith(2,neg_one,neg_one,mode,&out,&inexact,&why)==0&&out.sign_exp==(mode==2?0x8000:0)&&!out.significand);
        CHECK(mac_sane_arith(0,one,neg_one,mode,&out,&inexact,&why)==0&&out.sign_exp==(mode==2?0x8000:0)&&!out.significand);
    }
    {   /* tiny addends: far below the last place (sticky only), and the rounding that depends on them */
        Extended tiny130={0x3fff-130,UINT64_C(0x8000000000000000)},neg_tiny130={0xbfff-130,UINT64_C(0x8000000000000000)};
        CHECK(mac_sane_arith(0,one,tiny130,0,&out,&inexact,&why)==0&&inexact&&out.sign_exp==0x3fff&&out.significand==UINT64_C(0x8000000000000000));
        CHECK(mac_sane_arith(0,one,tiny130,1,&out,&inexact,&why)==0&&inexact&&out.significand==UINT64_C(0x8000000000000001));       /* up */
        CHECK(mac_sane_arith(0,one,neg_tiny130,3,&out,&inexact,&why)==0&&inexact&&out.sign_exp==0x3ffe&&out.significand==UINT64_MAX); /* just below 1 */
        CHECK(mac_sane_arith(0,one,neg_tiny130,0,&out,&inexact,&why)==0&&inexact&&out.sign_exp==0x3fff&&out.significand==UINT64_C(0x8000000000000000));
        Extended b64={0x3fff-64,UINT64_C(0x8000000000000001)};                                  /* 2^-64 (1 + 2^-63): just over half an ulp of 1 */
        CHECK(mac_sane_arith(0,one,b64,0,&out,&inexact,&why)==0&&inexact&&out.significand==UINT64_C(0x8000000000000001));
        Extended half_ulp={0x3fff-64,UINT64_C(0x8000000000000000)},odd={0x3fff,UINT64_C(0x8000000000000001)};
        CHECK(mac_sane_arith(0,one,half_ulp,0,&out,&inexact,&why)==0&&inexact&&out.significand==UINT64_C(0x8000000000000000));         /* tie, even stays */
        CHECK(mac_sane_arith(0,odd,half_ulp,0,&out,&inexact,&why)==0&&inexact&&out.significand==UINT64_C(0x8000000000000002));         /* tie, odd goes up */
        Extended ones={0x3fff,UINT64_MAX};                                                      /* 2 - 2^-63, odd: the tie rounds up into the next binade */
        CHECK(mac_sane_arith(0,ones,half_ulp,0,&out,&inexact,&why)==0&&inexact&&out.sign_exp==0x4000&&out.significand==UINT64_C(0x8000000000000000));
    }
    {   /* the ends of the exponent range are refused, not wrapped */
        Extended maxe={0x7ffe,UINT64_C(0x8000000000000000)},two={0x4000,UINT64_C(0x8000000000000000)},mine={0x0001,UINT64_C(0x8000000000000000)},halfv={0x3ffe,UINT64_C(0x8000000000000000)};
        CHECK(mac_sane_arith(4,maxe,two,0,&out,&inexact,&why)==-1);
        CHECK(mac_sane_arith(4,mine,halfv,0,&out,&inexact,&why)==-1);
        CHECK(mac_sane_arith(4,maxe,one,0,&out,&inexact,&why)==0&&out.sign_exp==0x7ffe);
        CHECK(mac_sane_arith(4,mine,one,0,&out,&inexact,&why)==0&&out.sign_exp==0x0001);
    }
    Extended three={0x4000,UINT64_C(0xc000000000000000)};
    CHECK(mac_sane_arith(6,one,three,0,&out,&inexact,&why)==0&&inexact&&out.sign_exp==0x3ffd&&out.significand==UINT64_C(0xaaaaaaaaaaaaaaab));  /* 1/3 */
    CHECK(mac_sane_arith(6,one,three,3,&out,&inexact,&why)==0&&out.significand==UINT64_C(0xaaaaaaaaaaaaaaaa));                                    /* toward zero */
}
int main(void){vectors();floats();refusals();direct();printf("SANE arithmetic checks: %s\n",failures?"FAILED":"ok");return failures!=0;}
