#include "finder_spring.h"
#include "finder_popup.h"
#include "app_switcher.h"
#include "finder_files.h"
#include "session_int.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static int folder(Session *s, const char *path) {
    for(int i=0;i<s->nwin;i++)if(s->win[i].kind==WIN_FOLDER && !strcmp(s->win[i].rel,path))return i;
    return -1;
}
static int item(Session *s, int wi, const char *name, int *x, int *y) {
    if(wi<0)return -1;
    for(int i=0;i<s->win[wi].nitems;i++)if(!strcmp(window_items(&s->win[wi])[i].name,name))
        return ospi_item_center(s,wi,i,x,y);
    return -1;
}
static void start(Session *s) {
    int source=folder(s,"");CHECK(source>=0);bring_front(s,source);source=s->nwin-1;
    int x=0,y=0;CHECK(!item(s,source,"Read Me",&x,&y));
    session_down(s,x,y,1);session_move(s,x+10,y+10);CHECK(s->drag==3 && s->down);
}
static void dwell(Session *s, int x, int y, uint32_t t) {
    session_move(s,x,y);session_tick(s,t);session_tick(s,t+599);
    session_tick(s,t+600);
}
int main(void) {
    char root[]="/tmp/os999-spring-XXXXXX",path[OSPI_PATH+40];
    CHECK(mkdtemp(root));CHECK(!volume_seed(root));
    snprintf(path,sizeof path,"%s/Documents/Nested",root);CHECK(!volume_mkdir_p(path));
    Session *s=session_create(root);CHECK(s);if(!s)return 1;
    int x=0,y=0;start(s);CHECK(!item(s,folder(s,""),"Documents",&x,&y));
    session_move(s,x,y);session_tick(s,100);session_tick(s,699);CHECK(folder(s,"Documents")<0);
    session_tick(s,700);CHECK(folder(s,"Documents")>=0 && s->press_win==folder(s,""));
    CHECK(!item(s,folder(s,"Documents"),"Nested",&x,&y));dwell(s,x,y,800);
    int nested=folder(s,"Documents/Nested");CHECK(nested>=0);
    if(nested>=0){Layout lay;window_layout(&s->win[nested],&lay);
        session_up(s,lay.content.x+lay.content.w-10,lay.content.y+lay.content.h-10);}
    snprintf(path,sizeof path,"%s/Documents/Nested/Read Me",root);CHECK(!access(path,F_OK));
    CHECK(folder(s,"Documents")<0 && folder(s,"Documents/Nested")>=0 && folder(s,"")>=0);
    CHECK(!s->spring.active && !s->down && !s->drag);
    session_command(s,CMD_UNDO);snprintf(path,sizeof path,"%s/Read Me",root);CHECK(!access(path,F_OK));
    int ni=folder(s,"Documents/Nested");if(ni>=0)close_index(s,ni);
    start(s);CHECK(!item(s,folder(s,""),"Documents",&x,&y));
    dwell(s,x,y,UINT32_MAX-200);CHECK(folder(s,"Documents")>=0);
    session_key(s,27,0);CHECK(!s->down && !s->drag && !s->spring.active && folder(s,"Documents")<0);
    CHECK(!access(path,F_OK));
    CHECK(!open_folder(s,"Documents"));finder_popup_set(s,&s->win[s->nwin-1],1);
    start(s);int doc=folder(s,"Documents");CHECK(doc>=0 && s->win[doc].collapsed);
    if(doc>=0){x=s->win[doc].x+30;y=s->win[doc].y+10;dwell(s,x,y,2000);}
    doc=folder(s,"Documents");CHECK(doc>=0 && !s->win[doc].collapsed && s->press_win==folder(s,""));
    session_key(s,27,0);doc=folder(s,"Documents");CHECK(doc>=0 && s->win[doc].collapsed);
    start(s);CHECK(!item(s,folder(s,""),"Applications",&x,&y));dwell(s,x,y,3000);
    CHECK(folder(s,"Applications")>=0);s->down=s->drag=0;session_tick(s,3700);
    CHECK(folder(s,"Applications")<0 && !s->spring.active); /* Host focus loss. */
    start(s);CHECK(!item(s,folder(s,""),"Applications",&x,&y));
    s->switcher.open=1;s->switcher.x=x-20;s->switcher.y=y-25;s->switcher.width=190;
    dwell(s,x,y,4000);CHECK(folder(s,"Applications")<0);s->switcher.open=0;
    session_key(s,27,0);
    start(s);CHECK(!item(s,folder(s,""),"Applications",&x,&y));
    session_move(s,x,y);session_tick(s,5000);session_move(s,10,400);session_tick(s,5400);
    session_move(s,x,y);session_tick(s,5500);session_tick(s,6000);CHECK(folder(s,"Applications")<0);
    session_tick(s,6100);CHECK(folder(s,"Applications")>=0);
    CHECK(!open_folder(s,"System Folder")); /* Not opened by the spring controller. */
    session_key(s,27,0);CHECK(folder(s,"Applications")<0 && folder(s,"System Folder")>=0);
    start(s);CHECK(!item(s,folder(s,""),"Applications",&x,&y));dwell(s,x,y,7000);
    CHECK(folder(s,"Applications")>=0);s->dialog=1;session_tick(s,7700);
    CHECK(folder(s,"Applications")<0 && !s->spring.active && !s->down && !s->drag);s->dialog=0;
    start(s);CHECK(!item(s,folder(s,""),"Applications",&x,&y));dwell(s,x,y,8000);
    CHECK(folder(s,"Applications")>=0);session_destroy(s);s=session_create(root);CHECK(s);
    CHECK(folder(s,"Applications")<0); /* Quit does not persist temporary windows. */
    char alias[OSPI_NAME];CHECK(!finder_alias(root,"","Applications",0,alias));reload_folders(s);
    start(s);CHECK(!item(s,folder(s,""),alias,&x,&y));dwell(s,x,y,9000);
    CHECK(folder(s,"Applications")>=0);session_key(s,27,0);CHECK(folder(s,"Applications")<0);
    session_destroy(s);CHECK(!archive_remove_tree(root));
    printf("Spring folders: %d checks, %d failures\n",checks,failures);return !!failures;
}
