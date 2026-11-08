#include "desktop_info.h"
#include "session_int.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
int main(void) {
    char root[]="/tmp/os999-desktop-info-XXXXXX";
    CHECK(mkdtemp(root)); CHECK(!volume_seed(root));
    Session *s=session_create(root); CHECK(s); if(!s)return 1;
    uint32_t *pixels=malloc(OSPI_W*OSPI_H*sizeof *pixels);CHECK(pixels);if(!pixels)return 1;
    Fb fb;fb_init(&fb,pixels,OSPI_W,OSPI_H);
    const char *names[]={"ChicagoFLF","Meslo Nerd Font Regular","Meslo Nerd Font Bold"};
    unsigned long hashes[3]={0};
    for(int i=0;i<3;i++) {
        char rel[OSPI_PATH];snprintf(rel,sizeof rel,"System Folder/Fonts/%s",names[i]);
        CHECK(desktop_info_open(s,rel)==1);
        Window *w=&s->win[s->nwin-1];Layout lay;window_layout(w,&lay);
        CHECK(!strcmp(w->title,names[i]));
#ifdef OSPI_FREETYPE
        CHECK(desktop_info_font_ready(i));
#else
        CHECK(!desktop_info_font_ready(i));
#endif
        fb_fill(&fb,0,0,fb.w,fb.h,C_WHITE);fb_clip(&fb,lay.content.x,lay.content.y,lay.content.w,lay.content.h);
        CHECK(desktop_info_paint(&fb,w,&lay));fb_unclip(&fb);
        int changed=0;
        for(int y=lay.content.y+30;y<lay.content.y+130;y++)
            for(int x=lay.content.x+10;x<lay.content.x+250;x++) {
                uint32_t v=pixels[y*OSPI_W+x];if(v!=C_WHITE)changed++;
                hashes[i]=hashes[i]*33+v;
            }
        CHECK(changed>80);close_index(s,s->nwin-1);
    }

#ifdef OSPI_FREETYPE
    CHECK(hashes[0]!=hashes[1] && hashes[1]!=hashes[2]);
#endif
    CHECK(!desktop_info_open(s,"Documents/ChicagoFLF"));
    char path[OSPI_PATH];snprintf(path,sizeof path,"%s/System Folder/Fonts/ChicagoFLF",root);
    FILE *f=fopen(path,"w");CHECK(f);if(f){fputs("user document",f);fclose(f);}
    CHECK(!volume_seed(root));CHECK(!desktop_info_open(s,"System Folder/Fonts/ChicagoFLF"));
    CHECK(!unlink(path));CHECK(!mkfifo(path,0600));
    CHECK(!desktop_info_open(s,"System Folder/Fonts/ChicagoFLF"));CHECK(!unlink(path));
    CHECK(!symlink("../About This Folder",path));
    CHECK(!desktop_info_open(s,"System Folder/Fonts/ChicagoFLF"));
    session_command(s,CMD_ABOUT);Window *w=&s->win[s->nwin-1];Layout lay;window_layout(w,&lay);
    CHECK(!strcmp(w->rel,":about"));
    CHECK(!desktop_info_click(s,w,lay.content.x+10,lay.content.y+10));CHECK(!s->browser_request);
    int x=lay.content.x+20,y=lay.content.y+lay.content.h-20;
    session_down(s,x,y,1);session_up(s,x,y);
    CHECK(s->browser_request);CHECK(!strcmp(s->browser_open_url,"https://apps.remnantsecurity.com"));
    s->browser_request=1;strcpy(s->browser_open_url,"https://pending.invalid");s->net_connect_via=2;
    CHECK(desktop_info_click(s,w,x,y));CHECK(!s->browser_request && !s->browser_open_url[0]);
    free(pixels);session_destroy(s);CHECK(!archive_remove_tree(root));
    printf("Desktop font/About regression: %d failures\n",failures);return !!failures;
}
