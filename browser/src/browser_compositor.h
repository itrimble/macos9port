// OS999 Browser: zero-CPU-copy compositor. Each Chromium frame (an IOSurface
// valid only inside OnAcceleratedPaint) is blitted on the GPU into a ring of
// OS999-owned IOSurfaces, which Core Animation presents directly. The owned
// surfaces are what the OS999 desktop will import in the integrated build.
#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

@interface OS999Compositor : NSObject
- (instancetype)initWithLayer:(CALayer*)layer;
// Copies |surface| (CEF pixel format |cefFormat|) cropped to |visible| into
// the next owned surface and presents it. Returns GPU copy time in ms, or -1.
- (double)ingest:(IOSurfaceRef)surface
          format:(int)cefFormat
         visible:(CGRect)visible
           popup:(BOOL)popup;
- (void)setPopupFrame:(CGRect)frameInPoints visible:(BOOL)visible;
- (void)setScale:(CGFloat)scale;
@property(nonatomic, readonly) IOSurfaceRef currentSurface;
@property(nonatomic, readonly) NSString* deviceName;
@end
