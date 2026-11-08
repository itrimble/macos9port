#include "desktop_context.h"
#include "session_int.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int last_command, opened_item, dialogs;
static char opened_folder[OSPI_PATH], command_folder[OSPI_PATH];
void select_only(Window *w, int selected) { for (int i = 0; i < w->nitems; i++) w->items[i].selected = i == selected; }
void bring_front(Session *s, int i) {
    s->desktop_focus = 0; s->desk_sel = -1;
    Window w = s->win[i];
    memmove(s->win + i, s->win + i + 1, (size_t)(s->nwin - i - 1) * sizeof w);
    s->win[s->nwin - 1] = w;
}
int open_folder(Session *s, const char *rel) {
    snprintf(opened_folder, sizeof opened_folder, "%s", rel);
    for (int i = 0; i < s->nwin; i++) if (!strcmp(s->win[i].rel, rel)) { bring_front(s, i); return 0; }
    if (s->nwin == OSPI_MAX_WIN) return -1;
    Window *w = &s->win[s->nwin++]; memset(w, 0, sizeof *w);
    w->kind = WIN_FOLDER; snprintf(w->rel, sizeof w->rel, "%s", rel); return 0;
}
void open_item(Session *s, Window *w, int i) { (void)s; (void)w; opened_item = i; }
void dialog_info(Session *s, const char *title, const char *body) { (void)s; (void)title; (void)body; dialogs++; }
void session_command(Session *s, int command) {
    last_command = command;
    Window *w = finder_current(s);
    snprintf(command_folder, sizeof command_folder, "%s", w ? w->rel : "none");
}
int text_height(const char *text, int width) { (void)text; (void)width; return 0; }
int menu_count(void) { return 0; }
const MenuItem *menu_at(int menu, int item) { (void)menu; (void)item; return NULL; }
void menu_title_rect(int menu, Rect *r) { (void)menu; *r = (Rect){0}; }
void menu_item_rect(int menu, int item, Rect *r) { (void)menu; (void)item; *r = (Rect){0}; }
int typeface_line_height(int body) { (void)body; return 14; }
void fb_clip(Fb *fb, int x, int y, int w, int h) { fb->clip = 1; fb->cx=x; fb->cy=y; fb->cw=w; fb->ch=h; }
void fb_fill(Fb *fb, int x, int y, int w, int h, uint32_t color) {
    (void)color;
    assert(x >= 0 && y >= 20 && x + w <= fb->w && y + h <= fb->h);
}
void fb_frame(Fb *fb, int x, int y, int w, int h, uint32_t color) { fb_fill(fb,x,y,w,h,color); }
void fb_hline(Fb *fb, int x, int y, int w, uint32_t color) { fb_fill(fb,x,y,w,1,color); }
void fb_text(Fb *fb, int x, int y, uint32_t color, const char *text) { (void)fb;(void)x;(void)y;(void)color;(void)text; }

static Session *new_session(void) {
    Session *s = calloc(1, sizeof *s); assert(s);
    s->sw=1024; s->sh=768; s->nwin=2; s->desk_sel=-1;
    s->desktop.kind=WIN_FOLDER; s->desktop.desktop_surface=1; strcpy(s->desktop.rel,"Desktop Folder");
    for (int i=0;i<2;i++) {
        Window *w=&s->win[i];
        w->kind=WIN_FOLDER; w->x=50+i*400; w->y=60; w->w=350; w->h=300; w->nitems=2;
        snprintf(w->rel,sizeof w->rel,"folder%d",i);
        strcpy(w->items[0].name,"First"); strcpy(w->items[1].name,"Second");
        w->items[0].selected=1;
    }
    last_command=0; opened_item=-1; dialogs=0;
    strcpy(opened_folder,"unset"); return s;
}
static void enter_first(Session *s) { assert(context_key(s,CONTEXT_KEY_DOWN,0)); assert(context_key(s,13,0)); }
int main(void) {
    Session *s=new_session();
    /* Right click second item in back window: bring it forward and bind that item. */
    assert(context_open(s,230,120));
    assert(!strcmp(s->win[1].rel,"folder0") && s->win[1].items[1].selected);
    assert(!s->win[0].items[0].selected);
    enter_first(s); assert(opened_item==1 && !context_active());
    /* Desktop disk must never invoke a stale selected item. */
    s->win[1].items[0].selected=1;
    assert(context_open(s,968,44));
    assert(s->desk_sel==0 && !s->win[1].items[0].selected);
    enter_first(s); assert(!strcmp(opened_folder,""));
    assert(context_open(s,972,706)); enter_first(s); assert(!strcmp(opened_folder,"Trash"));
    /* Blank desktop New Folder targets the desktop despite another front folder. */
    assert(context_open(s,10,500)); enter_first(s);
    assert(last_command==CMD_NEW_FOLDER && !strcmp(command_folder,"Desktop Folder"));
    assert(context_open(s,10,500));
    context_key(s,CONTEXT_KEY_DOWN,0); context_key(s,CONTEXT_KEY_DOWN,0); context_key(s,13,0);
    assert(last_command==CMD_CLEANUP);
    assert(context_open(s,10,500));
    int previous = last_command;
    s->down = 1;
    assert(context_down(s,20,548)); /* Separator does not dispatch. */
    assert(last_command == previous && context_active());
    assert(context_down(s,900,500) && !context_active());
    assert(!context_down(s,900,500));
    assert(context_open(s,10,500)); assert(context_key(s,27,0) && !context_active());
    assert(context_open(s,10,500)); assert(!context_key(s,'q',MOD_CMD) && !context_active());
    /* Popup clamps at bottom/right, and restores incoming framebuffer clip. */
    assert(context_open(s,1023,767));
    Fb fb={.w=1024,.h=768,.clip=1,.cx=5,.cy=6,.cw=7,.ch=8};
    context_draw(s,&fb); assert(fb.clip==1 && fb.cx==5 && fb.cy==6 && fb.cw==7 && fb.ch==8);
    context_close();
    s->dialog=1; assert(!context_open(s,10,500)); s->dialog=0;
    assert(!context_open(s,10,5));
    free(s);
    s=new_session();
    /* Empty area of a folder disables Open but New Folder targets clicked folder. */
    assert(context_open(s,80,300));
    context_key(s,CONTEXT_KEY_DOWN,0); enter_first(s);
    assert(last_command==CMD_NEW_FOLDER && !strcmp(command_folder,"folder0"));
    assert(context_open(s,230,120));
    strcpy(s->win[s->nwin-1].items[1].name,"Renamed");
    enter_first(s); assert(dialogs==1 && opened_item==-1);
    assert(context_open(s,80,120)); s->nwin=0;
    enter_first(s); assert(dialogs==2);
    context_close(); free(s);
    puts("desktop context checks passed"); return 0;
}
