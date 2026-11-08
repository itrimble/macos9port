#include "mac_runtime.h"
#include "mac_rgn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_regions_service(MacRuntime *,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define R 0x4000u
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static MacRuntime fresh(void){MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);rt.heap_next=0x100000;rt.heap_limit=0x200000;mac_cpu_set(rt.cpu,MAC_A7,SP);return rt;}
static uint32_t create(MacRuntime *rt){mac_cpu_set(rt->cpu,MAC_A7,SP);CHECK(mac_regions_service(rt,0xa8d8)==1);CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP);return rd(rt,SP,4);}
static int replace(MacRuntime *rt,uint32_t h,uint32_t r){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,r);wr(rt,SP+4,4,h);return mac_regions_service(rt,0xa8df);}
static int empty(MacRuntime *rt,uint32_t h){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,h);return mac_regions_service(rt,0xa8dd);}
/* SetEmptyRgn ($A8DD): size 10 and a zero bounding box, whatever the region held; one argument, popped. */
/* SectRgn/UnionRgn/DiffRgn/XorRgn on rectangular regions: exact, whether or not the result is one rectangle. */
typedef struct{int t,l,b,r;}B;
static void set_box(MacRuntime *rt,uint32_t h,B b){uint32_t p=rd(rt,h,4);wr(rt,p,2,10);wr(rt,p+2,2,(uint16_t)b.t);wr(rt,p+4,2,(uint16_t)b.l);wr(rt,p+6,2,(uint16_t)b.b);wr(rt,p+8,2,(uint16_t)b.r);}
static B get_box(MacRuntime *rt,uint32_t h){uint32_t p=rd(rt,h,4);return (B){(int16_t)rd(rt,p+2,2),(int16_t)rd(rt,p+4,2),(int16_t)rd(rt,p+6,2),(int16_t)rd(rt,p+8,2)};}

