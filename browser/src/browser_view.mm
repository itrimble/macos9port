#import "browser_view.h"

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#import "browser_compositor.h"
#include "browser_client.h"
#include "browser_stats.h"

@interface OS999BrowserView ()
- (void)gotBrowser:(CefRefPtr<CefBrowser>)browser;
- (void)lostBrowser;
- (void)gpuFrame:(const CefAcceleratedPaintInfo&)info popup:(BOOL)popup;
- (void)popup:(BOOL)show rect:(const CefRect&)rect;
- (float)scale;
@end

namespace {
NSString* NS(const std::string& s) {
  return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

class ViewDelegate : public BrowserDelegate {
 public:
  explicit ViewDelegate(OS999BrowserView* view) : view_(view) {}
  void GetViewInfo(int* w, int* h, float* scale) override {
    OS999BrowserView* v = view_;
    if (!v) return;
    NSSize size = v.forcedSize.width > 0 ? v.forcedSize : v.bounds.size;
    *w = (int)size.width;
    *h = (int)size.height;
    *scale = [v scale];
  }
  void ViewToScreen(int x, int y, int* sx, int* sy) override {
    OS999BrowserView* v = view_;
    if (!v.window) {
      *sx = x;
      *sy = y;
      return;
    }
    NSPoint p = [v convertPoint:NSMakePoint(x, y) toView:nil];
    p = [v.window convertPointToScreen:p];
    NSScreen* s = v.window.screen ?: NSScreen.mainScreen;
    *sx = (int)p.x;
    *sy = (int)(NSMaxY(s.frame) - p.y);  // CEF screen space is y-down
  }
  void OnBrowserCreated(CefRefPtr<CefBrowser> b) override { [view_ gotBrowser:b]; }
  void OnBrowserClosed() override { [view_ lostBrowser]; }
  void OnGpuFrame(const CefAcceleratedPaintInfo& info, bool popup) override {
    [view_ gpuFrame:info popup:popup];
  }
  void OnPopup(bool show, const CefRect& rect) override { [view_ popup:show rect:rect]; }
  void OnSecurity(bool secure) override { [view_.observer browserSecurityChanged:secure]; }
  void OnSecurityError(const std::string& text) override { [view_.observer browserSecurityError:NS(text)]; }
  void OnAddress(const std::string& url) override {
    [view_.observer browserAddressChanged:NS(url)];
  }
  void OnTitle(const std::string& title) override {
    [view_.observer browserTitleChanged:NS(title)];
  }
  void OnLoading(bool loading, bool back, bool forward) override {
    [view_.observer browserLoading:loading back:back forward:forward];
  }
  void OnCursor(CefCursorHandle cursor, int type) override {
    OS999BrowserView* v = view_;
    if (v.frameSink) {
      if ([v.observer respondsToSelector:@selector(browserCursor:)])
        [v.observer browserCursor:type];
      return;
    }
    if (cursor) [(__bridge NSCursor*)cursor set];
  }
  void OnConsole(const std::string& message) override {
    [view_.observer browserConsole:NS(message)];
  }
  void OnFullscreen(bool fullscreen) override {
    [view_.observer browserFullscreen:fullscreen];
  }

 private:
  __weak OS999BrowserView* view_;
};

int Modifiers(NSEvent* e) {
  NSEventModifierFlags f = e.modifierFlags;
  int m = 0;
  if (f & NSEventModifierFlagCapsLock) m |= EVENTFLAG_CAPS_LOCK_ON;
  if (f & NSEventModifierFlagShift) m |= EVENTFLAG_SHIFT_DOWN;
  if (f & NSEventModifierFlagControl) m |= EVENTFLAG_CONTROL_DOWN;
  if (f & NSEventModifierFlagOption) m |= EVENTFLAG_ALT_DOWN;
  if (f & NSEventModifierFlagCommand) m |= EVENTFLAG_COMMAND_DOWN;
  if (f & NSEventModifierFlagNumericPad) m |= EVENTFLAG_IS_KEY_PAD;
  NSUInteger buttons = NSEvent.pressedMouseButtons;
  if (buttons & 1) m |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  if (buttons & 2) m |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  if (buttons & 4) m |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
  return m;
}
}  // namespace

@implementation OS999BrowserView {
  CALayer* _content;
  OS999Compositor* _compositor;
  ViewDelegate* _delegate;
  CefRefPtr<BrowserClient> _client;
  CefRefPtr<CefBrowser> _browser;
  NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    // Layer-hosting: we own the layer tree, AppKit does not redraw it.
    CALayer* root = [CALayer layer];
    root.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
    self.layer = root;
    self.wantsLayer = YES;
    _content = [CALayer layer];
    _content.anchorPoint = CGPointZero;
    _content.contentsGravity = kCAGravityTopLeft;
    _content.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    _content.frame = root.bounds;
    [root addSublayer:_content];
    _compositor = [[OS999Compositor alloc] initWithLayer:_content];
    _delegate = new ViewDelegate(self);
  }
  return self;
}

