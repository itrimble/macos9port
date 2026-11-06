#include "os999_startup.h"
#include "ospi.h"
#include <stdio.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define SPIN_MS 3300u
#define SPIN_PERIOD_MS 4800.0
#define SETTLE_MS 1500u
#define REDUCED_SPIN_MS 300u
#define REDUCED_SETTLE_MS 180u
#define HOLD_MS 190u
#define REVEAL_MS 250u

static float *depth_buffer;
static size_t depth_capacity;

void os999_startup_release(void) {
    free(depth_buffer);
    depth_buffer = NULL;
    depth_capacity = 0;
}

int os999_startup_pref_read(const char *root) {
    char path[OSPI_PATH + 32];
    snprintf(path, sizeof path, "%s/.os999-startup", root);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char value[16] = {0};
    int enabled = 1;
    if (fgets(value, sizeof value, f) && value[0] == '0') enabled = 0;
    fclose(f);
    return enabled;
}

int os999_startup_pref_write(const char *root, int enabled) {
    char path[OSPI_PATH + 32];
    snprintf(path, sizeof path, "%s/.os999-startup", root);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%d\n", enabled ? 1 : 0) > 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? 0 : -1;
}

static uint32_t mix(uint32_t a, uint32_t b, double t) {
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8) {
        int av = (int)((a >> shift) & 255u), bv = (int)((b >> shift) & 255u);
        int v = (int)(av + (bv - av) * t + 0.5);
        out |= (uint32_t)v << shift;
    }
    return out;
}

static void emblem_shadow(Fb *fb, int cx, int cy, int width, int height) {
    if (width < 1 || height < 1) return;
    for (int y = 0; y <= height; y++) {
        for (int x = -width; x <= width; x++) {
            double nx = (double)x / width, ny = (double)y / height;
            double d = nx * nx + ny * ny;
            if (d >= 1.0) continue;
            int px = cx + x, py = cy + y;
            if (px < 0 || py < 0 || px >= fb->w || py >= fb->h) continue;
            size_t at = (size_t)py * (size_t)fb->w + (size_t)px;
            fb->px[at] = mix(fb->px[at], 0xFF17243Au, (1.0 - d) * 0.22);
        }
    }
}

typedef struct { double x, y, z; uint32_t color; } BootVertex;

static uint32_t apple_light(double nx, double ny, double nz) {
    const double lx=-0.42, ly=0.50, lz=0.76;
    double ndotl=fmax(0.0,nx*lx+ny*ly+nz*lz);
    double shine=pow(ndotl,24.0);
    uint32_t green=mix(0xFF47751Fu,0xFF9CCB43u,0.58+0.18*ny);
    green=mix(0xFF26391Au,green,0.23+0.70*ndotl);
    return mix(green,0xFFFFF5CEu,shine*0.52);
}

static BootVertex apple_vertex(double x,double y,double z,double angle,
                               int cx,int cy,double scale,uint32_t color) {
    double c=cos(angle),s=sin(angle);
    double rx=x*c+z*s, rz=-x*s+z*c;
    double perspective=3.8/(3.8-rz);
    return (BootVertex){cx+rx*scale*perspective,cy-y*scale*perspective,rz,color};
}

