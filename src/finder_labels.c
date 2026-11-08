#include "finder_labels.h"
#include "volume_directory.h"
#include "session_int.h"
#include "menu_navigation.h"
#include "desktop_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <math.h>

typedef struct { char names[8][64]; uint32_t colors[8]; } Labels;
static Labels current, draft;
static int initialized, field, selected, parent_preferences;
static Session *owner;
static char hex[7][7];
static const char filename[] = ".os999-labels";
static void default_labels(Labels *labels) {
    const char *names[] = {"None", "Essential", "Hot", "In Progress", "Cool", "Personal", "Project 1", "Project 2"};
    const uint32_t colors[] = {0xffdddddd,0xfff7a9a2,0xffffcf85,0xfff9ec8c,0xffa3dba2,0xffa4c9f1,0xffd3b1ed,0xffbcbcbc};
    memset(labels, 0, sizeof *labels);
    for (int i=0;i<8;i++) { strcpy(labels->names[i],names[i]);labels->colors[i]=colors[i]; }
}
static void defaults(void) { default_labels(&current);initialized=1; }
const char *finder_label_name(int label) { if(!initialized)defaults();return current.names[label>=0 && label<=7?label:0]; }
uint32_t finder_label_color(int label) { if(!initialized)defaults();return current.colors[label>=0 && label<=7?label:0]; }
uint32_t finder_label_ink(int label) {
    uint32_t c=finder_label_color(label);
    double channels[3]={(c>>16&255u)/255.0,(c>>8&255u)/255.0,(c&255u)/255.0};
    for(int i=0;i<3;i++)channels[i]=channels[i]<=0.04045?channels[i]/12.92:pow((channels[i]+0.055)/1.055,2.4);
    double luminance=0.2126*channels[0]+0.7152*channels[1]+0.0722*channels[2];
    return (luminance+0.05)/0.05 >= 1.05/(luminance+0.05)?C_BLACK:C_WHITE;
}
static int name_valid(const char *text, int empty) {
    size_t n=strlen(text);if(n>=64 || (!n && !empty))return 0;
    const unsigned char *p=(const unsigned char *)text;int visible=0;
    while(*p) {
        unsigned c=*p++;if(c<128){if(c<32 || c==127)return 0;visible|=c!=' ';continue;}
        unsigned cp;int count;
        if(c>=0xc2 && c<=0xdf){cp=c&31;count=1;}
        else if(c>=0xe0 && c<=0xef){cp=c&15;count=2;}
        else if(c>=0xf0 && c<=0xf4){cp=c&7;count=3;}else return 0;
        for(int i=0;i<count;i++){if((*p&0xc0)!=0x80)return 0;cp=(cp<<6)|(*p++&63);}
        if((count==2 && cp<0x800)||(count==3 && cp<0x10000)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return 0;
        visible=1;
    }
    return empty || visible;
}
static int color_value(const char *text, uint32_t *out) {
    if(strlen(text)!=6)return 0;
    uint32_t v=0;
    for(int i=0;i<6;i++) { unsigned c=(unsigned char)text[i];
        int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(n<0)return 0;v=(v<<4)|(unsigned)n; }
    *out=0xff000000u|v;return 1;
}
int finder_labels_load(Session *s) {
    defaults();owner=NULL;
    int dir=volume_directory_open(s->root,"");if(dir<0)return -1;
    int fd=openat(dir,filename,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);close(dir);
    if(fd<0)return errno==ENOENT?0:-1;
    struct stat st;char text[1024];size_t used=0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>=(off_t)sizeof text){close(fd);return -1;}
    while(used<sizeof text-1){ssize_t n=read(fd,text+used,sizeof text-1-used);
        if(n<0&&errno==EINTR)continue;if(n<0){close(fd);return -1;}if(!n)break;used+=(size_t)n;}
    close(fd);text[used]=0;
    if(used!=(size_t)st.st_size||memchr(text,0,used)||strncmp(text,"OS999-LABELS-1\n",15))return -1;
    Labels next=current;char *p=text+15;
    for(int i=1;i<8;i++){
        char *end=strchr(p,'\n');if(!end)return -1;*end=0;
        char *tab=strchr(p,'\t');if(!tab)return -1;*tab=0;
        if(!color_value(p,&next.colors[i])||!name_valid(tab+1,0))return -1;
        strcpy(next.names[i],tab+1);p=end+1;
    }
    if(*p)return -1;current=next;return 1;
}
static int save(Session *s, const Labels *labels) {
    int dir=volume_directory_open(s->root,"");if(dir<0)return -1;
    char temp[80];snprintf(temp,sizeof temp,"%s.tmp.%ld",filename,(long)getpid());
    int fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0){close(dir);return -1;}
    FILE *f=fdopen(fd,"w");if(!f){close(fd);unlinkat(dir,temp,0);close(dir);return -1;}
    int error=fputs("OS999-LABELS-1\n",f)<0;
    for(int i=1;i<8;i++)if(fprintf(f,"%06X\t%s\n",labels->colors[i]&0xffffffu,labels->names[i])<0)error=1;
    if(fflush(f)||fsync(fd))error=1;if(fclose(f))error=1;
    if(!error&&renameat(dir,temp,dir,filename))error=1;
    if(error)unlinkat(dir,temp,0);close(dir);return error?-1:0;
}
static void show(Session *s,const char *title) {
    dialog_info(s,title,"");s->dialog=5;s->dialog_cmd=CMD_LABELS_APPLY;
}
static void finish(Session *s) {
    owner=NULL;s->dialog=s->dialog_cmd=0;
    if(parent_preferences){dialog_info(s,"Finder Preferences","");s->dialog=4;s->dialog_cmd=CMD_SETTINGS_APPLY;}
    parent_preferences=0;
}
int finder_labels_cancel(Session *s) {
    if(s->dialog!=5||owner!=s)return 0;finish(s);return 1;
}
void finder_labels_preferences(Session *s) {
    if(s->dialog!=4||s->settings_kind!=CMD_PREFERENCES)return;
    s->dialog=0;finder_labels_command(s,CMD_EDIT_LABELS);parent_preferences=1;
}
int finder_labels_command(Session *s,int command) {
    if(command!=CMD_EDIT_LABELS && command!=CMD_LABELS_APPLY)return 0;
    if(!s||s->dialog)return 1;
    if(command==CMD_EDIT_LABELS){
        if(!initialized)defaults();draft=current;owner=s;field=0;selected=1;parent_preferences=0;
        for(int i=0;i<7;i++)snprintf(hex[i],sizeof hex[i],"%06X",draft.colors[i+1]&0xffffffu);
        show(s,"Labels");return 1;
    }
    if(owner!=s)return 1;
    for(int i=1;i<8;i++)if(!name_valid(draft.names[i],0)||!color_value(hex[i-1],&draft.colors[i])){
        show(s,"Enter names and six-digit RGB colors");return 1;}
    if(save(s,&draft)){show(s,"Labels could not be saved");return 1;}
    current=draft;finish(s);return 1;
}
static char *editing(void) { return field%2?hex[field/2]:draft.names[field/2+1]; }
int finder_labels_text(Session *s,const char *text) {
    if(s->dialog!=5||owner!=s)return 0;
    char next[64];char *value=editing();size_t used=selected?0:strlen(value),n=strlen(text);
    if(used+n>=(field%2?7u:sizeof next))return 1;
    memcpy(next,value,used);memcpy(next+used,text,n+1);
    if(!name_valid(next,1))return 1;
    if(field%2)for(size_t i=0;i<used+n;i++)if(!strchr("0123456789abcdefABCDEF",next[i]))return 1;
    strcpy(value,next);selected=0;return 1;
}
int finder_labels_key(Session *s,int key,int mods) {
    if(s->dialog!=5||owner!=s)return 0;
    if((mods&MOD_CMD)&&(key=='a'||key=='A')){selected=1;return 1;}
    if(mods&~MOD_SHIFT)return 0;
    if(key=='\t'||key==MENU_KEY_UP||key==MENU_KEY_DOWN){
        int step=key==MENU_KEY_UP?-2:key==MENU_KEY_DOWN?2:mods&MOD_SHIFT?-1:1;
        field=(field+step+14)%14;selected=1;return 1;
    }
    if(key==8||key==127){char *value=editing();size_t n=strlen(value);
        if(selected)value[0]=0;else if(n){do{n--;}while(n&&((unsigned char)value[n]&0xc0)==0x80);value[n]=0;}
        selected=0;return 1;}
    return 0;
}
static Rect field_rect(Rect box,int which) {
    return (Rect){box.x+(which%2?360:44),box.y+58+(which/2)*30,which%2?90:300,24};
}
int finder_labels_down(Session *s,int x,int y) {
    if(s->dialog!=5||owner!=s)return 0;
    Rect box,cancel,ok;dialog_layout(s,&box,&cancel,&ok);
    Rect reset={box.x+16,ok.y,120,ok.h};
    if(rect_has(reset,x,y)){default_labels(&draft);
        for(int i=0;i<7;i++)snprintf(hex[i],sizeof hex[i],"%06X",draft.colors[i+1]&0xffffffu);
        field=0;selected=1;s->down=0;return 1;}
    for(int i=0;i<7;i++){Rect swatch={box.x+16,box.y+60+i*30,20,20};
        if(rect_has(swatch,x,y)){field=i*2+1;selected=1;s->down=0;return 1;}}
    for(int i=0;i<14;i++)if(rect_has(field_rect(box,i),x,y)){field=i;selected=1;s->down=0;return 1;}
    return 0;
}
void finder_labels_paint(Fb *fb,const Session *s,Rect box) {
    if(s->dialog!=5||owner!=s)return;
    uint32_t ink=desktop_theme_color(THEME_FOREGROUND);
    fb_frame(fb,box.x+16,box.y+box.h-36,120,22,ink);
    fb_text(fb,box.x+24,box.y+box.h-32,ink,"Use Defaults");
    fb_text(fb,box.x+44,box.y+34,ink,"Label name");fb_text(fb,box.x+360,box.y+34,ink,"RGB color");
    for(int i=0;i<7;i++){uint32_t color=draft.colors[i+1];(void)color_value(hex[i],&color);
        fb_fill(fb,box.x+16,box.y+60+i*30,20,20,color);fb_frame(fb,box.x+16,box.y+60+i*30,20,20,ink);}
    for(int i=0;i<14;i++){Rect r=field_rect(box,i);const char *text=i%2?hex[i/2]:draft.names[i/2+1];
        fb_fill(fb,r.x,r.y,r.w,r.h,C_WHITE);fb_frame(fb,r.x,r.y,r.w,r.h,C_BLACK);
        fb_clip(fb,r.x+3,r.y+2,r.w-6,r.h-4);fb_text(fb,r.x+4,r.y+5,C_BLACK,text);
        if(field==i){if(selected)fb_invert(fb,r.x+3,r.y+3,r.w-6,r.h-6);
            else {int x=r.x+5+fb_text_width(text);fb_fill(fb,x,r.y+4,1,r.h-8,C_BLACK);}}
        fb_unclip(fb);}
}
