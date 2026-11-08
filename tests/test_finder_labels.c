#include "finder_labels.h"
#include "finder_files.h"
#include "menu_navigation.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if(!(c)){failures++;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);} } while(0)
static void write_file(const char *path,const char *text){FILE *f=fopen(path,"w");CHECK(f);if(f){CHECK(fputs(text,f)>=0);CHECK(!fclose(f));}}
static void rename_first(Session *s,const char *name){session_command(s,CMD_EDIT_LABELS);CHECK(s->dialog==5);session_text_input(s,name);}
int main(void){
    char root[]="/tmp/os999-labels-XXXXXX",path[OSPI_PATH+80],temp[OSPI_PATH+80],target[OSPI_PATH+80];
    CHECK(mkdtemp(root));CHECK(!volume_seed(root));Session *s=session_create(root);CHECK(s);if(!s)return 1;
    snprintf(path,sizeof path,"%s/.os999-labels",root);snprintf(temp,sizeof temp,"%s.tmp.%ld",path,(long)getpid());
    snprintf(target,sizeof target,"%s/sentinel",root);
    CHECK(!strcmp(finder_label_name(1),"Essential"));uint32_t original=finder_label_color(1);
    rename_first(s,"Urgent");session_key(s,'\t',0);session_text_input(s,"112233");
    CHECK(!strcmp(finder_label_name(1),"Essential")&&finder_label_color(1)==original);
    session_key(s,'\r',0);CHECK(!s->dialog && !strcmp(finder_label_name(1),"Urgent"));
    CHECK(finder_label_color(1)==0xff112233u && finder_label_ink(1)==C_WHITE);
    menu_update(s);CHECK(!strcmp(menu_at(MENU_LABELS,1)->label,"Urgent"));
    CHECK(!finder_label(root,"Documents","Notes",1));CHECK(finder_label(root,"Documents","Notes",-1)==1);
    rename_first(s,"Discarded");session_key(s,27,0);CHECK(!s->dialog&&!strcmp(finder_label_name(1),"Urgent"));
    rename_first(s,"Équipe");session_key(s,'\r',0);CHECK(!strcmp(finder_label_name(1),"Équipe"));
    rename_first(s,"Uncommitted");session_key(s,'\t',0);session_text_input(s,"12345");session_key(s,'\r',0);
    CHECK(s->dialog==5&&!strcmp(finder_label_name(1),"Équipe"));session_key(s,27,0);
    rename_first(s,"Invalid\nName");session_key(s,'\r',0);CHECK(!strcmp(finder_label_name(1),"Équipe"));
    session_command(s,CMD_PREFERENCES);CHECK(s->dialog==4);int flags=s->settings_flags;
    session_key(s,' ',0);session_key(s,MENU_KEY_DOWN,0);session_key(s,MENU_KEY_DOWN,0);session_key(s,' ',0);
    CHECK(s->dialog==5);session_text_input(s,"Parent edit");session_key(s,27,0);
    CHECK(s->dialog==4 && s->settings_flags==(flags^1) && !s->sound_toggle_request);
    CHECK(!strcmp(finder_label_name(1),"Équipe"));session_key(s,27,0);CHECK(!s->sound_toggle_request);
    rename_first(s,"Saved");session_key(s,'\t',0);session_text_input(s,"FFFFFF");session_key(s,'\r',0);
    CHECK(finder_label_ink(1)==C_BLACK);session_destroy(s);s=session_create(root);CHECK(s);
    CHECK(!strcmp(finder_label_name(1),"Saved")&&finder_label_color(1)==0xffffffffu);
    session_command(s,CMD_EDIT_LABELS);Rect box,cancel,ok;dialog_layout(s,&box,&cancel,&ok);
    CHECK(box.w>=460 && box.y+58+6*30+24<ok.y);
    session_down(s,box.x+20,box.y+65,1);session_up(s,box.x+20,box.y+65);session_text_input(s,"000000");
    uint32_t *pixels=calloc((size_t)OSPI_W*OSPI_H,sizeof *pixels);CHECK(pixels);
    if(pixels){Fb fb;fb_init(&fb,pixels,OSPI_W,OSPI_H);finder_labels_paint(&fb,s,box);
        CHECK(pixels[(box.y+65)*OSPI_W+box.x+20]==C_BLACK);free(pixels);}
    CHECK(finder_label_color(1)==0xffffffffu);session_key(s,27,0);
    session_command(s,CMD_EDIT_LABELS);session_down(s,box.x+30,ok.y+10,1);session_up(s,box.x+30,ok.y+10);
    CHECK(!strcmp(finder_label_name(1),"Saved"));session_key(s,27,0);CHECK(!strcmp(finder_label_name(1),"Saved"));
    char saved[1024]={0};FILE *f=fopen(path,"r");CHECK(f);if(f){size_t n=fread(saved,1,sizeof saved-1,f);CHECK(n>0);fclose(f);}
    write_file(path,"OS999-LABELS-1\n112233\tOnly one\n");CHECK(finder_labels_load(s)==-1);
    CHECK(!strcmp(finder_label_name(1),"Essential"));write_file(path,saved);CHECK(finder_labels_load(s)==1);
    CHECK(!unlink(path));CHECK(!mkfifo(path,0600));CHECK(finder_labels_load(s)==-1);CHECK(!unlink(path));
    write_file(target,"untouched\n");CHECK(!symlink(target,path));CHECK(finder_labels_load(s)==-1);
    rename_first(s,"Replacement");session_key(s,'\r',0);struct stat st;CHECK(!lstat(path,&st)&&S_ISREG(st.st_mode));
    f=fopen(target,"r");char sentinel[32]={0};CHECK(f);if(f){CHECK(fgets(sentinel,sizeof sentinel,f));fclose(f);}CHECK(!strcmp(sentinel,"untouched\n"));
    write_file(temp,"keep\n");rename_first(s,"Retry");session_key(s,'\r',0);
    CHECK(s->dialog==5&&!strcmp(finder_label_name(1),"Replacement"));CHECK(!unlink(temp));session_key(s,'\r',0);
    CHECK(!s->dialog&&!strcmp(finder_label_name(1),"Retry"));
    session_command(s,CMD_EDIT_LABELS);session_key(s,'\t',0);session_text_input(s,"909090");session_key(s,'\r',0);
    CHECK(finder_label_ink(1)==C_BLACK); /* Black 6.58:1; white only 3.19:1. */
    session_destroy(s);CHECK(!archive_remove_tree(root));
    printf("Finder labels: %d checks, %d failures\n",checks,failures);return !!failures;
}