/* Read a guest region back through the engine's own decoder. */
static int region_of(MacRuntime *rt,uint32_t h,MacRgn *g){
    const uint8_t *ram=mac_cpu_ram(rt->cpu);uint32_t p=(uint32_t)(ram[h]<<24|ram[h+1]<<16|ram[h+2]<<8|ram[h+3]);
    unsigned size=(unsigned)(ram[p]<<8|ram[p+1]);const char *why;return mac_rgn_decode(ram+p,size,g,&why);
}
static int in_box(B b,int x,int y){return b.b>b.t && b.r>b.l && y>=b.t && y<b.b && x>=b.l && x<b.r;}
static int oracle(unsigned op,B a,B b,int x,int y){
    int u=in_box(a,x,y),v=in_box(b,x,y);
    return op==0xa8e4?(u&&v):op==0xa8e5?(u||v):op==0xa8e6?(u&&!v):(u!=v);
}
static int algebra_call(MacRuntime *rt,unsigned op,uint32_t a,uint32_t b,uint32_t d){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,d);wr(rt,SP+4,4,b);wr(rt,SP+8,4,a);return mac_regions_service(rt,(uint16_t)op);}
static void algebra_table(void){
    enum{SECT=0xa8e4,UNION=0xa8e5,DIFF=0xa8e6,XOR=0xa8e7};
    static const struct{unsigned op;B a,b;int ok;B out;}c[]={
        /* intersection: always a rectangle */
        {SECT,{0,0,10,10},{5,5,20,20},1,{5,5,10,10}},{SECT,{0,0,10,10},{20,20,30,30},1,{0,0,0,0}},{SECT,{0,0,10,10},{0,0,0,0},1,{0,0,0,0}},
        {SECT,{0,0,10,10},{2,2,8,8},1,{2,2,8,8}},{SECT,{0,0,10,10},{10,0,20,10},1,{0,0,0,0}},
        /* union */
        {UNION,{0,0,10,10},{0,0,0,0},1,{0,0,10,10}},{UNION,{0,0,0,0},{3,3,9,9},1,{3,3,9,9}},{UNION,{0,0,0,0},{0,0,0,0},1,{0,0,0,0}},
        {UNION,{0,0,10,10},{2,2,8,8},1,{0,0,10,10}},{UNION,{2,2,8,8},{0,0,10,10},1,{0,0,10,10}},
        {UNION,{0,0,10,10},{0,5,10,20},1,{0,0,10,20}},{UNION,{0,0,10,10},{0,10,10,20},1,{0,0,10,20}},{UNION,{0,0,10,10},{0,11,10,20},0,{0,0,0,0}},
        {UNION,{0,0,10,10},{11,0,20,10},0,{0,0,0,0}},{UNION,{0,0,10,10},{5,0,20,10},1,{0,0,20,10}},{UNION,{0,0,10,10},{10,0,20,10},1,{0,0,20,10}},{UNION,{0,0,10,10},{5,5,20,20},0,{0,0,0,0}},
        /* difference */
        {DIFF,{0,0,0,0},{0,0,5,5},1,{0,0,0,0}},{DIFF,{0,0,10,10},{0,0,0,0},1,{0,0,10,10}},{DIFF,{0,0,10,10},{20,20,30,30},1,{0,0,10,10}},
        {DIFF,{2,2,8,8},{0,0,10,10},1,{0,0,0,0}},{DIFF,{0,0,10,10},{0,0,10,10},1,{0,0,0,0}},
        {DIFF,{0,0,10,10},{-5,-5,4,15},1,{4,0,10,10}},{DIFF,{0,0,10,10},{6,-5,20,15},1,{0,0,6,10}},{DIFF,{0,0,10,10},{3,-5,6,15},0,{0,0,0,0}},
        {DIFF,{0,0,10,10},{-5,-5,15,4},1,{0,4,10,10}},{DIFF,{0,0,10,10},{-5,6,15,20},1,{0,0,10,6}},{DIFF,{0,0,10,10},{-5,3,15,6},0,{0,0,0,0}},
        {DIFF,{0,0,10,10},{4,4,6,6},0,{0,0,0,0}},{DIFF,{0,0,10,10},{5,5,20,20},0,{0,0,0,0}},
        /* exclusive or */
        {XOR,{0,0,0,0},{3,3,9,9},1,{3,3,9,9}},{XOR,{3,3,9,9},{0,0,0,0},1,{3,3,9,9}},{XOR,{0,0,0,0},{0,0,0,0},1,{0,0,0,0}},
        {XOR,{0,0,10,10},{0,0,10,10},1,{0,0,0,0}},{XOR,{0,0,10,10},{0,10,10,20},1,{0,0,10,20}},{XOR,{0,0,10,10},{20,20,30,30},0,{0,0,0,0}},
        {XOR,{0,0,10,10},{5,5,20,20},0,{0,0,0,0}},{XOR,{0,0,10,10},{2,2,8,8},0,{0,0,0,0}},
    };
    for(unsigned i=0;i<sizeof c/sizeof *c;i++){
        MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt),d=create(&rt);set_box(&rt,a,c[i].a);set_box(&rt,b,c[i].b);set_box(&rt,d,(B){7,7,7,7});
        int rc=algebra_call(&rt,c[i].op,a,b,d);
        if(c[i].ok){B g=get_box(&rt,d);CHECK(rc==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+12 && g.t==c[i].out.t && g.l==c[i].out.l && g.b==c[i].out.b && g.r==c[i].out.r);
            CHECK(get_box(&rt,a).t==c[i].a.t && get_box(&rt,b).t==c[i].b.t);}                      /* sources are untouched */
        else{                                                                                      /* a result that is not one rectangle: stored exactly */
            MacRgn g;mac_rgn_init(&g);
            CHECK(rc==1 && !mac_cpu_halted(rt.cpu) && mac_cpu_get(rt.cpu,MAC_A7)==SP+12 && region_of(&rt,d,&g)==0 && g.n>1);
            for(int y=-3;y<35;y++)for(int x=-3;x<35;x++)CHECK(mac_rgn_contains(&g,x,y)==oracle(c[i].op,c[i].a,c[i].b,x,y));
            mac_rgn_free(&g);}
        mac_cpu_destroy(rt.cpu);
    }
    /* The destination may be one of the sources. */
    {MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});
     CHECK(algebra_call(&rt,0xa8e4,a,b,a)==1);B g=get_box(&rt,a);CHECK(g.t==5 && g.l==5 && g.b==10 && g.r==10);
     set_box(&rt,a,(B){0,0,10,10});CHECK(algebra_call(&rt,0xa8e6,a,b,b)==1);                    /* a minus b into b: an L, exactly */
     {MacRgn g;mac_rgn_init(&g);CHECK(region_of(&rt,b,&g)==0 && g.n==2);for(int y=-2;y<25;y++)for(int x=-2;x<25;x++)CHECK(mac_rgn_contains(&g,x,y)==oracle(0xa8e6,(B){0,0,10,10},(B){5,5,20,20},x,y));mac_rgn_free(&g);}
     mac_cpu_destroy(rt.cpu);}
    {MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){0,5,10,20});
     CHECK(algebra_call(&rt,0xa8e5,a,b,b)==1);B g=get_box(&rt,b);CHECK(g.t==0 && g.l==0 && g.b==10 && g.r==20);mac_cpu_destroy(rt.cpu);}
    /* Bad handles and stack. */
    for(unsigned k=0;k<5;k++){MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt),d=create(&rt);
        if(k==0)a=0;if(k==1)b=0x5000;if(k==2)d=0;
        if(k==3)wr(&rt,rd(&rt,a,4),2,22);                                                         /* a complex region (size word != 10) */
        int rc=k==4?(mac_cpu_set(rt.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(rt.cpu)-2),mac_regions_service(&rt,0xa8e4)):algebra_call(&rt,0xa8e4,a,b,d);
        CHECK(rc==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);}
}
/* OffsetRgn, InsetRgn, EmptyRgn, EqualRgn, PtInRgn, RectInRgn on rectangular regions. */
static int two_ints(MacRuntime *rt,unsigned op,uint32_t h,int dh,int dv){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,2,(uint16_t)dv);wr(rt,SP+2,2,(uint16_t)dh);wr(rt,SP+4,4,h);return mac_regions_service(rt,(uint16_t)op);}
static int bool_at(MacRuntime *rt,uint32_t at){return mac_cpu_ram(rt->cpu)[at];}
static void queries(void){
    MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt),e=create(&rt);
    set_box(&rt,a,(B){10,20,30,60});
    /* OffsetRgn / InsetRgn */
    CHECK(two_ints(&rt,0xa8e0,a,5,-3)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8);{B g=get_box(&rt,a);CHECK(g.t==7 && g.l==25 && g.b==27 && g.r==65);}
    CHECK(two_ints(&rt,0xa8e1,a,4,2)==1);{B g=get_box(&rt,a);CHECK(g.t==9 && g.l==29 && g.b==25 && g.r==61);}
    CHECK(two_ints(&rt,0xa8e1,a,-4,-2)==1);{B g=get_box(&rt,a);CHECK(g.t==7 && g.l==25 && g.b==27 && g.r==65);}              /* negative grows */
    CHECK(two_ints(&rt,0xa8e1,a,20,0)==1);{B g=get_box(&rt,a);CHECK(g.t==0 && g.l==0 && g.b==0 && g.r==0);}                  /* shrunk to nothing: empty */
    set_box(&rt,a,(B){10,20,30,60});
    CHECK(two_ints(&rt,0xa8e1,a,0,10)==1);{B g=get_box(&rt,a);CHECK(g.r==0 && g.b==0);}                                       /* exactly zero height: empty */
    CHECK(two_ints(&rt,0xa8e0,e,9,9)==1);{B g=get_box(&rt,e);CHECK(g.t==0 && g.l==0 && g.b==0 && g.r==0);}                  /* empty stays empty */
    CHECK(two_ints(&rt,0xa8e1,e,-9,-9)==1);{B g=get_box(&rt,e);CHECK(g.t==0 && g.b==0);}
    set_box(&rt,a,(B){10,20,30,60});
    /* EmptyRgn */
    wr(&rt,SP+4,2,0xeeee);mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,e);CHECK(mac_regions_service(&rt,0xa8e2)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && bool_at(&rt,SP+4)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,a);CHECK(mac_regions_service(&rt,0xa8e2)==1 && bool_at(&rt,SP+4)==0);
    /* EqualRgn: two empties are equal; same box equal; different not */
    #define EQ(x,y) (mac_cpu_set(rt.cpu,MAC_A7,SP),wr(&rt,SP,4,(y)),wr(&rt,SP+4,4,(x)),mac_regions_service(&rt,0xa8e3)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8 ? bool_at(&rt,SP+8) : -1)
    set_box(&rt,b,(B){10,20,30,60});CHECK(EQ(a,b)==1 && EQ(b,a)==1);
    set_box(&rt,b,(B){10,20,30,61});CHECK(EQ(a,b)==0);
    set_box(&rt,b,(B){11,20,30,60});CHECK(EQ(a,b)==0);
    set_box(&rt,b,(B){0,0,0,0});CHECK(EQ(a,b)==0 && EQ(b,e)==1 && EQ(e,b)==1);
    set_box(&rt,b,(B){5,5,5,9});CHECK(EQ(b,e)==1);                                                                          /* a degenerate box is empty too */
    /* PtInRgn: top/left inclusive, bottom/right exclusive; the point is (v,h) */
    #define PT(v,h,r) (mac_cpu_set(rt.cpu,MAC_A7,SP),wr(&rt,SP,4,(r)),wr(&rt,SP+4,2,(uint16_t)(v)),wr(&rt,SP+6,2,(uint16_t)(h)),mac_regions_service(&rt,0xa8e8)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8 ? bool_at(&rt,SP+8) : -1)
    CHECK(PT(10,20,a)==1 && PT(29,59,a)==1 && PT(20,40,a)==1);
    CHECK(PT(30,20,a)==0 && PT(10,60,a)==0 && PT(9,20,a)==0 && PT(10,19,a)==0 && PT(30,60,a)==0);
    CHECK(PT(0,0,e)==0 && PT(10,20,e)==0);
    /* RectInRgn: any overlap */
    #define RI(t,l,b2,r2,rg) (wr(&rt,R,2,(uint16_t)(t)),wr(&rt,R+2,2,(uint16_t)(l)),wr(&rt,R+4,2,(uint16_t)(b2)),wr(&rt,R+6,2,(uint16_t)(r2)),mac_cpu_set(rt.cpu,MAC_A7,SP),wr(&rt,SP,4,(rg)),wr(&rt,SP+4,4,R),mac_regions_service(&rt,0xa8e9)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8 ? bool_at(&rt,SP+8) : -1)
    CHECK(RI(0,0,100,100,a)==1 && RI(29,59,40,70,a)==1 && RI(15,25,20,30,a)==1);
    CHECK(RI(30,20,40,60,a)==0 && RI(10,60,30,70,a)==0 && RI(0,0,10,100,a)==0 && RI(0,0,100,20,a)==0);                        /* touching only */
    CHECK(RI(0,0,100,100,e)==0 && RI(5,5,5,50,a)==0);                                                                        /* empty region or empty rectangle */
    mac_cpu_destroy(rt.cpu);
    /* Bad handles, complex regions, bad stack: named halts. */
    for(unsigned k=0;k<12;k++){
        MacRuntime r2=fresh();uint32_t x=create(&r2),y=create(&r2);set_box(&r2,x,(B){0,0,10,10});
        static const unsigned ops[]={0xa8e0,0xa8e1,0xa8e2,0xa8e3,0xa8e8,0xa8e9};unsigned op=ops[k%6];
        mac_cpu_set(r2.cpu,MAC_A7,SP);
        if(k<6){wr(&r2,SP,4,0x5000);wr(&r2,SP+4,4,0x5000);wr(&r2,SP+8,4,0x5000);if(op==0xa8e0||op==0xa8e1)wr(&r2,SP+4,4,0x5000);}  /* not a region handle */
        else{wr(&r2,rd(&r2,x,4),2,22);wr(&r2,SP,4,x);wr(&r2,SP+4,4,y);wr(&r2,SP+8,4,x);if(op==0xa8e0||op==0xa8e1)wr(&r2,SP+4,4,x);} /* a complex region */
        int rc=(op==0xa8e0||op==0xa8e1)&&k>=6?(wr(&r2,SP+4,4,x),wr(&r2,SP,2,1),wr(&r2,SP+2,2,1),mac_regions_service(&r2,(uint16_t)op)):mac_regions_service(&r2,(uint16_t)op);
        CHECK(rc==-1 && mac_cpu_halted(r2.cpu));mac_cpu_destroy(r2.cpu);
    }
    {MacRuntime r2=fresh();mac_cpu_set(r2.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r2.cpu)-2);CHECK(mac_regions_service(&r2,0xa8e8)==-1 && mac_cpu_halted(r2.cpu));mac_cpu_destroy(r2.cpu);}
}
static void set_empty(void){
    MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4);
    wr(&rt,R,2,5);wr(&rt,R+2,2,6);wr(&rt,R+4,2,50);wr(&rt,R+6,2,60);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+2,2)==5 && rd(&rt,p+8,2)==60);
    wr(&rt,0x220,2,0xffff);
    CHECK(empty(&rt,h)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && rd(&rt,0x220,2)==0);
    CHECK(rd(&rt,p,2)==10 && rd(&rt,p+2,4)==0 && rd(&rt,p+6,4)==0 && rd(&rt,h,4)==p);
    CHECK(empty(&rt,h)==1 && rd(&rt,p+2,4)==0);                                    /* emptying an empty region is fine */
    wr(&rt,R,2,5);wr(&rt,R+2,2,6);wr(&rt,R+4,2,50);wr(&rt,R+6,2,60);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+2,2)==5);  /* and it can be refilled */
    CHECK(empty(&rt,0)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    rt=fresh();CHECK(empty(&rt,0x5000)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);     /* not a region handle */
    rt=fresh();h=create(&rt);mac_cpu_set(rt.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(rt.cpu)-2);
    CHECK(mac_regions_service(&rt,0xa8dd)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);   /* stack beyond RAM */
}
static void lifecycle(void){
    MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4);
    CHECK(h && rd(&rt,p,2)==10 && rd(&rt,p+2,4)==0 && rd(&rt,p+6,4)==0);
    wr(&rt,R,2,(uint16_t)-20);wr(&rt,R+2,2,(uint16_t)-10);wr(&rt,R+4,2,30);wr(&rt,R+6,2,40);
    CHECK(replace(&rt,h,R)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+p+2,mac_cpu_ram(rt.cpu)+R,8));
    CHECK(replace(&rt,h,p+2)==1); /* Overlapping source. */
    wr(&rt,R+4,2,(uint16_t)-20);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+2,4)==0 && rd(&rt,p+6,4)==0);
    wr(&rt,R+4,2,30);wr(&rt,R+6,2,(uint16_t)-11);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+6,4)==0);
    rt.handles[0].state=0x80;wr(&rt,R+6,2,40);CHECK(replace(&rt,h,R)==1 && rd(&rt,h,4)==p);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,h);CHECK(mac_regions_service(&rt,0xa8d9)==1 && rd(&rt,h,4)==0);
    CHECK(!rt.handles[0].handle && create(&rt)!=0 && rt.handle_count==1);
    CHECK(mac_regions_service(&rt,0xa8ea)==0 && mac_regions_service(&rt,0xa8d7)==0 && mac_regions_service(&rt,0xa8ee)==0);mac_cpu_destroy(rt.cpu);
}
static void failures(void){
    for(int kind=0;kind<8;kind++) {
        MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4),r=R;
        if(kind==0)h++;
        if(kind==1)r=MAC_RUNTIME_RAM-6;
        if(kind==2)rt.handles[0].resource=1;
        if(kind==3)rt.handles[0].loaded_code=1;
        if(kind==4)wr(&rt,h,4,p+4);
        if(kind==5){rt.handles[0].size=8;rt.handles[0].state=0x80;}
        if(kind==6){rt.handles[0].size=8;rt.heap_high=rt.heap_next+8;}
        if(kind==7){ /* A nine-character window title is also a 10-byte handle. */
            rt.handles[0].type=0x4f57494e;rt.window.address=0x9000;rt.window.handles[11]=h;
        }
        uint8_t before[12];memcpy(before,mac_cpu_ram(rt.cpu)+p,12);uint32_t heap=rt.heap_next;
        CHECK(replace(&rt,h,r)==-1 && mac_cpu_halted(rt.cpu));
        CHECK(!memcmp(before,mac_cpu_ram(rt.cpu)+p,12) && rt.heap_next==heap && mac_cpu_get(rt.cpu,MAC_A7)==SP);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4);rt.handles[0].size=8;
    wr(&rt,R,4,0);wr(&rt,R+4,4,0x000a0014);
    CHECK(replace(&rt,h,R)==1 && rd(&rt,h,4)!=p && rt.handles[0].size==10);
    CHECK(rd(&rt,rd(&rt,h,4)+6,4)==0x000a0014);mac_cpu_destroy(rt.cpu);
    rt=fresh();rt.heap_high=rt.heap_next+15;uint32_t heap=rt.heap_next;
    CHECK(create(&rt)==0 && rt.heap_next==heap && rt.handle_count==0 && (int16_t)rd(&rt,0x220,2)==-108);
    mac_cpu_destroy(rt.cpu);
    rt=fresh();mac_cpu_set(rt.cpu,MAC_A7,MAC_RUNTIME_RAM-6);
    CHECK(mac_regions_service(&rt,0xa8df)==-1);mac_cpu_destroy(rt.cpu);
}

