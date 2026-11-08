#include "embed_frames.h"

#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>

#include "browser_stats.h"
#include "os999_browser_ipc.h"

namespace {

id<MTLDevice> g_device;
id<MTLCommandQueue> g_queue;
mach_port_t g_desktop = MACH_PORT_NULL;

struct FrameMsg {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t surface;
  int32_t id;
  uint32_t width, height;
  uint64_t sequence;
};

id<MTLTexture> NewTexture(int w, int h, MTLPixelFormat f, IOSurfaceRef surface) {
  MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
                                                                               width:w
                                                                              height:h
                                                                           mipmapped:NO];
  d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  d.storageMode = surface ? MTLStorageModeShared : MTLStorageModePrivate;
  return surface ? [g_device newTextureWithDescriptor:d iosurface:surface plane:0]
                 : [g_device newTextureWithDescriptor:d];
}

class MacSink : public FrameSink {
 public:
  explicit MacSink(int id) : id_(id) {}
  ~MacSink() override { ReleaseRing(); }

  void Accelerated(bool popup, const CefAcceleratedPaintInfo& info) override {
    auto surface = static_cast<IOSurfaceRef>(info.shared_texture_io_surface);
    if (!g_queue || !surface) return;
    MTLPixelFormat f = info.format == CEF_COLOR_TYPE_RGBA_8888 ? MTLPixelFormatRGBA8Unorm
                                                               : MTLPixelFormatBGRA8Unorm;
    int sw = (int)IOSurfaceGetWidth(surface), sh = (int)IOSurfaceGetHeight(surface);
    CGRect v = CGRectMake(info.extra.visible_rect.x, info.extra.visible_rect.y,
                          info.extra.visible_rect.width, info.extra.visible_rect.height);
    if (CGRectIsEmpty(v)) v = CGRectMake(0, 0, sw, sh);
    v = CGRectIntersection(v, CGRectMake(0, 0, sw, sh));
    int w = (int)v.size.width, h = (int)v.size.height;
    if (w <= 0 || h <= 0) return;
    __strong id<MTLTexture>& keep = popup ? popup_tex_ : view_tex_;
    if (!keep || (int)keep.width != w || (int)keep.height != h || keep.pixelFormat != f)
      keep = NewTexture(w, h, f, nullptr);
    id<MTLTexture> src = NewTexture(sw, sh, f, surface);
    if (!src || !keep) return;
    CFTimeInterval t0 = CACurrentMediaTime();
    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:src sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake((int)v.origin.x, (int)v.origin.y, 0)
               sourceSize:MTLSizeMake(w, h, 1)
                toTexture:keep destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    // CEF reuses |surface| after this callback: finish the read now, then
    // compose from our own copies.
    Compose(cmd);
    copy_ms_ = (CACurrentMediaTime() - t0) * 1000.0;
  }

  void Software(bool, const void*, int, int) override {
    static int reported;
    if (!reported++) BrowserStats::Get().Log("software paint received; GPU path unavailable");
  }

  void Popup(bool show, const CefRect& r) override {
    popup_shown_ = show && r.width > 0 && r.height > 0;
    popup_rect_ = r;
    if (!popup_shown_) popup_tex_ = nil;
    if (view_tex_ && g_queue) Compose([g_queue commandBuffer]);  // reveal/hide now
  }

