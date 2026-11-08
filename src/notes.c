#include "notes.h"
#include "finder_recent.h"
#include "session_int.h"
#include "volume_directory.h"
#include "draw.h"
#include "desktop_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#endif

#define NOTE_LIMIT 65535
static Window *front(const Session *s) {
    if (!s || s->desktop_focus || s->dialog || s->menu >= 0 || !s->nwin) return NULL;
    Window *w = (Window *)&s->win[s->nwin - 1];
    if (s->browser_focused || s->guest_focused || s->media_focused || session_overlay_active(s)) return NULL;
    return w->kind == WIN_TEXT && !w->collapsed && w->note_editable ? w : NULL;
}
int notes_active(const Session *s) { return front(s) != NULL; }
static int directory(const Session *s, const Window *w, char *name) {
    char rel[OSPI_PATH]; snprintf(rel,sizeof rel,"%s",w->rel);
    char *leaf = strrchr(rel,'/');
    if (!leaf) return -1;
    snprintf(name,OSPI_NAME,"%s",leaf+1); *leaf=0;
    if (!name[0] || name[0]=='.') return -1;
    return volume_directory_open(s->root,rel);
}
/* Compare the entire saved text before replacing it. A changed or linked file
 * is preserved; failed edits leave both the on-screen and stored text intact. */