/* Complex regions through every call, growth and capacity, and hostile records. */
static void complex_regions(void){
    /* An L and a staircase built with the real calls, then queried and edited. */
    MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt),c=create(&rt),d=create(&rt);
    set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){0,5,5,20});
    CHECK(algebra_call(&rt,0xa8e5,a,b,c)==1);                                         /* union of two overlapping boxes: a stepped shape */
    MacRgn g;mac_rgn_init(&g);CHECK(region_of(&rt,c,&g)==0 && g.n==2);
    uint32_t p=rd(&rt,c,4);CHECK(rd(&rt,p,2)>10 && (int16_t)rd(&rt,p+2,2)==0 && (int16_t)rd(&rt,p+4,2)==0 && (int16_t)rd(&rt,p+6,2)==10 && (int16_t)rd(&rt,p+8,2)==20);   /* size word and bounding box */
    /* PtInRgn / RectInRgn / EqualRgn / EmptyRgn on it */
    #define PTC(v,h) (mac_cpu_set(rt.cpu,MAC_A7,SP),wr(&rt,SP,4,c),wr(&rt,SP+4,2,(uint16_t)(v)),wr(&rt,SP+6,2,(uint16_t)(h)),mac_regions_service(&rt,0xa8e8)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8 ? bool_at(&rt,SP+8) : -1)
    CHECK(PTC(2,15)==1 && PTC(7,15)==0 && PTC(7,3)==1 && PTC(2,25)==0 && PTC(5,5)==1 && PTC(5,12)==0);
    #define RIC(t,l,b2,r2) (wr(&rt,R,2,(uint16_t)(t)),wr(&rt,R+2,2,(uint16_t)(l)),wr(&rt,R+4,2,(uint16_t)(b2)),wr(&rt,R+6,2,(uint16_t)(r2)),mac_cpu_set(rt.cpu,MAC_A7,SP),wr(&rt,SP,4,c),wr(&rt,SP+4,4,R),mac_regions_service(&rt,0xa8e9)==1 ? bool_at(&rt,SP+8) : -1)
    CHECK(RIC(6,12,9,18)==0 && RIC(4,12,9,18)==1 && RIC(0,0,1,1)==1 && RIC(10,0,12,5)==0);   /* inside the notch, touching it, the corner, below */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,c);wr(&rt,SP+4,4,c);CHECK(mac_regions_service(&rt,0xa8e3)==1 && bool_at(&rt,SP+8)==1);        /* EqualRgn with itself */
    CHECK(algebra_call(&rt,0xa8e5,b,a,d)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,d);wr(&rt,SP+4,4,c);CHECK(mac_regions_service(&rt,0xa8e3)==1 && bool_at(&rt,SP+8)==1);   /* union commutes: equal */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,a);wr(&rt,SP+4,4,c);CHECK(mac_regions_service(&rt,0xa8e3)==1 && bool_at(&rt,SP+8)==0);        /* differs from a box */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,c);wr(&rt,SP+4,2,0xdead);CHECK(mac_regions_service(&rt,0xa8e2)==1 && bool_at(&rt,SP+4)==0 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4);   /* EmptyRgn */
    /* OffsetRgn moves every part; InsetRgn on a non-rectangle is a named halt that leaves it alone */
    CHECK(two_ints(&rt,0xa8e0,c,3,-2)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8);
    mac_rgn_free(&g);mac_rgn_init(&g);CHECK(region_of(&rt,c,&g)==0 && g.n==2 && mac_rgn_contains(&g,18,-2) && !mac_rgn_contains(&g,18,3) && mac_rgn_contains(&g,3,0) && !mac_rgn_contains(&g,2,0));
    {uint32_t pc=rd(&rt,c,4);unsigned sz=rd(&rt,pc,2);uint8_t save[256];memcpy(save,mac_cpu_ram(rt.cpu)+pc,sz);
     CHECK(two_ints(&rt,0xa8e1,c,1,1)==-1 && mac_cpu_halted(rt.cpu) && !memcmp(save,mac_cpu_ram(rt.cpu)+pc,sz));}   /* refused, record untouched (raw RAM: reads fail once halted) */
    mac_rgn_free(&g);mac_cpu_destroy(rt.cpu);
    /* CopyRgn and SetRectRgn */
    rt=fresh();a=create(&rt);b=create(&rt);c=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});algebra_call(&rt,0xa8e6,a,b,c);   /* c is an L */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,b);wr(&rt,SP+4,4,c);CHECK(mac_regions_service(&rt,0xa8dc)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8);   /* CopyRgn(c, b) */
    {MacRgn x,y;mac_rgn_init(&x);mac_rgn_init(&y);CHECK(region_of(&rt,c,&x)==0 && region_of(&rt,b,&y)==0 && mac_rgn_equal(&x,&y) && x.n==2);mac_rgn_free(&x);mac_rgn_free(&y);}
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,b);wr(&rt,SP+4,4,a);CHECK(mac_regions_service(&rt,0xa8dc)==1);          /* copying a box over an L shrinks it back to a box */
    CHECK(get_box(&rt,b).t==0 && get_box(&rt,b).r==10 && rd(&rt,rd(&rt,b,4),2)==10);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,2,(uint16_t)40);wr(&rt,SP+2,2,(uint16_t)30);wr(&rt,SP+4,2,(uint16_t)20);wr(&rt,SP+6,2,(uint16_t)10);wr(&rt,SP+8,4,b);   /* SetRectRgn(b, left 10, top 20, right 30, bottom 40) */
    CHECK(mac_regions_service(&rt,0xa8de)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+12);{B g2=get_box(&rt,b);CHECK(g2.t==20 && g2.l==10 && g2.b==40 && g2.r==30);}
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,2,5);wr(&rt,SP+2,2,5);wr(&rt,SP+4,2,9);wr(&rt,SP+6,2,9);wr(&rt,SP+8,4,b);CHECK(mac_regions_service(&rt,0xa8de)==1);{B g2=get_box(&rt,b);CHECK(g2.t==0 && g2.l==0 && g2.b==0 && g2.r==0);}   /* an inverted rectangle is empty */
    mac_cpu_destroy(rt.cpu);
    /* Capacity: a region alternating between simple and complex reuses its block (no heap leak after the first growth). */
    rt=fresh();a=create(&rt);b=create(&rt);c=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});
    CHECK(algebra_call(&rt,0xa8e5,a,b,c)==1);uint32_t heap=rt.heap_next;
    for(int i=0;i<50;i++){set_box(&rt,c,(B){0,0,10,10});mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,c);wr(&rt,SP+4,4,a);mac_regions_service(&rt,0xa8dc);CHECK(algebra_call(&rt,0xa8e5,a,b,c)==1);}
    CHECK(rt.heap_next==heap);mac_cpu_destroy(rt.cpu);
    /* A locked handle is not grown. */
    rt=fresh();a=create(&rt);b=create(&rt);c=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});
    for(size_t i=0;i<rt.handle_count;i++)if(rt.handles[i].handle==c)rt.handles[i].state|=0x80;
    CHECK(algebra_call(&rt,0xa8e5,a,b,c)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* Hostile records: each is refused by name and leaves the destination alone. */
    for(unsigned k=0;k<5;k++){
        rt=fresh();a=create(&rt);b=create(&rt);c=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});algebra_call(&rt,0xa8e5,a,b,c);   /* c is complex */
        uint32_t pc=rd(&rt,c,4);
        if(k==0)wr(&rt,pc,2,(uint16_t)(rd(&rt,pc,2)+2));                                   /* rgnSize larger than the handle */
        if(k==1)wr(&rt,pc+10,2,0x7fff);                                                    /* the first scan line is the terminator, with data after it */
        if(k==2)wr(&rt,pc,2,9);                                                            /* rgnSize below the header */
        if(k==3)wr(&rt,pc,2,0xffff);                                                       /* a huge odd size */
        if(k==4){for(size_t i=0;i<rt.handle_count;i++)if(rt.handles[i].handle==c)rt.handles[i].size=4;}   /* the table says the handle is only 4 bytes */
        int rc=algebra_call(&rt,0xa8e4,c,a,a);
        CHECK(rc==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    }
}

