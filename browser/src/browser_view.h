// OS999 Browser: layer-hosting NSView that owns one windowless Chromium
// browser, presents its GPU frames and forwards mouse/keyboard input.
#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>

#include <string>

#include "include/cef_browser.h"

@protocol OS999BrowserViewObserver <NSObject>
- (void)browserSecurityChanged:(BOOL)secure;
- (void)browserSecurityError:(NSString*)text;
- (void)browserAddressChanged:(NSString*)url;
- (void)browserTitleChanged:(NSString*)title;
- (void)browserLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward;
- (void)browserConsole:(NSString*)message;
- (void)browserFullscreen:(BOOL)fullscreen;
- (void)browserClosed;
@optional
- (void)browserCursor:(int)type;
@end


@interface OS999BrowserView : NSView
@property(nonatomic, weak) id<OS999BrowserViewObserver> observer;
- (void)createBrowserWithURL:(NSString*)url frameRate:(int)fps;
- (CefRefPtr<CefBrowser>)browser;
- (void)closeBrowser;
- (void)runScript:(NSString*)js;
- (NSString*)gpuDeviceName;
// Embedded mode (inside the OS999 desktop): fixed DIP size and scale, frames
// handed to |frameSink| instead of being shown in a window of our own.
@property(nonatomic) CGFloat forcedScale;
@property(nonatomic) NSSize forcedSize;
@property(nonatomic, copy) void (^frameSink)(IOSurfaceRef surface, int width, int height);
- (void)embedMouse:(int)kind x:(int)x y:(int)y a:(int)a b:(int)b mods:(int)mods;
- (void)embedKey:(int)kind code:(int)code ch:(int)ch uch:(int)uch mods:(int)mods;
- (void)embedText:(NSString*)text;
- (void)embedResized;
@end
