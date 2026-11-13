#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include <SDL_syswm.h>
#include <mach/mach.h>
#include <pthread.h>
#include <servers/bootstrap.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "browser_overlay.h"
#include "os999_browser_ipc.h"

enum { SLOTS = 4 };

/* Click-through: SDL keeps receiving every event; ospi routes them. */
@interface OS999OverlayView : NSView
@end
@implementation OS999OverlayView
- (NSView *)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}
@end

typedef struct {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t surface;
    int32_t id;
    uint32_t width, height;
    uint64_t sequence;
    mach_msg_max_trailer_t trailer;
} FrameRecv;

static OS999OverlayView *g_view[SLOTS];
static CALayer *g_content[SLOTS];
static CAShapeLayer *g_mask[SLOTS];
static IOSurfaceRef g_current[SLOTS];
static atomic_int g_bound[SLOTS];
static atomic_uint_least64_t g_frames[SLOTS];
static int g_order[SLOTS];
static mach_port_t g_port = MACH_PORT_NULL;
static char g_name[128];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void *receive_frames(void *arg) {
    (void)arg;
    for (;;) {
        FrameRecv m;
        kern_return_t kr = mach_msg(&m.header, MACH_RCV_MSG, 0, sizeof m, g_port,
                                    MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) {
            if (kr == MACH_RCV_INVALID_NAME || kr == MACH_RCV_PORT_DIED) break;
            continue;
        }
        if (m.header.msgh_id != OS999B_FRAME_MSG_ID ||
            !(m.header.msgh_bits & MACH_MSGH_BITS_COMPLEX)) {
            mach_msg_destroy(&m.header);
            continue;
        }
        IOSurfaceRef surface = IOSurfaceLookupFromMachPort(m.surface.name);
        mach_port_deallocate(mach_task_self(), m.surface.name);
        if (!surface) continue;
        int slot = -1;
        for (int i = 0; i < SLOTS; i++) if (atomic_load(&g_bound[i]) == m.id) slot = i;
        pthread_mutex_lock(&g_lock);
        if (slot < 0 || !g_content[slot]) {
            pthread_mutex_unlock(&g_lock);
            CFRelease(surface);
            continue;
        }
        /* GPU-only hand-off: Core Animation samples the helper's surface. */
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        g_content[slot].contents = (__bridge id)surface;
        [CATransaction commit];
        [CATransaction flush];
        if (g_current[slot]) CFRelease(g_current[slot]);
        g_current[slot] = surface;
        pthread_mutex_unlock(&g_lock);
        atomic_fetch_add(&g_frames[slot], 1);
    }
    return NULL;
}

int overlay_listen(char *name, size_t n) {
    if (g_port == MACH_PORT_NULL) {
        snprintf(g_name, sizeof g_name, "org.remnantsecurity.os999.browser.%d", (int)getpid());
        kern_return_t kr = bootstrap_check_in(bootstrap_port, g_name, &g_port);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "OS999 Browser: bootstrap_check_in failed (%d)\n", kr);
            g_port = MACH_PORT_NULL;
            return -1;
        }
        pthread_t thread;
        if (pthread_create(&thread, NULL, receive_frames, NULL) != 0) return -1;
        pthread_detach(thread);
    }
    snprintf(name, n, "%s", g_name);
    return 0;
}

void overlay_window_open(int slot, char *target, size_t n) {
    if (n) target[0] = 0;  /* frames come over Mach, not a named surface */
    if (slot >= 0 && slot < SLOTS) atomic_store(&g_frames[slot], 0);
}

void overlay_bind(int slot, int id) {
    if (slot < 0 || slot >= SLOTS) return;
    if (atomic_exchange(&g_bound[slot], id) == id) return;
    /* A different tab: never show the previous tab's last frame. */
    pthread_mutex_lock(&g_lock);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    g_content[slot].contents = nil;
    [CATransaction commit];
    if (g_current[slot]) CFRelease(g_current[slot]);
    g_current[slot] = NULL;
    pthread_mutex_unlock(&g_lock);
}

void overlay_window_close(int slot) {
    if (slot < 0 || slot >= SLOTS) return;
    atomic_store(&g_bound[slot], 0);
    pthread_mutex_lock(&g_lock);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    g_content[slot].contents = nil;
    g_view[slot].hidden = YES;
    [CATransaction commit];
    if (g_current[slot]) CFRelease(g_current[slot]);
    g_current[slot] = NULL;
    pthread_mutex_unlock(&g_lock);
    atomic_store(&g_frames[slot], 0);
}

static NSWindow *cocoa_window(SDL_Window *win) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_COCOA) return nil;
    return info.info.cocoa.window;
}