/* Gaps the first round left: records that look valid but outrun their handle, results too complex to store, bad pointers, ownership. */
static void more_checks(void){
    /* A well-formed L encoding that is longer than the handle it sits in (the handle's real size is 10): refused, not read past its end. */
    MacRuntime rt=fresh();uint32_t a=create(&rt),c=create(&rt);set_box(&rt,a,(B){0,0,10,10});
    static const uint8_t L[]={0x00,0x24,0,0,0,0,0,4,0,4, 0,0,0,0,0,4,0x7f,0xff, 0,2,0,2,0,4,0x7f,0xff, 0,4,0,0,0,2,0x7f,0xff, 0x7f,0xff};
    memcpy(mac_cpu_ram(rt.cpu)+rd(&rt,c,4),L,sizeof L);
    CHECK(algebra_call(&rt,0xa8e4,c,a,a)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* The same record in a handle that really is that big is fine: grow c by a union first, then overwrite. */
    /* A result needing more than 256 rectangles in one row cannot be stored: a named halt that says why. */
    rt=fresh();a=create(&rt);uint32_t b=create(&rt);c=create(&rt);int failed=-1;
    for(int i=0;i<300 && failed<0;i++){set_box(&rt,b,(B){0,i*2,1,i*2+1});if(algebra_call(&rt,0xa8e5,c,b,c)!=1)failed=i;}
    CHECK(failed==256 && mac_cpu_halted(rt.cpu) && strstr(mac_cpu_error(rt.cpu),"too complex"));mac_cpu_destroy(rt.cpu);
    /* RectInRgn with a rectangle pointer that runs off RAM. */
    rt=fresh();a=create(&rt);set_box(&rt,a,(B){0,0,10,10});
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,a);wr(&rt,SP+4,4,(uint32_t)mac_cpu_ram_size(rt.cpu)-4);CHECK(mac_regions_service(&rt,0xa8e9)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* DisposeRgn refuses a region that belongs to a port or a window. */
    rt=fresh();a=create(&rt);set_box(&rt,a,(B){0,0,10,10});
    for(size_t i=0;i<rt.handle_count;i++)if(rt.handles[i].handle==a)rt.handles[i].type=0x43505254;
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,a);CHECK(mac_regions_service(&rt,0xa8d9)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* A port's pattern handle shares its type and is not a region, whatever it holds. */
    rt=fresh();a=create(&rt);set_box(&rt,a,(B){0,0,10,10});
    for(size_t i=0;i<rt.handle_count;i++)if(rt.handles[i].handle==a)rt.handles[i].type=0x43505254;
    wr(&rt,rd(&rt,a,4),2,0);mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,a);wr(&rt,SP+4,2,0);CHECK(mac_regions_service(&rt,0xa8e2)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* mac_region_box_algebra: rectangles only; a result that is not one rectangle returns 0. */
    {int out[4];const int A[4]={0,0,10,10},Bx[4]={0,10,10,20},Cx[4]={0,11,10,20},D[4]={5,5,20,20},E2[4]={2,2,8,8};
     CHECK(mac_region_box_algebra(0xa8e5,A,Bx,out)==1 && out[0]==0 && out[1]==0 && out[2]==10 && out[3]==20);          /* touching: one box */
     CHECK(mac_region_box_algebra(0xa8e5,A,Cx,out)==0);                                                                  /* a gap: two boxes */
     CHECK(mac_region_box_algebra(0xa8e4,A,D,out)==1 && out[0]==5 && out[1]==5 && out[2]==10 && out[3]==10);           /* intersection */
     CHECK(mac_region_box_algebra(0xa8e6,A,E2,out)==0);                                                                  /* a hole */
     CHECK(mac_region_box_algebra(0xa8e6,A,Bx,out)==1 && out[0]==0 && out[1]==0 && out[2]==10 && out[3]==10);          /* disjoint: unchanged */
     CHECK(mac_region_box_algebra(0xa8e7,A,A,out)==1 && out[0]==0 && out[1]==0 && out[2]==0 && out[3]==0);             /* xor with itself: empty */
     CHECK(mac_region_box_algebra(0xa8e7,A,Bx,out)==1 && out[3]==20);                                                    /* xor of touching boxes */
     CHECK(mac_region_box_algebra(0xa8e6,A,D,out)==0);}                                                                  /* an L */
}

/* OpenRgn .. CloseRgn: outline calls are observed (and left to draw); the region is what the outline encloses, even-odd. */
#define PORTA 0x9000u
static MacRuntime rec_rt(void){MacRuntime rt=fresh();rt.current_port=PORTA;return rt;}
static void obs2(MacRuntime *rt,uint16_t op,int a,int b){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,2,(uint16_t)b);wr(rt,SP+2,2,(uint16_t)a);mac_rgn_record_observe(rt,op);}   /* (h,v) or (dh,dv): v on top */
static void obs_rect(MacRuntime *rt,B b){wr(rt,R,2,(uint16_t)b.t);wr(rt,R+2,2,(uint16_t)b.l);wr(rt,R+4,2,(uint16_t)b.b);wr(rt,R+6,2,(uint16_t)b.r);mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,R);mac_rgn_record_observe(rt,0xa8a1);}
static int close_into(MacRuntime *rt,uint32_t h){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,h);return mac_regions_service(rt,0xa8db);}
static int open_it(MacRuntime *rt){mac_cpu_set(rt->cpu,MAC_A7,SP);return mac_regions_service(rt,0xa8da);}
static void recording(void){
    /* a rectangle by MoveTo and three LineTos (the fourth side is the implicit close) */
    MacRuntime rt=rec_rt();uint32_t d=create(&rt);
    CHECK(open_it(&rt)==1 && rt.rgn_rec && mac_cpu_get(rt.cpu,MAC_A7)==SP);
    obs2(&rt,0xa893,2,3);obs2(&rt,0xa891,10,3);obs2(&rt,0xa891,10,8);obs2(&rt,0xa891,2,8);
    CHECK(close_into(&rt,d)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && !rt.rgn_rec);
    {B g=get_box(&rt,d);CHECK(rd(&rt,rd(&rt,d,4),2)==10 && g.t==3 && g.l==2 && g.b==8 && g.r==10);}
    mac_cpu_destroy(rt.cpu);
    /* an L, drawn with LineTo, Line and Move */
    rt=rec_rt();d=create(&rt);open_it(&rt);
    obs2(&rt,0xa893,0,0);obs2(&rt,0xa892,10,0);obs2(&rt,0xa892,0,5);obs2(&rt,0xa891,5,5);obs2(&rt,0xa892,0,5);obs2(&rt,0xa891,0,10);
    CHECK(close_into(&rt,d)==1);{MacRgn g;mac_rgn_init(&g);CHECK(region_of(&rt,d,&g)==0 && g.n==2);
        for(int y=-2;y<14;y++)for(int x=-2;x<14;x++)CHECK(mac_rgn_contains(&g,x,y)==((y>=0&&y<5&&x>=0&&x<10)||(y>=5&&y<10&&x>=0&&x<5)));mac_rgn_free(&g);}
    mac_cpu_destroy(rt.cpu);
    /* the path starts at the pen's position when OpenRgn is called (v 6, h 4), and Move without ink starts nothing */
    rt=rec_rt();d=create(&rt);wr(&rt,PORTA+48,2,6);wr(&rt,PORTA+50,2,4);open_it(&rt);
    obs2(&rt,0xa891,12,6);obs2(&rt,0xa891,12,9);obs2(&rt,0xa891,4,9);
    CHECK(close_into(&rt,d)==1);{B g=get_box(&rt,d);CHECK(g.t==6 && g.l==4 && g.b==9 && g.r==12);}
    mac_cpu_destroy(rt.cpu);
    /* FrameRect records a closed rectangle; two nested frames make a hole (even-odd); separate sub-paths are closed separately */
    rt=rec_rt();d=create(&rt);open_it(&rt);obs_rect(&rt,(B){0,0,10,10});obs_rect(&rt,(B){3,3,7,7});
    CHECK(close_into(&rt,d)==1);{MacRgn g;mac_rgn_init(&g);CHECK(region_of(&rt,d,&g)==0 && g.n==4 && mac_rgn_contains(&g,1,1) && !mac_rgn_contains(&g,5,5));mac_rgn_free(&g);}
    mac_cpu_destroy(rt.cpu);
    rt=rec_rt();d=create(&rt);open_it(&rt);obs2(&rt,0xa893,0,0);obs2(&rt,0xa891,4,0);obs2(&rt,0xa891,4,3);obs2(&rt,0xa891,0,3);   /* a sub-path left open at a MoveTo is closed back to its start */
    obs2(&rt,0xa893,10,10);obs2(&rt,0xa891,14,10);obs2(&rt,0xa891,14,13);obs2(&rt,0xa891,10,13);
    CHECK(close_into(&rt,d)==1);{MacRgn g;mac_rgn_init(&g);CHECK(region_of(&rt,d,&g)==0 && g.n==2 && mac_rgn_contains(&g,1,1) && mac_rgn_contains(&g,11,11) && !mac_rgn_contains(&g,6,6));mac_rgn_free(&g);}
    mac_cpu_destroy(rt.cpu);
    rt=rec_rt();obs2(&rt,0xa891,5,5);obs_rect(&rt,(B){0,0,5,5});CHECK(!rt.rgn_rec && !mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);   /* observing with no region open does nothing */
    /* an empty outline gives an empty region; an inverted FrameRect records nothing */
    rt=rec_rt();d=create(&rt);set_box(&rt,d,(B){1,1,2,2});open_it(&rt);obs_rect(&rt,(B){5,5,5,9});CHECK(close_into(&rt,d)==1 && rd(&rt,rd(&rt,d,4),2)==10 && get_box(&rt,d).b==0);mac_cpu_destroy(rt.cpu);
    /* an inverted FrameRect (bottom above top, or right left of left) records nothing: it must not become a rectangle */
    rt=rec_rt();d=create(&rt);open_it(&rt);obs_rect(&rt,(B){9,0,5,10});obs_rect(&rt,(B){0,9,10,5});CHECK(close_into(&rt,d)==1 && get_box(&rt,d).b==0 && get_box(&rt,d).r==0 && rd(&rt,rd(&rt,d,4),2)==10);mac_cpu_destroy(rt.cpu);
    /* refusals: each halts by name */
    for(unsigned k=0;k<10;k++){
        rt=rec_rt();d=create(&rt);int rc=0;
        if(k==0){CHECK(open_it(&rt)==1);rc=open_it(&rt);}                                                          /* a second OpenRgn */
        if(k==1)rc=close_into(&rt,d);                                                                              /* CloseRgn without OpenRgn */
        if(k==2){open_it(&rt);obs2(&rt,0xa893,0,0);obs2(&rt,0xa891,5,5);rc=close_into(&rt,d);}                    /* a diagonal */
        if(k==3){open_it(&rt);mac_cpu_set(rt.cpu,MAC_A7,SP);rc=mac_cpu_halted(rt.cpu)?-1:(wr(&rt,SP,4,0),mac_regions_service(&rt,0xa8db));}   /* a NIL destination */
        if(k==4){open_it(&rt);obs2(&rt,0xa8b7,0,0);rc=mac_cpu_halted(rt.cpu)?-1:0;}                               /* FrameOval */
        if(k==5){open_it(&rt);obs2(&rt,0xa8c6,0,0);rc=mac_cpu_halted(rt.cpu)?-1:0;}                               /* FramePoly */
        if(k==6){open_it(&rt);obs2(&rt,0xa8d2,0,0);rc=mac_cpu_halted(rt.cpu)?-1:0;}                               /* FrameRgn */
        if(k==7){open_it(&rt);for(int i=0;i<4100 && !mac_cpu_halted(rt.cpu);i++)obs2(&rt,0xa891,i&1?0:1,0);rc=mac_cpu_halted(rt.cpu)?-1:0;}   /* too many segments */
        if(k==8){rt.current_port=0;rc=open_it(&rt);}                                                               /* no port */
        if(k==9){open_it(&rt);obs2(&rt,0xa893,0,0);obs2(&rt,0xa891,0,6);obs2(&rt,0xa891,3,6);obs2(&rt,0xa891,3,0);mac_cpu_set(rt.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(rt.cpu)-2);rc=mac_regions_service(&rt,0xa8db);}   /* bad stack */
        CHECK(rc==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    }
    /* the destination keeps its old contents when the close is refused */
    rt=rec_rt();d=create(&rt);set_box(&rt,d,(B){7,7,9,9});open_it(&rt);obs2(&rt,0xa893,0,0);obs2(&rt,0xa891,5,5);close_into(&rt,d);
    {const uint8_t *ram=mac_cpu_ram(rt.cpu);uint32_t dp=(uint32_t)(ram[d]<<24|ram[d+1]<<16|ram[d+2]<<8|ram[d+3]);CHECK(mac_cpu_halted(rt.cpu) && ram[dp+3]==7 && ram[dp+9]==9);}mac_cpu_destroy(rt.cpu);
}
int main(void){recording();complex_regions();more_checks();queries();algebra_table();set_empty();lifecycle();failures();puts("rectangular region ownership checks passed");}
