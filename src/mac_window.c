#include "mac_window_int.h"
#include <string.h>
#include <stdlib.h>

/* One independent standard color window. Public record and behavior ABI:
 * dev.os9.ca/techpubs/mac/Toolbox/Toolbox-215.html, -227.html, -260.html.
 * No WDEF guest code or Apple assets. Overlapping windows/custom WDEFs,
 * dragging, tracking, zoom actions and complex regions fail as unsupported. */
#define WINDOW_OWNED UINT32_C(0x4f57494e)
static int fail(MacRuntime *rt,const char *s){mac_cpu_halt(rt->cpu,s);return -1;}
static int valid(MacRuntime *rt,uint32_t p,size_t n,int aligned) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && (!aligned || !(p&1)) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(rt->cpu,p,n,v);}
static void rect(MacRuntime *rt,uint32_t p,int t,int l,int b,int r) {
    wr(rt,p,2,(uint16_t)t);wr(rt,p+2,2,(uint16_t)l);wr(rt,p+4,2,(uint16_t)b);wr(rt,p+6,2,(uint16_t)r);
}
static MacHandle *record(MacRuntime *rt,uint32_t h,unsigned size) {
    if(!valid(rt,h,4,1))return NULL;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle==h && m->size>=size && valid(rt,m->pointer,m->size,1) && rd(rt,h,4)==m->pointer)return m;
    }
    return NULL;
}
static uint32_t region(MacRuntime *rt,uint32_t h) {
    MacHandle *m=record(rt,h,10);
    return m && rd(rt,m->pointer,2)==10 ? m->pointer : 0;
}
static int display(MacRuntime *rt) {
    return rt->screen_depth==8 && rt->screen_width==640 && rt->screen_height==480 &&
        rt->screen_row_bytes==640 && valid(rt,rt->screen_base,640*480,0);
}
static int window_valid(MacRuntime *rt) {
    MacWindowState *w=&rt->window;
    if(!w->address || !valid(rt,w->address,156,1) || !display(rt) ||
       !valid(rt,w->backdrop,640*480,0))return 0;
    const unsigned sizes[13]={50,26,10,10,28,28,28,2056,10,10,10,1,16};
    const unsigned offsets[7]={2,8,24,28,32,58,62};
    if((rd(rt,w->address+6,2)&0xc000)!=0xc000)return 0;
    for(unsigned i=0;i<13;i++) {
        MacHandle *m=record(rt,w->handles[i],sizes[i]);
        if(!m || m->type!=(i<8?UINT32_C(0x43505254):WINDOW_OWNED))return 0;
        if(i<8 && m->size!=sizes[i])return 0;
        for(unsigned j=0;j<i;j++)if(w->handles[j]==w->handles[i])return 0;
    }
    if(rd(rt,rd(rt,w->handles[0],4)+42,4)!=w->handles[7])return 0;
    for(unsigned i=0;i<7;i++)if(rd(rt,w->address+offsets[i],4)!=w->handles[i])return 0;
    for(unsigned i=0;i<3;i++)if(rd(rt,w->address+114+i*4,4)!=w->handles[8+i] || !region(rt,w->handles[8+i]))return 0;
    return region(rt,w->handles[2]) && region(rt,w->handles[3]);
}
static uint32_t new_handle(MacRuntime *rt,unsigned size) {
    unsigned slot=0;while(slot<MAC_RUNTIME_HANDLES && (rt->handles[slot].handle || rt->handles[slot].pointer))slot++;
    uint32_t h=mac_runtime_alloc(rt,4),p=mac_runtime_alloc(rt,size);
    wr(rt,h,4,p);rt->handles[slot]=(MacHandle){.handle=h,.pointer=p,.size=size,.type=WINDOW_OWNED};
    if(rt->handle_count<=slot)rt->handle_count=slot+1;
    return h;
}
static void activate(MacRuntime *rt,int active) {
    MacWindowState *w=&rt->window;if(w->active==active)return;
    w->active=active;wr(rt,w->address+111,1,(unsigned)active);
    /* Queue without changing the global physical modifier state. */
    if(rt->event_count<64) {
        unsigned slot=(rt->event_head+rt->event_count++)%64;
        rt->events[slot]=(MacEvent){8,(uint16_t)((rt->modifiers&~1u)|(unsigned)active),w->address,rt->ticks,rt->mouse_x,rt->mouse_y};
    }
}
static void restore_background(MacRuntime *rt) {
    MacWindowState *w=&rt->window;int frame=mac_window_frame(w->proc_id);
    int t=w->bounds[0]-mac_window_top_extent(w->proc_id),l=w->bounds[1]-frame,b=w->bounds[2]+frame,r=w->bounds[3]+frame;
    if(t<0)t=0;if(l<0)l=0;if(b>480)b=480;if(r>640)r=640;
    uint8_t *ram=mac_cpu_ram(rt->cpu);
    for(int y=t;y<b;y++)memcpy(ram+rt->screen_base+y*640+l,ram+w->backdrop+y*640+l,(size_t)(r-l));
}
static void visibility(MacRuntime *rt,int show,int notify) {
    MacWindowState *w=&rt->window;
    if(w->visible==show)return;
    if(!show){if(mac_window_front(rt)==w->address)restore_background(rt);}   /* a window under the front one changes no pixel (see mac_window_stack.c) */
    else memcpy(mac_cpu_ram(rt->cpu)+w->backdrop,mac_cpu_ram(rt->cpu)+rt->screen_base,307200);
    w->visible=show;wr(rt,w->address+110,1,(unsigned)show);
    int t=w->bounds[0],l=w->bounds[1],b=w->bounds[2],r=w->bounds[3];
    rect(rt,region(rt,w->handles[2])+2,0,0,show?b-t:0,show?r-l:0);
    rect(rt,region(rt,w->handles[10])+2,show?t:0,show?l:0,show?b:0,show?r:0);
    if(notify)activate(rt,show);
    if(show)mac_window_draw(rt,1);
}
static int create_window(MacRuntime *rt,uint32_t sp) {
    if(!valid(rt,sp,30,1))return fail(rt,"NewCWindow stack is invalid.");
    uint8_t args[26],title[256]={0};memcpy(args,mac_cpu_ram(rt->cpu)+sp,26);
    uint32_t storage=rd(rt,sp+22,4),bounds=rd(rt,sp+18,4),text=rd(rt,sp+14,4),behind=rd(rt,sp+6,4),refcon=rd(rt,sp,4);
    int proc=(int16_t)rd(rt,sp+10,2),visible=!!rd(rt,sp+12,1),close=!!rd(rt,sp+4,1);
    if(!mac_window_registry_valid(rt))return fail(rt,"Window list registry is invalid.");
    if(rt->window_count>=8)return fail(rt,"Owned window registry capacity is exhausted.");
    if(visible && mac_window_front(rt) && behind!=UINT32_MAX)return fail(rt,"A visible window created behind another visible window needs compositing.");
    if(visible && rt->event_count>=64)return fail(rt,"NewCWindow cannot publish its activation event because the queue is full.");
    if((proc!=12 && proc!=4 && proc!=2 && proc!=1) || (behind!=0 && behind!=UINT32_MAX))return fail(rt,"Only standard zoomNoGrow, noGrowDocProc, plainDBox and dBoxProc windows with native chrome are supported; custom WDEFs are unsupported.");
    if(!valid(rt,bounds,8,1) || !valid(rt,text,1,0) || !valid(rt,text,(size_t)rd(rt,text,1)+1,0) ||
       (storage && !valid(rt,storage,156,1)))return fail(rt,"NewCWindow rectangle, title, or storage exceeds guest RAM.");
    memcpy(title,mac_cpu_ram(rt->cpu)+text,(size_t)rd(rt,text,1)+1);
    int t=(int16_t)rd(rt,bounds,2),l=(int16_t)rd(rt,bounds+2,2),b=(int16_t)rd(rt,bounds+4,2),r=(int16_t)rd(rt,bounds+6,2);
    if(t<0 || l<0 || b>480 || r>640 || b-t<1 || r-l<1 || !display(rt) ||
       !rt->main_device || !valid(rt,rt->qd_globals,4,1) || !(rt->manager_flags&2))
        return fail(rt,"NewCWindow requires initialized 640x480 indexed display and bounded content rectangle.");
    if(storage && ((storage<sp+30 && storage+156>sp) ||
       (storage<rt->screen_base+307200 && storage+156>rt->screen_base) ||
       (storage<rt->qd_globals+4 && storage+156>rt->qd_globals)))
        return fail(rt,"NewCWindow storage aliases active stack, display, or QuickDraw globals.");
    if(!record(rt,rt->screen_pixmap,50) || !record(rt,rt->screen_colors,2056))
        return fail(rt,"NewCWindow source display handles are invalid.");
    if(storage) {
        for(unsigned i=0;i<rt->window_count;i++) {
            uint32_t old=rt->window_order[i];
            if(storage<old+156 && storage+156>old)return fail(rt,"New window storage overlaps an existing window record.");
        }
        if(storage<0x10000)return fail(rt,"NewCWindow storage overlaps reserved low memory.");
        for(size_t i=0;i<rt->handle_count;i++) {
            MacHandle *m=&rt->handles[i];
            int user_pointer=!m->handle && !m->type && !m->resource &&
                storage>=m->pointer && (uint64_t)storage+156<=(uint64_t)m->pointer+m->size;
            if(m->handle && storage<(uint64_t)m->handle+4 && (uint64_t)storage+156>m->handle)
                return fail(rt,"NewCWindow storage overlaps a handle master pointer.");
            if(m->pointer && !user_pointer && storage<(uint64_t)m->pointer+m->size &&
               (uint64_t)storage+156>m->pointer)
                return fail(rt,"NewCWindow storage overlaps an existing runtime record.");
        }
    }
    /* Reserve enough capacity for OpenCPort's eight handles plus five window
     * handles and all storage before touching caller memory or WindowList. */
    unsigned free_slots=0;for(unsigned i=0;i<MAC_RUNTIME_HANDLES;i++)if(!rt->handles[i].handle && !rt->handles[i].pointer)free_slots++;
    /* OpenCPort: 32+50+26+10+10+28+28+28+2056 = 2268 bytes.
     * Our five masters:20; three aligned regions:36; title:round4; state:16. */
    uint32_t need=2268+307200+20+36+((title[0]+4u)&~3u)+16+(storage?0:156);
    uint32_t ceiling=rt->heap_limit;
    if(rt->heap_high && rt->heap_high<ceiling)ceiling=rt->heap_high;
    if(ceiling>mac_cpu_ram_size(rt->cpu))ceiling=(uint32_t)mac_cpu_ram_size(rt->cpu);
    if(free_slots<(storage?13u:14u) || rt->heap_next>ceiling || need>ceiling-rt->heap_next) {
        wr(rt,sp+26,4,0);wr(rt,0x220,2,(uint16_t)-108);mac_cpu_set(rt->cpu,MAC_A7,sp+26);return 1;
    }
    if(storage && storage<rt->heap_next+need && storage+156>rt->heap_next)
        return fail(rt,"NewCWindow caller storage overlaps pending heap allocations.");
    if(!mac_window_park(rt))return fail(rt,"No owned window registry slot is free.");
    MacWindowState fresh={0};fresh.owns_storage=!storage;
    if(!storage) {
        storage=mac_runtime_alloc(rt,156);unsigned slot=0;
        while(rt->handles[slot].handle || rt->handles[slot].pointer)slot++;
        rt->handles[slot]=(MacHandle){.pointer=storage,.size=156};
        if(rt->handle_count<=slot)rt->handle_count=slot+1;
    }
    fresh.address=storage;fresh.backdrop=mac_runtime_alloc(rt,307200);
    memcpy(mac_cpu_ram(rt->cpu)+fresh.backdrop,mac_cpu_ram(rt->cpu)+rt->screen_base,307200);
    memset(mac_cpu_ram(rt->cpu)+storage,0,156);
    wr(rt,sp,4,storage);mac_cpu_set(rt->cpu,MAC_A7,sp);
    if(mac_color_port_service(rt,0xaa00)!=1)return -1; /* Capacity was preflighted. */
    memcpy(mac_cpu_ram(rt->cpu)+sp,args,26);
    const unsigned offsets[7]={2,8,24,28,32,58,62};
    for(unsigned i=0;i<7;i++)fresh.handles[i]=rd(rt,storage+offsets[i],4);
    uint32_t pm=rd(rt,fresh.handles[0],4);fresh.handles[7]=rd(rt,pm+42,4);
    for(unsigned i=8;i<11;i++){fresh.handles[i]=new_handle(rt,10);wr(rt,rd(rt,fresh.handles[i],4),2,10);}
    fresh.handles[11]=new_handle(rt,title[0]+1);fresh.handles[12]=new_handle(rt,16);
    memcpy(mac_cpu_ram(rt->cpu)+rd(rt,fresh.handles[11],4),title,title[0]+1);
    fresh.bounds[0]=(int16_t)t;fresh.bounds[1]=(int16_t)l;fresh.bounds[2]=(int16_t)b;fresh.bounds[3]=(int16_t)r;
    fresh.proc_id=(uint16_t)proc;memcpy(fresh.title,title,sizeof title);rt->window=fresh;
    rect(rt,pm+6,-t,-l,480-t,640-l);rect(rt,storage+16,0,0,b-t,r-l);
    rect(rt,region(rt,fresh.handles[2])+2,0,0,0,0);
    int frame=mac_window_frame(proc);
    rect(rt,region(rt,fresh.handles[8])+2,t-mac_window_top_extent(proc),l-frame,b+frame,r+frame);
    rect(rt,region(rt,fresh.handles[9])+2,t,l,b,r);
    uint32_t state=rd(rt,fresh.handles[12],4);rect(rt,state,t,l,b,r);rect(rt,state+8,20,0,480,640);
    wr(rt,storage+108,2,8);wr(rt,storage+112,1,(unsigned)close);wr(rt,storage+113,1,(unsigned)(proc==12)); /* spareFlag: zoom box only on zoomNoGrow */
    for(unsigned i=0;i<3;i++)wr(rt,storage+114+i*4,4,fresh.handles[8+i]);
    /* windowDefProc remains NIL: native chrome is not a guest executable
     * WDEF handle. Direct WDEF invocation/custom WDEFs are unsupported.
     * dataHandle is a genuine 16-byte WStateData (user/standard rectangles). */
    wr(rt,storage+130,4,fresh.handles[12]);wr(rt,storage+134,4,fresh.handles[11]);wr(rt,storage+138,2,title[0]*8u);
    wr(rt,storage+152,4,refcon);wr(rt,storage+68,2,1); /* application font */
    mac_window_publish(rt,behind==UINT32_MAX);visibility(rt,visible,1);
    wr(rt,sp+26,4,storage);wr(rt,0x220,2,0);mac_cpu_set(rt->cpu,MAC_A7,sp+26);return 1;
}
int mac_window_update_event(MacRuntime *rt,uint16_t mask,MacEvent *out) {
    if(!(mask&(1u<<6)))return 0;
    uint32_t front=mac_window_front(rt);
    if(!front || !mac_window_select(rt,front) || rt->window.updating)return 0;
    if(!window_valid(rt))return fail(rt,"Window update state contains invalid owned records.");
    uint32_t p=region(rt,rt->window.handles[10])+2;
    if((int16_t)rd(rt,p,2)>=(int16_t)rd(rt,p+4,2) || (int16_t)rd(rt,p+2,2)>=(int16_t)rd(rt,p+6,2))return 0;
    *out=(MacEvent){6,rt->modifiers,rt->window.address,rt->ticks,rt->mouse_x,rt->mouse_y};return 1;
}
static int close_window(MacRuntime *rt,uint32_t sp,int dispose_storage) {
    MacWindowState *w=&rt->window;
    if(dispose_storage && !w->owns_storage)return fail(rt,"DisposeWindow cannot release caller-owned window storage; use CloseWindow.");
    if(w->updating)return fail(rt,"CloseWindow cannot interrupt an unmatched BeginUpdate.");
    int was_current=rt->current_port==w->address;
    if(w->visible && !mac_window_stack_ok(rt,w->address,0))return fail(rt,"Closing a visible window that is not frontmost needs compositing.");
    if(w->visible && mac_window_front(rt)==w->address)restore_background(rt);
    if(mac_color_port_service(rt,0xaa02)!=1)return -1;
    for(unsigned i=8;i<13;i++) {
        MacHandle *m=record(rt,w->handles[i],1);wr(rt,m->handle,4,0);memset(m,0,sizeof(*m));
    }
    /* Drop stale activation events targeting the closed window. */
    unsigned keep=0;
    for(unsigned i=0;i<rt->event_count;i++) {
        MacEvent e=rt->events[(rt->event_head+i)%64];
        if((e.what==8 || e.what==6) && e.message==w->address)continue;
        rt->events[(rt->event_head+keep++)%64]=e;
    }
    rt->event_count=keep;mac_window_unlink(rt,w->address);
    if(was_current) {rt->current_port=rt->wm_port;wr(rt,rt->qd_globals,4,rt->wm_port);}
    memset(mac_cpu_ram(rt->cpu)+w->address+108,0,48);
    if(dispose_storage)for(size_t i=0;i<rt->handle_count;i++)
        if(!rt->handles[i].handle && rt->handles[i].pointer==w->address)memset(&rt->handles[i],0,sizeof(MacHandle));
    memset(w,0,sizeof(*w));
    mac_cpu_set(rt->cpu,MAC_A7,sp+4);return 1;
}
/* PaintBehind preserves content pixels while invalidating exposed content.
 * Public contract: Toolbox-285.html and Toolbox-286.html. Only the registered
 * single window and exact rectangular update unions are supported. */
