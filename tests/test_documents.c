#include "session_int.h"
#include "draw.h"
#include "notes.h"
#include "volume_documents.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
static int failures,checks;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s dialog=%s\n",__LINE__,#c,s?s->dialog_body:"");failures++;}}while(0)
static int find(Window*w,const char*n){for(int i=0;i<w->nitems;i++)if(!strcmp(window_items(w)[i].name,n))return i;return -1;}
int main(void){
 char root[]="build/documents-XXXXXX";Session*s=NULL;CHECK(mkdtemp(root));CHECK(!volume_seed(root));s=session_create(root);CHECK(s);if(!s)return 1;
 session_command(s,CMD_CLOSE_ALL);CHECK(!open_folder(s,"Documents"));Window*w=&s->win[s->nwin-1];int i=find(w,"Notes"),x=0,y=0;CHECK(i>=0);CHECK(!ospi_item_center(s,s->nwin-1,i,&x,&y));
 session_tick(s,1000);session_down(s,x,y,1);session_up(s,x,y);session_tick(s,1100);session_down(s,x,y,2);session_up(s,x,y);
 CHECK(s->nwin==2&&s->win[1].kind==WIN_TEXT&&!strcmp(s->win[1].title,"Notes"));
 session_command(s,CMD_CLOSE);CHECK(s->nwin==1);
 s->input_now_ms=2000;session_down(s,x,y,1);session_up(s,x,y);session_down(s,x,y,2);session_up(s,x,y);
 CHECK(s->nwin==2&&s->win[1].kind==WIN_TEXT);
 session_command(s,CMD_NEW_NOTE);CHECK(notes_active(s));w=&s->win[s->nwin-1];
 CHECK(!strcmp(w->rel,"Documents/Untitled Note.txt"));
 session_text_input(s,"Hello café");session_key(s,'\r',0);session_text_input(s,"Second paragraph");
 char saved[4096];CHECK(!volume_read(root,w->rel,saved,sizeof saved));CHECK(!strcmp(saved,"Hello café\nSecond paragraph"));
#ifdef __APPLE__
 char meta_path[OSPI_PATH];snprintf(meta_path,sizeof meta_path,"%s/%s",root,w->rel);
 CHECK(!setxattr(meta_path,"user.os999.note-test","kept",4,0,0));
#endif
 session_key(s,8,0);CHECK(!strcmp(window_text(w),"Hello café\nSecond paragrap"));
#ifdef __APPLE__
 char attr[8]={0};CHECK(getxattr(meta_path,"user.os999.note-test",attr,sizeof attr,0,0)==4&&!strcmp(attr,"kept"));
#endif
 session_key(s,'a',MOD_CMD);session_text_input(s,"Replacement");CHECK(!strcmp(window_text(w),"Replacement"));
 session_set_clip(s," pasted");session_command(s,CMD_PASTE);CHECK(!strcmp(window_text(w),"Replacement pasted"));
 session_command(s,CMD_NEW_NOTE);CHECK(!strcmp(s->win[s->nwin-1].rel,"Documents/Untitled Note 2.txt"));
 char path[OSPI_PATH];snprintf(path,sizeof path,"%s/Documents/Untitled Note 2.txt",root);
 FILE*f=fopen(path,"w");CHECK(f);if(f){fputs("external",f);fclose(f);}
 session_text_input(s,"must not overwrite");CHECK(s->dialog);s->dialog=0;
 CHECK(!volume_read(root,"Documents/Untitled Note 2.txt",saved,sizeof saved)&&!strcmp(saved,"external"));
 CHECK(!session_open_text(s,"Legacy Read Me","Read Me",OS999_LEGACY_README));w=&s->win[s->nwin-1];
 CHECK(!strcmp(window_text(w),OS999_CURRENT_README));CHECK(!w->note_editable);
 CHECK(text_height(window_text(w),200)>text_height(window_text(w),600));
 CHECK(text_height("one\ntwo",600)==2*typeface_line_height(1));
 int cx,cy;text_caret("a\nb",600,2,&cx,&cy);CHECK(cx==0&&cy==typeface_line_height(1));
 CHECK(text_offset("a\nb",600,0,typeface_line_height(1))==2);
 const char*guides[]={"Getting Started","Applications Guide","Storage and Installers","Help and Limitations"};
 for(int j=0;j<4;j++){char rel[256];snprintf(rel,sizeof rel,"Documents/%s",guides[j]);CHECK(!volume_read(root,rel,saved,sizeof saved)&&strlen(saved)>200);}
 snprintf(path,sizeof path,"%s/Documents/Getting Started",root);f=fopen(path,"w");CHECK(f);if(f){fputs("User guide",f);fclose(f);}
 CHECK(!volume_seed(root));CHECK(!volume_read(root,"Documents/Getting Started",saved,sizeof saved)&&!strcmp(saved,"User guide"));
 session_command(s,CMD_NEW_NOTE);w=&s->win[s->nwin-1];snprintf(path,sizeof path,"%s/%s",root,w->rel);
 CHECK(!unlink(path));CHECK(!symlink("Getting Started",path));session_text_input(s,"bad");CHECK(s->dialog);s->dialog=0;
 CHECK(!volume_read(root,"Documents/Getting Started",saved,sizeof saved)&&!strcmp(saved,"User guide"));
 session_command(s,CMD_NEW_NOTE);w=&s->win[s->nwin-1];
 session_text_input(s,"é");session_key(s,8,0);CHECK(!window_text(w)[0]);
 session_text_input(s,"abc");Layout lay;window_layout(w,&lay);
 session_down(s,lay.content.x+8,lay.content.y+6,1);session_up(s,lay.content.x+8,lay.content.y+6);
 session_text_input(s,"start ");CHECK(!strcmp(window_text(w),"start abc"));
 session_key(s,'a',MOD_CMD);char *long_note=malloc(5001);CHECK(long_note);memset(long_note,'x',5000);long_note[5000]=0;
 session_text_input(s,long_note);CHECK(strlen(window_text(w))==5000);
 char note_path[OSPI_PATH],note_title[OSPI_NAME];snprintf(note_path,sizeof note_path,"%s",w->rel);snprintf(note_title,sizeof note_title,"%s",w->title);
 session_command(s,CMD_CLOSE);CHECK(!notes_open_document(s,note_title,note_path));
 CHECK(s->nrecent>0&&!strcmp(s->recent[0].path,note_path));
 CHECK(strlen(window_text(&s->win[s->nwin-1]))==5000&&!strcmp(window_text(&s->win[s->nwin-1]),long_note));free(long_note);
 session_command(s,CMD_CLOSE_ALL);CHECK(!open_folder(s,""));w=&s->win[s->nwin-1];i=find(w,"Read Me");CHECK(i>=0);
 CHECK(!ospi_item_center(s,s->nwin-1,i,&x,&y));s->input_now_ms=4000;session_down(s,x,y,1);session_up(s,x,y);s->input_now_ms=4100;session_down(s,x,y,2);session_up(s,x,y);
 CHECK(s->win[s->nwin-1].kind==WIN_TEXT&&!strcmp(s->win[s->nwin-1].rel,"Read Me"));
 session_destroy(s);printf("%d document checks, %d failures\n",checks,failures);return !!failures;
}
