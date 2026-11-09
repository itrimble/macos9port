/* SFPutFile (Pack3 selector 1): the native save-as dialog, driven through mac_stdfile_service. */
#include "mac_dialog_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fails++;}}while(0)
static int fails;
#define SP 0x8000u
#define PC 0x5002u
#define SCR 0xe00000u
#define REPLY 0x9000u
#define PROMPT 0x9200u
#define ORIG 0x9300u
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void pstr(MacRuntime*r,uint32_t at,const char*s){size_t n=strlen(s);mac_cpu_ram(r->cpu)[at]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+at+1,s,n);}
static MacRuntime rig(const char*orig){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    r.screen_base=SCR;r.screen_width=640;r.screen_height=480;r.screen_row_bytes=640;r.screen_depth=8;r.palette[0]=0xffffff;
    for(unsigned i=0;i<640*480;i++)mac_cpu_ram(r.cpu)[SCR+i]=(uint8_t)(i*7);
    pstr(&r,PROMPT,"Save Game As:");pstr(&r,ORIG,orig);memset(mac_cpu_ram(r.cpu)+REPLY,0xee,74);return r;
}
static int put(MacRuntime*r,uint32_t where){
    wr(r,SP,2,1);wr(r,SP+2,4,REPLY);wr(r,SP+6,4,0);wr(r,SP+10,4,ORIG);wr(r,SP+14,4,PROMPT);wr(r,SP+18,4,where);
    mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);return mac_stdfile_service(r,0xa9ea);
}
static int again(MacRuntime*r){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);return mac_stdfile_service(r,0xa9ea);}
static void key(MacRuntime*r,unsigned ch,unsigned mods){mac_runtime_post_event(r,3,ch,(uint16_t)mods,0,0);}
static void type(MacRuntime*r,const char*s){for(;*s;s++)key(r,(unsigned char)*s,0);}
static void click(MacRuntime*r,int x,int y){mac_runtime_post_event(r,1,0,0,(int16_t)x,(int16_t)y);mac_runtime_post_event(r,2,0,0,(int16_t)x,(int16_t)y);}
static int open_now(MacRuntime*r){return r->stdput!=NULL;}
static int good(MacRuntime*r){return rd(r,REPLY,1);}
static int name_is(MacRuntime*r,const char*s){return rd(r,REPLY+10,1)==strlen(s)&&!memcmp(mac_cpu_ram(r->cpu)+REPLY+11,s,strlen(s));}
static int screen_same(MacRuntime*r){for(unsigned i=0;i<640*480;i++)if(mac_cpu_ram(r->cpu)[SCR+i]!=(uint8_t)(i*7))return 0;return 1;}
/* Dialog at where=(top 100,left 100): Save button x 300..380, y 200..220; Cancel x 210..290. */
#define WHERE ((100u<<16)|100u)
#define SAVEX 340,210
#define CANCELX 250,210

