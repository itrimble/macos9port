#include "ospi.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "folder_view.h"
#include "folder_navigation.h"
#include "menu_navigation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static const MenuItem *action(int menu, int command) {
    for(int i=0;i<menu_nitems(menu);i++) if(menu_at(menu,i)->cmd==command) return menu_at(menu,i);
    return NULL;
}
int main(void) {
    char root[]="/tmp/os999-menu-integration-XXXXXX";
    CHECK(mkdtemp(root)!=NULL); CHECK(volume_seed(root)==0);
    Session *s=session_create(root); if(!s)return 1;
    menu_update(s);
    CHECK(action(1,CMD_OPEN)->flags&MI_DISABLED);
    CHECK(action(2,CMD_COPY)->flags&MI_DISABLED);
    session_key(s,'a',MOD_CMD);
    Window *w=&s->win[s->nwin-1];
    for(int i=0;i<w->nitems;i++)CHECK(w->items[i].selected);
    session_key(s,'c',MOD_CMD);
    CHECK(s->clipboard_write_request && strstr(s->clipboard_out,"Applications"));
    CHECK(strchr(s->clipboard_out,'\n')!=NULL);
    session_command(s,CMD_CLEANUP);
    for(int i=0;i<w->nitems;i++)CHECK(w->items[i].selected);
    session_command(s,CMD_VIEW_LIST);session_command(s,CMD_SORT_KIND);
    CHECK(w->view_mode==VIEW_LIST && w->sort_mode==SORT_KIND);
    menu_update(s);CHECK(action(3,CMD_VIEW_LIST)->flags&MI_CHECKED);
    int x,y;CHECK(ospi_item_center(s,s->nwin-1,0,&x,&y)==0);
    CHECK(item_index_at(w,x,y)==0);
    session_down(s,x,y,1);session_up(s,x,y);
    session_key(s,'i',MOD_CMD);
    CHECK(s->dialog && !strcmp(s->dialog_title,"Get Info"));
    CHECK(strstr(s->dialog_body,root)==NULL);
    session_key(s,27,0);
    Layout layout; window_layout(w,&layout);
    char selected[OSPI_NAME]; strcpy(selected,w->items[0].name);
    session_down(s,layout.content.x+30,layout.content.y+5,1);
    session_up(s,layout.content.x+30,layout.content.y+5);
    CHECK(w->sort_mode==SORT_NAME && !s->down);
    int selected_count=0;
    for(int i=0;i<w->nitems;i++) if(w->items[i].selected) {
        selected_count++; CHECK(!strcmp(w->items[i].name,selected));
    }
    CHECK(selected_count==1);
    session_key(s,FOLDER_KEY_END,0); CHECK(w->items[w->nitems-1].selected);
    session_key(s,FOLDER_KEY_HOME,0); CHECK(w->items[0].selected);
    session_key(s,MENU_KEY_DOWN,MOD_OTHER); CHECK(w->items[0].selected);
    session_key(s,MENU_KEY_DOWN,0); CHECK(w->items[1].selected);
    session_key(s,'a',0); CHECK(!strcmp(w->items[0].name,"Applications") && w->items[0].selected);
    session_down(s,layout.content.x+layout.content.w-12,layout.content.y+5,1);
    session_up(s,layout.content.x+layout.content.w-12,layout.content.y+5);
    CHECK(w->sort_mode==SORT_KIND && !s->dialog);
    CHECK(ospi_item_center(s,s->nwin-1,0,&x,&y)==0);
    session_context(s,x,y); CHECK(context_active());
    session_key(s,'c',MOD_CMD); CHECK(!context_active());
    CHECK(!strcmp(s->clipboard_out,w->items[0].name));
    session_context(s,x,y); session_key(s,'i',MOD_CMD);
    CHECK(!context_active() && s->dialog);
    session_key(s,27,0);
    int count=w->nitems;
    w->nitems=20; w->scroll=0; window_layout(w,&layout);
    session_down(s,layout.vbar.x+5,layout.vbar.y+layout.vbar.h-5,1);
    session_up(s,layout.vbar.x+5,layout.vbar.y+layout.vbar.h-5);
    CHECK(w->scroll==FOLDER_LIST_ROW);
    w->nitems=count; w->scroll=0;
    int nwin=s->nwin;
    session_command(s,CMD_CONTROL_PANELS);
    menu_update(s);CHECK(!(action(1,CMD_OPEN)->flags&MI_DISABLED));
    session_key(s,'o',MOD_CMD);CHECK(!appearance_panel_can_open());
    menu_update(s);CHECK(action(1,CMD_OPEN)->flags&MI_DISABLED);
    session_command(s,CMD_CLOSE);CHECK(!appearance_panel_active()&&s->nwin==nwin);
    s->desk_sel=0;
    session_command(s,CMD_HELP_KEYS);CHECK(s->win[s->nwin-1].kind==WIN_TEXT);
    session_key(s,'c',MOD_CMD);CHECK(strstr(s->clipboard_out,"Cmd-I")!=NULL);
    menu_update(s);CHECK(!strcmp(action(2,CMD_COPY)->label,"Copy Text"));
    /* S08/S09: the leftmost title is the OS999 fruit mark; a click on it opens
     * the OS999 menu with About This Mac first, Control Panels, and Shut Down. */
    {
        Rect mark; menu_title_rect(0,&mark);
        CHECK(mark.w==34 && mark.x==6 && mark.h==20);
        CHECK(menu_title_at(mark.x+mark.w/2,mark.y+mark.h/2)==0);
        s->menu=-1;
        session_down(s,mark.x+mark.w/2,mark.y+mark.h/2,1);
        CHECK(s->menu==0);
        CHECK(menu_nitems(0)==4 && !strcmp(menu_at(0,0)->label,"About This Mac") && menu_at(0,0)->cmd==CMD_ABOUT);
        CHECK(!strcmp(menu_at(0,1)->label,"Control Panels") && menu_at(0,3)->cmd==CMD_SHUTDOWN);
        session_up(s,mark.x+mark.w/2,mark.y+mark.h/2);
        session_key(s,27,0);CHECK(s->menu==-1);
    }
    s->menu=2;s->menu_hot=-1;
    session_key(s,MENU_KEY_DOWN,0);CHECK(menu_at(2,s->menu_hot)->cmd==CMD_COPY);
    session_key(s,27,0);CHECK(s->menu==-1);
    session_command(s,CMD_CLOSE_ALL);menu_update(s);
    CHECK(s->nwin==0 && (action(1,CMD_CLOSE)->flags&MI_DISABLED));
    s->clipboard_write_request=0;session_key(s,'c',MOD_CMD);CHECK(!s->clipboard_write_request);
    session_command(s,CMD_OPEN); /* No selection: no new window. */
    s->desk_sel=1; session_command(s,CMD_NEW_FOLDER);
    CHECK(s->nwin==1 && !strcmp(s->win[0].rel,"Trash"));
    s->desk_sel=0; session_command(s,CMD_NEW_FOLDER);
    CHECK(!strcmp(s->win[s->nwin-1].rel,""));
    session_context(s,10,s->sh-100); CHECK(context_active());
    session_key(s,'n',MOD_CMD); CHECK(!context_active());
    CHECK(!strcmp(s->win[s->nwin-1].rel,""));
    session_destroy(s);
    printf("menu integration: %s\n",failures?"FAILED":"ok");return !!failures;
}