- (void)dealloc {
  if (_client) _client->Detach();
  delete _delegate;
}

- (BOOL)isFlipped {
  return YES;
}
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
  return YES;
}
- (float)scale {
  if (self.forcedScale > 0) return (float)self.forcedScale;
  return self.window ? (float)self.window.backingScaleFactor : 2.0f;
}
- (NSString*)gpuDeviceName {
  return _compositor.deviceName;
}
- (CefRefPtr<CefBrowser>)browser {
  return _browser;
}

- (void)createBrowserWithURL:(NSString*)url frameRate:(int)fps {
  _client = new BrowserClient(_delegate);
  CefWindowInfo info;
  info.SetAsWindowless((__bridge CefWindowHandle)self);
  info.shared_texture_enabled = true;  // GPU IOSurface frames only
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.webgl = STATE_ENABLED;
  settings.javascript = STATE_ENABLED;
  settings.windowless_frame_rate = fps;
  [_compositor setScale:[self scale]];
  CefBrowserHost::CreateBrowser(info, _client, url.UTF8String, settings,
                                nullptr, nullptr);
}

- (void)closeBrowser {
  if (_browser) _browser->GetHost()->CloseBrowser(false);
  else [self.observer browserClosed];
}

- (void)runScript:(NSString*)js {
  if (_browser)
    _browser->GetMainFrame()->ExecuteJavaScript(js.UTF8String, "os999://stats", 0);
}

- (void)gotBrowser:(CefRefPtr<CefBrowser>)browser {
  _browser = browser;
  _browser->GetHost()->WasHidden(false);
  _browser->GetHost()->Invalidate(PET_VIEW);
  _browser->GetHost()->SetFocus(self.window.firstResponder == self);
}

- (void)lostBrowser {
  _browser = nullptr;
  if (_client) _client->Detach();
  _client = nullptr;
  [self.observer browserClosed];
}

- (void)gpuFrame:(const CefAcceleratedPaintInfo&)info popup:(BOOL)popup {
  auto surface = static_cast<IOSurfaceRef>(info.shared_texture_io_surface);
  CGRect visible = CGRectMake(info.extra.visible_rect.x, info.extra.visible_rect.y,
                              info.extra.visible_rect.width,
                              info.extra.visible_rect.height);
  double ms = [_compositor ingest:surface format:info.format visible:visible popup:popup];
  static int logged;
  if (logged < 3) {
    logged++;
    BrowserStats::Get().Log("paint: surface=" + std::to_string(surface != nullptr) +
                            " w=" + std::to_string((int)IOSurfaceGetWidth(surface)) +
                            " visible=" + std::to_string((int)visible.size.width) + "x" +
                            std::to_string((int)visible.size.height) +
                            " fmt=" + std::to_string((int)info.format) + " ms=" + std::to_string(ms));
  }
  if (ms >= 0 && !popup) {
    BrowserStats::Get().OnFrame((int)visible.size.width, (int)visible.size.height, ms);
    if (self.frameSink)
      self.frameSink(_compositor.currentSurface, (int)visible.size.width,
                     (int)visible.size.height);
  }
}

- (void)popup:(BOOL)show rect:(const CefRect&)r {
  // CEF gives DIP coordinates with a top-left origin; the layer is bottom-left.
  CGFloat h = NSHeight(self.bounds);
  CGRect f = CGRectMake(r.x, h - r.y - r.height, r.width, r.height);
  [_compositor setPopupFrame:f visible:show && r.width > 0];
}

- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  if (_browser) _browser->GetHost()->WasResized();
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [_compositor setScale:[self scale]];
  if (_browser) {
    _browser->GetHost()->NotifyScreenInfoChanged();
    _browser->GetHost()->WasResized();
  }
}

- (void)updateTrackingAreas {
  if (_tracking) [self removeTrackingArea:_tracking];
  _tracking = [[NSTrackingArea alloc]
      initWithRect:self.bounds
           options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                   NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_tracking];
  [super updateTrackingAreas];
}

// ---- Mouse ---------------------------------------------------------------

- (CefMouseEvent)mouse:(NSEvent*)e {
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  CefMouseEvent m;
  m.x = (int)p.x;
  m.y = (int)p.y;
  m.modifiers = Modifiers(e);
  return m;
}
- (void)click:(NSEvent*)e button:(cef_mouse_button_type_t)b up:(bool)up {
  if (!_browser) return;
  if (!up) [self.window makeFirstResponder:self];
  _browser->GetHost()->SendMouseClickEvent([self mouse:e], b, up,
                                           (int)MAX(1, e.clickCount));
}
- (void)mouseDown:(NSEvent*)e { [self click:e button:MBT_LEFT up:false]; }
- (void)mouseUp:(NSEvent*)e { [self click:e button:MBT_LEFT up:true]; }
- (void)rightMouseDown:(NSEvent*)e { [self click:e button:MBT_RIGHT up:false]; }
- (void)rightMouseUp:(NSEvent*)e { [self click:e button:MBT_RIGHT up:true]; }
- (void)otherMouseDown:(NSEvent*)e { [self click:e button:MBT_MIDDLE up:false]; }
- (void)otherMouseUp:(NSEvent*)e { [self click:e button:MBT_MIDDLE up:true]; }
- (void)move:(NSEvent*)e leave:(bool)leave {
  if (_browser) _browser->GetHost()->SendMouseMoveEvent([self mouse:e], leave);
}
- (void)mouseMoved:(NSEvent*)e { [self move:e leave:false]; }
- (void)mouseDragged:(NSEvent*)e { [self move:e leave:false]; }
- (void)rightMouseDragged:(NSEvent*)e { [self move:e leave:false]; }
- (void)otherMouseDragged:(NSEvent*)e { [self move:e leave:false]; }
- (void)mouseExited:(NSEvent*)e { [self move:e leave:true]; }
- (void)scrollWheel:(NSEvent*)e {
  if (!_browser) return;
  CGFloat k = e.hasPreciseScrollingDeltas ? 1 : 40;
  _browser->GetHost()->SendMouseWheelEvent([self mouse:e],
                                           (int)(e.scrollingDeltaX * k),
                                           (int)(e.scrollingDeltaY * k));
}

// ---- Keyboard ------------------------------------------------------------

- (BOOL)becomeFirstResponder {
  if (_browser) _browser->GetHost()->SetFocus(true);
  return YES;
}
- (BOOL)resignFirstResponder {
  if (_browser) _browser->GetHost()->SetFocus(false);
  return YES;
}
- (CefKeyEvent)key:(NSEvent*)e {
  CefKeyEvent k;
  k.native_key_code = e.keyCode;
  k.modifiers = Modifiers(e);
  NSString* c = e.characters;
  NSString* u = e.charactersIgnoringModifiers;
  if (c.length) k.character = [c characterAtIndex:0];
  if (u.length) k.unmodified_character = [u characterAtIndex:0];
  return k;
}
- (void)keyDown:(NSEvent*)e {
  if (!_browser) return;
  CefKeyEvent k = [self key:e];
  k.type = KEYEVENT_KEYDOWN;
  _browser->GetHost()->SendKeyEvent(k);
  // Arrow/function keys live in the private-use range and carry no text.
  bool function_key = k.character >= 0xF700 && k.character <= 0xF8FF;
  if (!function_key && !(k.modifiers & EVENTFLAG_COMMAND_DOWN) && k.character) {
    k.type = KEYEVENT_CHAR;
    _browser->GetHost()->SendKeyEvent(k);
  }
}
- (void)keyUp:(NSEvent*)e {
  if (!_browser) return;
  CefKeyEvent k = [self key:e];
  k.type = KEYEVENT_KEYUP;
  _browser->GetHost()->SendKeyEvent(k);
}