static int paint_behind(MacRuntime *rt,uint32_t sp) {
    MacWindowState *w=&rt->window;
    if(rd(rt,sp+4,4)!=w->address || !window_valid(rt) ||
       mac_window_front(rt)!=w->address || !mac_window_registry_valid(rt))
        return fail(rt,"PaintBehind requires the single registered window list.");
    if(w->updating)return fail(rt,"PaintBehind inside BeginUpdate is unsupported.");
    uint32_t h=rd(rt,sp,4);MacHandle *m=record(rt,h,10);
    int window_region=w->address && (h==w->handles[8] || h==w->handles[9] || h==w->handles[10]);
    int complex_region=m && !m->resource && !m->loaded_code && m->type==0x4f52474e && rd(rt,m->pointer,2)>10;
    if(!m || m->resource || m->loaded_code || (rd(rt,m->pointer,2)!=10 && !complex_region) ||
       (m->type && m->type!=0x4f52474e &&
        !(m->type==0x43505254 && m->size==10) &&
        !(m->type==WINDOW_OWNED && m->size==10 && window_region)))
        return fail(rt,"PaintBehind requires an owned rectangular region.");
    int16_t clip[4],area[4],combined[4];
    uint32_t up=region(rt,w->handles[10]);
    int exposed_outside=-1;                       /* set for a complex region: does it reach the screen outside this window */
    if(complex_region) {
        int structure[4]={w->bounds[0]-mac_window_top_extent(w->proc_id),w->bounds[1]-mac_window_frame(w->proc_id),
                          w->bounds[2]+mac_window_frame(w->proc_id),w->bounds[3]+mac_window_frame(w->proc_id)},inside[4];
        int rc=mac_region_window_view(rt,h,structure,inside,&exposed_outside);
        if(rc<0)return -1;
        if(rc==0)return fail(rt,"PaintBehind requires an owned rectangular region.");
        /* The part inside the window is treated as its bounding box: the update area may be larger than the region strictly needs. */
        for(unsigned i=0;i<4;i++)clip[i]=(int16_t)inside[i];
    }
    for(unsigned i=0;i<4;i++) {
        if(!complex_region)clip[i]=(int16_t)rd(rt,m->pointer+2+i*2,2);
        int edge=w->bounds[i];
        area[i]=(int16_t)(i<2?(clip[i]>edge?clip[i]:edge):(clip[i]<edge?clip[i]:edge));
        combined[i]=(int16_t)rd(rt,up+2+i*2,2);
    }
    /* We do not yet own a live desktop repaint model. The creation-time
     * backdrop is only a close/hide snapshot, not authoritative desktop art.
     * Refuse any on-screen damage outside this window before touching pixels. */
    int visible_clip[4]={clip[0]>0?clip[0]:0,clip[1]>0?clip[1]:0,
                         clip[2]<480?clip[2]:480,clip[3]<640?clip[3]:640};
    int frame=mac_window_frame(w->proc_id);
    int structure_top=w->bounds[0]-mac_window_top_extent(w->proc_id);
    if(complex_region ? exposed_outside!=0 || !w->visible :
       visible_clip[0]<visible_clip[2] && visible_clip[1]<visible_clip[3] &&
       (!w->visible || visible_clip[0]<structure_top || visible_clip[1]<w->bounds[1]-frame ||
        visible_clip[2]>w->bounds[2]+frame || visible_clip[3]>w->bounds[3]+frame))
        return fail(rt,"PaintBehind exposed desktop repaint is not implemented.");
    int add=w->visible && rd(rt,0x9da,2) && area[0]<area[2] && area[1]<area[3];
    if(add) {
        if(combined[0]>=combined[2] || combined[1]>=combined[3])memcpy(combined,area,sizeof area);
        else {
            int16_t both[4],intersection[4];
            for(unsigned i=0;i<4;i++) {
                int a=area[i],b=combined[i];
                both[i]=(int16_t)(i<2?(a<b?a:b):(a>b?a:b));
                intersection[i]=(int16_t)(i<2?(a>b?a:b):(a<b?a:b));
            }
            int64_t a=(int64_t)(area[2]-area[0])*(area[3]-area[1]);
            int64_t b=(int64_t)(combined[2]-combined[0])*(combined[3]-combined[1]);
            int64_t overlap=intersection[0]<intersection[2] && intersection[1]<intersection[3]?
                (int64_t)(intersection[2]-intersection[0])*(intersection[3]-intersection[1]):0;
            if((int64_t)(both[2]-both[0])*(both[3]-both[1])!=a+b-overlap)
                return fail(rt,"PaintBehind requires a complex update-region union that is unsupported.");
            memcpy(combined,both,sizeof both);
        }
    }
    if(clip[0]<clip[2] && clip[1]<clip[3])mac_window_draw_clipped(rt,clip);
    if(add)for(unsigned i=0;i<4;i++)wr(rt,up+2+i*2,2,(uint16_t)combined[i]);
    wr(rt,0x9dc,2,1); /* PaintBehind restores PaintWhite after its internal call. */
    mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;
}
/* SizeWindow public contract: Toolbox-251. Keep rectangular-region support
 * exact: if the required update union is nonrectangular, stop before mutation. */
