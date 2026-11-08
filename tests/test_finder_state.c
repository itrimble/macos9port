#include "finder_state.h"
#include "finder_files.h"
#include "finder_search.h"
#include "session_int.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static void state(const char *root, const char *text) {
    char path[1024]; snprintf(path,sizeof path,"%s/.os999-finder-state",root);
    FILE *f=fopen(path,"w"); CHECK(f); if(f){fputs(text,f);fclose(f);}
}
int main(void) {
    char root[]="/tmp/os999-finder-state-XXXXXX"; CHECK(mkdtemp(root)); CHECK(!volume_seed(root));
    Session *s=session_create(root); CHECK(s); if(!s)return 1;
    CHECK(!finder_label(root,"Documents","Notes",3));
    Item results[8];int limited=0;
    int count=finder_search(root,"Notes",results,8,&limited);
    CHECK(count==2 && !limited);
    int document=-1;
    for(int i=0;i<count;i++)if(results[i].label==3)document=i;
    CHECK(document>=0);
    if(document<0)return 1;
    char note[OSPI_PATH+32];snprintf(note,sizeof note,"%s/Documents/Notes",root);
    struct stat metadata;CHECK(!stat(note,&metadata));
#ifdef __APPLE__
    CHECK(results[document].created==(int64_t)metadata.st_birthtime);
#else
    CHECK(results[document].created==0);
#endif
    CHECK(s->nwin==1); CHECK(!open_folder(s,"Documents"));
    CHECK(window_items(&s->win[s->nwin-1])[0].created==results[document].created);
    Window *w=&s->win[s->nwin-1]; w->view_mode=VIEW_LIST; w->sort_mode=SORT_DATE;
    w->sort_reverse=1;w->list_columns[0]=350;w->list_columns[1]=550;w->list_columns[2]=720;
    w->x=230;w->y=180;w->w=600;w->h=410;
    close_index(s,s->nwin-1); CHECK(!open_folder(s,"Documents"));w=&s->win[s->nwin-1];
    CHECK(w->view_mode==VIEW_LIST && w->sort_mode==SORT_DATE && w->x==230 && w->h==410);
    CHECK(!open_folder(s,"Applications"));bring_front(s,1);
    session_destroy(s);s=session_create(root);CHECK(s && s->nwin==3);
    CHECK(!strcmp(s->win[s->nwin-1].rel,"Documents"));
    w=&s->win[s->nwin-1];CHECK(w->view_mode==VIEW_LIST && w->x==230 && w->h==410);
    CHECK(w->sort_reverse==1 && w->list_columns[0]==350 && w->list_columns[1]==550 && w->list_columns[2]==720);
    Layout columns;window_layout(w,&columns);
    int divider=columns.content.x+columns.content.w*350/1000, header_y=columns.content.y+12;
    session_down(s,divider,header_y,1);CHECK(s->press_zone==Z_COLUMN);
    session_move(s,columns.content.x+columns.content.w*430/1000,header_y);
    session_up(s,columns.content.x+columns.content.w*430/1000,header_y);
    CHECK(w->list_columns[0]>=425 && w->list_columns[0]<=430 && !s->drag && !s->down);
    session_command(s,CMD_RESET_COLUMNS);CHECK(!w->list_columns[0] && w->sort_mode==SORT_DATE && w->sort_reverse);
    session_command(s,CMD_AS_POPUP); w=&s->win[s->nwin-1];
    CHECK(w->popup && w->collapsed && w->y==s->sh-TITLE_H && w->w<260);
    int x=w->x+30,y=w->y+10;
    session_down(s,x,y,1);session_up(s,x,y);
    CHECK(w->popup && !w->collapsed && w->y+w->h==s->sh && w->h==410);
    session_down(s,10,400,1);session_up(s,10,400);
    CHECK(w->collapsed && s->desktop_focus);
    session_destroy(s);s=session_create(root);w=&s->win[s->nwin-1];
    CHECK(w->popup && w->collapsed && w->popup_h==410);
    session_command(s,CMD_AS_WINDOW);w=&s->win[s->nwin-1];
    CHECK(!w->popup && !w->collapsed && w->x==230 && w->y==180 && w->w==600 && w->h==410);
    w->x=s->sw-10;w->y=s->sh-10;session_clamp(s);
    CHECK(w->w==600 && w->h==410 && w->x+w->w<=s->sw && w->y+w->h<=s->sh);
    finder_state_remember(s,w,1);
    CHECK(!finder_rename(root,"","Documents","Renamed"));
    finder_state_retarget(s,"","Documents","","Renamed");
    CHECK(!strcmp(w->rel,"Renamed") && !strcmp(w->title,"Renamed"));
    close_index(s,s->nwin-1);CHECK(!open_folder(s,"Renamed"));
    w=&s->win[s->nwin-1];CHECK(w->view_mode==VIEW_LIST && w->sort_mode==SORT_DATE && w->w==600);
    CHECK(!finder_rename(root,"","Renamed","Documents"));
    finder_state_retarget(s,"","Renamed","","Documents");
    session_command(s,CMD_CLOSE_ALL);session_destroy(s);s=session_create(root);CHECK(s->nwin==0);
    CHECK(!open_folder(s,"Documents"));CHECK(s->win[0].sort_mode==SORT_DATE);
    session_command(s,CMD_SORT_CREATED);session_destroy(s);s=session_create(root);
    CHECK(s->nwin==1 && s->win[0].sort_mode==SORT_CREATED);
    session_command(s,CMD_SORT_LABEL);session_destroy(s);s=session_create(root);
    CHECK(s->nwin==1 && s->win[0].sort_mode==SORT_LABEL);session_destroy(s);
    state(root,"OS999-FINDER-2\n0 40 400 200 1 0 0 1 0\t../outside\n0 40 400 200 1 0 0 1 0\tMissing\n9999999999999999999999 40 400 200 1 0 0 1 0\tDocuments\n0 40 400 200 1 0 0 1 0\tApplications\n");
    s=session_create(root);CHECK(s->nwin==1 && !strcmp(s->win[0].rel,"Applications"));session_destroy(s);
    state(root,"OS999-FINDER-3\n0 40 400 200 1 0 0 1 0 0 400 -2147483648 800\tDocuments\n0 40 400 200 1 0 0 1 0 1 400 600 750\tApplications\n");
    s=session_create(root);CHECK(s->nwin==1 && !strcmp(s->win[0].rel,"Applications") && s->win[0].sort_reverse);session_destroy(s);
    state(root,"invalid header\n");s=session_create(root);CHECK(s->nwin==1 && !s->win[0].rel[0]);session_destroy(s);
    char path[1024];snprintf(path,sizeof path,"%s/Link",root);CHECK(!symlink("/tmp",path));
    CHECK(!finder_directory_exists(root,"Link"));CHECK(!finder_directory_exists(root,"../"));
    s=session_create(root);CHECK(open_folder(s,"Link")<0);CHECK(open_folder(s,"Missing")<0);session_destroy(s);
    CHECK(!archive_remove_tree(root));printf("Finder state: %d checks, %d failures\n",checks,failures);return !!failures;
}
