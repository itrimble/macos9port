#include "desktop_commands.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void dialog_info(Session *s, const char *title, const char *body) {
    s->dialog = 1;
    snprintf(s->dialog_title, sizeof s->dialog_title, "%s", title);
    snprintf(s->dialog_body, sizeof s->dialog_body, "%s", body);
}
static void info(Session *s) { s->dialog = 0; assert(desktop_command(s, 420)); }
int main(void) {
    Session *s = calloc(1, sizeof *s); assert(s);
    s->desk_sel = -1; s->nwin = 1;
    Window *w = &s->win[0]; w->kind = WIN_FOLDER; w->nitems = 2;
    strcpy(w->items[0].name, "One"); strcpy(w->items[1].name, "Two");
    assert(!desktop_command_enabled(s, 422));
    assert(desktop_command(s, 421) && w->items[0].selected && w->items[1].selected);
    assert(!desktop_command_enabled(s, 420));
    assert(desktop_command(s, 422));
    assert(s->clipboard_write_request && !strcmp(s->clipboard_out, "One\nTwo"));
    w->kind = WIN_TEXT; strcpy(w->text, "Entire\ntext\n");
    assert(!desktop_command_enabled(s, 421));
    assert(desktop_command(s, 422) && !strcmp(s->clipboard_out, w->text));
    w->kind = WIN_FOLDER; w->nitems = OSPI_MAX_ITEMS;
    for (int i = 0; i < w->nitems; i++) { memset(w->items[i].name, 'x', OSPI_NAME - 1); w->items[i].name[OSPI_NAME - 1] = 0; w->items[i].selected = 1; }
    strcpy(s->clipboard_out, "preserved"); s->clipboard_write_request = 0;
    assert(desktop_command(s, 422) && !s->dialog && s->clipboard_write_request);
    assert(strlen(session_clipboard_output(s)) == OSPI_MAX_ITEMS * OSPI_NAME - 1);
    s->clipboard_write_request = 0;
    memset(w->items[0].name, 'x', sizeof w->items[0].name);
    assert(desktop_command(s, 422) && s->dialog && !s->clipboard_write_request);
    assert(strlen(session_clipboard_output(s)) == OSPI_MAX_ITEMS * OSPI_NAME - 1);
    char root[] = "/tmp/os999-command-test.XXXXXX"; assert(mkdtemp(root));
    snprintf(s->root, sizeof s->root, "%s", root);
    char path[1024], outside[] = "/tmp/os999-outside-test.XXXXXX";
    int fd = mkstemp(outside); assert(fd >= 0); assert(write(fd,"outside-secret",14)==14); close(fd);
    snprintf(path, sizeof path, "%s/One", root);
    FILE *f = fopen(path, "wb"); assert(f); assert(fwrite("abc",1,3,f)==3); fclose(f);
    memset(w,0,sizeof *w); w->kind=WIN_FOLDER; w->nitems=1;
    strcpy(w->items[0].name,"One"); w->items[0].selected=1; w->items[0].kind=ITEM_FILE;
    info(s); assert(strstr(s->dialog_body,"3 bytes") && !strstr(s->dialog_body,root));
    w->items[0].selected=0; info(s); assert(strstr(s->dialog_body,"Volume"));
    snprintf(path,sizeof path,"%s/Link",root); assert(!symlink(outside,path));
    strcpy(w->items[0].name,"Link"); w->items[0].selected=1;
    info(s); assert(strstr(s->dialog_body,"not followed") && !strstr(s->dialog_body,"outside-secret"));
    strcpy(w->rel,"Link"); strcpy(w->items[0].name,"child");
    info(s); assert(strstr(s->dialog_body,"unavailable"));
    strcpy(w->rel,"../"); info(s); assert(strstr(s->dialog_body,"unavailable"));
    strcpy(w->rel,""); strcpy(w->items[0].name,"../outside"); info(s); assert(strstr(s->dialog_body,"unavailable"));
    s->desk_sel=0; info(s); assert(strstr(s->dialog_body,"Volume"));
    s->dialog=0; assert(!desktop_command_enabled(s,421) && !desktop_command_enabled(s,422));
    snprintf(path,sizeof path,"%s/Trash",root); assert(!mkdir(path,0700));
    s->desk_sel=1; info(s); assert(strstr(s->dialog_body,"Name: Trash"));
    s->nwin=0; s->desk_sel=-1; s->dialog=0; assert(!desktop_command_enabled(s,420));
    assert(!desktop_command(s,423)); assert(!desktop_command_enabled(NULL,420));
    rmdir(path); snprintf(path,sizeof path,"%s/One",root); unlink(path);
    snprintf(path,sizeof path,"%s/Link",root); unlink(path); unlink(outside); rmdir(root); free(s->clipboard_large_out); free(s);
    puts("desktop command checks passed"); return 0;
}
