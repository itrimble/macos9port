#include "mac_window_int.h"

extern const uint8_t font8x8_basic[128][8]; /* Existing public-domain font. */
/* Independently authored one-window chrome. This is not Apple's WDEF or art.
 * Coordinates are global; drawing clips every pixel to the physical screen. */
typedef struct { MacRuntime *rt; int t,l,b,r; } Drawing;
static void fill(Drawing *d,int left,int top,int right,int bottom,uint8_t ink) {
    MacRuntime *rt=d->rt;
    if(left<d->l)left=d->l;if(top<d->t)top=d->t;
    if(right>d->r)right=d->r;if(bottom>d->b)bottom=d->b;
    if(left<0)left=0;if(top<0)top=0;
    if(right>rt->screen_width)right=rt->screen_width;
    if(bottom>rt->screen_height)bottom=rt->screen_height;
    uint8_t *ram=mac_cpu_ram(rt->cpu);
    for(int y=top;y<bottom;y++)for(int x=left;x<right;x++)
        ram[rt->screen_base+(uint32_t)y*rt->screen_row_bytes+(uint32_t)x]=ink;
}
static void box(Drawing *d,int l,int t,int r,int b,uint8_t ink) {
    fill(d,l,t,r,t+1,ink);fill(d,l,b-1,r,b,ink);
    fill(d,l,t,l+1,b,ink);fill(d,r-1,t,r,b,ink);
}
/* Nearest palette entry to a 16-bit RGB triple (squared distance in 8-bit RGB). */
static uint8_t nearest(const MacRuntime *rt,const uint16_t rgb[3]) {
    unsigned best=0;long best_d=-1;
    for(unsigned i=0;i<256;i++) {
        uint32_t c=rt->palette[i];
        long dr=(long)((c>>16)&255)-(rgb[0]>>8),dg=(long)((c>>8)&255)-(rgb[1]>>8),db=(long)(c&255)-(rgb[2]>>8);
        long d=dr*dr+dg*dg+db*db;if(best_d<0 || d<best_d){best_d=d;best=i;}
    }
    return (uint8_t)best;
}
static uint8_t part(const MacRuntime *rt,const MacWindowState *w,unsigned n,uint8_t fallback) {
    return (w->color_set>>n)&1 ? nearest(rt,w->color_rgb[n]) : fallback;
}
static void draw(MacRuntime *rt, int clear_content,const int16_t clip[4]) {
    Drawing canvas={rt,0,0,rt->screen_height,rt->screen_width},*d=&canvas;
    if(clip){d->t=clip[0];d->l=clip[1];d->b=clip[2];d->r=clip[3];}
    MacWindowState *w=&rt->window;if(!w->visible)return;
    int t=w->bounds[0],l=w->bounds[1],b=w->bounds[2],r=w->bounds[3];
    /* Choose nearest black/white among the current actual device colors. */
    unsigned dark=0,light=0,min=UINT32_MAX,max=0;
    for(unsigned i=0;i<256;i++) {
        uint32_t rgb=rt->palette[i];unsigned v=((rgb>>16)&255)+((rgb>>8)&255)+(rgb&255);
        if(v<min){min=v;dark=i;}if(v>=max){max=v;light=i;}
    }
    /* Window color table ('wctb') parts: 0 content background, 1 frame, 2 title text; defaults are the palette's
     * lightest and darkest entries. */
    uint8_t paper=part(rt,w,0,(uint8_t)light),frame=part(rt,w,1,(uint8_t)dark),ink=part(rt,w,2,(uint8_t)dark);
    if(clear_content)fill(d,l,t,r,b,paper);
    /* Original dBoxProc-style geometry: 7px surrounding frame, outer rule,
     * and 2px inner rule starting 2px inward; no guest content is painted.
     * Public description: Inside Macintosh I, Windows and Resources I-273. */
    if(w->proc_id==1){
        fill(d,l-7,t-7,r+7,t,(uint8_t)light);fill(d,l-7,b,r+7,b+7,(uint8_t)light);
        fill(d,l-7,t,l,b,(uint8_t)light);fill(d,r,t,r+7,b,(uint8_t)light);
        box(d,l-7,t-7,r+7,b+7,frame);
        box(d,l-5,t-5,r+5,b+5,frame);box(d,l-4,t-4,r+4,b+4,frame);return;
    }
    /* plainDBox has no title, inner border, drag, grow or close control. */
    if(w->proc_id==2){box(d,l-1,t-1,r+1,b+1,frame);return;}
    fill(d,l-1,t-20,r+1,t,(uint8_t)light);
    box(d,l-1,t-20,r+1,b+1,frame);
    fill(d,l,t-1,r,t,frame);
    if(w->active) {
        if(mac_cpu_ram(rt->cpu)[w->address+112])box(d,l+5,t-15,l+15,t-5,frame);
        if(w->proc_id==12){box(d,r-15,t-15,r-5,t-5,frame);fill(d,r-14,t-13,r-6,t-12,frame);}
    }
    int max_chars=(r-l-48)/8,n=w->title[0];if(n>max_chars)n=max_chars;
    int x=l+(r-l-n*8)/2;
    for(int i=0;i<n;i++) {
        unsigned c=w->title[i+1];if(c>=128)c='?';
        for(int y=0;y<8;y++)for(int bit=0;bit<8;bit++)
            if(font8x8_basic[c][y]&(1u<<bit))fill(d,x+i*8+bit,t-14+y,x+i*8+bit+1,t-13+y,ink);
    }
}
void mac_window_draw(MacRuntime *rt,int clear_content){draw(rt,clear_content,NULL);}
void mac_window_draw_clipped(MacRuntime *rt,const int16_t clip[4]){draw(rt,0,clip);}
int mac_window_hit(MacRuntime *rt,int x,int y) {
    MacWindowState *w=&rt->window;if(!w->address || !w->visible)return 0;
    int t=w->bounds[0],l=w->bounds[1],b=w->bounds[2],r=w->bounds[3];
    if(x>=l && x<r && y>=t && y<b)return 3; /* inContent */
    if(w->proc_id==1 || w->proc_id==2)return 0; /* Its frame has no interactive part. */
    if(x<l-1 || x>=r+1 || y<t-20 || y>=b+1)return 0;
    if(y<t && w->active) {
        if(mac_cpu_ram(rt->cpu)[w->address+112] && x>=l+5 && x<l+15 && y>=t-15 && y<t-5)return 6;
        if(w->proc_id==12 && x>=r-15 && x<r-5 && y>=t-15 && y<t-5)return 8; /* inZoomIn */
    }
    return y<t ? 4 : 0; /* inDrag; frame sides are not content. */
}
