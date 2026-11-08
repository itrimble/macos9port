#include "finder_apps.h"
#include "finder_print.h"
#include "finder_recent.h"
#include "ospi.h"
#include "desktop_finder.h"
#include "finder_search.h"
#include "finder_files.h"
#include "menu_navigation.h"
#include "session_int.h"
#include "archive_fs.h"
#include "draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static Window *front(Session *s) { return &s->win[s->nwin - 1]; }
static int select_item(Session *s, const char *name) {
    Window *w = front(s); select_only(w, -1);
    for (int i = 0; i < w->nitems; i++) if (!strcmp(w->items[i].name, name)) { select_only(w, i); return i; }
    return -1;
}
static int fake_media_running(const Session *s) { (void)s; return 1; }
static void fake_media_draw(Session *s, void *fb) {
    if (!s->media_hidden) fb_fill(fb, 300, 220, 100, 80, 0xFF0102F3);
}
static void test_media_app(Session *s) {
    static const SessionMediaHooks hooks = {.running = fake_media_running, .draw = fake_media_draw};
    session_set_media_hooks(&hooks);
    int focus = 0;
    session_command(s, CMD_APP_MEDIA);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(s->media_focused && !focus);
    session_command(s, CMD_APP_HIDE_OTHERS);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(s->finder_hidden && !s->media_hidden);
    session_command(s, CMD_APP_HIDE);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(!s->finder_hidden && s->media_hidden && !s->media_focused);
    session_command(s, CMD_APP_MEDIA);
    finder_apps_sync(s, 1, "Guest", &focus);
    CHECK(focus && !s->media_focused); /* A newly launched guest takes focus. */
    session_command(s, CMD_APP_MEDIA);
    finder_apps_sync(s, 1, "Guest", &focus);
    CHECK(!focus && s->media_focused);
    session_command(s, CMD_APP_HIDE_OTHERS);
    CHECK(s->guest_hidden && s->finder_hidden && !s->media_hidden);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(s->media_focused && s->finder_hidden); /* Background guest exit. */
    session_command(s, CMD_APP_SHOW_ALL);
    CHECK(!s->finder_hidden && !s->media_hidden);
    uint32_t *frame = calloc((size_t)OSPI_W * OSPI_H, sizeof *frame);
    CHECK(frame);
    Window saved = s->win[0];
    s->win[0].x = 260; s->win[0].y = 180; s->win[0].w = 300; s->win[0].h = 200;
    if (frame) {
        session_paint(s, frame, OSPI_W, OSPI_H);
        CHECK(frame[250 * OSPI_W + 350] == 0xFF0102F3);
        session_command(s, CMD_APP_FINDER);
        finder_apps_sync(s, 0, "", &focus);
        session_paint(s, frame, OSPI_W, OSPI_H);
        CHECK(frame[250 * OSPI_W + 350] != 0xFF0102F3);
        free(frame);
    }
    s->win[0] = saved;
    session_set_media_hooks(NULL);
    session_command(s, CMD_APP_FINDER);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(!finder_apps_enabled(s, CMD_APP_MEDIA));
}
int main(void) {
    char root[] = "/tmp/os999-finder-desktop-XXXXXX", path[1024];
    CHECK(mkdtemp(root)); CHECK(!volume_seed(root));
    snprintf(path, sizeof path, "%s/Documents/Example", root);
    FILE *f = fopen(path, "wb"); CHECK(f); if (f) { fputs("Hello", f); fclose(f); }
    Session *s = session_create(root); CHECK(s); if (!s) return 1;
    CHECK(!open_folder(s, "Documents")); CHECK(select_item(s, "Example") >= 0);
    CHECK(finder_command_enabled(s, CMD_RENAME)); session_command(s, CMD_RENAME);
    CHECK(s->dialog == 3 && s->edit_selected);
    session_text_input(s, "Renamed"); CHECK(!strcmp(s->edit_text, "Renamed"));
    session_key(s, '\r', 0); CHECK(!s->dialog); CHECK(select_item(s, "Renamed") >= 0);
    CHECK(select_item(s, "Example") < 0);
    session_command(s, CMD_UNDO); CHECK(select_item(s, "Example") >= 0);
    session_command(s, CMD_LABEL); CHECK(s->dialog == 4);
    session_key(s, MENU_KEY_DOWN, 0); session_key(s, ' ', 0); session_key(s, '\r', 0);
    CHECK(!s->dialog && finder_label(root, "Documents", "Example", -1) == 1);
    session_command(s, CMD_VIEW_OPTIONS); CHECK(s->dialog == 4);
    session_key(s, MENU_KEY_DOWN, 0); session_key(s, ' ', 0); session_key(s, '\r', 0);
    CHECK(front(s)->view_mode == VIEW_BUTTONS);
    session_command(s, CMD_VIEW_ICONS);
    s->host_sound_enabled = 1; s->host_startup_enabled = 1;
    session_command(s, CMD_PREFERENCES); CHECK(s->dialog == 4);
    session_key(s, ' ', 0); session_key(s, 27, 0); CHECK(!s->sound_toggle_request);
    session_command(s, CMD_PREFERENCES); session_key(s, ' ', 0); session_key(s, '\r', 0);
    CHECK(s->sound_toggle_request && !s->startup_toggle_request);
    session_command(s, CMD_DUPLICATE); CHECK(select_item(s, "Example copy") >= 0);
    session_command(s, CMD_MOVE_TRASH); CHECK(select_item(s, "Example copy") < 0);
    session_command(s, CMD_UNDO); CHECK(select_item(s, "Example copy") >= 0);
    session_command(s, CMD_MAKE_ALIAS); CHECK(select_item(s, "Example copy alias") >= 0);
    CHECK(front(s)->items[select_item(s, "Example copy alias")].kind == ITEM_ALIAS);
    session_command(s, CMD_SHOW_ORIGINAL); CHECK(!strcmp(front(s)->rel, "Documents"));
    CHECK(front(s)->items[select_item(s, "Example copy")].selected);
    session_command(s, CMD_FAVORITE);
    session_command(s, CMD_FAVORITES); CHECK(!strcmp(front(s)->rel, "Favorites"));
    CHECK(select_item(s, "Example copy alias") >= 0);
    session_command(s, CMD_OPEN); CHECK(front(s)->kind == WIN_TEXT && !strcmp(front(s)->text, "Hello"));
    session_command(s, CMD_CLOSE);
    session_command(s, CMD_FIND); CHECK(s->dialog == 3);
    session_text_input(s, "EXAMPLE"); session_key(s, '\r', 0);
    CHECK(front(s)->search_results && front(s)->nitems >= 2);
    CHECK(!finder_command_enabled(s, CMD_RENAME));
    CHECK(select_item(s, "Example") >= 0); session_command(s, CMD_OPEN);
    CHECK(front(s)->kind == WIN_TEXT && !strcmp(front(s)->text, "Hello"));
    session_command(s, CMD_RESTART); CHECK(s->dialog == 2);
    session_key(s, 27, 0); CHECK(s->nwin > 1);
    session_command(s, CMD_RESTART); session_key(s, '\r', 0);
    CHECK(s->nwin == 1 && !front(s)->rel[0]);
    int focus=0;
    CHECK(!finder_apps_enabled(s,CMD_APP_GUEST));
    CHECK(!finder_apps_sync(s,1,"Fixture Application",&focus));
    CHECK(focus && s->guest_focused && !s->desktop_focus);
    menu_update(s);CHECK(!strcmp(menu_title(7),"Fixture Application"));
    session_command(s,CMD_APP_FINDER);CHECK(finder_apps_sync(s,1,"Fixture Application",&focus));
    CHECK(!focus && !s->desktop_focus);
    CHECK(!finder_apps_guest_hit(s,0,front(s)->x+30,front(s)->y+30));
    session_command(s,CMD_APP_GUEST);finder_apps_sync(s,1,"Fixture Application",&focus);
    CHECK(finder_apps_guest_hit(s,1,front(s)->x+30,front(s)->y+30));
    CHECK(!finder_apps_guest_hit(s,1,100,5));
    session_command(s,CMD_APP_FINDER);finder_apps_sync(s,1,"Fixture Application",&focus);
    session_command(s,CMD_APP_HIDE);finder_apps_sync(s,1,"Fixture Application",&focus);
    CHECK(s->finder_hidden && focus && !s->guest_hidden);
    session_command(s,CMD_APP_HIDE);finder_apps_sync(s,1,"Fixture Application",&focus);
    CHECK(!s->finder_hidden && !focus && s->guest_hidden);
    session_command(s,CMD_APP_SHOW_ALL);CHECK(!s->guest_hidden && !s->finder_hidden);
    session_command(s,CMD_APP_GUEST);finder_apps_sync(s,1,"Fixture Application",&focus);CHECK(focus);
    session_command(s,CMD_APP_HIDE_OTHERS);CHECK(s->finder_hidden);
    finder_apps_sync(s,0,"",&focus);CHECK(!s->guest_active && !s->finder_hidden && !focus && !s->desktop_focus);
    menu_update(s);CHECK(!strcmp(menu_title(7),"Finder"));
    test_media_app(s);
    CHECK(!finder_print_enabled(s,CMD_PAGE_SETUP));
    s->print_available=1;CHECK(finder_print_enabled(s,CMD_PAGE_SETUP));
    session_command(s,CMD_PAGE_SETUP);CHECK(s->print_request==PRINT_PAGE_SETUP);
    CHECK(!finder_print_enabled(s,CMD_PRINT_WINDOW));s->print_request=0;
    CHECK(!open_folder(s,"Documents"));CHECK(select_item(s,"Example")>=0);
    CHECK(finder_print_enabled(s,CMD_PRINT));session_command(s,CMD_PRINT);
    CHECK(s->print_request==PRINT_TEXT && !strcmp(s->print_text,"Hello") && !strcmp(s->print_title,"Example"));
    s->print_request=0;session_command(s,CMD_PRINT_WINDOW);
    CHECK(s->print_request==PRINT_WINDOW && s->print_rect.w==front(s)->w && s->print_rect.h==front(s)->h);
    s->print_request=0;front(s)->collapsed=1;CHECK(!finder_print_enabled(s,CMD_PRINT_WINDOW));front(s)->collapsed=0;
    snprintf(path,sizeof path,"%s/Documents/Binary",root);f=fopen(path,"wb");CHECK(f);
    if(f){fputc('A',f);fputc(0,f);fclose(f);}reload_folders(s);CHECK(select_item(s,"Binary")>=0);
    session_command(s,CMD_PRINT);CHECK(s->dialog && !s->print_request);s->dialog=0;
    f=fopen(path,"wb");CHECK(f);if(f){CHECK(!ftruncate(fileno(f),65536));fclose(f);}
    session_command(s,CMD_PRINT);CHECK(s->dialog && !s->print_request);s->dialog=0;
    s->desktop_focus=1;CHECK(!finder_print_enabled(s,CMD_PRINT_WINDOW) && !finder_print_enabled(s,CMD_PRINT));s->desktop_focus=0;
    CHECK(s->nrecent>=1);
    finder_recent_note(s,"Documents/Example",0);
    CHECK(!strcmp(s->recent[0].path,"Documents/Example"));
    int recent=s->nrecent;finder_recent_note(s,"Documents/Example",0);CHECK(s->nrecent==recent);
    finder_recent_note(s,"../outside",0);CHECK(s->nrecent==recent);
    finder_recent_note(s,"Documents/Missing",0);CHECK(s->nrecent==recent);
    session_destroy(s);s=session_create(root);CHECK(s->nrecent==recent);
    menu_update(s);CHECK(menu_nitems(MENU_RECENT_DOCS)>=3);
    CHECK(menu_at(MENU_RECENT_DOCS,0)->cmd==CMD_RECENT_BASE);
    session_command(s,CMD_RECENT_BASE);CHECK(front(s)->kind==WIN_TEXT && !strcmp(front(s)->text,"Hello"));
    session_command(s,CMD_RECENT_CLEAR_DOCS);CHECK(s->nrecent==0);
    session_destroy(s);s=session_create(root);CHECK(s->nrecent==0);
    menu_update(s);CHECK(menu_at(MENU_RECENT_DOCS,0)->flags&MI_DISABLED);
    snprintf(path,sizeof path,"%s/Documents/Outside",root);CHECK(!symlink("/etc/passwd",path));
    finder_recent_note(s,"Documents/Outside",0);CHECK(s->nrecent==0);
    session_destroy(s);
    snprintf(path,sizeof path,"%s/.os999-recent",root);f=fopen(path,"w");CHECK(f);
    if(f){fputs("OS999-RECENT-1\n0\t../outside\n0\tDocuments/Outside\n0\tDocuments/Example\n0\tDocuments/Example\n",f);fclose(f);}
    s=session_create(root);CHECK(s->nrecent==1 && !strcmp(s->recent[0].path,"Documents/Example"));
    char alias[OSPI_NAME];
    CHECK(!finder_alias(root,"Documents","Example",0,alias));
    CHECK(!finder_rename(root,"Documents","Example","Original moved"));
    CHECK(!open_folder(s,"Documents"));CHECK(select_item(s,alias)>=0);
    CHECK(front(s)->items[select_item(s,alias)].kind==ITEM_ALIAS);
    session_command(s,CMD_SHOW_ORIGINAL);
    int original_selected=0;
    for(int i=0;i<front(s)->nitems;i++)
        if(!strcmp(front(s)->items[i].name,"Original moved")) original_selected=front(s)->items[i].selected;
    CHECK(!strcmp(front(s)->rel,"Documents") && original_selected);
    CHECK(select_item(s,alias)>=0);session_command(s,CMD_OPEN);
    CHECK(front(s)->kind==WIN_TEXT && !strcmp(front(s)->text,"Hello"));
    session_command(s,CMD_CLOSE);
    snprintf(path,sizeof path,"%s/Documents/Original moved",root);CHECK(!unlink(path));
    reload_folders(s);CHECK(select_item(s,alias)>=0);
    CHECK(front(s)->items[select_item(s,alias)].kind==ITEM_ALIAS);
    int windows=s->nwin;session_command(s,CMD_OPEN);
    CHECK(s->dialog && !strcmp(s->dialog_title,"Open Alias") && s->nwin==windows);
    session_destroy(s); CHECK(!archive_remove_tree(root));
    printf("Finder desktop: %d checks, %d failures\n", checks, failures); return !!failures;
}