// ---- Embedded input (messages from the OS999 desktop) --------------------

- (void)embedResized {
  [_compositor setScale:[self scale]];
  if (_browser) {
    _browser->GetHost()->NotifyScreenInfoChanged();
    _browser->GetHost()->WasResized();
    _browser->GetHost()->WasHidden(false);
    _browser->GetHost()->Invalidate(PET_VIEW);
  }
}

- (void)embedMouse:(int)kind x:(int)x y:(int)y a:(int)a b:(int)b mods:(int)mods {
  if (!_browser) return;
  CefMouseEvent m;
  m.x = x;
  m.y = y;
  m.modifiers = mods;
  auto host = _browser->GetHost();
  switch (kind) {
    case 2:  // OS999B_MOUSE_MOVE
      host->SendMouseMoveEvent(m, false);
      break;
    case 3:  // OS999B_MOUSE_DOWN
    case 4: {  // OS999B_MOUSE_UP
      cef_mouse_button_type_t button = a == 2 ? MBT_RIGHT : a == 1 ? MBT_MIDDLE : MBT_LEFT;
      host->SendMouseClickEvent(m, button, kind == 4, b > 0 ? b : 1);
      break;
    }
    case 5:  // OS999B_MOUSE_LEAVE
      host->SendMouseMoveEvent(m, true);
      break;
    case 6:  // OS999B_WHEEL
      host->SendMouseWheelEvent(m, a, b);
      break;
  }
}

- (void)embedKey:(int)kind code:(int)code ch:(int)ch uch:(int)uch mods:(int)mods {
  if (!_browser) return;
  if (kind == 7 && (mods & EVENTFLAG_COMMAND_DOWN)) {  // Cmd shortcuts
    auto f = _browser->GetFocusedFrame();
    switch (uch) {
      case 'c': if (f) f->Copy(); return;
      case 'v': if (f) f->Paste(); return;
      case 'x': if (f) f->Cut(); return;
      case 'a': if (f) f->SelectAll(); return;
      case 'z': if (f) (mods & EVENTFLAG_SHIFT_DOWN) ? f->Redo() : f->Undo(); return;
    }
  }
  CefKeyEvent k;
  k.type = kind == 7 ? KEYEVENT_KEYDOWN : KEYEVENT_KEYUP;  // OS999B_KEY_DOWN
  k.native_key_code = code;
  k.character = (char16_t)ch;
  k.unmodified_character = (char16_t)uch;
  k.modifiers = mods;
  _browser->GetHost()->SendKeyEvent(k);
  // Return and Tab need a char event to act (submit, focus move).
  if (kind == 7 && (ch == '\r' || ch == '\t')) {
    k.type = KEYEVENT_CHAR;
    _browser->GetHost()->SendKeyEvent(k);
  }
}

- (void)embedText:(NSString*)text {
  if (!_browser) return;
  for (NSUInteger i = 0; i < text.length; i++) {
    CefKeyEvent k;
    k.type = KEYEVENT_CHAR;
    k.character = k.unmodified_character = [text characterAtIndex:i];
    _browser->GetHost()->SendKeyEvent(k);
  }
}

// Edit menu targets (Cmd-X/C/V/A/Z) go to the focused frame.
- (CefRefPtr<CefFrame>)focused {
  return _browser ? _browser->GetFocusedFrame() : nullptr;
}
- (void)cut:(id)s { if (auto f = [self focused]) f->Cut(); }
- (void)copy:(id)s { if (auto f = [self focused]) f->Copy(); }
- (void)paste:(id)s { if (auto f = [self focused]) f->Paste(); }
- (void)selectAll:(id)s { if (auto f = [self focused]) f->SelectAll(); }
- (void)undo:(id)s { if (auto f = [self focused]) f->Undo(); }
- (void)redo:(id)s { if (auto f = [self focused]) f->Redo(); }
@end
