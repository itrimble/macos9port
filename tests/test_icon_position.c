#include "finder_icon_position.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while(0)
static int attribute(const char *path,const char *value,size_t n) {
#ifdef __APPLE__
    return setxattr(path,"user.os999.desktop-position",value,n,0,0);
#else
    return setxattr(path,"user.os999.desktop-position",value,n,0);
#endif
}
int main(void) {
    char root[]="/tmp/os999-icon-position-XXXXXX",file[512],renamed[512],link[512],fifo[512];
    CHECK(mkdtemp(root));snprintf(file,sizeof file,"%s/Item",root);FILE *f=fopen(file,"w");CHECK(f);if(f)fclose(f);
    int x=123,y=456;CHECK(finder_icon_position_read(root,"","Item",&x,&y)==0&&x==123&&y==456);
    CHECK(!finder_icon_position_write(root,"","Item",270,150));
    CHECK(finder_icon_position_read(root,"","Item",&x,&y)==1&&x==270&&y==150);
    snprintf(renamed,sizeof renamed,"%s/Renamed",root);CHECK(!rename(file,renamed));
    CHECK(finder_icon_position_read(root,"","Renamed",&x,&y)==1&&x==270&&y==150);
    CHECK(finder_icon_position_write(root,"","Renamed",-1,0)<0);
    CHECK(finder_icon_position_write(root,"","Renamed",0,32768)<0);
    const char *invalid[]={"OS999-ICON-1 -1 5\n","OS999-ICON-1 4 32768\n","OS999-ICON-1 999999999999999999999 2\n","OS999-ICON-1 1 2\nextra","OS999-ICON-1 1 2"};
    for(size_t i=0;i<sizeof invalid/sizeof *invalid;i++){
        CHECK(!attribute(renamed,invalid[i],strlen(invalid[i])));x=7;y=8;
        CHECK(finder_icon_position_read(root,"","Renamed",&x,&y)<0&&x==7&&y==8);
    }
    const char nul[]="OS999-ICON-1 1 2\n\0junk";CHECK(!attribute(renamed,nul,sizeof nul-1));
    CHECK(finder_icon_position_read(root,"","Renamed",&x,&y)<0);
    snprintf(link,sizeof link,"%s/Link",root);CHECK(!symlink("Renamed",link));
    CHECK(finder_icon_position_write(root,"","Link",1,2)<0);
    CHECK(finder_icon_position_read(root,"","Link",&x,&y)<0);
    CHECK(finder_icon_position_write(root,"","../Renamed",1,2)<0);
    CHECK(finder_icon_position_write(root,"../","Renamed",1,2)<0);
    snprintf(fifo,sizeof fifo,"%s/Pipe",root);CHECK(!mkfifo(fifo,0600));
    CHECK(finder_icon_position_read(root,"","Pipe",&x,&y)<0);
    CHECK(!unlink(fifo));CHECK(!unlink(link));CHECK(!unlink(renamed));CHECK(!rmdir(root));
    printf("Icon positions: %d checks, %d failures\n",checks,failures);return !!failures;
}