static void saving(void){
    MacRuntime r=rig("");CHECK(put(&r,WHERE)==1 && open_now(&r) && mac_cpu_get(r.cpu,MAC_PC)==0x5000 && mac_cpu_get(r.cpu,MAC_A7)==SP);
    CHECK(!screen_same(&r));                                              /* the dialog is drawn */
    click(&r,SAVEX);CHECK(again(&r)==1 && open_now(&r));                  /* Save is dimmed while the name is empty */
    key(&r,13,0);CHECK(again(&r)==1 && open_now(&r));                     /* ... and Return does nothing */
    type(&r,"Ab:c d");key(&r,8,0);key(&r,8,0);key(&r,8,0);key(&r,8,0);key(&r,8,0);key(&r,8,0);key(&r,8,0);   /* ':' is refused; over-deleting is safe */
    type(&r,"Game:1");CHECK(again(&r)==1 && open_now(&r));
    click(&r,SAVEX);CHECK(again(&r)==1 && !open_now(&r) && mac_cpu_get(r.cpu,MAC_A7)==SP+22 && screen_same(&r));
    CHECK(good(&r)==1 && rd(&r,REPLY+1,1)==0 && rd(&r,REPLY+2,4)==0 && rd(&r,REPLY+6,2)==0xffff && rd(&r,REPLY+8,2)==0 && name_is(&r,"Game1"));
    mac_cpu_destroy(r.cpu);
    r=rig("");put(&r,WHERE);type(&r,"X");key(&r,13,0);CHECK(again(&r)==1 && !open_now(&r) && good(&r)==1 && name_is(&r,"X"));mac_cpu_destroy(r.cpu);
    r=rig("");put(&r,WHERE);type(&r,"X");key(&r,3,0);CHECK(again(&r)==1 && !open_now(&r) && good(&r)==1);mac_cpu_destroy(r.cpu);
    /* a name cannot grow past 31 characters */
    r=rig("");put(&r,WHERE);type(&r,"0123456789012345678901234567890123456789");key(&r,13,0);CHECK(again(&r)==1 && good(&r)==1 && rd(&r,REPLY+10,1)==31);mac_cpu_destroy(r.cpu);
}
static void cancelling(void){
    for(unsigned k=0;k<3;k++){
        MacRuntime r=rig("Keep");put(&r,WHERE);
        if(k==0)click(&r,CANCELX);if(k==1)key(&r,27,0);if(k==2)key(&r,'.',0x100);
        CHECK(again(&r)==1 && !open_now(&r) && good(&r)==0 && !memcmp(mac_cpu_ram(r.cpu)+REPLY,(uint8_t[74]){0},74) && screen_same(&r) && mac_cpu_get(r.cpu,MAC_A7)==SP+22);
        mac_cpu_destroy(r.cpu);
    }
    /* a press on Save that is released outside it does nothing; Cmd-S style keys with the command bit insert nothing */
    MacRuntime r=rig("Q");put(&r,WHERE);mac_runtime_post_event(&r,1,0,0,340,210);mac_runtime_post_event(&r,2,0,0,10,10);key(&r,'z',0x100);CHECK(again(&r)==1 && open_now(&r));
    key(&r,13,0);CHECK(again(&r)==1 && good(&r)==1 && name_is(&r,"Q"));mac_cpu_destroy(r.cpu);
}
static void origname(void){
    MacRuntime r=rig("Saved:Game");put(&r,WHERE);key(&r,13,0);CHECK(again(&r)==1 && name_is(&r,"Saved-Game"));mac_cpu_destroy(r.cpu);
    char longname[80];memset(longname,'n',70);longname[70]=0;r=rig(longname);put(&r,WHERE);key(&r,13,0);CHECK(again(&r)==1 && rd(&r,REPLY+10,1)==31);mac_cpu_destroy(r.cpu);
    r=rig("A");put(&r,WHERE);key(&r,8,0);key(&r,8,0);key(&r,13,0);CHECK(again(&r)==1 && open_now(&r));mac_cpu_destroy(r.cpu);      /* deleting the prefilled name empties it: Save is off */
    r=rig("A");mac_cpu_ram(r.cpu)[ORIG+1]=0x01;put(&r,WHERE);key(&r,13,0);CHECK(again(&r)==1 && name_is(&r,"-"));mac_cpu_destroy(r.cpu);  /* control characters become '-' */
}
static void placement(void){
    MacRuntime r=rig("a");CHECK(put(&r,((600u<<16)|600u))==1);                      /* where is clamped onto the screen: the border reaches row 479 and column 639 exactly */
    {unsigned d=0;for(int x=400;x<640;x++)if(mac_cpu_ram(r.cpu)[SCR+479*640+x]!=(uint8_t)((479*640+x)*7))d++;CHECK(d>100);d=0;for(int x=400;x<540;x++)if(mac_cpu_ram(r.cpu)[SCR+340*640+x]!=(uint8_t)((340*640+x)*7))d++;CHECK(d>100);d=0;for(int y=340;y<480;y++)if(mac_cpu_ram(r.cpu)[SCR+y*640+639]!=(uint8_t)((y*640+639)*7))d++;CHECK(d>100);}
    key(&r,13,0);CHECK(again(&r)==1 && screen_same(&r));mac_cpu_destroy(r.cpu);
    r=rig("a");CHECK(put(&r,((0xfff0u<<16)|0xfff0u))==1);key(&r,13,0);CHECK(again(&r)==1 && screen_same(&r));mac_cpu_destroy(r.cpu);
}
static void refusals(void){
    for(unsigned k=0;k<10;k++){
        MacRuntime r=rig("a");int rc;
        if(k==0){wr(&r,SP+6,4,0);mac_cpu_set(r.cpu,MAC_A7,SP+1);mac_cpu_set(r.cpu,MAC_PC,PC);rc=mac_stdfile_service(&r,0xa9ea);}            /* odd stack */
        else if(k==1){mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-10);wr(&r,(uint32_t)mac_cpu_ram_size(r.cpu)-10,2,1);rc=mac_stdfile_service(&r,0xa9ea);}  /* stack too short */
        else{
            wr(&r,SP,2,1);wr(&r,SP+2,4,REPLY);wr(&r,SP+6,4,0);wr(&r,SP+10,4,ORIG);wr(&r,SP+14,4,PROMPT);wr(&r,SP+18,4,WHERE);
            if(k==2)wr(&r,SP+6,4,0x7000);                                                  /* a dialog hook */
            if(k==3)wr(&r,SP+2,4,(uint32_t)mac_cpu_ram_size(r.cpu)-10);                     /* reply record outside RAM */
            if(k==4)wr(&r,SP+2,4,0);
            if(k==5){mac_cpu_ram(r.cpu)[mac_cpu_ram_size(r.cpu)-1]=200;wr(&r,SP+14,4,(uint32_t)mac_cpu_ram_size(r.cpu)-1);}                     /* prompt string runs off RAM */
            if(k==6){mac_cpu_ram(r.cpu)[mac_cpu_ram_size(r.cpu)-1]=200;wr(&r,SP+10,4,(uint32_t)mac_cpu_ram_size(r.cpu)-1);}                   /* origName likewise */
            if(k==7)r.screen_depth=1;
            if(k==8){mac_cpu_ram(r.cpu)[mac_cpu_ram_size(r.cpu)-100]=250;wr(&r,SP+14,4,(uint32_t)mac_cpu_ram_size(r.cpu)-100);}
            if(k==9){r.screen_base=(uint32_t)mac_cpu_ram_size(r.cpu)-100;}
            mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,PC);rc=mac_stdfile_service(&r,0xa9ea);
        }
        if(!(rc==-1 && mac_cpu_halted(r.cpu) && !r.stdput))fprintf(stderr,"refusal case %u rc=%d halted=%d put=%p\n",k,rc,mac_cpu_halted(r.cpu),(void*)r.stdput);CHECK(rc==-1 && mac_cpu_halted(r.cpu) && !r.stdput);mac_cpu_destroy(r.cpu);
    }
    /* called again from a different place, or with a different stack, while the dialog is open; another selector while open */
    MacRuntime r=rig("a");put(&r,WHERE);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,PC+2);CHECK(mac_stdfile_service(&r,0xa9ea)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    r=rig("a");put(&r,WHERE);wr(&r,SP,2,2);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,PC);CHECK(mac_stdfile_service(&r,0xa9ea)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    /* an unknown selector still halts by name */
    r=rig("a");wr(&r,SP,2,3);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,PC);CHECK(mac_stdfile_service(&r,0xa9ea)==-1);mac_cpu_destroy(r.cpu);
    /* destroying the runtime state while the dialog is open frees it */
    r=rig("a");put(&r,WHERE);mac_stdput_destroy(&r);CHECK(!r.stdput);mac_stdput_destroy(&r);mac_cpu_destroy(r.cpu);
}
/* StandardPutFile (selector 5): reply, defaultName, prompt; centred; StandardFileReply of 88 bytes. */
static int put5(MacRuntime*r){
    wr(r,SP,2,5);wr(r,SP+2,4,REPLY);wr(r,SP+6,4,ORIG);wr(r,SP+10,4,PROMPT);memset(mac_cpu_ram(r->cpu)+REPLY,0xee,88);
    mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);return mac_stdfile_service(r,0xa9ea);
}
static void standard_put(void){
    MacRuntime r=rig("Start");CHECK(put5(&r)==1 && open_now(&r));
    CHECK(!screen_same(&r));
    {unsigned d=0;for(int x=170;x<470;x++)if(mac_cpu_ram(r.cpu)[SCR+170*640+x]!=(uint8_t)((170*640+x)*7))d++;CHECK(d>100);}   /* centred: top row 170, left 170 */
    {unsigned on=0,off=0;for(int y=200;y<300;y++){on+=mac_cpu_ram(r.cpu)[SCR+y*640+170]!=(uint8_t)((y*640+170)*7);off+=mac_cpu_ram(r.cpu)[SCR+y*640+169]!=(uint8_t)((y*640+169)*7);}CHECK(on>50 && off==0);}   /* left edge exactly 170 */
    type(&r,"X");click(&r,410,280);CHECK(again(&r)==1 && !open_now(&r) && mac_cpu_get(r.cpu,MAC_A7)==SP+14 && screen_same(&r));
    CHECK(good(&r)==1 && rd(&r,REPLY+1,1)==0 && rd(&r,REPLY+2,4)==0 && rd(&r,REPLY+6,2)==0xffff && rd(&r,REPLY+8,4)==2);
    CHECK(rd(&r,REPLY+12,1)==6 && !memcmp(mac_cpu_ram(r.cpu)+REPLY+13,"StartX",6) && rd(&r,REPLY+19,4)==0 && rd(&r,REPLY+76,4)==0 && rd(&r,REPLY+80,4)==0 && rd(&r,REPLY+84,4)==0);
    mac_cpu_destroy(r.cpu);
    r=rig("Q");put5(&r);click(&r,320,280);CHECK(again(&r)==1 && !open_now(&r) && mac_cpu_get(r.cpu,MAC_A7)==SP+14);   /* Cancel */
    {unsigned nz=0;for(unsigned i=0;i<88;i++)nz+=mac_cpu_ram(r.cpu)[REPLY+i]!=0;CHECK(nz==0);}
    mac_cpu_destroy(r.cpu);
    for(unsigned k=0;k<4;k++){                                      /* refusals */
        r=rig("a");wr(&r,SP,2,5);wr(&r,SP+2,4,REPLY);wr(&r,SP+6,4,ORIG);wr(&r,SP+10,4,PROMPT);
        if(k==0)wr(&r,SP+2,4,(uint32_t)mac_cpu_ram_size(r.cpu)-80);   /* room for an SFReply but not a StandardFileReply */
        if(k==1)wr(&r,SP+10,4,(uint32_t)mac_cpu_ram_size(r.cpu)+8);
        if(k==2)wr(&r,SP+6,4,0);
        mac_cpu_set(r.cpu,MAC_A7,k==3?(uint32_t)mac_cpu_ram_size(r.cpu)-6:SP);mac_cpu_set(r.cpu,MAC_PC,PC);
        CHECK(mac_stdfile_service(&r,0xa9ea)==-1 && mac_cpu_halted(r.cpu) && !r.stdput);mac_cpu_destroy(r.cpu);
    }
}
int main(void){
    standard_put();saving();cancelling();origname();placement();refusals();
    if(!fails)puts("SFPutFile checks passed");
    return fails?1:0;
}