static void apple_triangle(Fb *fb,BootVertex a,BootVertex b,BootVertex c,
                           double angle,int cx,int cy,double scale) {
    a=apple_vertex(a.x,a.y,a.z,angle,cx,cy,scale,a.color);
    b=apple_vertex(b.x,b.y,b.z,angle,cx,cy,scale,b.color);
    c=apple_vertex(c.x,c.y,c.z,angle,cx,cy,scale,c.color);
    double area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    if(fabs(area)<0.001)return;
    int x0=(int)fmax(0,floor(fmin(a.x,fmin(b.x,c.x))));
    int x1=(int)fmin(fb->w-1,ceil(fmax(a.x,fmax(b.x,c.x))));
    int y0=(int)fmax(0,floor(fmin(a.y,fmin(b.y,c.y))));
    int y1=(int)fmin(fb->h-1,ceil(fmax(a.y,fmax(b.y,c.y))));
    double ar=(a.color>>16)&255,ag=(a.color>>8)&255,ab=a.color&255;
    double br=(b.color>>16)&255,bg=(b.color>>8)&255,bb=b.color&255;
    double cr=(c.color>>16)&255,cg=(c.color>>8)&255,cb=c.color&255;
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        double wa=((b.x-x)*(c.y-y)-(b.y-y)*(c.x-x))/area;
        double wb=((c.x-x)*(a.y-y)-(c.y-y)*(a.x-x))/area;
        double wc=1.0-wa-wb;
        if(wa<-.002||wb<-.002||wc<-.002)continue;
        size_t at=(size_t)y*(size_t)fb->w+(size_t)x;
        double z=wa*a.z+wb*b.z+wc*c.z;
        if(z<=depth_buffer[at])continue;
        depth_buffer[at]=(float)z;
        fb->px[at]=0xFF000000u|((uint32_t)(wa*ar+wb*br+wc*cr)<<16)|
                   ((uint32_t)(wa*ag+wb*bg+wc*cg)<<8)|(uint32_t)(wa*ab+wb*bb+wc*cb);
    }
}

static void emblem(Fb *fb, double angle) {
    /* Original upright Granny Smith fruit mesh; not an Apple logo. */
    enum { LAT=32,LON=48 };
    BootVertex mesh[LAT+1][LON+1];
    int cx=fb->w/2,cy=fb->h/2-65;
    double scale=fmin(142.0,(double)fb->h*0.19)*0.25;
    size_t count=(size_t)fb->w*(size_t)fb->h;
    if(count>depth_capacity){
        float *grown=realloc(depth_buffer,count*sizeof *depth_buffer);
        if(!grown)return;
        depth_buffer=grown;depth_capacity=count;
    }
    for(size_t i=0;i<count;i++)depth_buffer[i]=-1.0e9f;
    emblem_shadow(fb,cx,cy+(int)(scale*.89),(int)(scale*.45),(int)(scale*.07));
    for(int i=0;i<=LAT;i++){
        double phi=-PI/2+PI*(double)i/LAT,sy=sin(phi),cp=cos(phi);
        double dent=.11*exp(-pow((phi-1.43)/.16,2.0));
        double radius=cp*(.93+.10*sy)+dent;
        for(int j=0;j<=LON;j++){
            double theta=2*PI*(double)j/LON;
            double lobe=1+.035*cos(5*theta+.2*sy);
            double x=radius*lobe*cos(theta),z=radius*lobe*sin(theta);
            double nx=x/(radius*radius+.04),ny=sy,nz=z/(radius*radius+.04);
            double n=sqrt(nx*nx+ny*ny+nz*nz);if(n>0){nx/=n;ny/=n;nz/=n;}
            double y=sy-dent+.03*cos(5*theta)*cp;
            mesh[i][j]=(BootVertex){x,y,z,apple_light(nx,ny,nz)};
        }
    }
    for(int i=0;i<LAT;i++)for(int j=0;j<LON;j++){
        BootVertex a=mesh[i][j],b=mesh[i][j+1],c=mesh[i+1][j],d=mesh[i+1][j+1];
        apple_triangle(fb,a,c,b,angle,cx,cy,scale);
        apple_triangle(fb,b,c,d,angle,cx,cy,scale);
    }
    BootVertex stem[4]={{-.018,.88,0,0xFF755331u},{.018,.88,0,0xFF977044u},
                        {.045,1.12,.01,0xFF977044u},{.075,1.12,.01,0xFF684629u}};
    apple_triangle(fb,stem[0],stem[1],stem[2],angle,cx,cy,scale);
    apple_triangle(fb,stem[1],stem[3],stem[2],angle,cx,cy,scale);
    BootVertex leaf[4]={{.03,1.00,.015,0xFF4B8628u},{.13,1.12,.02,0xFF85B83Au},
                        {.49,1.23,.025,0xFFA4D64Eu},{.27,1.07,.05,0xFF5A9A2Eu}};
    apple_triangle(fb,leaf[0],leaf[1],leaf[3],angle,cx,cy,scale);
    apple_triangle(fb,leaf[1],leaf[2],leaf[3],angle,cx,cy,scale);
}

