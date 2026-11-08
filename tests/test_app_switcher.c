#include "app_switcher.h"
#include "finder_apps.h"
#include "desktop_gesture.h"
#include "desktop_theme.h"
#include "archive_fs.h"
#include "finder_drag.h"
#include "session_int.h"
#include "draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static void click(Session *s, int x, int y) { session_down(s,x,y,1);session_up(s,x,y); }
static void write_file(const char *path, const char *text) {
    FILE *f=fopen(path,"w");CHECK(f);if(f){CHECK(fputs(text,f)>=0);CHECK(!fclose(f));}
}
static int opened, open_result;
static int media_running(const Session *s) { (void)s; return 1; }
static int media_file(const char *path) { return strstr(path, ".wav") != NULL; }
static int media_open(Session *s, const char *path) { (void)s; (void)path; opened++; return open_result; }
static void check_drops(Session *s) {
    char path[OSPI_PATH+32]; snprintf(path,sizeof path,"%s/drop.wav",s->root); write_file(path,"fixture");
    reload_folders(s); Window *w=&s->win[0]; int index=-1;
    for(int i=0;i<w->nitems;i++) if(!strcmp(window_items(w)[i].name,"drop.wav")) index=i;
    CHECK(index>=0); if(index<0)return;
    select_only(w,index); s->undo_count=1; s->undo_entries[0].type=4;
    SessionMediaHooks hooks={.running=media_running,.is_media_file=media_file,.try_open_file=media_open};
    session_set_media_hooks(&hooks);
    s->switcher.open=1;s->switcher.horizontal=s->switcher.collapsed=s->switcher.small=s->switcher.no_names=0;
    s->switcher.width=190;s->switcher.x=400;s->switcher.y=200;s->dialog=0;
    Rect r;CHECK(app_switcher_bounds(s,&r));int x=r.x+40,y=r.y+22+84+15;
    CHECK(app_switcher_application_at(s,x,y)==3);
    finder_drag_drop(s,0,x,y);CHECK(opened==1 && !s->dialog && !access(path,F_OK));
    open_result=-1;finder_drag_drop(s,0,x,y);CHECK(opened==2 && s->dialog);s->dialog=0;open_result=0;
    finder_drag_drop(s,0,r.x+30,r.y+10);CHECK(opened==2 && !s->dialog);
    s->dialog=1;finder_drag_drop(s,0,x,y);CHECK(opened==2);s->dialog=0;
    window_items(w)[index].kind=ITEM_FOLDER;finder_drag_drop(s,0,x,y);CHECK(opened==2 && s->dialog);s->dialog=0;
    window_items(w)[index].kind=ITEM_FILE;
    int other=index?0:1;window_items(w)[other].selected=1;
    finder_drag_drop(s,0,x,y);CHECK(opened==2 && s->dialog);s->dialog=0;window_items(w)[other].selected=0;
    CHECK(!unlink(path));CHECK(!symlink("missing.wav",path));
    finder_drag_drop(s,0,x,y);CHECK(opened==2 && s->dialog);s->dialog=0;CHECK(!unlink(path));
    finder_drag_drop(s,0,x,y);CHECK(opened==2 && s->dialog);s->dialog=0;
    CHECK(s->undo_count==1 && s->undo_entries[0].type==4);s->undo_count=0;
    session_set_media_hooks(NULL);
}
static void check_preferences(Session *s) {
    char path[OSPI_PATH+96],target[OSPI_PATH+96],temp[OSPI_PATH+96];
    snprintf(path,sizeof path,"%s/.os999-application-switcher",s->root);
    snprintf(target,sizeof target,"%s/sentinel",s->root);
    snprintf(temp,sizeof temp,"%s/.os999-application-switcher.tmp.%ld",s->root,(long)getpid());
    s->switcher.open=1;s->switcher.x=81;s->switcher.y=100;s->switcher.width=120;
    s->switcher.small=s->switcher.no_names=s->switcher.horizontal=1;s->switcher.collapsed=0;
    CHECK(!app_switcher_save(s));
    s->switcher.x=99;s->switcher.gesture=1;s->switcher.tear_armed=1;
    CHECK(app_switcher_load(s)==1 && s->switcher.x==81 && !s->switcher.gesture && !s->switcher.tear_armed);
    const char *bad[]={"OS999-SWITCHER-1\n1 0 30 190 0 0 0 2\n", "OS999-SWITCHER-1\n1 0 30 190 0 0 0 0\nextra",
        "OS999-SWITCHER-1\n1 99999999999999999999999 30 190 0 0 0 0\n", "OS999-SWITCHER-2\n1 0 30 190 0 0 0 0\n"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++) {write_file(path,bad[i]);CHECK(app_switcher_load(s)==-1 && s->switcher.x==81);}
    CHECK(!unlink(path));CHECK(!mkfifo(path,0600));CHECK(app_switcher_load(s)==-1);CHECK(!unlink(path));
    write_file(target,"untouched\n");CHECK(!symlink(target,path));CHECK(app_switcher_load(s)==-1);
    CHECK(!app_switcher_save(s));struct stat st;CHECK(!lstat(path,&st) && S_ISREG(st.st_mode));
    FILE *f=fopen(target,"r");char buf[64]={0};CHECK(f);if(f){CHECK(fgets(buf,sizeof buf,f));fclose(f);}CHECK(!strcmp(buf,"untouched\n"));
    write_file(temp,"keep\n");CHECK(app_switcher_save(s)==-1);CHECK(!unlink(temp));
    CHECK(app_switcher_load(s)==1 && s->switcher.x==81);
    Rect r;CHECK(app_switcher_bounds(s,&r));
    finder_drag_drop(s,0,r.x+20,r.y+30);CHECK(s->dialog && !strcmp(s->dialog_title,"Open in Application"));s->dialog=0;
}
int main(void) {
    char root[] = "/tmp/os999-switcher-XXXXXX"; CHECK(mkdtemp(root)); CHECK(!volume_seed(root));
    Session *s = session_create(root); CHECK(s); if (!s) return 1;
    s->guest_active = s->browser_active = 1;
    session_command(s,CMD_APP_SWITCHER);
    Rect r; CHECK(app_switcher_bounds(s,&r) && r.h == 150);
    CHECK(s->menu == -1 && finder_apps_front(s) == 1);
    click(s,r.x+45,r.y+22+42+15); CHECK(s->guest_focused && !s->switcher.gesture);
    s->mouse_mods = MOD_OPTION;
    click(s,r.x+45,r.y+22+84+15); CHECK(s->browser_focused && s->guest_hidden);
    s->mouse_mods = 0;
    click(s,r.x+45,r.y+22+42+15); CHECK(s->guest_focused && !s->guest_hidden);
    s->dialog = 1; click(s,r.x+45,r.y+30); CHECK(s->guest_focused); s->dialog = 0;
    click(s,r.x+r.w-28,r.y+12); CHECK(s->switcher.collapsed);
    CHECK(app_switcher_bounds(s,&r) && r.h == 20);
    click(s,r.x+r.w-28,r.y+12); CHECK(!s->switcher.collapsed);
    app_switcher_bounds(s,&r);
    click(s,r.x+r.w-10,r.y+10); CHECK(s->switcher.no_names);
    app_switcher_bounds(s,&r); CHECK(r.w == 76);
    s->mouse_mods = MOD_OPTION; click(s,r.x+r.w-10,r.y+10); CHECK(s->switcher.small);
    s->mouse_mods |= MOD_SHIFT; click(s,r.x+r.w-10,r.y+10); CHECK(s->switcher.horizontal);
    app_switcher_bounds(s,&r); CHECK(r.h == 50 && r.w == 82);
    s->mouse_mods = 0; click(s,r.x+r.w-10,r.y+10); CHECK(!s->switcher.no_names);
    app_switcher_bounds(s,&r); CHECK(r.w == 574);
    session_down(s,r.x+30,r.y+10,1); CHECK(finder_apps_pointer_capture(s));
    session_move(s,-100,-100); session_up(s,-100,-100);
    app_switcher_bounds(s,&r); CHECK(r.x == 0 && r.y == TITLE_H && !finder_apps_pointer_capture(s));
    session_down(s,r.x+30,r.y+10,1); session_up(s,r.x+130,r.y+80);
    app_switcher_bounds(s,&r); CHECK(r.x == 100 && r.y == TITLE_H + 70);
    session_down(s,r.x+r.w-2,r.y+r.h-5,1); session_move(s,r.x+200,r.y+30); session_up(s,r.x+365,r.y+30);
    CHECK(s->switcher.width == 120);
    app_switcher_bounds(s,&r);
    session_down(s,r.x+10,r.y+10,1); session_up(s,r.x-10,r.y); CHECK(s->switcher.open);
    click(s,r.x+10,r.y+10); CHECK(!s->switcher.open);
    menu_update(s); menu_title_rect(7,&r); session_down(s,r.x+5,r.y+5,1);
    CHECK(s->menu == 7 && s->switcher.tear_armed);
    session_move(s,350,500); CHECK(s->switcher.open && s->switcher.gesture && s->menu == -1);
    session_up(s,350,500); CHECK(!s->switcher.gesture && !s->switcher.tear_armed);
    CHECK(finder_apps_front(s) == 2);
    app_switcher_bounds(s,&r); CHECK(finder_apps_occluded(s,2,r.x+10,r.y+10));
    s->switcher.open = 0; s->down=1; s->press_zone=Z_ITEM; s->drag=3;
    s->mx=350; s->my=250; CHECK(finder_apps_pointer_capture(s));
    CHECK(!finder_apps_guest_hit(s,1,350,250));
    Rect regions[DESKTOP_GESTURE_REGIONS]; CHECK(session_gesture_regions(s,regions)==8);
    uint32_t *px=calloc((size_t)OSPI_W*OSPI_H,sizeof *px); CHECK(px);
    if(px) {
        Fb fb; fb_init(&fb,px,OSPI_W,OSPI_H); fb_fill(&fb,0,0,OSPI_W,OSPI_H,0xFF123456);
        session_overlay_paint(s,px,OSPI_W,OSPI_H);
        CHECK(px[238*OSPI_W+334]==desktop_theme_color(THEME_FOREGROUND));
        CHECK(px[239*OSPI_W+335]==desktop_theme_color(THEME_PANEL));
        CHECK(px[250*OSPI_W+350]==0xFF123456); /* Transparent ghost interior. */
        session_overlay_paint(s,px,OSPI_W,OSPI_H);
        CHECK(px[238*OSPI_W+334]==desktop_theme_color(THEME_FOREGROUND));
        free(px);
    }
    s->drag=5;s->press_win=0;s->press_x=0;s->press_y=0;s->mx=400;s->my=400;
    CHECK(session_gesture_regions(s,regions)==4);
    Layout layout;window_layout(&s->win[0],&layout);
    for(int i=0;i<4;i++) if(regions[i].w && regions[i].h)
        CHECK(regions[i].x>=layout.content.x && regions[i].y>=layout.content.y);
    check_drops(s);check_preferences(s);session_destroy(s);s=session_create(root);CHECK(s);
    if(s){CHECK(s->switcher.open && s->switcher.x==81 && s->switcher.width==120 && s->switcher.small &&
        s->switcher.no_names && s->switcher.horizontal && !s->switcher.gesture && !s->switcher.tear_armed);
        CHECK(finder_apps_front(s)==1);session_destroy(s);}
    CHECK(!archive_remove_tree(root));
    printf("Application Switcher: %d checks, %d failures\n",checks,failures);return !!failures;
}
