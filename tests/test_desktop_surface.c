#include "finder_desktop_surface.h"
#include "folder_navigation.h"
#include "finder_desktop_positions.h"
#include "finder_icon_position.h"
#include "session_int.h"
#include "desktop_finder.h"
#include "desktop_commands.h"
#include "desktop_context.h"
#include "finder_drag.h"
#include "finder_files.h"
#include "finder_print.h"
#include "archive_fs.h"
#include "menu_navigation.h"
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
static void select_desktop(Session *s,const char *name) {
    int i=item(&s->desktop,name),x=0,y=0;CHECK(i>=0);
    CHECK(!ospi_item_center(s,OSPI_DESKTOP_WINDOW,i,&x,&y));
    s->input_now_ms+=700;session_down(s,x,y,1);session_up(s,x,y);
    CHECK(s->desktop_focus && s->desk_sel==-1 && window_items(&s->desktop)[i].selected);
}
int main(void) {
    char root[]="/tmp/os999-desktop-surface-XXXXXX",path[OSPI_PATH];
    CHECK(mkdtemp(root));CHECK(!volume_seed(root));Session *s=session_create(root);CHECK(s);if(!s)return 1;
    CHECK(s->desktop.kind==WIN_FOLDER && s->desktop.desktop_surface && !strcmp(s->desktop.rel,"Desktop Folder"));
    CHECK(item(&s->win[0],"Desktop Folder")<0);session_command(s,CMD_CLOSE_ALL);CHECK(!s->nwin);
    CHECK(finder_current(s)==&s->desktop);
    session_down(s,400,400,1);session_up(s,400,400);
    session_command(s,CMD_NEW_FOLDER);CHECK(!s->nwin && s->desktop.nitems==1);
    CHECK(item(&s->desktop,"untitled folder")>=0);
    snprintf(path,sizeof path,"%s/Desktop Folder/Note",root);FILE *f=fopen(path,"w");CHECK(f);if(f){fputs("Desktop text",f);fclose(f);}
    session_tick(s,2000);CHECK(item(&s->desktop,"Note")>=0);
    select_desktop(s,"Note");CHECK(finder_command_enabled(s,CMD_RENAME));CHECK(desktop_command_enabled(s,CMD_COPY));
    session_command(s,CMD_COPY);CHECK(!strcmp(session_clipboard_output(s),"Note"));
    session_command(s,CMD_RENAME);CHECK(s->dialog==3);session_text_input(s,"Renamed");session_key(s,'\r',0);
    CHECK(!s->nwin && item(&s->desktop,"Renamed")>=0);session_command(s,CMD_UNDO);CHECK(item(&s->desktop,"Note")>=0);
    select_desktop(s,"Note");session_command(s,CMD_DUPLICATE);CHECK(s->desktop.nitems==3);session_command(s,CMD_UNDO);CHECK(s->desktop.nitems==2);
    select_desktop(s,"Note");session_command(s,CMD_LABEL_BASE+2);CHECK(window_items(&s->desktop)[item(&s->desktop,"Note")].label==2);
    s->print_available=1;CHECK(finder_print_enabled(s,CMD_PRINT));CHECK(!finder_print_enabled(s,CMD_PRINT_WINDOW));
    session_command(s,CMD_PRINT);CHECK(s->print_request==PRINT_TEXT&&!strcmp(s->print_text,"Desktop text"));s->print_request=0;
    session_command(s,CMD_MOVE_TRASH);CHECK(item(&s->desktop,"Note")<0);session_command(s,CMD_UNDO);CHECK(item(&s->desktop,"Note")>=0);
    select_desktop(s,"Note");session_command(s,CMD_OPEN);if(s->nwin!=1||s->win[0].kind!=WIN_TEXT)fprintf(stderr,"desktop open: windows=%d dialog=%d body=%s selected=%d cmd=%d\n",s->nwin,s->dialog,s->dialog_body,s->desk_sel,CMD_OPEN);CHECK(s->nwin==1&&s->win[0].kind==WIN_TEXT);
    session_command(s,CMD_CLOSE_ALL);CHECK(!s->nwin);
    CHECK(!open_folder(s,"Documents"));int before=s->desktop.nitems;
    session_command(s,CMD_NEW_FOLDER);select_only(&s->win[0],item(&s->win[0],"untitled folder"));
    finder_drag_drop(s,0,600,500);CHECK(s->desktop.nitems==before); /* Existing desktop folder is not replaced. */
    CHECK(s->dialog);s->dialog=0;
    select_only(&s->win[0],-1);CHECK(!open_folder(s,""));select_only(&s->win[s->nwin-1],item(&s->win[s->nwin-1],"Read Me"));
    finder_drag_drop(s,s->nwin-1,600,500);CHECK(item(&s->desktop,"Read Me")>=0);
    session_command(s,CMD_CLOSE_ALL);select_desktop(s,"Read Me");
    int tx,ty;CHECK(!ospi_desktop_center(s,1,&tx,&ty));finder_drag_drop(s,OSPI_DESKTOP_WINDOW,tx,ty);
    CHECK(item(&s->desktop,"Read Me")<0);session_command(s,CMD_UNDO);CHECK(item(&s->desktop,"Read Me")>=0);
    select_desktop(s,"Note");int x,y;CHECK(!ospi_item_center(s,OSPI_DESKTOP_WINDOW,item(&s->desktop,"Note"),&x,&y));
    CHECK(context_open(s,x,y));session_key(s,CONTEXT_KEY_DOWN,0);session_key(s,'\r',0);CHECK(!context_active()&&s->nwin==1&&s->win[0].kind==WIN_TEXT);
    session_command(s,CMD_CLOSE_ALL);s->desktop_focus=1;s->desk_sel=-1;
    session_key(s,'n',0);CHECK(window_items(&s->desktop)[item(&s->desktop,"Note")].selected);
    uint32_t *pixels=calloc((size_t)OSPI_W*OSPI_H,sizeof *pixels);CHECK(pixels);if(pixels){session_paint(s,pixels,OSPI_W,OSPI_H);free(pixels);}
    session_destroy(s);s=session_create(root);CHECK(s&&item(&s->desktop,"Note")>=0);
    session_command(s,CMD_CLOSE_ALL);select_desktop(s,"Note");
    int index=item(&s->desktop,"Note");CHECK(!ospi_item_center(s,OSPI_DESKTOP_WINDOW,index,&x,&y));
    int initial_x=x,initial_y=y;
    session_down(s,x,y,1);session_move(s,x+20,y+20);CHECK(s->drag==3);
    session_up(s,x+200,y+180);CHECK(!s->down&&!s->drag&&!s->dialog);
    CHECK(!ospi_item_center(s,OSPI_DESKTOP_WINDOW,index,&x,&y));
    CHECK(x==initial_x+200&&y==initial_y+180);
    CHECK(item_index_at(&s->desktop,x,y)==index);
    int saved_x=0,saved_y=0;
    CHECK(finder_icon_position_read(root,"Desktop Folder","Note",&saved_x,&saved_y)==1);
    session_command(s,CMD_RENAME);session_text_input(s,"Placed Note");session_key(s,'\r',0);
    index=item(&s->desktop,"Placed Note");CHECK(index>=0);
    CHECK(window_items(&s->desktop)[index].desktop_x==saved_x&&window_items(&s->desktop)[index].desktop_y==saved_y);
    CHECK(!finder_desktop_position_item(s,"Placed Note",800,650));
    Item *placed=&window_items(&s->desktop)[index];saved_x=placed->desktop_x;saved_y=placed->desktop_y;
    s->sw=400;s->sh=300;session_clamp(s);Rect small=desktop_item_cell(&s->desktop,index);
    CHECK(small.x>=s->desktop.x&&small.x+small.w<=s->desktop.x+s->desktop.w);
    CHECK(small.y>=s->desktop.y&&small.y+small.h<=s->desktop.y+s->desktop.h);
    CHECK(placed->desktop_x==saved_x&&placed->desktop_y==saved_y);
    s->sw=OSPI_W;s->sh=OSPI_H;session_clamp(s);Rect large=desktop_item_cell(&s->desktop,index);
    CHECK(large.x==s->desktop.x+saved_x&&large.y==s->desktop.y+saved_y);
    session_destroy(s);s=session_create(root);CHECK(s);session_command(s,CMD_CLOSE_ALL);
    index=item(&s->desktop,"Placed Note");placed=&window_items(&s->desktop)[index];
    CHECK(placed->desktop_x==saved_x&&placed->desktop_y==saved_y);
    s->desktop_focus=1;s->desk_sel=-1;select_only(&s->desktop,index);
    int second=item(&s->desktop,"Read Me");window_items(&s->desktop)[second].selected=1;
    Rect a=desktop_item_cell(&s->desktop,index),b=desktop_item_cell(&s->desktop,second);
    CHECK(!ospi_item_center(s,OSPI_DESKTOP_WINDOW,index,&x,&y));
    session_down(s,x,y,1);session_move(s,x-10,y-10);session_up(s,x-40,y-40);
    Rect moved_a=desktop_item_cell(&s->desktop,index),moved_b=desktop_item_cell(&s->desktop,second);
    CHECK(moved_a.x==a.x-40&&moved_a.y==a.y-40);
    CHECK(moved_a.x-moved_b.x==a.x-b.x&&moved_a.y-moved_b.y==a.y-b.y);
    session_command(s,CMD_CLEANUP);CHECK(!s->dialog);
    for(int i=0;i<s->desktop.nitems;i++){
        Item *entry=&window_items(&s->desktop)[i];
        CHECK(entry->desktop_position==1&&entry->desktop_x==i*CELL_W&&entry->desktop_y==0);
    }
    session_command(s,CMD_SORT_KIND);CHECK(window_items(&s->desktop)[0].kind==ITEM_FOLDER);
    CHECK(window_items(&s->desktop)[0].desktop_x==0);
    session_command(s,CMD_VIEW_OPTIONS);CHECK(s->dialog==4);
    for(int i=0;i<7;i++)session_key(s,MENU_KEY_DOWN,0);
    CHECK(s->settings_row==0);
    for(int i=0;i<6;i++)session_key(s,MENU_KEY_DOWN,0);
    session_key(s,' ',0);session_key(s,'\r',0);
    CHECK(!s->dialog&&s->desktop.view_mode==VIEW_ICONS&&s->desktop.sort_mode==SORT_LABEL);
    for(int app=0;app<3;app++){
        s->browser_focused=app==0;s->guest_focused=app==1;s->media_focused=app==2;
        session_command(s,CMD_DESKTOP_ITEMS);CHECK(s->nwin==0);
    }
    s->browser_focused=s->guest_focused=s->media_focused=0;
    /* Excess desktop items remain reachable by the footer and keyboard. */
    for(int i=0;i<72;i++){
        snprintf(path,sizeof path,"%s/Desktop Folder/Overflow %03d",root,i);
        FILE *extra=fopen(path,"w");CHECK(extra);if(extra)fclose(extra);
    }
    session_tick(s,20000);CHECK(s->desktop.nitems==75&&finder_desktop_overflow(s)>0);
    CHECK(!s->nwin);session_down(s,40,s->sh-18,1);session_up(s,40,s->sh-18);
    CHECK(s->nwin==1&&!strcmp(s->win[0].rel,"Desktop Folder")&&s->win[0].nitems==75);
    session_command(s,CMD_CLOSE_ALL);s->desktop_focus=1;s->desk_sel=-1;
    session_key(s,FOLDER_KEY_END,0);
    CHECK(s->nwin==1&&!strcmp(s->win[0].rel,"Desktop Folder"));
    int picked=-1;for(int i=0;i<s->win[0].nitems;i++)if(window_items(&s->win[0])[i].selected)picked=i;
    CHECK(picked>=0);CHECK(!ospi_item_center(s,0,picked,&x,&y));Layout shown;window_layout(&s->win[0],&shown);
    CHECK(rect_has(shown.content,x,y));
    session_command(s,CMD_CLOSE_ALL);session_command(s,CMD_DESKTOP_ITEMS);
    CHECK(s->nwin==1&&!strcmp(s->win[0].rel,"Desktop Folder"));
    session_destroy(s);
    CHECK(!archive_remove_tree(root));printf("Desktop surface: %d checks, %d failures\n",checks,failures);return !!failures;
}
