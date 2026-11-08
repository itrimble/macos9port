#include "web_apps.h"
// OS999 Browser: host-native Chromium (CEF 156) with classic OS999 chrome.
// Rendering is windowless (OSR) with shared GPU textures; see browser/README.md.
#import <Cocoa/Cocoa.h>

#include "include/cef_app.h"
#include "include/cef_application_mac.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_library_loader.h"

#include "browser_app.h"
#include "browser_stats.h"
#include "version_guard.h"
#import "browser_view.h"
#import "chrome_bar.h"
#include "embed/embed_frames.h"
#include "embed/embed_host.h"

static NSString* const kHome = @"https://www.youtube.com";
static NSMutableArray* gWindows;
static BOOL gQuitting;

struct Options {
  std::string url, quality, log;
  int width = 1280, height = 860, fps = 60, exit_after = 0, report_at = 0, quality_step = 0;
  bool test = false;
  int embed_fd = -1;
  std::string embed_port, download_dir;
};
static Options gOpt;
static EmbedHost* gEmbed;  // embedded session (lives until exit)
static id<NSObject> gActivity;

@interface OS999Application : NSApplication <CefAppProtocol> {
  BOOL _sending;
}
@end
@implementation OS999Application
- (BOOL)isHandlingSendEvent { return _sending; }
- (void)setHandlingSendEvent:(BOOL)v { _sending = v; }
- (void)sendEvent:(NSEvent*)e {
  CefScopedSendingEvent scope;
  [super sendEvent:e];
}
- (void)terminate:(id)sender {
  gQuitting = YES;
  if (gWindows.count == 0) CefQuitMessageLoop();
  for (id w in [gWindows copy]) [w performSelector:@selector(chromeClose)];
}
@end

@interface OS999Window : NSWindow
@end
@implementation OS999Window
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface OS999Frame : NSView
@end
@implementation OS999Frame
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)r {
  [[NSColor blackColor] setStroke];
  [[NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 0.5, 0.5)] stroke];
}
@end

@interface OS999BrowserWindow : NSObject <OS999ChromeDelegate, OS999BrowserViewObserver, NSWindowDelegate>
- (instancetype)initWithURL:(NSString*)url;
@end

@implementation OS999BrowserWindow {
  OS999Window* _window;
  OS999TitleBar* _title;
  OS999ToolBar* _tool;
  OS999BrowserView* _view;
  OS999StatusBar* _status;
  NSTimer* _timer;
  id _escMonitor;
  BOOL _fullscreen, _shaded;
  NSRect _savedFrame;
  NSString* _video, *_securityError;
  double _lastDecoded, _lastVideoT, _videoFps, _qStart;
}

- (instancetype)initWithURL:(NSString*)url {
  if (!(self = [super init])) return nil;
  NSRect frame = NSMakeRect(80, 80, gOpt.width, gOpt.height);
  _window = [[OS999Window alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _window.releasedWhenClosed = NO;
  _window.hasShadow = YES;
  _window.delegate = self;
  _window.minSize = NSMakeSize(420, 240);
  _window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;
  OS999Frame* root = [[OS999Frame alloc] initWithFrame:frame];
  _window.contentView = root;
  _title = [OS999TitleBar new];
  _title.delegate = self;
  _tool = [OS999ToolBar new];
  _tool.delegate = self;
  _view = [OS999BrowserView new];
  _view.observer = self;
  _status = [OS999StatusBar new];
  _status.text = @"Starting Chromium…";
  for (NSView* v in @[ _title, _tool, _view, _status ]) [root addSubview:v];
  [self layout];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
  [_window makeFirstResponder:_view];
  [_view createBrowserWithURL:url frameRate:gOpt.fps];
  [_tool setURL:url];
  _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:self
                                          selector:@selector(tick) userInfo:nil
                                           repeats:YES];
  __weak OS999BrowserWindow* weak = self;
  _escMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                      handler:^NSEvent*(NSEvent* e) {
    OS999BrowserWindow* s = weak;
    if (s && s->_fullscreen && e.keyCode == 53 && [s->_view browser]) {
      [s->_view browser]->GetHost()->ExitFullscreen(true);
      return nil;
    }
    return e;
  }];
  [gWindows addObject:self];
  return self;
}