  void Target(const std::string&) override {}
  CefWindowHandle ParentHandle() override {
    if (!parent_) parent_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)];
    return (__bridge CefWindowHandle)parent_;
  }
  double LastCopyMs() const override { return copy_ms_; }

 private:
  void ReleaseRing() {
    for (int i = 0; i < 3; i++) {
      ring_tex_[i] = nil;
      if (ring_[i]) CFRelease(ring_[i]);
      ring_[i] = nullptr;
    }
  }

  bool EnsureRing(int w, int h, MTLPixelFormat f) {
    if (ring_tex_[0] && ring_w_ == w && ring_h_ == h && ring_f_ == f) return true;
    ReleaseRing();
    NSDictionary* props = @{
      (id)kIOSurfaceWidth : @(w),
      (id)kIOSurfaceHeight : @(h),
      (id)kIOSurfaceBytesPerElement : @4,
      (id)kIOSurfacePixelFormat : @(f == MTLPixelFormatRGBA8Unorm ? 'RGBA' : 'BGRA'),
    };
    for (int i = 0; i < 3; i++) {
      ring_[i] = IOSurfaceCreate((__bridge CFDictionaryRef)props);
      if (!ring_[i]) return false;
      ring_tex_[i] = NewTexture(w, h, f, ring_[i]);
      if (!ring_tex_[i]) return false;
    }
    ring_w_ = w;
    ring_h_ = h;
    ring_f_ = f;
    return true;
  }

  void Compose(id<MTLCommandBuffer> cmd) {
    int w = (int)view_tex_.width, h = (int)view_tex_.height;
    if (!EnsureRing(w, h, view_tex_.pixelFormat)) return;
    int slot = next_;
    next_ = (next_ + 1) % 3;
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:view_tex_ sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                toTexture:ring_tex_[slot] destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    if (popup_shown_ && popup_tex_ && popup_tex_.pixelFormat == view_tex_.pixelFormat) {
      // Native <select> list: clip into the view and draw it on top.
      int px = MAX(0, popup_rect_.x), py = MAX(0, popup_rect_.y);
      int pw = MIN((int)popup_tex_.width, w - px), ph = MIN((int)popup_tex_.height, h - py);
      if (pw > 0 && ph > 0)
        [blit copyFromTexture:popup_tex_ sourceSlice:0 sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(pw, ph, 1)
                    toTexture:ring_tex_[slot] destinationSlice:0 destinationLevel:0
            destinationOrigin:MTLOriginMake(px, py, 0)];
    }
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    Send(ring_[slot], w, h);
  }

  void Send(IOSurfaceRef surface, int w, int h) {
    if (g_desktop == MACH_PORT_NULL) return;
    FrameMsg msg = {};
    msg.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0) | MACH_MSGH_BITS_COMPLEX;
    msg.header.msgh_size = sizeof msg;
    msg.header.msgh_remote_port = g_desktop;
    msg.header.msgh_id = OS999B_FRAME_MSG_ID;
    msg.body.msgh_descriptor_count = 1;
    msg.surface.name = IOSurfaceCreateMachPort(surface);
    msg.surface.disposition = MACH_MSG_TYPE_MOVE_SEND;
    msg.surface.type = MACH_MSG_PORT_DESCRIPTOR;
    msg.id = id_;
    msg.width = (uint32_t)w;
    msg.height = (uint32_t)h;
    msg.sequence = ++sequence_;
    if (mach_msg(&msg.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof msg, 0, MACH_PORT_NULL,
                 0, MACH_PORT_NULL) != KERN_SUCCESS)
      mach_port_deallocate(mach_task_self(), msg.surface.name);
  }

  int id_, next_ = 0, ring_w_ = 0, ring_h_ = 0;
  MTLPixelFormat ring_f_ = MTLPixelFormatInvalid;
  IOSurfaceRef ring_[3] = {};
  id<MTLTexture> ring_tex_[3];
  id<MTLTexture> view_tex_, popup_tex_;
  bool popup_shown_ = false;
  CefRect popup_rect_;
  NSView* parent_ = nil;
  uint64_t sequence_ = 0;
  double copy_ms_ = 0;
};

}  // namespace

void FrameSink::Init(const std::string& port_name) {
  g_device = MTLCreateSystemDefaultDevice();
  g_queue = [g_device newCommandQueue];
  if (bootstrap_look_up(bootstrap_port, port_name.c_str(), &g_desktop) != KERN_SUCCESS) {
    g_desktop = MACH_PORT_NULL;
    BrowserStats::Get().Log("embed: bootstrap_look_up failed for " + port_name);
  }
}

std::unique_ptr<FrameSink> FrameSink::Create(int id) {
  return std::make_unique<MacSink>(id);
}
