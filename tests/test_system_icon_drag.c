#include "session_int.h"
#include "finder_drag.h"
#include "folder_navigation.h"
#include "desktop_gesture.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static int item(const Window *w,const char *name) {
    for(int i=0;i<w->nitems;i++)if(!strcmp(window_items(w)[i].name,name))return i;
    return -1;
}
static void drag(Session *s,int which,int dx,int dy) {
    int x,y;CHECK(!ospi_desktop_center(s,which,&x,&y));s->input_now_ms+=700;
    session_down(s,x,y,1);session_move(s,x+dx/2,y+dy/2);CHECK(s->drag==4);
    Rect regions[DESKTOP_GESTURE_REGIONS];CHECK(session_gesture_regions(s,regions)>0);
    session_up(s,x+dx,y+dy);CHECK(!s->down&&!s->drag);
}
int main(void) {
    char root[]="/tmp/os999-system-drag-XXXXXX",path[OSPI_PATH+96];CHECK(mkdtemp(root));CHECK(!volume_seed(root));
    Session *s=session_create(root);CHECK(s);if(!s)return 1;session_command(s,CMD_CLOSE_ALL);
    Rect before,after,hit;session_desktop_icon_rect(s,0,&before,&hit);
    drag(s,0,-300,200);CHECK(!s->dialog);session_desktop_icon_rect(s,0,&after,&hit);
    CHECK(after.x==before.x-300&&after.y==before.y+200);
    CHECK(session_desktop_which(s,after.x+16,after.y+16)==0);
    CHECK(session_desktop_which(s,before.x+16,before.y+16)!=0);
    session_desktop_icon_rect(s,1,&before,&hit);drag(s,1,-550,-150);CHECK(!s->dialog);
    session_desktop_icon_rect(s,1,&after,&hit);CHECK(after.x==before.x-550&&after.y==before.y-150);
    int trash_x=after.x+16,trash_y=after.y+16;
    CHECK(session_desktop_which(s,trash_x,trash_y)==1);
    CHECK(!open_folder(s,""));select_only(&s->win[0],item(&s->win[0],"Read Me"));
    finder_drag_drop(s,0,trash_x,trash_y);
    snprintf(path,sizeof path,"%s/Trash/Read Me",root);CHECK(!access(path,F_OK));session_command(s,CMD_UNDO);CHECK(access(path,F_OK)<0);
    session_command(s,CMD_CLOSE_ALL);
    int x,y;CHECK(!ospi_desktop_center(s,0,&x,&y));session_down(s,x,y,2);session_up(s,x,y);
    CHECK(s->nwin==1&&!s->win[0].rel[0]);session_command(s,CMD_CLOSE_ALL);
    SystemIconPositions saved=s->system_icons;session_destroy(s);s=session_create(root);CHECK(s);
    CHECK(s->system_icons.mask==3&&s->system_icons.x[0]==saved.x[0]&&s->system_icons.y[1]==saved.y[1]);
    session_command(s,CMD_CLOSE_ALL);s->sw=400;s->sh=300;session_clamp(s);
    session_desktop_icon_rect(s,0,&after,&hit);CHECK(hit.x>=0&&hit.y>=TITLE_H&&hit.x+hit.w<=s->sw&&hit.y+hit.h<=s->sh);
    CHECK(s->system_icons.x[0]==saved.x[0]&&s->system_icons.y[0]==saved.y[0]);
    s->sw=OSPI_W;s->sh=OSPI_H;session_clamp(s);session_desktop_icon_rect(s,0,&after,&hit);
    CHECK(after.x==saved.x[0]&&after.y==saved.y[0]);
    snprintf(path,sizeof path,"%s/.os999-desktop-icons.tmp.%ld",root,(long)getpid());FILE *f=fopen(path,"w");CHECK(f);if(f){fputs("keep",f);fclose(f);}
    drag(s,0,30,30);CHECK(s->dialog&&s->system_icons.x[0]==saved.x[0]);CHECK(!unlink(path));s->dialog=0;
    drag(s,0,30,30);CHECK(!s->dialog&&s->system_icons.x[0]==saved.x[0]+30);
    saved=s->system_icons;CHECK(!ospi_desktop_center(s,0,&x,&y));
    s->input_now_ms+=700;session_down(s,x,y,1);session_move(s,trash_x,trash_y);session_up(s,trash_x,trash_y);
    CHECK(s->dialog&&!strcmp(s->dialog_title,"Eject")&&s->system_icons.x[0]==saved.x[0]);s->dialog=0;
    s->desktop_focus=1;s->desk_sel=-1;session_key(s,'t',0);CHECK(s->desk_sel==1);
    session_key(s,'\r',0);CHECK(s->nwin==1&&!strcmp(s->win[0].rel,"Trash"));session_command(s,CMD_CLOSE_ALL);
    s->desktop_focus=1;s->desk_sel=-1;session_key(s,'o',0);CHECK(s->desk_sel==0);
    CHECK(!ospi_desktop_center(s,0,&x,&y));s->input_now_ms+=700;
    session_down(s,x,y,1);session_move(s,x-20,y+20);session_key(s,27,0);
    CHECK(!s->down&&!s->drag&&s->system_icons.x[0]==saved.x[0]);
    session_destroy(s);CHECK(!archive_remove_tree(root));
    printf("System icon dragging: %d checks, %d failures\n",checks,failures);return !!failures;
}