static int save(Session *s, Window *w, const char *text) {
    char name[OSPI_NAME], temp[80];
    int dir=directory(s,w,name); if(dir<0)return -1;
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    struct stat st;size_t old_n=strlen(window_text(w));int ok=0;
    char *old=malloc(old_n+1);
    if(fd>=0 && old && !fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_nlink==1 &&
       st.st_size==(off_t)old_n && read(fd,old,old_n)==(ssize_t)old_n && !memcmp(old,window_text(w),old_n))ok=1;
    free(old);if(fd>=0)close(fd);
    if(!ok){close(dir);return -1;}
    int out=-1;
    for(int i=0;i<100&&out<0;i++){
        snprintf(temp,sizeof temp,".os999-note-%ld-%d",(long)getpid(),i);
        out=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(out<0&&errno!=EEXIST)break;
    }
    if(out<0){close(dir);return -1;}
#ifdef __APPLE__
    int source=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(source<0 || fcopyfile(source,out,NULL,COPYFILE_XATTR)!=0){
        if(source>=0)close(source);close(out);unlinkat(dir,temp,0);close(dir);return -1;
    }
    close(source);
#endif
    size_t n=strlen(text);ssize_t written=write(out,text,n);
    ok=written==(ssize_t)n && fchmod(out,st.st_mode&0777)==0 && fsync(out)==0;
    if(close(out))ok=0;
    struct stat live;
    if(fstatat(dir,name,&live,AT_SYMLINK_NOFOLLOW) || live.st_dev!=st.st_dev || live.st_ino!=st.st_ino ||
       live.st_size!=st.st_size)ok=0;
#ifdef __APPLE__
    if(ok && (live.st_mtimespec.tv_sec!=st.st_mtimespec.tv_sec || live.st_mtimespec.tv_nsec!=st.st_mtimespec.tv_nsec))ok=0;
#else
    if(ok && (live.st_mtim.tv_sec!=st.st_mtim.tv_sec || live.st_mtim.tv_nsec!=st.st_mtim.tv_nsec))ok=0;
#endif
    if(ok)ok=renameat(dir,temp,dir,name)==0;
    if(!ok)unlinkat(dir,temp,0);
    close(dir);return ok?0:-1;
}
static int replace(Session*s,Window*w,size_t from,size_t to,const char*insert){
    size_t len=strlen(window_text(w)),n=strlen(insert);
    if(from>to||to>len||n>NOTE_LIMIT-(len-(to-from)))return -1;
    char*next=malloc(len-(to-from)+n+1);if(!next)return -1;
    memcpy(next,window_text(w),from);memcpy(next+from,insert,n);
    memcpy(next+from+n,window_text(w)+to,len-to+1);
    /* Allocate the display replacement before committing to disk. */
    Window copy={0};int rc=window_text_set(&copy,next);
    if(!rc)rc=save(s,w,next);
    if(!rc){free(w->expanded_text);w->expanded_text=copy.expanded_text;copy.expanded_text=NULL;
        memcpy(w->text,copy.text,sizeof w->text);w->note_caret=from+n;w->note_select_all=0;
        Layout lay;window_layout(w,&lay);int x,y;text_caret(window_text(w),lay.content.w-12,w->note_caret,&x,&y);
        if(y<w->scroll)w->scroll=y;if(y>w->scroll+lay.content.h-24)w->scroll=y-lay.content.h+24;
        session_clamp(s);
    }
    free(copy.expanded_text);free(next);return rc;
}
int notes_input(Session*s,const char*text){
    Window*w=front(s);if(!w||!text)return 0;
    for(const unsigned char*p=(const unsigned char*)text;*p;p++)if((*p<32&&*p!='\n'&&*p!='\t')||*p==127)return 1;
    size_t n=strlen(window_text(w));if(w->note_caret>n)w->note_caret=n;
    if(replace(s,w,w->note_select_all?0:w->note_caret,w->note_select_all?n:w->note_caret,text))
        dialog_info(s,"Note not saved","The note changed on disk or could not be written. Your saved file was preserved. Reopen it before editing again.");
    return 1;
}
int notes_key(Session*s,int key,int mods){
    Window*w=front(s);if(!w)return 0;
    if(mods&MOD_CMD){
        if(key=='a'||key=='A'){w->note_select_all=1;return 1;}
        if(key=='v'||key=='V'){
            if(s->clipboard_read_error)dialog_info(s,"Paste","The clipboard could not be read. The note was not changed.");
            else notes_input(s,session_clipboard_input(s));
            return 1;
        }
        return 0;
    }
    if(key=='\r')return notes_input(s,"\n");
    size_t n=strlen(window_text(w)),pos=w->note_caret;if(pos>n)pos=n;
    if(key==8||key==127){
        if(w->note_select_all)return notes_input(s,"");
        if(!pos)return 1;
        size_t start=pos-1;while(start&&((unsigned char)window_text(w)[start]&0xc0)==0x80)start--;
        if(replace(s,w,start,pos,""))dialog_info(s,"Note not saved","The saved note was preserved. Reopen it before editing again.");return 1;
    }
    if(key==1073741904){if(pos){pos--;while(pos&&((unsigned char)window_text(w)[pos]&0xc0)==0x80)pos--;}}
    else if(key==1073741903){if(pos<n){pos++;while(pos<n&&((unsigned char)window_text(w)[pos]&0xc0)==0x80)pos++;}}
    else if(key==1073741898)pos=0;
    else if(key==1073741901)pos=n;
    else return 0;
    w->note_caret=pos;w->note_select_all=0;return 1;
}
void notes_new(Session*s){
    if(s->nwin>=OSPI_MAX_WIN){dialog_info(s,"New Note","Close a window first.");return;}
    int dir=volume_directory_open(s->root,"Documents");
    if(dir<0){dialog_info(s,"New Note","The Documents folder is unavailable.");return;}
    char name[OSPI_NAME],rel[OSPI_PATH];int fd=-1;
    for(int i=1;i<=1000;i++){
        snprintf(name,sizeof name,i==1?"Untitled Note.txt":"Untitled Note %d.txt",i);
        fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
        if(fd>=0||errno!=EEXIST)break;
    }
    if(fd<0){close(dir);dialog_info(s,"New Note","Could not create a new note.");return;}
    close(fd);close(dir);snprintf(rel,sizeof rel,"Documents/%s",name);
    if(session_open_text(s,name,rel,""))dialog_info(s,"New Note","The note was created in Documents. Close a window to open it.");
    reload_folders(s);
}
/* Return 1 for other documents so their existing viewer remains in charge. */
int notes_open_document(Session *s, const char *title, const char *rel) {
    if (!s) return -1;
    size_t length = rel ? strlen(rel) : 0;
    if (!rel || length >= OSPI_PATH || (strcmp(rel, "Documents/Notes") &&
        (strncmp(rel, "Documents/", 10) || length < 5 || strcmp(rel + length - 4, ".txt")))) return 1;
    Window location = {0};
    snprintf(location.rel, sizeof location.rel, "%s", rel);
    char name[OSPI_NAME];
    int dir = directory(s, &location, name);
    int fd = dir < 0 ? -1 : openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (dir >= 0) close(dir);
    struct stat st;
    char *body = NULL;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > NOTE_LIMIT)
        goto fail;
    size_t size = (size_t)st.st_size;
    body = malloc(size + 1);
    if (!body) goto fail;
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, body + got, size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto fail;
        got += (size_t)n;
    }
    body[size] = 0;
    if (memchr(body, 0, size)) goto fail;
    close(fd); fd = -1;
    if (session_open_text(s, title, rel, body)) goto fail;
    free(body); finder_recent_note(s, rel, 0); return 0;
fail:
    if (fd >= 0) close(fd);
    free(body);
    dialog_info(s, "Open Note", "Could not read this plain-text note safely. Notes support up to 65,535 bytes; the saved file was not changed.");
    return -1;
}

int notes_down(Session*s,int x,int y){
    Window*w=front(s);if(!w)return 0;Layout lay;window_layout(w,&lay);
    if(!rect_has(lay.content,x,y))return 0;
    w->note_caret=text_offset(window_text(w),lay.content.w-12,x-lay.content.x-8,y-lay.content.y-6+w->scroll);
    w->note_select_all=0;return 1;
}
void notes_paint(struct Fb*fb,const Session*s,const Window*w,const Layout*lay){
    if(!w->note_editable||!s->nwin||w!=&s->win[s->nwin-1]||s->desktop_focus)return;
    int x,y;text_caret(window_text(w),lay->content.w-12,w->note_caret,&x,&y);
    fb_fill(fb,lay->content.x+8+x,lay->content.y+6+y-w->scroll,1,typeface_line_height(1),desktop_theme_color(THEME_FOREGROUND));
}