void os999_draw_menu_apple(Fb *fb,int x,int y) {
    if(!fb)return;
    static const uint8_t half_width[]={2,4,5,6,7,7,7,7,6,6,5,4,3,2};
    const uint32_t green=0xFF78AD32u,light=0xFFA3CE55u,dark=0xFF477625u;
    fb_fill(fb,x+8,y+1,1,3,0xFF77542Fu);
    fb_fill(fb,x+9,y+2,3,1,0xFF9ECC4Eu);
    fb_fill(fb,x+8,y+3,4,1,0xFF85B83Bu);
    for(int row=0;row<14;row++){
        int width=half_width[row];
        fb_fill(fb,x+8-width,y+4+row,width*2,1,green);
        if(row>=4&&row<=10) fb_fill(fb,x+5,y+4+row,2,1,light);
        if(row>=8) fb_fill(fb,x+8+width-2,y+4+row,2,1,dark);
    }
}

static int bitmap_width_scaled(const char *text, int scale) {
    int width = 0;
    while (text && *text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        width += 8 * scale;
        text += bytes;
    }
    return width;
}

static void bitmap_draw_scaled(Fb *fb, int x, int y, uint32_t color,
                              const char *text, int scale) {
    while (text && *text) {
        int bytes = 0;
        const uint8_t *rows = NULL;
        if (!font_next(text, &bytes, &rows) || bytes <= 0) break;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                if (rows[row] & (uint8_t)(1u << col))
                    fb_fill(fb, x + col * scale, y + row * scale,
                            scale, scale, color);
            }
        }
        x += 8 * scale;
        text += bytes;
    }
}

void os999_startup_begin(Os999Startup *s, uint32_t now, int reduced_motion) {
    if (!s) return;
    *s = (Os999Startup){.phase = OS999_STARTUP_SPIN,
                        .phase_started = now,
                        .reduced_motion = reduced_motion};
}

void os999_startup_fail(Os999Startup *s, uint32_t now) {
    if (!s) return;
    s->phase = OS999_STARTUP_ERROR;
    s->phase_started = now;
    s->failed = 1;
}

int os999_startup_active(const Os999Startup *s) {
    return s && s->phase != OS999_STARTUP_INACTIVE && s->phase != OS999_STARTUP_DONE;
}

static void advance(Os999Startup *s, uint32_t now, int ready) {
    uint32_t elapsed = now - s->phase_started;
    if (s->phase == OS999_STARTUP_ERROR) {
        /* A failed intro is bounded and yields back to the desktop after a message. */
        if (elapsed >= 30000u) s->phase = OS999_STARTUP_DONE;
    } else if (s->phase == OS999_STARTUP_SPIN &&
               elapsed >= (s->reduced_motion ? REDUCED_SPIN_MS : SPIN_MS) && ready) {
        s->ready = 1; s->phase = OS999_STARTUP_SETTLE; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_SETTLE &&
               elapsed >= (s->reduced_motion ? REDUCED_SETTLE_MS : SETTLE_MS)) {
        s->phase = OS999_STARTUP_HOLD; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_HOLD && elapsed >= HOLD_MS) {
        s->phase = OS999_STARTUP_REVEAL; s->phase_started = now;
    } else if (s->phase == OS999_STARTUP_REVEAL && elapsed >= REVEAL_MS) {
        s->phase = OS999_STARTUP_DONE;
    }
}

