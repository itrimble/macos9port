#include "mac_runtime.h"
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
/* SectRgn/UnionRgn/DiffRgn/XorRgn on rectangular regions: exact when the result is a rectangle, a named halt when it is not. */
typedef struct{int t,l,b,r;}B;
static void set_box(MacRuntime *rt,uint32_t h,B b){uint32_t p=rd(rt,h,4);wr(rt,p,2,10);wr(rt,p+2,2,(uint16_t)b.t);wr(rt,p+4,2,(uint16_t)b.l);wr(rt,p+6,2,(uint16_t)b.b);wr(rt,p+8,2,(uint16_t)b.r);}
static B get_box(MacRuntime *rt,uint32_t h){uint32_t p=rd(rt,h,4);return (B){(int16_t)rd(rt,p+2,2),(int16_t)rd(rt,p+4,2),(int16_t)rd(rt,p+6,2),(int16_t)rd(rt,p+8,2)};}
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
        else{const uint8_t *ram=mac_cpu_ram(rt.cpu);uint32_t dp=(uint32_t)(ram[d]<<24|ram[d+1]<<16|ram[d+2]<<8|ram[d+3]);   /* raw RAM: reads fail once halted */
            CHECK(rc==-1 && mac_cpu_halted(rt.cpu) && ram[dp+3]==7 && ram[dp+9]==7);}               /* a named halt, destination untouched */
        mac_cpu_destroy(rt.cpu);
    }
    /* The destination may be one of the sources. */
    {MacRuntime rt=fresh();uint32_t a=create(&rt),b=create(&rt);set_box(&rt,a,(B){0,0,10,10});set_box(&rt,b,(B){5,5,20,20});
     CHECK(algebra_call(&rt,0xa8e4,a,b,a)==1);B g=get_box(&rt,a);CHECK(g.t==5 && g.l==5 && g.b==10 && g.r==10);
     set_box(&rt,a,(B){0,0,10,10});CHECK(algebra_call(&rt,0xa8e6,a,b,b)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);}
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
    CHECK(mac_regions_service(&rt,0xa8ea)==0 && mac_regions_service(&rt,0xa8da)==0 && mac_regions_service(&rt,0xa8ee)==0);mac_cpu_destroy(rt.cpu);
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
int main(void){queries();algebra_table();set_empty();lifecycle();failures();puts("rectangular region ownership checks passed");}