- (void)layout {
  NSRect b = _window.contentView.bounds;
  CGFloat w = NSWidth(b), h = NSHeight(b);
  BOOL chrome = !_fullscreen;
  _title.hidden = _tool.hidden = _status.hidden = !chrome;
  if (!chrome) {
    _view.frame = b;
    return;
  }
  _title.frame = NSMakeRect(1, 1, w - 2, 20);
  _tool.frame = NSMakeRect(1, 21, w - 2, 36);
  _status.frame = NSMakeRect(1, h - 17, w - 2, 16);
  _view.frame = NSMakeRect(1, 57, w - 2, MAX(0, h - 57 - 17));
  _view.hidden = _shaded;
  [_tool resizeSubviewsWithOldSize:_tool.frame.size];
}
- (void)windowDidResize:(NSNotification*)n { [self layout]; }
- (void)windowDidBecomeKey:(NSNotification*)n { _title.active = YES; [_title setNeedsDisplay:YES]; }
- (void)windowDidResignKey:(NSNotification*)n { _title.active = NO; [_title setNeedsDisplay:YES]; }

// ---- Chrome actions -------------------------------------------------------

- (void)chromeBack { if ([_view browser]) [_view browser]->GoBack(); }
- (void)chromeForward { if ([_view browser]) [_view browser]->GoForward(); }
- (void)chromeReloadOrStop {
  auto b = [_view browser];
  if (!b) return;
  if (b->IsLoading()) b->StopLoad();
  else b->Reload();
}
- (void)chromeHome { [self load:kHome]; }
- (void)load:(NSString*)url {
  if ([_view browser]) [_view browser]->GetMainFrame()->LoadURL(url.UTF8String);
}
- (void)chromeNavigate:(NSString*)text {
  NSString* t = [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
  if (!t.length) return;
  BOOL hasScheme = [t rangeOfString:@"://"].location != NSNotFound ||
                   [t hasPrefix:@"about:"] || [t hasPrefix:@"chrome:"];
  BOOL looksLikeHost = [t rangeOfString:@" "].location == NSNotFound &&
                       [t rangeOfString:@"."].location != NSNotFound;
  NSString* url = t;
  if (!hasScheme && looksLikeHost) url = [@"https://" stringByAppendingString:t];
  if (!hasScheme && !looksLikeHost) {
    NSString* q = [t stringByAddingPercentEncodingWithAllowedCharacters:
                         NSCharacterSet.URLQueryAllowedCharacterSet];
    url = [@"https://www.google.com/search?q=" stringByAppendingString:q];
  }
  [self load:url];
  [_window makeFirstResponder:_view];
}
- (void)chromeClose {
  [_timer invalidate];
  [_view closeBrowser];
}
- (void)chromeZoom {
  NSRect vis = _window.screen.visibleFrame;
  if (NSEqualRects(_window.frame, vis) && !NSIsEmptyRect(_savedFrame)) {
    [_window setFrame:_savedFrame display:YES animate:NO];
  } else {
    _savedFrame = _window.frame;
    [_window setFrame:vis display:YES animate:NO];
  }
}
- (void)chromeCollapse {  // classic WindowShade
  NSRect f = _window.frame;
  static const CGFloat kShade = 22;
  if (!_shaded) {
    _savedFrame = f;
    _shaded = YES;
    _window.minSize = NSMakeSize(420, kShade);
    f.origin.y += f.size.height - kShade;
    f.size.height = kShade;
  } else {
    _shaded = NO;
    f.origin.y -= _savedFrame.size.height - f.size.height;
    f.size.height = _savedFrame.size.height;
    _window.minSize = NSMakeSize(420, 240);
  }
  [_window setFrame:f display:YES];
  [self layout];
}
- (void)installApp { WebApps::Install([_view browser], [self](bool, const std::string& s) { self->_status.text = [NSString stringWithUTF8String:s.c_str()]; }); }
- (void)removeApp { WebApps::Remove([_view browser], [self](bool, const std::string& s) { self->_status.text = [NSString stringWithUTF8String:s.c_str()]; }); }
- (void)focusAddress { [_tool focusAddress]; }
- (void)openDiagnostics:(NSString*)url { [self load:url]; }

// ---- Browser events -------------------------------------------------------

- (void)browserSecurityChanged:(BOOL)secure { [_tool setSecure:secure]; }
- (void)browserSecurityError:(NSString*)text { _securityError = text; _status.text = text; }
- (void)browserAddressChanged:(NSString*)url { [_tool setURL:url]; }
- (void)browserTitleChanged:(NSString*)title {
  _title.title = [NSString stringWithFormat:@"Chromium - OS999 Edition — %@", title];
  _window.title = title;
  [_title setNeedsDisplay:YES];
}
- (void)browserLoading:(BOOL)l back:(BOOL)b forward:(BOOL)f {
  [_tool setLoading:l back:b forward:f];
}
- (void)browserFullscreen:(BOOL)fs {
  if (fs == _fullscreen) return;
  _fullscreen = fs;
  if (fs) {
    _savedFrame = _window.frame;
    NSApp.presentationOptions = NSApplicationPresentationHideDock |
                                NSApplicationPresentationHideMenuBar;
    [_window setFrame:_window.screen.frame display:YES];
  } else {
    NSApp.presentationOptions = NSApplicationPresentationDefault;
    [_window setFrame:_savedFrame display:YES];
  }
  [self layout];
}
- (void)browserConsole:(NSString*)msg {
  if ([msg hasPrefix:@"OS999MEDIA "]) {
    BrowserStats::Get().Log(std::string("media ") + msg.UTF8String);
    return;
  }
  if (![msg hasPrefix:@"OS999VIDEO "]) return;
  NSString* json = [msg substringFromIndex:11];
  NSDictionary* d = [NSJSONSerialization
      JSONObjectWithData:[json dataUsingEncoding:NSUTF8StringEncoding]
                 options:0 error:nil];
  if (![d isKindOfClass:NSDictionary.class]) return;
  double decoded = [d[@"dec"] doubleValue], t = CACurrentMediaTime();
  if (_lastVideoT > 0 && decoded >= _lastDecoded)
    _videoFps = (decoded - _lastDecoded) / (t - _lastVideoT);
  _lastDecoded = decoded;
  _lastVideoT = t;
  NSString* codec = [d[@"fmt"] description] ?: @"";
  _video = [NSString stringWithFormat:@"video %@×%@ %@ %.1f fps · dropped %@/%@ · %@%@",
            d[@"w"], d[@"h"], d[@"ql"], _videoFps, d[@"drop"], d[@"dec"],
            [d[@"muted"] boolValue] ? @"muted" : @"sound on",
            [d[@"paused"] boolValue] ? @" · paused" : @""];
  BrowserStats::Get().Log(std::string("video ") + json.UTF8String +
                          " decoded_fps=" + std::to_string(_videoFps) +
                          " codec=" + codec.UTF8String);
}
- (void)browserClosed {
  [_timer invalidate];
  if (_escMonitor) [NSEvent removeMonitor:_escMonitor];
  _escMonitor = nil;
  if (_fullscreen) NSApp.presentationOptions = NSApplicationPresentationDefault;
  [_window orderOut:nil];
  [gWindows removeObject:self];
  if (gWindows.count == 0) CefQuitMessageLoop();
}

// ---- Measurement ----------------------------------------------------------

- (void)tick {
  double fps, copy;
  int w, h;
  if (BrowserStats::Get().Sample(&fps, &copy, &w, &h)) {
    NSString* line = [NSString stringWithFormat:@"GPU frames %.1f fps · %d×%d px · copy %.2f ms",
                      fps, w, h, copy];
    _status.text = _video ? [line stringByAppendingFormat:@" · %@", _video] : line;
    const VersionResult& vr = VersionGuard::Get().result();
    if (!vr.message.empty())
      _status.text = [NSString stringWithFormat:@"%s  |  %@", vr.message.c_str(), _status.text];
    if (_securityError.length) _status.text = _securityError;
    BrowserStats::Get().Log(std::string("frame ") + line.UTF8String);
  }
#ifdef OS999_ENABLE_TEST_HOOKS
  [_view runScript:
      @"if(location.href.indexOf('chrome://media-internals')==0){"
       "var ls=[].slice.call(document.querySelectorAll('*')).filter(function(e){"
       "return e.children.length==0&&/^blob:/.test(e.textContent.trim());}),out=[];"
       "ls.forEach(function(e){e.click();if(e.parentElement)e.parentElement.click();"
       "var t=document.body.innerText,i=t.search(/kVideoDecoderName/);"
       "out.push(i<0?'no-decoder-row':t.substring(Math.max(0,i-1500),i+1500));});"
       "console.log('OS999MEDIA n='+ls.length+' '+out.join(' ||| ').replace(/\\n/g,' | '));}"];
  // Read the page's own <video> statistics (no page changes besides quality).
  // --os999-quality=hd2160,hd1440,... cycles every --os999-quality-step secs.
  NSArray* qs = gOpt.quality.empty() ? @[]
      : [[NSString stringWithUTF8String:gOpt.quality.c_str()] componentsSeparatedByString:@","];
  NSString* q = @"";
  if (qs.count) {
    if (_qStart == 0) _qStart = CACurrentMediaTime();
    NSUInteger i = gOpt.quality_step > 0
        ? (NSUInteger)((CACurrentMediaTime() - _qStart) / gOpt.quality_step) : 0;
    q = qs[MIN(i, qs.count - 1)];
  }
  NSString* js = [NSString stringWithFormat:
      @"(function(){var v=document.querySelector('video');if(!v)return;"
       "var p=document.getElementById('movie_player'),q='%@';"
       "if(q&&p&&p.setPlaybackQualityRange&&p.getPlaybackQuality()!=q){"
       "p.setPlaybackQualityRange(q,q);}"
       "var s=v.getVideoPlaybackQuality(),n=p&&p.getStatsForNerds?p.getStatsForNerds():{};"
       "console.log('OS999VIDEO '+JSON.stringify({t:+v.currentTime.toFixed(1),"
       "paused:v.paused,muted:v.muted,vol:v.volume,w:v.videoWidth,h:v.videoHeight,"
       "dec:s.totalVideoFrames,drop:s.droppedVideoFrames,"
       "ql:p&&p.getPlaybackQuality?p.getPlaybackQuality():'',fmt:n.codecs||''}))})()", q];
  [_view runScript:js];
#endif
}
@end

// ---- Application ----------------------------------------------------------

@interface OS999AppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation OS999AppDelegate
- (OS999BrowserWindow*)front {
  for (OS999BrowserWindow* w in gWindows) return w;
  return nil;
}
- (void)newWindow:(id)s { (void)[[OS999BrowserWindow alloc] initWithURL:kHome]; }
- (void)openLocation:(id)s { [[self front] focusAddress]; }
- (void)closeWindow:(id)s { [[self front] chromeClose]; }
- (void)reload:(id)s { [[self front] chromeReloadOrStop]; }
- (void)back:(id)s { [[self front] chromeBack]; }
- (void)forward:(id)s { [[self front] chromeForward]; }
- (void)installApp:(id)s { [[self front] installApp]; }
- (void)removeApp:(id)s { [[self front] removeApp]; }
- (void)apps:(id)s { [[self front] openDiagnostics:@"https://os999.invalid/apps/"]; }
- (void)gpuPage:(id)s { [[self front] openDiagnostics:@"chrome://gpu"]; }
- (void)mediaPage:(id)s { [[self front] openDiagnostics:@"chrome://media-internals"]; }
@end

static void AddItem(NSMenu* m, NSString* t, SEL a, NSString* k, id target) {
  NSMenuItem* i = [m addItemWithTitle:t action:a keyEquivalent:k];
  i.target = target;
}

static void BuildMenus(OS999AppDelegate* d) {
  NSMenu* bar = [NSMenu new];
  NSMenu* app = [[NSMenu alloc] initWithTitle:@"OS999 Browser"];
  AddItem(app, @"About OS999 Browser", @selector(orderFrontStandardAboutPanel:), @"", nil);
  [app addItem:NSMenuItem.separatorItem];
  AddItem(app, @"Quit OS999 Browser", @selector(terminate:), @"q", nil);
  NSMenu* file = [[NSMenu alloc] initWithTitle:@"File"];
  AddItem(file, @"New Window", @selector(newWindow:), @"n", d);
  AddItem(file, @"Open Location…", @selector(openLocation:), @"l", d);
  AddItem(file, @"Close Window", @selector(closeWindow:), @"w", d);
  AddItem(file, @"Install Web App", @selector(installApp:), @"I", d);
  AddItem(file, @"Web Apps", @selector(apps:), @"A", d);
  AddItem(file, @"Remove Web App Shortcut", @selector(removeApp:), @"U", d);
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  AddItem(edit, @"Undo", @selector(undo:), @"z", nil);
  AddItem(edit, @"Redo", @selector(redo:), @"Z", nil);
  [edit addItem:NSMenuItem.separatorItem];
  AddItem(edit, @"Cut", @selector(cut:), @"x", nil);
  AddItem(edit, @"Copy", @selector(copy:), @"c", nil);
  AddItem(edit, @"Paste", @selector(paste:), @"v", nil);
  AddItem(edit, @"Select All", @selector(selectAll:), @"a", nil);
  NSMenu* view = [[NSMenu alloc] initWithTitle:@"View"];
  AddItem(view, @"Reload", @selector(reload:), @"r", d);
  AddItem(view, @"Back", @selector(back:), @"[", d);
  AddItem(view, @"Forward", @selector(forward:), @"]", d);
  [view addItem:NSMenuItem.separatorItem];
  AddItem(view, @"GPU Diagnostics", @selector(gpuPage:), @"", d);
  AddItem(view, @"Media Diagnostics", @selector(mediaPage:), @"", d);
  for (NSMenu* m in @[ app, file, edit, view ]) {
    NSMenuItem* top = [bar addItemWithTitle:m.title action:nil keyEquivalent:@""];
    top.submenu = m;
  }
  NSApp.mainMenu = bar;
}

static std::string Switch(CefRefPtr<CefCommandLine> c, const char* name) {
  return c->HasSwitch(name) ? c->GetSwitchValue(name).ToString() : std::string();
}

#include "launch_policy.h"
int main(int argc, char* argv[]) {
  if (!BrowserLaunchAllowed(argc, argv)) return 64;
  CefScopedLibraryLoader loader;
  if (!loader.LoadInMain()) return 1;
  CefMainArgs args(argc, argv);
  @autoreleasepool {
    [OS999Application sharedApplication];
    CefRefPtr<CefCommandLine> cl = CefCommandLine::CreateCommandLine();
    cl->InitFromArgv(argc, argv);
    gOpt.url = Switch(cl, "url");
    gOpt.quality = Switch(cl, "os999-quality");
    gOpt.log = Switch(cl, "os999-stats-log");
    gOpt.test = cl->HasSwitch("os999-test");
    std::string size = Switch(cl, "os999-size");
    if (!size.empty()) sscanf(size.c_str(), "%dx%d", &gOpt.width, &gOpt.height);
    std::string fps = Switch(cl, "os999-fps");
    if (!fps.empty()) gOpt.fps = atoi(fps.c_str());
    std::string qstep = Switch(cl, "os999-quality-step");
    if (!qstep.empty()) gOpt.quality_step = atoi(qstep.c_str());
    std::string rep = Switch(cl, "os999-report-at");
    if (!rep.empty()) gOpt.report_at = atoi(rep.c_str());
    std::string ex = Switch(cl, "os999-exit-after");
    if (!ex.empty()) gOpt.exit_after = atoi(ex.c_str());
    std::string efd = Switch(cl, "os999-embed-fd");
    if (!efd.empty()) gOpt.embed_fd = atoi(efd.c_str());
    gOpt.embed_port = Switch(cl, "os999-embed-port");
    gOpt.download_dir = Switch(cl, "os999-download-dir");
    BrowserStats::Get().SetLogPath(gOpt.log);

    NSString* support = [NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject
        stringByAppendingPathComponent:gOpt.embed_fd >= 0 ? @"OS999 Browser Security Embedded"
                                                         : @"OS999 Browser Security"];
    std::string profile = Switch(cl, "os999-profile");
    if (!profile.empty()) {
      if (profile[0] != '/') return 64;
      support = [NSString stringWithUTF8String:profile.c_str()];
    }
    support = [support stringByResolvingSymlinksInPath];
    if (!support.length) return 64;
    CefSettings settings;
    settings.windowless_rendering_enabled = true;
    settings.persist_session_cookies = true;
    CefString(&settings.root_cache_path) = support.UTF8String;
    CefString(&settings.cache_path) =
        [support stringByAppendingPathComponent:@"Profile"].UTF8String;
    CefString(&settings.log_file) =
        [support stringByAppendingPathComponent:@"chromium.log"].UTF8String;
#if !defined(CEF_USE_SANDBOX)
#error "OS999 Browser requires the Chromium sandbox (checklist B03)."
#endif

    gWindows = [NSMutableArray new];
    OS999AppDelegate* delegate = [OS999AppDelegate new];
    NSString* first = gOpt.url.empty() ? kHome : [NSString stringWithUTF8String:gOpt.url.c_str()];
    CefRefPtr<BrowserApp> app(new BrowserApp(
        [first]() {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (gOpt.embed_fd >= 0) {  // inside the OS999 desktop
              // No windows of our own: keep App Nap from throttling frames.
              gActivity = [[NSProcessInfo processInfo]
                  beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
                                    reason:@"Embedded OS999 browser rendering"];
              FrameSink::Init(gOpt.embed_port);
              gEmbed = new EmbedHost(gOpt.embed_fd, gOpt.download_dir);
              gEmbed->Start();
              return;
            }
            OS999RegisterChromeFont();
            VersionGuard::Get().Start(nullptr);
            (void)[[OS999BrowserWindow alloc] initWithURL:first];
            if (gOpt.report_at > 0)
              dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)gOpt.report_at * NSEC_PER_SEC),
                             dispatch_get_main_queue(), ^{
                               [[(OS999AppDelegate*)NSApp.delegate front]
                                   openDiagnostics:@"chrome://media-internals"];
                             });
            if (gOpt.exit_after > 0)
              dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)gOpt.exit_after * NSEC_PER_SEC),
                             dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
          });
        },
        gOpt.test));
    if (!CefInitialize(args, settings, app.get(), nullptr)) return CefGetExitCode();
    if (gOpt.embed_fd >= 0) {
      // Embedded: no Dock icon, menu bar or windows; OS999 owns all of those.
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    } else {
      NSApp.delegate = delegate;
      BuildMenus(delegate);
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    BrowserStats::Get().Log(std::string("start cef ") + CEF_VERSION);
    CefRunMessageLoop();
    CefShutdown();
  }
  return 0;
}
