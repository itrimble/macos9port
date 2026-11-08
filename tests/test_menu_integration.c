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
static int failures, guest_calls, guest_last, guest_result = 1;
static int guest_command(int command) { guest_calls++; guest_last = command; return guest_result; }
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
    session_down(s,layout.content.x+layout.content.w/2,layout.content.y+5,1);
    session_up(s,layout.content.x+layout.content.w/2,layout.content.y+5);
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
    /* Hierarchical menus support hover, arrow navigation and activation. */
    menu_update(s);
    int arrange=-1,label=-1;
    for(int i=0;i<menu_nitems(3);i++)if(menu_at(3,i)->cmd==CMD_MENU_ARRANGE)arrange=i;
    for(int i=0;i<menu_nitems(1);i++)if(menu_at(1,i)->cmd==CMD_MENU_LABELS)label=i;
    CHECK(arrange>=0 && label>=0);
    s->menu=3;s->menu_hot=arrange;
    session_key(s,MENU_KEY_RIGHT,0);CHECK(s->menu_child==MENU_ARRANGE && s->menu_child_hot==0);
    session_key(s,MENU_KEY_DOWN,0);session_key(s,MENU_KEY_DOWN,0);
    session_key(s,'\r',0);CHECK(s->menu==-1 && w->sort_mode==SORT_DATE);
    s->menu=3;s->menu_hot=arrange;session_key(s,MENU_KEY_RIGHT,0);
    session_key(s,27,0);CHECK(!s->menu_child && s->menu==3);
    session_key(s,MENU_KEY_RIGHT,0);session_key(s,MENU_KEY_LEFT,0);
    CHECK(!s->menu_child && s->menu_hot==arrange);
    Rect row;menu_item_rect(3,arrange,&row);session_move(s,row.x+20,row.y+row.h/2);
    CHECK(s->menu_child==MENU_ARRANGE);
    menu_item_rect(MENU_ARRANGE,4,&row);CHECK(row.x>=0 && row.x+row.w<=OSPI_W && row.y+row.h<=OSPI_H);
    session_move(s,row.x+20,row.y+row.h/2);CHECK(s->menu_child_hot==4);
    session_down(s,row.x+20,row.y+row.h/2,1);session_up(s,row.x+20,row.y+row.h/2);
    CHECK(w->sort_mode==SORT_SIZE && s->menu==-1);
    session_command(s,CMD_SORT_CREATED);menu_update(s);
    CHECK(w->sort_mode==SORT_CREATED && (action(MENU_ARRANGE,CMD_SORT_CREATED)->flags&MI_CHECKED));
    session_command(s,CMD_SORT_LABEL);menu_update(s);
    CHECK(w->sort_mode==SORT_LABEL && (action(MENU_ARRANGE,CMD_SORT_LABEL)->flags&MI_CHECKED));
    session_command(s,CMD_VIEW_OPTIONS);CHECK(s->dialog==4);
    for(int i=0;i<7;i++)session_key(s,MENU_KEY_DOWN,0);
    session_key(s,' ',0);session_key(s,'\r',0);
    CHECK(!s->dialog && w->sort_mode==SORT_CREATED);
    session_command(s,CMD_VIEW_OPTIONS);
    for(int i=0;i<8;i++)session_key(s,MENU_KEY_DOWN,0);
    session_key(s,' ',0);session_key(s,'\r',0);
    CHECK(!s->dialog && w->sort_mode==SORT_LABEL);
    s->guest_active=s->guest_focused=1;session_command(s,CMD_SORT_CREATED);menu_update(s);
    CHECK(w->sort_mode==SORT_LABEL && (action(MENU_ARRANGE,CMD_SORT_CREATED)->flags&MI_DISABLED));
    s->guest_active=s->guest_focused=0;
    s->menu=1;s->menu_hot=label;menu_update(s);
    session_key(s,MENU_KEY_RIGHT,0);CHECK(s->menu_child==MENU_LABELS);
    session_key(s,MENU_KEY_DOWN,0);session_key(s,'\r',0);
    int labeled=0;for(int i=0;i<w->nitems;i++)if(w->items[i].selected)labeled+=w->items[i].label==1;
    CHECK(labeled==1 && !s->dialog);
    /* Blank desktop owns commands even while folder windows remain open. */
    session_down(s,10,s->sh-100,1); session_up(s,10,s->sh-100);
    CHECK(s->desktop_focus && s->desk_sel==-1);
    menu_update(s);
    CHECK(action(1,CMD_OPEN)->flags&MI_DISABLED);
    CHECK(action(1,CMD_CLOSE)->flags&MI_DISABLED);
    CHECK(action(1,CMD_GET_INFO)->flags&MI_DISABLED);
    CHECK(action(2,CMD_SELECT_ALL)->flags&MI_DISABLED);
    CHECK(action(3,CMD_VIEW_LIST)->flags&MI_DISABLED);
    int before=s->nwin;
    session_command(s,CMD_CLOSE); CHECK(s->nwin==before);
    session_command(s,CMD_SELECT_ALL);
    for(int i=0;i<w->nitems;i++) CHECK(!w->items[i].selected);
    session_down(s,w->x+80,w->y+10,1); session_up(s,w->x+80,w->y+10);
    CHECK(!s->desktop_focus);
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
        CHECK(!strcmp(menu_at(0,0)->label,"About This Mac") && menu_at(0,0)->cmd==CMD_ABOUT);
        CHECK(action(0,CMD_MENU_PANELS) && action(0,CMD_TERMINAL_CONSOLE) && action(0,CMD_BROWSER));
        CHECK(action(4,CMD_SHUTDOWN));
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
    /* Modal dialogs retain their pending choice even when Quit is pressed. */
    for (int app=0;app<3;app++) {
        s->browser_focused = app == 1; s->media_focused = app == 2;
        s->dialog = 2; s->dialog_cmd = CMD_EMPTY_DO;
        session_key(s,'q',MOD_CMD); session_key(s,'Q',MOD_CMD);
        CHECK(!s->quit && s->dialog == 2 && s->dialog_cmd == CMD_EMPTY_DO);
        CHECK(!s->browser_action);
        session_key(s,27,0); CHECK(!s->dialog && !s->dialog_cmd && !s->quit);
    }
    s->browser_focused = s->media_focused = 0;
    /* App-scoped menus and direct command dispatch must agree. */
    int blocked[] = {CMD_DUPLICATE, CMD_RENAME, CMD_MOVE_TRASH, CMD_MAKE_ALIAS,
        CMD_UNDO, CMD_COPY, CMD_SELECT_ALL, CMD_PRINT_WINDOW, CMD_VIEW_LIST,
        CMD_EMPTY_ASK, CMD_SORT_KIND, CMD_LABEL_BASE + 1};
    s->browser_windows = 1; s->browser_edit_flags = 15;
    int windows = s->nwin;
    Window saved = s->win[windows - 1];
    s->clipboard_write_request = 0;
    for (int app = 0; app < 3; app++) {
        s->browser_focused = app == 0; s->media_focused = app == 1; s->guest_focused = app == 2;
        menu_update(s);
        CHECK(action(1,CMD_DUPLICATE)->flags & MI_DISABLED);
        CHECK(!!(action(2,CMD_COPY)->flags & MI_DISABLED) == !!app);
        CHECK(action(1,CMD_PRINT_WINDOW)->flags & MI_DISABLED);
        CHECK(action(3,CMD_VIEW_LIST)->flags & MI_DISABLED);
        CHECK(!menu_cmd_for_key('d'));
        CHECK(menu_cmd_for_key('c') == (app ? 0 : CMD_COPY));
        for (unsigned i=0;i<sizeof blocked/sizeof blocked[0];i++) session_command(s,blocked[i]);
        CHECK(!s->dialog && s->nwin == windows && !s->clipboard_write_request);
        CHECK(!memcmp(&saved,&s->win[windows - 1],sizeof saved));
        session_command(s,CMD_NEW_FOLDER);
        CHECK(!s->dialog && s->nwin == windows);
        if (!app) {
            CHECK(!strcmp(action(1,CMD_NEW_FOLDER)->label,"New Window"));
            CHECK(s->browser_action == BROWSER_NEW_WINDOW);
        } else CHECK(action(1,CMD_NEW_FOLDER)->flags & MI_DISABLED);
    }
    s->browser_focused = s->media_focused = s->guest_focused = 0;
    menu_update(s);
    CHECK(!strcmp(action(1,CMD_NEW_FOLDER)->label,"New Folder"));
    CHECK(!(action(1,CMD_NEW_FOLDER)->flags & MI_DISABLED));
    w = &s->win[s->nwin-1];
    int alias_source = -1;
    for (int i=0;i<w->nitems;i++) {
        w->items[i].selected = !strcmp(w->items[i].name,"Documents");
        if (w->items[i].selected) alias_source = i;
    }
    CHECK(alias_source >= 0); s->desktop_focus = 0; s->desk_sel = -1;
    menu_update(s); CHECK(menu_cmd_for_key('m') == CMD_MAKE_ALIAS);
    int items_before_alias = w->nitems;
    session_key(s,'m',MOD_CMD);
    CHECK(!s->dialog && w->nitems == items_before_alias + 1);
    int made_alias = 0;
    for (int i=0;i<w->nitems;i++) made_alias |= w->items[i].kind == ITEM_ALIAS;
    CHECK(made_alias);
    s->guest_active = s->guest_focused = 1; s->guest_menu_command = guest_command;
    snprintf(s->guest_title, sizeof s->guest_title, "Fixture App");
    menu_update(s);
    CHECK(!strcmp(action(1,CMD_QUIT)->label,"Quit Fixture App"));
    CHECK(action(1,CMD_CLOSE_ALL)->flags & MI_DISABLED);
    int retained = s->nwin;
    session_key(s,'w',MOD_CMD); CHECK(guest_calls == 1 && guest_last == CMD_CLOSE);
    session_key(s,'q',MOD_CMD); CHECK(guest_calls == 2 && guest_last == CMD_QUIT);
    CHECK(!s->quit && s->nwin == retained && s->guest_active);
    session_command(s,CMD_CLOSE_ALL); CHECK(guest_calls == 2 && s->nwin == retained);
    guest_result = 0; session_command(s,CMD_QUIT);
    CHECK(guest_calls == 3 && s->dialog && !s->quit && s->guest_active);
    session_key(s,27,0); s->guest_menu_command = NULL; menu_update(s);
    CHECK(action(1,CMD_QUIT)->flags & MI_DISABLED);
    session_command(s,CMD_QUIT); CHECK(!s->quit && guest_calls == 3);
    s->guest_active = s->guest_focused = 0;
    session_destroy(s);
    printf("menu integration: %s\n",failures?"FAILED":"ok");return !!failures;
}
