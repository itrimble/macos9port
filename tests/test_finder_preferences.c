#include "finder_preferences.h"
#include "finder_spring.h"
#include "session_int.h"
#include "menu_navigation.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);} } while(0)
static void write_file(const char *path,const char *text){FILE *f=fopen(path,"w");CHECK(f);if(f){CHECK(fputs(text,f)>=0);CHECK(!fclose(f));}}
static void row(Session *s,int n){for(int i=0;i<n;i++)session_key(s,MENU_KEY_DOWN,0);}
static int item(Session *s,const char *name,int *x,int *y){
    for(int i=0;i<s->win[0].nitems;i++)if(!strcmp(window_items(&s->win[0])[i].name,name))return ospi_item_center(s,0,i,x,y);
    return -1;
}
int main(void){
    char root[]="/tmp/os999-finder-prefs-XXXXXX",path[OSPI_PATH+96],temp[OSPI_PATH+96],target[OSPI_PATH+96];
    CHECK(mkdtemp(root));CHECK(!volume_seed(root));Session *s=session_create(root);CHECK(s);if(!s)return 1;
    snprintf(path,sizeof path,"%s/.os999-finder-preferences",root);
    snprintf(temp,sizeof temp,"%s/.os999-finder-preferences.tmp.%ld",root,(long)getpid());
    snprintf(target,sizeof target,"%s/sentinel",root);
    CHECK(!s->spring_disabled && s->spring_delay_ms==600);
    session_command(s,CMD_PREFERENCES);row(s,3);session_key(s,' ',0);row(s,1);
    session_key(s,MENU_KEY_RIGHT,0);CHECK(s->settings_delay==700);session_key(s,27,0);
    CHECK(!s->spring_disabled && s->spring_delay_ms==600);
    session_command(s,CMD_PREFERENCES);row(s,3);session_key(s,' ',0);row(s,1);
    session_key(s,MENU_KEY_LEFT,0);session_key(s,'\r',0);
    CHECK(s->spring_disabled && s->spring_delay_ms==500 && !s->dialog);
    session_destroy(s);s=session_create(root);CHECK(s->spring_disabled && s->spring_delay_ms==500);
    session_command(s,CMD_PREFERENCES);row(s,4);Rect box,cancel,ok;dialog_layout(s,&box,&cancel,&ok);
    int y=box.y+40+4*26+12;
    session_down(s,box.x+150,y,1);session_up(s,box.x+150,y);CHECK(s->settings_delay==100);
    session_down(s,box.x+box.w-40,y,1);session_up(s,box.x+box.w-40,y);CHECK(s->settings_delay==2000);
    session_key(s,MENU_KEY_RIGHT,0);CHECK(s->settings_delay==2000);session_key(s,'\r',0);
    CHECK(s->spring_delay_ms==2000);
    session_command(s,CMD_PREFERENCES);
    session_down(s,box.x+150,y,1);CHECK(s->down && s->drag==7);
    session_move(s,box.x+150+(box.w-190)/2,y);CHECK(s->settings_delay==1100);
    session_up(s,box.x+box.w+200,y);CHECK(s->settings_delay==2000 && !s->down && !s->drag);
    session_down(s,box.x+150,y,1);session_key(s,27,0);
    CHECK(!s->dialog && !s->down && !s->drag && s->spring_delay_ms==2000);
    session_command(s,CMD_PREFERENCES);session_down(s,box.x+150,y,1);session_key(s,'\r',0);
    CHECK(!s->dialog && !s->down && !s->drag && s->spring_delay_ms==100);
    CHECK(!finder_preferences_save(s,0,2000));
    CHECK(finder_preferences_save(s,1,99)==-1 && s->spring_disabled);
    CHECK(finder_preferences_save(s,2,600)==-1 && s->spring_delay_ms==2000);
    write_file(temp,"keep\n");session_command(s,CMD_PREFERENCES);session_key(s,' ',0);row(s,3);session_key(s,' ',0);session_key(s,'\r',0);
    CHECK(s->dialog==4 && s->spring_disabled && !s->sound_toggle_request);
    CHECK(!unlink(temp));session_key(s,'\r',0);CHECK(!s->spring_disabled && s->sound_toggle_request);s->sound_toggle_request=0;
    const char *bad[]={"OS999-PREFERENCES-1\n1 99\n","OS999-PREFERENCES-1\n2 600\n","OS999-PREFERENCES-1\n1 600\nextra","OS999-PREFERENCES-1\n1 999999999999999999999999\n"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++){write_file(path,bad[i]);CHECK(finder_preferences_load(s)==-1 && !s->spring_disabled && s->spring_delay_ms==600);}
    CHECK(!unlink(path));CHECK(!mkfifo(path,0600));CHECK(finder_preferences_load(s)==-1);CHECK(!unlink(path));
    write_file(target,"untouched\n");CHECK(!symlink(target,path));CHECK(finder_preferences_load(s)==-1);
    CHECK(!finder_preferences_save(s,1,1200));struct stat st;CHECK(!lstat(path,&st)&&S_ISREG(st.st_mode));
    FILE *f=fopen(target,"r");char buf[32]={0};CHECK(f);if(f){CHECK(fgets(buf,sizeof buf,f));fclose(f);}CHECK(!strcmp(buf,"untouched\n"));
    int x=0,iy=0;CHECK(!item(s,"Read Me",&x,&iy));session_down(s,x,iy,1);session_move(s,x+10,iy+10);
    CHECK(!item(s,"Documents",&x,&iy));session_move(s,x,iy);session_tick(s,100);session_tick(s,1299);CHECK(s->nwin==1);
    session_tick(s,1300);CHECK(s->nwin==2 && s->spring.active);
    CHECK(!finder_preferences_save(s,0,1200));session_tick(s,1301);CHECK(s->nwin==1&&!s->spring.active&&!s->down);
    session_destroy(s);CHECK(!archive_remove_tree(root));printf("Finder preferences: %d checks, %d failures\n",checks,failures);return !!failures;
}
