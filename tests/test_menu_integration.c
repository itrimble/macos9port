#include "ospi.h"
#include "appearance_panel.h"
#include "folder_view.h"
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
    s->menu=2;s->menu_hot=-1;
    session_key(s,MENU_KEY_DOWN,0);CHECK(menu_at(2,s->menu_hot)->cmd==CMD_COPY);
    session_key(s,27,0);CHECK(s->menu==-1);
    session_command(s,CMD_CLOSE_ALL);menu_update(s);
    CHECK(s->nwin==0 && (action(1,CMD_CLOSE)->flags&MI_DISABLED));
    s->clipboard_write_request=0;session_key(s,'c',MOD_CMD);CHECK(!s->clipboard_write_request);
    session_destroy(s);
    printf("menu integration: %s\n",failures?"FAILED":"ok");return !!failures;
}
