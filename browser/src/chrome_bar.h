// OS999 Browser: classic Platinum window chrome drawn natively (title bar,
// toolbar, status bar, grow box). Original drawing; no Apple assets.
#import <Cocoa/Cocoa.h>

@protocol OS999ChromeDelegate <NSObject>
- (void)chromeBack;
- (void)chromeForward;
- (void)chromeReloadOrStop;
- (void)chromeHome;
- (void)chromeNavigate:(NSString*)text;
- (void)chromeClose;
- (void)chromeZoom;
- (void)chromeCollapse;
@end

NSFont* OS999ChromeFont(CGFloat size);
void OS999RegisterChromeFont(void);

@interface OS999TitleBar : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic, weak) id<OS999ChromeDelegate> delegate;
@property(nonatomic) BOOL active;
@end

@interface OS999ToolBar : NSView <NSTextFieldDelegate>
@property(nonatomic, weak) id<OS999ChromeDelegate> delegate;
- (void)setSecure:(BOOL)secure;
- (void)setURL:(NSString*)url;
- (void)setLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward;
- (void)focusAddress;
@end

@interface OS999StatusBar : NSView
@property(nonatomic, copy) NSString* text;
@end
