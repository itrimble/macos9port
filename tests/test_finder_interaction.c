#include "ospi.h"
#include "session_int.h"
#include "finder_drag.h"
#include "finder_batch.h"
#include "finder_files.h"
#include "finder_recent.h"
#include "archive_fs.h"
#include "menu_navigation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static int item(Window *w,const char *name) {
    for(int i=0;i<w->nitems;i++)if(!strcmp(w->items[i].name,name))return i;
    return -1;
}
static void click(Session *s,int index,int mods) {
    int x,y; CHECK(!ospi_item_center(s,s->nwin-1,index,&x,&y));
    s->mouse_mods=mods; s->input_now_ms+=700;session_down(s,x,y,1);session_up(s,x,y);
}
static void alias_drops(void) {
    char root[]="/tmp/os999-alias-drop-XXXXXX",path[OSPI_PATH],alias[OSPI_NAME];
    CHECK(mkdtemp(root));CHECK(!volume_seed(root));
    snprintf(path,sizeof path,"%s/Documents/Drop file",root);
    FILE *f=fopen(path,"w");CHECK(f);if(f){fputs("drop fixture",f);fclose(f);}
    CHECK(!finder_alias(root,"","Applications",0,alias));
    Session *s=session_create(root);CHECK(s);if(!s)return;
    CHECK(!open_folder(s,"Documents"));
    select_only(&s->win[s->nwin-1],item(&s->win[s->nwin-1],"Drop file"));
    CHECK(!open_folder(s,""));int source=-1;
    for(int i=0;i<s->nwin;i++)if(!strcmp(s->win[i].rel,"Documents"))source=i;
    CHECK(source>=0);int x=0,y=0;
    CHECK(!ospi_item_center(s,s->nwin-1,item(&s->win[s->nwin-1],alias),&x,&y));
    finder_drag_drop(s,source,x,y);
    CHECK(access(path,F_OK)<0);
    char destination[OSPI_PATH];snprintf(destination,sizeof destination,"%s/Applications/Drop file",root);
    CHECK(!access(destination,F_OK));CHECK(s->undo_count==1);
    session_command(s,CMD_UNDO);CHECK(!access(path,F_OK)&&access(destination,F_OK)<0);
    select_only(&s->win[source],item(&s->win[source],"Drop file"));
    s->mouse_mods=MOD_OPTION;finder_drag_drop(s,source,x,y);
    CHECK(!access(path,F_OK)&&!access(destination,F_OK));
    session_command(s,CMD_UNDO);CHECK(!access(path,F_OK)&&access(destination,F_OK)<0);
    /* A file alias and a broken alias must not fall back to the containing folder. */
    char file_alias[OSPI_NAME];CHECK(!finder_alias(root,"","Read Me",0,file_alias));reload_folders(s);
    CHECK(!ospi_item_center(s,s->nwin-1,item(&s->win[s->nwin-1],file_alias),&x,&y));
    select_only(&s->win[source],item(&s->win[source],"Drop file"));s->mouse_mods=0;
    finder_drag_drop(s,source,x,y);CHECK(s->dialog&&!access(path,F_OK)&&!s->undo_count);s->dialog=0;
    snprintf(destination,sizeof destination,"%s/Read Me",root);CHECK(!unlink(destination));
    reload_folders(s);CHECK(!ospi_item_center(s,s->nwin-1,item(&s->win[s->nwin-1],file_alias),&x,&y));
    finder_drag_drop(s,source,x,y);CHECK(s->dialog&&!access(path,F_OK)&&!s->undo_count);s->dialog=0;
    snprintf(destination,sizeof destination,"%s/Drop file",root);CHECK(access(destination,F_OK)<0);
    session_destroy(s);CHECK(!archive_remove_tree(root));
}
int main(void) {
    char root[]="/tmp/os999-interaction-XXXXXX",path[1024];CHECK(mkdtemp(root));CHECK(!volume_seed(root));
    for(int i=0;i<3;i++) {
        snprintf(path,sizeof path,"%s/Documents/%c",root,'A'+i);
        FILE *f=fopen(path,"w");CHECK(f);if(f){fputs("fixture",f);fclose(f);}
    }
    Session *s=session_create(root);CHECK(s);if(!s)return 1;
    CHECK(!open_folder(s,"Documents"));Window *w=&s->win[s->nwin-1];
    int a=item(w,"A"),b=item(w,"B"),c=item(w,"C");CHECK(a>=0 && b>=0 && c>=0);
    click(s,a,0);click(s,b,MOD_SHIFT);CHECK(w->items[a].selected && w->items[b].selected);
    click(s,b,MOD_SHIFT);CHECK(w->items[a].selected && !w->items[b].selected);
    session_command(s,CMD_VIEW_LIST);w=&s->win[s->nwin-1];a=item(w,"A");c=item(w,"C");
    click(s,a,0);click(s,c,MOD_SHIFT);CHECK(w->items[0].selected && w->items[1].selected && w->items[2].selected);
    session_key(s,MENU_KEY_UP,MOD_SHIFT);CHECK(w->items[0].selected && w->items[1].selected && !w->items[2].selected);
    session_key(s,MENU_KEY_DOWN,0);CHECK(!w->items[0].selected && !w->items[1].selected && w->items[2].selected);
    session_command(s,CMD_VIEW_ICONS);Layout lay;window_layout(w,&lay);
    s->mouse_mods=0;session_down(s,lay.content.x+4,lay.content.y+lay.content.h-2,1);
    session_move(s,lay.content.x+CELL_W*2-4,lay.content.y+2);CHECK(s->drag==5);
    session_up(s,lay.content.x+CELL_W*2-4,lay.content.y+2);CHECK(w->items[0].selected && w->items[1].selected);
    int source=s->nwin-1;select_only(w,item(w,"A"));
    CHECK(!open_folder(s,"Applications"));Window *target=&s->win[s->nwin-1];window_layout(target,&lay);
    /* Drop on folder background, not a newly seeded application or Utilities. */
    int drop_x=0,drop_y=0;
    for(int y=lay.content.y+2;y<lay.content.y+lay.content.h-2&&!drop_y;y++)
        for(int x=lay.content.x+2;x<lay.content.x+lay.content.w-2;x++)
            if(item_index_at(target,x,y)<0){drop_x=x;drop_y=y;break;}
    CHECK(drop_y!=0);
    s->mouse_mods=0;finder_drag_drop(s,source,drop_x,drop_y);
    snprintf(path,sizeof path,"%s/Applications/A",root);CHECK(!access(path,F_OK));
    snprintf(path,sizeof path,"%s/Documents/A",root);CHECK(access(path,F_OK)<0);
    session_command(s,CMD_UNDO);CHECK(!access(path,F_OK));
    w=&s->win[source];select_only(w,item(w,"B"));s->mouse_mods=MOD_OPTION;
    finder_drag_drop(s,source,drop_x,drop_y);
    snprintf(path,sizeof path,"%s/Applications/B",root);CHECK(!access(path,F_OK));
    snprintf(path,sizeof path,"%s/Documents/B",root);CHECK(!access(path,F_OK));
    session_command(s,CMD_UNDO);
    snprintf(path,sizeof path,"%s/Applications/B",root);CHECK(access(path,F_OK)<0);
    w=&s->win[source];select_only(w,item(w,"A"));w->items[item(w,"C")].selected=1;
    s->mouse_mods=0;finder_drag_drop(s,source,drop_x,drop_y);
    CHECK(s->undo_count==2);
    snprintf(path,sizeof path,"%s/Documents/C",root);
    FILE *conflict=fopen(path,"w");CHECK(conflict);if(conflict)fclose(conflict);
    session_command(s,CMD_UNDO);CHECK(s->undo_count==2 && s->dialog);
    CHECK(!unlink(path));s->dialog=0;session_command(s,CMD_UNDO);CHECK(s->undo_count==0);
    CHECK(!access(path,F_OK));
    snprintf(path,sizeof path,"%s/Documents/A",root);CHECK(!access(path,F_OK));
    CHECK(!open_folder(s,"Documents"));w=&s->win[s->nwin-1];
    select_only(w,item(w,"A"));w->items[item(w,"C")].selected=1;
    session_command(s,CMD_MOVE_TRASH);CHECK(s->undo_count==2);CHECK(access(path,F_OK)<0);
    session_command(s,CMD_UNDO);CHECK(s->undo_count==0);CHECK(!access(path,F_OK));
    /* A replacement at the journaled path must not be treated as our item. */
    char saved[1024], fork[1024], saved_fork[1024];
    snprintf(path,sizeof path,"%s/Documents/A",root);
    snprintf(saved,sizeof saved,"%s/Documents/A saved",root);
    finder_undo_begin(s);finder_undo_record(s,4,"Documents","Documents","A");
    CHECK(!rename(path,saved));
    FILE *replacement=fopen(path,"w");CHECK(replacement);if(replacement){fputs("replacement",replacement);fclose(replacement);}
    session_command(s,CMD_UNDO);CHECK(s->dialog && s->undo_count==1);
    CHECK(!access(path,F_OK) && !access(saved,F_OK));
    CHECK(!unlink(path));CHECK(!rename(saved,path));s->dialog=0;
    session_command(s,CMD_UNDO);CHECK(!s->undo_count && access(path,F_OK)<0);
    char parent[OSPI_PATH], restored[OSPI_NAME];
    CHECK(!finder_put_away(root,"A",parent,restored));reload_folders(s);

    /* Replacing a sidecar is also a different object, even if the data fork is intact. */
    snprintf(fork,sizeof fork,"%s/Documents/A.rsrc",root);
    snprintf(saved_fork,sizeof saved_fork,"%s/Documents/A saved.rsrc",root);
    FILE *resource=fopen(fork,"w");CHECK(resource);if(resource){fputs("resource",resource);fclose(resource);}
    finder_undo_begin(s);finder_undo_record(s,4,"Documents","Documents","A");
    CHECK(!rename(fork,saved_fork));
    resource=fopen(fork,"w");CHECK(resource);if(resource)fclose(resource);
    session_command(s,CMD_UNDO);CHECK(s->dialog && s->undo_count==1 && !access(path,F_OK));
    CHECK(!unlink(fork));CHECK(!rename(saved_fork,fork));s->dialog=0;
    session_command(s,CMD_UNDO);CHECK(!s->undo_count && access(path,F_OK)<0);
    CHECK(!finder_put_away(root,"A",parent,restored));reload_folders(s);

    /* Put Away has its own inverse, with a valid origin for the next Put Away. */
    w=&s->win[s->nwin-1];select_only(w,item(w,"A"));session_command(s,CMD_MOVE_TRASH);
    CHECK(!open_folder(s,"Trash"));w=&s->win[s->nwin-1];select_only(w,item(w,"A"));
    session_command(s,CMD_PUT_AWAY);CHECK(s->undo_count==1 && !access(path,F_OK));
    session_command(s,CMD_UNDO);CHECK(!s->undo_count && access(path,F_OK)<0);
    CHECK(!finder_put_away(root,"A",parent,restored));
    CHECK(!strcmp(parent,"Documents") && !strcmp(restored,"A"));
    CHECK(!access(path,F_OK) && !access(fork,F_OK));

    /* Renaming uses the same identity guard as the other commands. */
    CHECK(!open_folder(s,"Documents"));w=&s->win[s->nwin-1];select_only(w,item(w,"A"));
    session_command(s,CMD_RENAME);session_text_input(s,"Changed");session_key(s,'\r',0);
    snprintf(path,sizeof path,"%s/Documents/Changed",root);
    CHECK(s->undo_count==1 && !access(path,F_OK));CHECK(!rename(path,saved));
    CHECK(!symlink("A saved",path));
    session_command(s,CMD_UNDO);CHECK(s->dialog && s->undo_count==1);
    CHECK(!unlink(path));CHECK(!rename(saved,path));s->dialog=0;
    session_command(s,CMD_UNDO);CHECK(!s->undo_count);
    snprintf(path,sizeof path,"%s/Documents/A",root);CHECK(!access(path,F_OK));
    /* Recent entries follow both a leaf rename and a containing-folder move. */
    finder_recent_note(s,"Documents/B",0);
    w=&s->win[s->nwin-1];select_only(w,item(w,"B"));
    session_command(s,CMD_RENAME);session_text_input(s,"Recent B");session_key(s,'\r',0);
    CHECK(!strcmp(s->recent[0].path,"Documents/Recent B"));
    session_command(s,CMD_UNDO);CHECK(!strcmp(s->recent[0].path,"Documents/B"));
    CHECK(!open_folder(s,""));w=&s->win[s->nwin-1];select_only(w,item(w,"Documents"));
    session_command(s,CMD_RENAME);session_text_input(s,"Renamed Documents");session_key(s,'\r',0);
    CHECK(!strcmp(s->recent[0].path,"Renamed Documents/B"));
    session_destroy(s);s=session_create(root);CHECK(s);
    CHECK(!strcmp(s->recent[0].path,"Renamed Documents/B"));
    session_command(s,CMD_RECENT_BASE);
    CHECK(s->win[s->nwin-1].kind==WIN_TEXT && !strcmp(s->win[s->nwin-1].text,"fixture"));
    session_destroy(s);CHECK(!archive_remove_tree(root));
    alias_drops();
    printf("Finder interaction: %d checks, %d failures\n",checks,failures);return !!failures;
}
