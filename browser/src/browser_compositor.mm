#import "browser_compositor.h"

#import <Metal/Metal.h>

#include "include/internal/cef_types.h"

enum { kRing = 3 };

@interface OS999Plane : NSObject {
 @public
  IOSurfaceRef surfaces[kRing];
  id<MTLTexture> textures[kRing];
  int next, width, height;
  MTLPixelFormat format;
}
@property(nonatomic, strong) CALayer* layer;
@end

@implementation OS999Plane
- (void)reset {
  for (int i = 0; i < kRing; i++) {
    textures[i] = nil;
    if (surfaces[i]) CFRelease(surfaces[i]);
    surfaces[i] = NULL;
  }
  width = height = 0;
}
- (void)dealloc {
  [self reset];
}
- (BOOL)ensure:(id<MTLDevice>)device w:(int)w h:(int)h f:(MTLPixelFormat)f {
  if (w == width && h == height && f == format && textures[0]) return YES;
  [self reset];
  uint32_t fourcc = f == MTLPixelFormatRGBA8Unorm ? 'RGBA' : 'BGRA';
  NSDictionary* props = @{
    (id)kIOSurfaceWidth : @(w),
    (id)kIOSurfaceHeight : @(h),
    (id)kIOSurfaceBytesPerElement : @4,
    (id)kIOSurfacePixelFormat : @(fourcc),
  };
  MTLTextureDescriptor* d =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
                                                         width:w
                                                        height:h
                                                     mipmapped:NO];
  d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  d.storageMode = MTLStorageModeShared;
  for (int i = 0; i < kRing; i++) {
    surfaces[i] = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!surfaces[i]) return NO;
    textures[i] = [device newTextureWithDescriptor:d
                                         iosurface:surfaces[i]
                                             plane:0];
    if (!textures[i]) return NO;
  }
  width = w;
  height = h;
  format = f;
  next = 0;
  return YES;
}
@end

@implementation OS999Compositor {
  id<MTLDevice> _device;
  id<MTLCommandQueue> _queue;
  OS999Plane* _view;
  OS999Plane* _popup;
  CGFloat _scale;
}

- (instancetype)initWithLayer:(CALayer*)layer {
  if ((self = [super init])) {
    _device = MTLCreateSystemDefaultDevice();
    _queue = [_device newCommandQueue];
    _scale = 1;
    _view = [OS999Plane new];
    _view.layer = layer;
    _popup = [OS999Plane new];
    _popup.layer = [CALayer layer];
    _popup.layer.hidden = YES;
    _popup.layer.anchorPoint = CGPointZero;
    [layer addSublayer:_popup.layer];
  }
  return self;
}

- (NSString*)deviceName {
  return _device.name;
}

- (IOSurfaceRef)currentSurface {
  int idx = (_view->next + kRing - 1) % kRing;
  return _view->surfaces[idx];
}

- (void)setScale:(CGFloat)scale {
  _scale = scale > 0 ? scale : 1;
  _view.layer.contentsScale = _scale;
  _popup.layer.contentsScale = _scale;
}

- (void)setPopupFrame:(CGRect)frame visible:(BOOL)visible {
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  _popup.layer.frame = frame;
  _popup.layer.hidden = !visible;
  [CATransaction commit];
}

- (double)ingest:(IOSurfaceRef)surface
          format:(int)cefFormat
         visible:(CGRect)visible
           popup:(BOOL)popup {
  if (!_queue || !surface) return -1;
  MTLPixelFormat f = cefFormat == CEF_COLOR_TYPE_RGBA_8888
                         ? MTLPixelFormatRGBA8Unorm
                         : MTLPixelFormatBGRA8Unorm;
  int sw = (int)IOSurfaceGetWidth(surface);
  int sh = (int)IOSurfaceGetHeight(surface);
  if (CGRectIsEmpty(visible)) visible = CGRectMake(0, 0, sw, sh);
  visible = CGRectIntersection(visible, CGRectMake(0, 0, sw, sh));
  int w = (int)visible.size.width, h = (int)visible.size.height;
  if (w <= 0 || h <= 0) return -1;

  OS999Plane* plane = popup ? _popup : _view;
  if (![plane ensure:_device w:w h:h f:f]) return -1;

  MTLTextureDescriptor* d =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f
                                                         width:sw
                                                        height:sh
                                                     mipmapped:NO];
  d.usage = MTLTextureUsageShaderRead;
  d.storageMode = MTLStorageModeShared;
  id<MTLTexture> src = [_device newTextureWithDescriptor:d
                                               iosurface:surface
                                                   plane:0];
  if (!src) return -1;

  int slot = plane->next;
  CFTimeInterval t0 = CACurrentMediaTime();
  id<MTLCommandBuffer> cmd = [_queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
  [blit copyFromTexture:src
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake((int)visible.origin.x,
                                      (int)visible.origin.y, 0)
             sourceSize:MTLSizeMake(w, h, 1)
              toTexture:plane->textures[slot]
       destinationSlice:0
       destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
  [blit endEncoding];
  [cmd commit];
  // CEF recycles |surface| as soon as OnAcceleratedPaint returns, so every
  // read must finish here. The copy is GPU-only; the CPU just waits.
  [cmd waitUntilCompleted];
  double ms = (CACurrentMediaTime() - t0) * 1000.0;

  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  plane.layer.contents = (__bridge id)plane->surfaces[slot];
  [CATransaction commit];
  plane->next = (slot + 1) % kRing;
  return cmd.status == MTLCommandBufferStatusCompleted ? ms : -1;
}
@end
