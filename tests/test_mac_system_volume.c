#include "mac_system_volume.h"
#include <stdio.h>
#include <string.h>
static int failed;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);failed++;}}while(0)
enum { PB=0x100, NAME=0x300, BUFFER=0x400 };
static void put(MacRuntime*r,unsigned off,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,PB+off,n,v));}
static uint32_t get(MacRuntime*r,unsigned off,unsigned n){return mac_cpu_read(r->cpu,PB+off,n);}
static void name(MacRuntime*r,const char*s){size_t n=strlen(s);mac_cpu_ram(r->cpu)[NAME]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+NAME+1,s,n);put(r,18,4,NAME);}
static void reset(MacRuntime*r){memset(mac_cpu_ram(r->cpu)+PB,0,128);put(r,22,2,0xfffe);}
static int call(MacRuntime*r,MacSystemVolume*v,unsigned op,unsigned selector){
    mac_cpu_set(r->cpu,MAC_A0,PB);mac_cpu_set(r->cpu,MAC_D0,selector);mac_cpu_set(r->cpu,MAC_A7,0x1000);
    CHECK(mac_system_volume_service(r,v,(uint16_t)op)==1);
    int error=(int16_t)get(r,16,2);CHECK((int32_t)mac_cpu_get(r->cpu,MAC_D0)==error);
    CHECK(mac_cpu_get(r->cpu,MAC_A0)==PB && mac_cpu_get(r->cpu,MAC_A7)==0x1000);
    CHECK((mac_cpu_get(r->cpu,MAC_SR)&15)==(error?8u:4u));return error;
}
static void read_params(MacRuntime*r,unsigned ref,int count,int mode,int offset){
    reset(r);put(r,24,2,ref);put(r,32,4,BUFFER);put(r,36,4,(uint32_t)count);
    put(r,44,2,(uint16_t)mode);put(r,46,4,(uint32_t)offset);
}
int main(void){
    MacRuntime r={0};r.cpu=mac_cpu_create_model(8192,MAC_CPU_68020);CHECK(r.cpu!=NULL);if(!r.cpu)return 1;
    MacSystemVolume*v=mac_system_volume_create(&r);CHECK(v!=NULL);if(!v)return 1;
    CHECK(mac_cpu_read(r.cpu,0xa58,2)==64);CHECK(mac_cpu_ram(r.cpu)[0xad8]==6);
    CHECK(!memcmp(mac_cpu_ram(r.cpu)+0xad9,"System",6));
    read_params(&r,64,286,1,0);CHECK(call(&r,v,0xa002,0)==0);CHECK(get(&r,40,4)==286);
    MacResourceFork fork;CHECK(mac_resource_open(&fork,mac_cpu_ram(r.cpu)+BUFFER,286,NULL,0)==0);
    CHECK(mac_resource_count(&fork)==0);
    uint8_t original[286];memcpy(original,mac_cpu_ram(r.cpu)+BUFFER,286);
    reset(&r);put(&r,24,2,64);put(&r,18,4,NAME);CHECK(call(&r,v,0xa260,8)==0);
    CHECK(get(&r,32,4)==16 && get(&r,36,2)==0x6200 && get(&r,40,4)==286);
    CHECK(get(&r,44,4)==512 && get(&r,48,4)==286 && get(&r,52,2)==0xfffe && get(&r,58,4)==2);
    CHECK(mac_cpu_ram(r.cpu)[NAME]==6);
    read_params(&r,64,300,1,0);CHECK(call(&r,v,0xa002,0)==-39);CHECK(get(&r,40,4)==286 && get(&r,46,4)==286);
    CHECK(!memcmp(original,mac_cpu_ram(r.cpu)+BUFFER,286));
    read_params(&r,64,2,2,-2);CHECK(call(&r,v,0xa002,0)==0);
    CHECK(mac_cpu_ram(r.cpu)[BUFFER]==0xff && mac_cpu_ram(r.cpu)[BUFFER+1]==0xff);
    read_params(&r,64,-1,0,0);CHECK(call(&r,v,0xa002,0)==-50);
    read_params(&r,64,1,1,-1);CHECK(call(&r,v,0xa002,0)==-40);
    read_params(&r,64,1,0x80,0);CHECK(call(&r,v,0xa002,0)==-50);
    reset(&r);put(&r,24,2,64);CHECK(call(&r,v,0xa001,0)==-54);CHECK(call(&r,v,0xa003,0)==-46);
    CHECK(call(&r,v,0xa011,0)==0 && get(&r,28,4)==286);
    put(&r,44,2,1);put(&r,46,4,12);CHECK(call(&r,v,0xa044,0)==0);
    CHECK(call(&r,v,0xa018,0)==0 && get(&r,46,4)==12);
    reset(&r);name(&r,"System");put(&r,27,1,1);CHECK(call(&r,v,0xa000,0)==0);unsigned data_ref=get(&r,24,2);CHECK(data_ref==65);
    read_params(&r,data_ref,1,0,0);CHECK(call(&r,v,0xa002,0)==-39 && get(&r,40,4)==0);
    CHECK(call(&r,v,0xa001,0)==0);CHECK(call(&r,v,0xa001,0)==-51);
    reset(&r);name(&r,"sYsTeM");CHECK(call(&r,v,0xa20a,0)==0);unsigned rf=get(&r,24,2);
    read_params(&r,rf,16,1,0);CHECK(call(&r,v,0xa002,0)==0);CHECK(!memcmp(original,mac_cpu_ram(r.cpu)+BUFFER,16));
    reset(&r);name(&r,"System");put(&r,27,1,3);CHECK(call(&r,v,0xa000,0)==-54);
    name(&r,"../System");put(&r,27,1,1);CHECK(call(&r,v,0xa000,0)==-43);
    reset(&r);put(&r,28,2,1);put(&r,18,4,NAME);CHECK(call(&r,v,0xa260,8)==0 && get(&r,24,2)==64);
    put(&r,28,2,2);CHECK(call(&r,v,0xa260,8)==0 && get(&r,24,2)==rf);
    put(&r,28,2,3);CHECK(call(&r,v,0xa260,8)==-51);
    reset(&r);put(&r,18,4,NAME);CHECK(call(&r,v,0xa260,7)==0);
    CHECK(get(&r,32,2)==0xfffe && get(&r,48,4)==2 && mac_cpu_ram(r.cpu)[NAME]==12);
    reset(&r);put(&r,48,4,2);CHECK(call(&r,v,0xa260,1)==0 && get(&r,22,2)==0xfffe);
    CHECK(call(&r,v,0xa260,2)==0);put(&r,48,4,99);CHECK(call(&r,v,0xa260,1)==-120);
    reset(&r);put(&r,28,2,0xffff);put(&r,48,4,2);put(&r,18,4,NAME);
    CHECK(call(&r,v,0xa260,9)==0 && get(&r,30,1)==0x10 && get(&r,52,2)==1 && get(&r,100,4)==1);
    uint32_t created=get(&r,72,4);CHECK(created>2082844800u && get(&r,76,4)==created);
    reset(&r);name(&r,"System");put(&r,48,4,2);CHECK(call(&r,v,0xa260,9)==0);
    CHECK(get(&r,24,2)==64 && get(&r,30,1)==0x85 && get(&r,48,4)==16 && get(&r,64,4)==286 && get(&r,68,4)==512 && get(&r,100,4)==2);
    reset(&r);put(&r,28,2,1);put(&r,48,4,2);put(&r,18,4,NAME);CHECK(call(&r,v,0xa260,9)==0);
    CHECK(mac_cpu_ram(r.cpu)[NAME]==6);reset(&r);put(&r,28,2,2);CHECK(call(&r,v,0xa260,9)==-43);
    read_params(&r,64,16,1,0);put(&r,32,4,8185);put(&r,40,4,0x12345678);
    CHECK(call(&r,v,0xa002,0)==-50 && get(&r,40,4)==0x12345678);
    read_params(&r,64,16,1,0);put(&r,32,4,PB+40);put(&r,40,4,0x12345678);
    CHECK(call(&r,v,0xa002,0)==-50 && get(&r,40,4)==0x12345678);
    reset(&r);put(&r,24,2,64);CHECK(call(&r,v,0xa018,0)==0 && get(&r,46,4)==12);
    reset(&r);put(&r,24,2,64);put(&r,18,4,8188);put(&r,40,4,0x12345678);
    CHECK(call(&r,v,0xa260,8)==-50 && get(&r,40,4)==0x12345678);
    reset(&r);put(&r,24,2,0);CHECK(call(&r,v,0xa260,8)==-51);
    reset(&r);put(&r,28,2,0xffff);put(&r,48,4,2);put(&r,18,4,8188);put(&r,52,2,123);
    CHECK(call(&r,v,0xa260,9)==-50 && get(&r,52,2)==123);
    reset(&r);put(&r,18,4,NAME);CHECK(call(&r,v,0xa207,0)==0);
    CHECK(get(&r,38,2)==0x8000 && get(&r,46,2)==1 && get(&r,48,4)==512 && get(&r,62,2)==0);
    CHECK(get(&r,82,4)==1 && get(&r,90,4)==2 && get(&r,30,4)==created);
    reset(&r);put(&r,22,2,0);put(&r,28,2,2);CHECK(call(&r,v,0xa207,0)==0 && get(&r,22,2)==0xfffe);
    reset(&r);put(&r,22,2,0);put(&r,28,2,0xffff);name(&r,"OS Pi System");
    CHECK(call(&r,v,0xa207,0)==0 && get(&r,22,2)==0xfffe);
    reset(&r);put(&r,28,2,3);CHECK(call(&r,v,0xa207,0)==-35);
    reset(&r);put(&r,18,4,8190);put(&r,40,4,0x12345678);CHECK(call(&r,v,0xa207,0)==-50 && get(&r,40,4)==0x12345678);
    reset(&r);put(&r,18,4,PB+90);CHECK(call(&r,v,0xa207,0)==-50);
    reset(&r);CHECK(call(&r,v,0xa607,0)==-50);
    reset(&r);put(&r,22,2,0xffff);mac_cpu_set(r.cpu,MAC_D0,7);CHECK(mac_system_volume_service(&r,v,0xa260)==0);
    mac_cpu_set(r.cpu,MAC_D0,123);CHECK(mac_system_volume_service(&r,v,0xa260)==0);
    /* Exactly 50 bytes remain: ordinary IOParam does not require HFS bytes. */
    memset(mac_cpu_ram(r.cpu)+8142,0,50);mac_cpu_write(r.cpu,8142+24,2,64);
    mac_cpu_set(r.cpu,MAC_A0,8142);CHECK(mac_system_volume_service(&r,v,0xa011)==1);
    CHECK(mac_cpu_read(r.cpu,8142+28,4)==286 && !mac_cpu_halted(r.cpu));
    /* GetEOF requires only its output through byte31, for System files too. */
    memset(mac_cpu_ram(r.cpu)+8160,0,32);mac_cpu_write(r.cpu,8184,2,64);
    mac_cpu_set(r.cpu,MAC_A0,8160);CHECK(mac_system_volume_service(&r,v,0xa011)==1);
    CHECK(mac_cpu_read(r.cpu,8188,4)==286 && !mac_cpu_halted(r.cpu));
    mac_cpu_set(r.cpu,MAC_A0,8180);mac_cpu_set(r.cpu,MAC_D0,9);memset(mac_cpu_ram(r.cpu)+8180,0x5a,12);
    CHECK(mac_system_volume_service(&r,v,0xa260)==-1);CHECK(mac_cpu_halted(r.cpu));
    for(unsigned i=8180;i<8192;i++)CHECK(mac_cpu_ram(r.cpu)[i]==0x5a);
    mac_system_volume_destroy(v);mac_cpu_destroy(r.cpu);
    printf("Authored system volume: %s\n",failed?"FAILED":"ok");return failed!=0;
}