static void render_intro(Fb *fb, const Os999Startup *s, uint32_t now) {
    for (int y = 0; y < fb->h; y++) {
        double t = (double)y / (fb->h ? fb->h : 1);
        uint32_t c = mix(0xFF344768u, 0xFF536B91u, t);
        fb_fill(fb, 0, y, fb->w, 1, c);
    }
    uint32_t elapsed = now - s->phase_started;
    double angle = 0.0;
    if (!s->reduced_motion && s->phase == OS999_STARTUP_SPIN) {
        angle = ((double)(now - s->phase_started) / SPIN_PERIOD_MS) * 2.0 * PI;
    } else if (!s->reduced_motion && s->phase == OS999_STARTUP_SETTLE) {
        double t = (double)elapsed / SETTLE_MS;
        if (t > 1.0) t = 1.0;
        double left = 2.0 * PI * (1.0 - (double)SPIN_MS / SPIN_PERIOD_MS);
        /* Match the spin's gentle velocity, then taper smoothly to a stop. */
        double stop = t + t * t - t * t * t;
        angle = 2.0 * PI * (double)SPIN_MS / SPIN_PERIOD_MS + left * stop;
    }
    emblem(fb, angle);
    const char *name = "OS999";
    const char *tagline = "Made for the fun of it.";
    int has_typeface = s->ready && typeface_startup_ready();
    int nw = has_typeface ? typeface_width(2, name, (int)strlen(name)) : bitmap_width_scaled(name, 3);
    int tw = has_typeface ? typeface_width(3, tagline, (int)strlen(tagline)) : bitmap_width_scaled(tagline, 2);
    int nx = (fb->w - nw) / 2, tx = (fb->w - tw) / 2;
    if (has_typeface) {
        typeface_draw(fb, 2, nx, fb->h / 2 + 92, 0xFFF3F5F8u, name, (int)strlen(name));
        typeface_draw(fb, 3, tx, fb->h / 2 + 146, 0xFFD6DDE8u, tagline, (int)strlen(tagline));
    } else {
        bitmap_draw_scaled(fb, nx, fb->h / 2 + 100, 0xFFF3F5F8u, name, 3);
        bitmap_draw_scaled(fb, tx, fb->h / 2 + 154, 0xFFD6DDE8u, tagline, 2);
    }
    if (s->phase == OS999_STARTUP_ERROR) {
        int x = fb->w / 2 - 275, y = fb->h / 2 - 104;
        fb_fill(fb, x + 7, y + 8, 550, 208, 0xFF26344Du);
        fb_fill(fb, x, y, 550, 208, 0xFFF4F5F7u);
        fb_frame(fb, x, y, 550, 208, 0xFF17243Au);
        fb_text(fb, x + 28, y + 26, 0xFF202B3Cu, "OS999 could not start.");
        fb_text(fb, x + 28, y + 66, 0xFF344259u, "Desktop initialization failed.");
        fb_text(fb, x + 28, y + 88, 0xFF344259u, "Check volume permissions, then retry.");
        fb_fill(fb, x + 82, y + 142, 150, 40, 0xFF526F9Du);
        fb_frame(fb, x + 82, y + 142, 150, 40, 0xFF233653u);
        fb_text(fb, x + 131, y + 158, C_WHITE, "Retry");
        fb_fill(fb, x + 318, y + 142, 150, 40, 0xFFD8DDE6u);
        fb_frame(fb, x + 318, y + 142, 150, 40, 0xFF66758Au);
        fb_text(fb, x + 371, y + 158, 0xFF202B3Cu, "Close");
    }
}

void os999_startup_frame(Os999Startup *s, uint32_t now, int ready,
                         uint32_t *pixels, const uint32_t *desktop,
                         int width, int height) {
    if (!s || !pixels || width < 1 || height < 1) return;
    advance(s, now, ready);
    Fb fb; fb_init(&fb, pixels, width, height);
    if (!os999_startup_active(s)) {
        os999_startup_release();
        if (desktop && desktop != pixels)
            memcpy(pixels, desktop, (size_t)width * (size_t)height * sizeof *pixels);
        return;
    }
    render_intro(&fb, s, now);
    if (s->phase == OS999_STARTUP_REVEAL && desktop) {
        double t = (double)(now - s->phase_started) / REVEAL_MS;
        if (t > 1.0) t = 1.0;
        size_t count = (size_t)width * (size_t)height;
        for (size_t i = 0; i < count; i++) pixels[i] = mix(pixels[i], desktop[i], t);
    }
}
