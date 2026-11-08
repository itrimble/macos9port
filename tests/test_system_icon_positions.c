#include "system_icon_positions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static void put(const char *path,const char *text) { FILE *f=fopen(path,"w");CHECK(f);if(f){CHECK(fputs(text,f)>=0);CHECK(!fclose(f));} }
int main(void) {
    char root[]="/tmp/os999-system-icon-XXXXXX",path[512],temp[512],target[512];CHECK(mkdtemp(root));
    snprintf(path,sizeof path,"%s/.os999-desktop-icons",root);
    snprintf(temp,sizeof temp,"%s/.os999-desktop-icons.tmp.%ld",root,(long)getpid());
    snprintf(target,sizeof target,"%s/sentinel",root);
    SystemIconPositions p={0},loaded={0};CHECK(system_icon_positions_load(root,&loaded)==0&&!loaded.mask);
    p=(SystemIconPositions){.mask=3,.x={100,600},.y={80,400}};
    CHECK(!system_icon_positions_save(root,&p));CHECK(system_icon_positions_load(root,&loaded)==1);
    CHECK(loaded.mask==3&&loaded.x[0]==100&&loaded.y[0]==80&&loaded.x[1]==600&&loaded.y[1]==400);
    p.mask=4;CHECK(system_icon_positions_save(root,&p)<0);p.mask=3;
    p.x[0]=-1;CHECK(system_icon_positions_save(root,&p)<0);p.x[0]=100;
    put(temp,"keep\n");p.x[0]=200;CHECK(system_icon_positions_save(root,&p)<0);
    CHECK(system_icon_positions_load(root,&loaded)==1&&loaded.x[0]==100);CHECK(!unlink(temp));
    CHECK(!system_icon_positions_save(root,&p));CHECK(system_icon_positions_load(root,&loaded)==1&&loaded.x[0]==200);
    const char *bad[]={"OS999-DESKTOP-ICONS-1\n4 1 2 3 4\n","OS999-DESKTOP-ICONS-1\n3 -1 2 3 4\n","OS999-DESKTOP-ICONS-1\n3 1 2 3 32768\n","OS999-DESKTOP-ICONS-1\n3 1 2 3 999999999999999999999999\n","OS999-DESKTOP-ICONS-1\n3 1 2 3 4\nextra"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++){put(path,bad[i]);CHECK(system_icon_positions_load(root,&loaded)<0&&!loaded.mask);}
    CHECK(!unlink(path));CHECK(!mkfifo(path,0600));CHECK(system_icon_positions_load(root,&loaded)<0);CHECK(!unlink(path));
    put(target,"untouched\n");CHECK(!symlink(target,path));CHECK(system_icon_positions_load(root,&loaded)<0);
    CHECK(!system_icon_positions_save(root,&p));struct stat st;CHECK(!lstat(path,&st)&&S_ISREG(st.st_mode));
    FILE *f=fopen(target,"r");char text[32]={0};CHECK(f);if(f){CHECK(fgets(text,sizeof text,f));fclose(f);}CHECK(!strcmp(text,"untouched\n"));
    CHECK(!unlink(target));CHECK(!unlink(path));CHECK(!rmdir(root));
    printf("System icon positions: %d checks, %d failures\n",checks,failures);return !!failures;
}