float overlay_scale(SDL_Window *win, int logical_w) {
    NSWindow *w = cocoa_window(win);
    int ww = logical_w, wh = 0;
    SDL_GetWindowSize(win, &ww, &wh);
    float backing = w ? (float)w.backingScaleFactor : 2.0f;
    return backing * (float)ww / (float)logical_w;
}

static void make_view(int slot, NSView *cv) {
    g_view[slot] = [[OS999OverlayView alloc] initWithFrame:NSZeroRect];
    CALayer *root = [CALayer layer];
    g_view[slot].layer = root;  /* layer-hosting: we own this tree */
    g_view[slot].wantsLayer = YES;
    CALayer *content = [CALayer layer];
    content.anchorPoint = CGPointZero;
    content.contentsGravity = kCAGravityResize;
    content.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    [root addSublayer:content];
    g_mask[slot] = [CAShapeLayer layer];
    g_mask[slot].fillRule = kCAFillRuleEvenOdd;
    pthread_mutex_lock(&g_lock);
    g_content[slot] = content;
    pthread_mutex_unlock(&g_lock);
    g_order[slot] = -1;
    [cv addSubview:g_view[slot] positioned:NSWindowAbove relativeTo:nil];
}

typedef struct { CGMutablePathRef path; OverlayRect content; double sx, sy; } MaskContext;
static void add_visible_region(OverlayRect r, void *opaque) {
    MaskContext *c = opaque;
    CGPathAddRect(c->path, NULL, CGRectMake((r.x - c->content.x) * c->sx,
        (c->content.y + c->content.h - r.y - r.h) * c->sy, r.w * c->sx, r.h * c->sy));
}

void overlay_place(SDL_Window *win, int lw, int lh, int slot, int visible, int order,
                   OverlayRect r, const OverlayRect *holes, int nholes) {
    NSWindow *w = cocoa_window(win);
    if (!w || slot < 0 || slot >= SLOTS) return;
    NSView *cv = w.contentView;
    if (!g_view[slot]) make_view(slot, cv);
    if (g_order[slot] != order) {  /* keep layer stacking equal to window order */
        g_order[slot] = order;
        for (int o = 0; o < SLOTS * 2; o++)
            for (int i = 0; i < SLOTS; i++)
                if (g_view[i] && g_order[i] == o)
                    [cv addSubview:g_view[i] positioned:NSWindowAbove relativeTo:nil];
    }
    int ww = lw, wh = lh;
    SDL_GetWindowSize(win, &ww, &wh);
    double sx = (double)ww / lw, sy = (double)wh / lh;
    double H = NSHeight(cv.bounds);
    NSRect f = NSMakeRect(r.x * sx, cv.isFlipped ? r.y * sy : H - (r.y + r.h) * sy,
                          r.w * sx, r.h * sy);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    g_view[slot].hidden = !visible || r.w <= 0 || r.h <= 0;
    if (!NSEqualRects(g_view[slot].frame, f)) g_view[slot].frame = f;
    CGRect bounds = CGRectMake(0, 0, f.size.width, f.size.height);
    g_content[slot].frame = bounds;
    g_content[slot].contentsScale = w.backingScaleFactor;
    if (nholes > 0) {
        CGMutablePathRef path = CGPathCreateMutable();
        MaskContext context = {path, r, sx, sy};
        overlay_visible_regions(r, holes, nholes, add_visible_region, &context);
        g_mask[slot].frame = bounds;
        g_mask[slot].path = path;
        CGPathRelease(path);
        g_view[slot].layer.mask = g_mask[slot];
    } else {
        g_view[slot].layer.mask = nil;
    }
    [CATransaction commit];
}

void overlay_draw_cpu(int slot, uint32_t *frame, int fw, int fh, OverlayRect content) {
    (void)slot; (void)frame; (void)fw; (void)fh; (void)content;  /* GPU path */
}

uint64_t overlay_frames(int slot) {
    return slot >= 0 && slot < SLOTS ? atomic_load(&g_frames[slot]) : 0;
}

int overlay_probe(int slot, int *w, int *h, SurfaceStats *out) {
    if (slot < 0 || slot >= SLOTS || !out) return -1;
    int rc = -1;
    pthread_mutex_lock(&g_lock);
    IOSurfaceRef s = g_current[slot];
    if (s && IOSurfaceLock(s, kIOSurfaceLockReadOnly, NULL) == kIOReturnSuccess) {
        int sw = (int)IOSurfaceGetWidth(s), sh = (int)IOSurfaceGetHeight(s);
        rc = frame_surface_stats(IOSurfaceGetBaseAddress(s), sw, sh, IOSurfaceGetBytesPerRow(s), out);
        if (rc == 0) { if (w) *w = sw; if (h) *h = sh; }
        IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, NULL);
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