static int size_window(MacRuntime *rt,uint32_t sp) {
    MacWindowState *w=&rt->window;
    int height=(int16_t)rd(rt,sp+2,2),width=(int16_t)rd(rt,sp+4,2);
    int updating=rd(rt,sp,1)!=0;
    if(!width && !height){mac_cpu_set(rt->cpu,MAC_A7,sp+10);return 1;}
    if(w->updating || !mac_window_registry_valid(rt))return fail(rt,"SizeWindow requires stable owned window geometry.");
    int t=w->bounds[0],l=w->bounds[1],old_b=w->bounds[2],old_r=w->bounds[3],b=t+height,r=l+width;
    if(width<1 || height<1 || width>640 || height>480)return fail(rt,"SizeWindow exceeds supported content dimensions.");
    /* A window may be sized so that part of it lies below or right of the screen (a game can size first and move after); every pixel
     * loop below and in MoveWindow clips to the screen. The update region cannot name the off-screen part, so growing an off-screen
     * window with updating = TRUE is refused rather than losing it. */
    if((b>480 || r>640) && updating && w->visible)return fail(rt,"SizeWindow with updating on a window that extends off-screen is unsupported.");
    int local_t=(int16_t)rd(rt,w->address+16,2),local_l=(int16_t)rd(rt,w->address+18,2);
    if(local_t+height>INT16_MAX || local_l+width>INT16_MAX)return fail(rt,"SizeWindow local dimensions overflow signed words.");
    if(w->visible && mac_window_front(rt)!=w->address)return fail(rt,"SizeWindow requires the single visible window.");
    uint32_t up=region(rt,w->handles[10]),vis=region(rt,w->handles[2]);
    int update[4];for(unsigned i=0;i<4;i++)update[i]=(int16_t)rd(rt,up+2+2*i,2);
    if(updating && w->visible) {
        /* Count the exact union of the existing update rectangle and newly
         * exposed content. This avoids silently replacing an L-shape by its
         * bounding box while the region implementation remains rectangular. */
        int bounds[4]={480,640,0,0};unsigned area=0;
        if(update[0]<0 || update[1]<0 || update[2]>480 || update[3]>640)
            return fail(rt,"SizeWindow update region exceeds display bounds.");
        for(int y=0;y<480;y++)for(int x=0;x<640;x++) {
            int old_update=y>=update[0] && y<update[2] && x>=update[1] && x<update[3];
            int exposed=y>=t && y<b && x>=l && x<r && (y>=old_b || x>=old_r);
            if(!old_update && !exposed)continue;
            area++;if(y<bounds[0])bounds[0]=y;if(x<bounds[1])bounds[1]=x;
            if(y+1>bounds[2])bounds[2]=y+1;if(x+1>bounds[3])bounds[3]=x+1;
        }
        if(area && (unsigned)((bounds[2]-bounds[0])*(bounds[3]-bounds[1]))!=area)
            return fail(rt,"SizeWindow requires a complex update-region union.");
        if(area)memcpy(update,bounds,sizeof update);else memset(update,0,sizeof update);
    }
    if(w->visible) {
        int frame=mac_window_frame(w->proc_id),top=t-mac_window_top_extent(w->proc_id);
        uint8_t *ram=mac_cpu_ram(rt->cpu);unsigned light=0,max=0;
        for(unsigned i=0;i<256;i++) {uint32_t c=rt->palette[i];unsigned v=((c>>16)&255)+((c>>8)&255)+(c&255);if(v>=max){max=v;light=i;}}
        /* Restore only old pixels no longer in the new content. Retain the
         * overlapping guest image; redraw authored chrome after geometry. */
        for(int y=top<0?0:top;y<old_b+frame && y<480;y++)for(int x=l-frame<0?0:l-frame;x<old_r+frame && x<640;x++)
            if(y<t || y>=b || x<l || x>=r)ram[rt->screen_base+y*640+x]=ram[w->backdrop+y*640+x];
        for(int y=t;y<b && y<480;y++)for(int x=l;x<r && x<640;x++)if(y>=old_b || x>=old_r)ram[rt->screen_base+y*640+x]=(uint8_t)light;
    }
    w->bounds[2]=(int16_t)b;w->bounds[3]=(int16_t)r;
    rect(rt,w->address+16,local_t,local_l,local_t+height,local_l+width);
    int frame=mac_window_frame(w->proc_id);
    rect(rt,region(rt,w->handles[8])+2,t-mac_window_top_extent(w->proc_id),l-frame,b+frame,r+frame);
    rect(rt,region(rt,w->handles[9])+2,t,l,b,r);
    rect(rt,rd(rt,w->handles[12],4),t,l,b,r);
    if(w->visible)rect(rt,vis+2,local_t,local_l,local_t+height,local_l+width);
    else rect(rt,vis+2,0,0,0,0);
    if(updating && w->visible)rect(rt,up+2,update[0],update[1],update[2],update[3]);
    mac_window_draw(rt,0);mac_cpu_set(rt->cpu,MAC_A7,sp+10);return 1;
}
/* MoveWindow: Toolbox-246. Preserve local coordinates and guest pixels. */
static int move_window(MacRuntime *rt,uint32_t sp) {
    MacWindowState *w=&rt->window;int top=(int16_t)rd(rt,sp+2,2),left=(int16_t)rd(rt,sp+4,2),front=rd(rt,sp,1)!=0;
    int height=w->bounds[2]-w->bounds[0],width=w->bounds[3]-w->bounds[1];
    int dy=top-w->bounds[0],dx=left-w->bounds[1];
    if(w->updating || !mac_window_registry_valid(rt) || top<0 || left<0 || top+height>480 || left+width>640)
        return fail(rt,"MoveWindow requires stable geometry within the owned display.");
    if(w->visible && mac_window_front(rt)!=w->address)return fail(rt,"MoveWindow requires a single visible window.");
    if(!w->visible)front=0;   /* front=TRUE means SelectWindow (Toolbox-246); on a hidden window this runtime's SelectWindow does nothing, so neither does this */
    if(front && !w->active && rt->event_count>=64)return fail(rt,"MoveWindow activation queue is full.");
    uint32_t pm=rd(rt,w->handles[0],4),regions[3];int edges[3][4],bitmap[4];
    for(unsigned i=0;i<4;i++) {
        bitmap[i]=(int16_t)rd(rt,pm+6+i*2,2)-(i&1?dx:dy);
        if(bitmap[i]<INT16_MIN || bitmap[i]>INT16_MAX)return fail(rt,"MoveWindow bitmap origin overflows signed words.");
    }
    for(unsigned j=0;j<3;j++) {
        regions[j]=region(rt,w->handles[8+j]);
        for(unsigned i=0;i<4;i++)edges[j][i]=(int16_t)rd(rt,regions[j]+2+i*2,2);
        if(edges[j][0]>=edges[j][2] || edges[j][1]>=edges[j][3]){memset(edges[j],0,sizeof edges[j]);continue;}
        for(unsigned i=0;i<4;i++){edges[j][i]+=i&1?dx:dy;if(edges[j][i]<INT16_MIN || edges[j][i]>INT16_MAX)return fail(rt,"MoveWindow region coordinates overflow signed words.");}
    }
    uint8_t *pixels=NULL,*ram=mac_cpu_ram(rt->cpu);int copy_h=height,copy_w=width;
    if(w->visible) {
        pixels=calloc((size_t)width*height,1);if(!pixels)return fail(rt,"MoveWindow cannot allocate pixel snapshot.");
        /* Only the part of the window that is on the screen has pixels to carry along. */
        if(w->bounds[0]+height>480)copy_h=480-w->bounds[0];if(w->bounds[1]+width>640)copy_w=640-w->bounds[1];
        for(int y=0;y<copy_h;y++)memcpy(pixels+y*width,ram+rt->screen_base+(w->bounds[0]+y)*640+w->bounds[1],(size_t)copy_w);
        restore_background(rt);memcpy(ram+w->backdrop,ram+rt->screen_base,307200);
    }
    w->bounds[0]=(int16_t)top;w->bounds[1]=(int16_t)left;w->bounds[2]=(int16_t)(top+height);w->bounds[3]=(int16_t)(left+width);
    for(unsigned i=0;i<4;i++)wr(rt,pm+6+i*2,2,(uint16_t)bitmap[i]);
    for(unsigned j=0;j<3;j++)rect(rt,regions[j]+2,edges[j][0],edges[j][1],edges[j][2],edges[j][3]);
    rect(rt,rd(rt,w->handles[12],4),top,left,top+height,left+width);
    if(front){mac_window_to_front(rt,w->address);activate(rt,1);}
    mac_window_draw(rt,0);
    if(pixels){
        if(copy_h<height || copy_w<width) {      /* part of the window was off-screen: its pixels never existed, so the content starts out paper */
            unsigned light=0,max=0;for(unsigned i=0;i<256;i++){uint32_t c=rt->palette[i];unsigned v=((c>>16)&255)+((c>>8)&255)+(c&255);if(v>=max){max=v;light=i;}}
            for(int y=0;y<height;y++)memset(ram+rt->screen_base+(top+y)*640+left,(int)light,(size_t)width);
        }
        for(int y=0;y<copy_h;y++)memcpy(ram+rt->screen_base+(top+y)*640+left,pixels+y*width,(size_t)copy_w);free(pixels);
        if(copy_h<height || copy_w<width)mac_window_draw(rt,0);   /* the chrome over the paper fill */
    }
    mac_cpu_set(rt->cpu,MAC_A7,sp+10);return 1;
}
int mac_window_service(MacRuntime *rt,uint16_t opcode) {
    unsigned args,result=0;
    switch(opcode) {
    case 0xa91b:case 0xa91d:args=10;break;
    case 0xaa45:args=26;result=4;break;
    case 0xa924:args=0;result=4;break;
    case 0xa922:case 0xa923:case 0xa915:case 0xa916:case 0xa91f:case 0xa920:
    case 0xa909:case 0xa92d:case 0xa914:args=4;break;
    case 0xa908:args=6;break;
    case 0xa917:args=4;result=4;break;
    case 0xa80a:args=4;result=2;break;
    case 0xa918:case 0xa919:case 0xa90d:case 0xa90a:args=8;break;
    case 0xa92c:args=8;result=2;break;
    default:return 0;
    }
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(!valid(rt,sp,args+result,1))return fail(rt,"Window call stack is invalid.");
    if(opcode==0xaa45)return create_window(rt,sp);
    if(opcode==0xa90d) {
        if(!rd(rt,sp+4,4)){mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;}   /* PaintBehind(NIL, rgn): no window to start from, nothing to paint (as CalcVisBehind(NIL)) */
        if(!mac_window_select(rt,rd(rt,sp+4,4)))return fail(rt,"PaintBehind start window is not registered.");
        return paint_behind(rt,sp);
    }
    if(opcode==0xa924) {wr(rt,sp,4,mac_window_front(rt));return 1;}
    if(opcode==0xa92c) {
        uint32_t out=rd(rt,sp,4);
        if(!valid(rt,out,4,1))return fail(rt,"FindWindow output pointer is invalid.");
        uint32_t front=mac_window_front(rt);
        if(front)mac_window_select(rt,front);
        if(front && !window_valid(rt))return fail(rt,"FindWindow has invalid owned window records.");
        int mbar=(int)rd(rt,0xbaa,2),vv=(int16_t)rd(rt,sp+4,2);                    /* a point in the menu bar is inMenuBar (1), with no window, before any window is asked */
        if(mbar>0 && vv>=0 && vv<mbar){wr(rt,out,4,0);wr(rt,sp+8,2,1);mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;}
        int hit=front?mac_window_hit(rt,(int16_t)rd(rt,sp+6,2),(int16_t)rd(rt,sp+4,2)):0;
        wr(rt,out,4,hit?rt->window.address:0);wr(rt,sp+8,2,(unsigned)hit);mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;
    }
    uint32_t address=rd(rt,sp+((opcode==0xa91d || opcode==0xa91b)?6:opcode==0xa918 || opcode==0xa919 || opcode==0xa90a?4:opcode==0xa908?2:0),4);
    if(opcode==0xa90a && !address){mac_cpu_set(rt->cpu,MAC_A7,sp+8);return 1;}   /* CalcVisBehind(NIL, rgn): no window to recalculate */
    if(!mac_window_select(rt,address) || !window_valid(rt))return fail(rt,"Window call requires the live registered window and valid owned records.");
    if(opcode==0xa91d)return size_window(rt,sp);
    if(opcode==0xa91b)return move_window(rt,sp);
    MacWindowState *w=&rt->window;
    uint32_t vis=region(rt,w->handles[2]),update=region(rt,w->handles[10]);
    if(opcode==0xa909 || opcode==0xa90a) { /* CalcVis; public Toolbox-287, global content to local vis. CalcVisBehind(start, clobbered) recomputes the one window
        * there is the same way: the clobbered region is not used to narrow it. */
        if(w->updating || !mac_window_registry_valid(rt))return fail(rt,"CalcVis requires stable owned window geometry.");
        for(unsigned i=0;i<rt->window_count;i++) {
            MacWindowState *other=mac_window_find(rt,rt->window_order[i]);
            if(other && other!=w && other->visible && w->visible)
                return fail(rt,"CalcVis overlapping visible windows require complex region subtraction.");
        }
        uint32_t content=region(rt,w->handles[9]),pm=rd(rt,w->handles[0],4);
        int t=(int16_t)rd(rt,content+2,2),l=(int16_t)rd(rt,content+4,2);
        int b=(int16_t)rd(rt,content+6,2),r=(int16_t)rd(rt,content+8,2);
        unsigned menu=rd(rt,0xbaa,2);
        if(menu>480)return fail(rt,"CalcVis menu bar exceeds the owned display.");
        if(t<(int)menu)t=(int)menu;if(l<0)l=0;if(b>480)b=480;if(r>640)r=640;
        if(!w->visible || t>=b || l>=r)rect(rt,vis+2,0,0,0,0);
        else {
            int dy=(int16_t)rd(rt,pm+6,2),dx=(int16_t)rd(rt,pm+8,2);
            if(t+dy<INT16_MIN || b+dy>INT16_MAX || l+dx<INT16_MIN || r+dx>INT16_MAX)
                return fail(rt,"CalcVis local coordinates exceed signed words.");
            rect(rt,vis+2,t+dy,l+dx,b+dy,r+dx);
        }
    } else if(opcode==0xa922) {
        if(w->updating)return fail(rt,"Nested BeginUpdate is unsupported.");
        int16_t intersection[4];
        for(unsigned i=0;i<4;i++) {
            int a=(int16_t)rd(rt,vis+2+i*2,2);w->saved_vis[i]=(int16_t)a;
            int b=(int16_t)rd(rt,update+2+i*2,2)-w->bounds[i&1];
            intersection[i]=(int16_t)(i<2?(a>b?a:b):(a<b?a:b));
        }
        if(intersection[0]>=intersection[2] || intersection[1]>=intersection[3])memset(intersection,0,sizeof intersection);
        for(unsigned i=0;i<4;i++)wr(rt,vis+2+i*2,2,(uint16_t)intersection[i]);
        rect(rt,update+2,0,0,0,0);w->updating=1;
    } else if(opcode==0xa923) {
        if(!w->updating)return fail(rt,"EndUpdate requires a matching BeginUpdate.");
        for(unsigned i=0;i<4;i++)wr(rt,vis+2+i*2,2,(uint16_t)w->saved_vis[i]);w->updating=0;
    } else if(opcode==0xa915 || opcode==0xa916) {
        if(w->updating)return fail(rt,"Window visibility cannot change inside BeginUpdate.");
        int show=opcode==0xa915;
        if(show && mac_palette_check(rt,w))return -1; /* a palette we cannot realize refuses before the window shows */
        if(w->visible!=show && !mac_window_stack_ok(rt,w->address,show))return fail(rt,"Windows can only be shown or hidden in stacking order (frontmost first); anything else needs compositing.");
        if(w->active!=show && rt->event_count>=64)return fail(rt,"Window activation event queue is full.");
        visibility(rt,show,1);if(show && w->palette_handle && mac_palette_activate(rt,w))return -1;
    } else if(opcode==0xa908) { /* ShowHide(window, showFlag): visibility only; the window is neither activated nor reordered */
        if(w->updating)return fail(rt,"Window visibility cannot change inside BeginUpdate.");
        int show=rd(rt,sp,1)!=0;
        if(show && mac_palette_check(rt,w))return -1;
        if(w->visible!=show && !mac_window_stack_ok(rt,w->address,show))return fail(rt,"Windows can only be shown or hidden in stacking order (frontmost first); anything else needs compositing.");
        visibility(rt,show,0);
    } else if(opcode==0xa91f || opcode==0xa920) {
        if(opcode==0xa920 && !w->visible)mac_window_to_front(rt,w->address);
        if(w->visible){
            if(!w->active && rt->event_count>=64)return fail(rt,"Window activation event queue is full.");
            mac_window_to_front(rt,w->address);activate(rt,1);mac_window_draw(rt,0);
        }
    } else if(opcode==0xa92d || opcode==0xa914)return close_window(rt,sp,opcode==0xa914);
    else if(opcode==0xa80a)wr(rt,sp+4,2,w->proc_id&15);
    else if(opcode==0xa917)wr(rt,sp+4,4,rd(rt,address+152,4));
    else if(opcode==0xa918)wr(rt,address+152,4,rd(rt,sp,4));
    else if(opcode==0xa919) {
        uint32_t out=rd(rt,sp,4);unsigned size=w->title[0]+1;
        if(!valid(rt,out,size,0))return fail(rt,"GetWTitle output range exceeds guest RAM.");
        memmove(mac_cpu_ram(rt->cpu)+out,w->title,size);
    }
    mac_cpu_set(rt->cpu,MAC_A7,sp+args);return 1;
}
