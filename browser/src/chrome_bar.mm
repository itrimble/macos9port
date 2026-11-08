#import "chrome_bar.h"

#import <CoreText/CoreText.h>

static NSColor* Gray(CGFloat v) {
  return [NSColor colorWithCalibratedWhite:v alpha:1];
}

void OS999RegisterChromeFont(void) {
  NSURL* url = [[NSBundle mainBundle] URLForResource:@"ChicagoFLF"
                                       withExtension:@"ttf"];
  if (url)
    CTFontManagerRegisterFontsForURL((__bridge CFURLRef)url,
                                     kCTFontManagerScopeProcess, NULL);
}

NSFont* OS999ChromeFont(CGFloat size) {
  NSFont* f = [NSFont fontWithName:@"ChicagoFLF" size:size];
  return f ? f : [NSFont boldSystemFontOfSize:size];
}

// Raised Platinum bevel: light top-left, dark bottom-right.
static void Bevel(NSRect r, BOOL pressed) {
  [Gray(pressed ? 0.62 : 0.87) setFill];
  NSRectFill(r);
  [Gray(pressed ? 0.45 : 1.0) setFill];
  NSRectFill(NSMakeRect(r.origin.x, r.origin.y, r.size.width, 1));
  NSRectFill(NSMakeRect(r.origin.x, r.origin.y, 1, r.size.height));
  [Gray(pressed ? 1.0 : 0.45) setFill];
  NSRectFill(NSMakeRect(r.origin.x, NSMaxY(r) - 1, r.size.width, 1));
  NSRectFill(NSMakeRect(NSMaxX(r) - 1, r.origin.y, 1, r.size.height));
}

static void Box(NSRect r) {  // small title-bar widget box
  [Gray(0.0) setStroke];
  [[NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)] stroke];
  NSRect in = NSInsetRect(r, 1, 1);
  Bevel(in, NO);
}

@implementation OS999TitleBar {
  NSInteger _pressed;  // 0 none, 1 close, 2 zoom, 3 collapse
}
- (BOOL)isFlipped {
  return YES;
}
- (NSRect)closeRect {
  return NSMakeRect(8, 4, 12, 12);
}
- (NSRect)zoomRect {
  return NSMakeRect(NSWidth(self.bounds) - 40, 4, 12, 12);
}
- (NSRect)collapseRect {
  return NSMakeRect(NSWidth(self.bounds) - 22, 4, 12, 12);
}
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  [Gray(0.87) setFill];
  NSRectFill(b);
  NSDictionary* attrs = @{
    NSFontAttributeName : OS999ChromeFont(12),
    NSForegroundColorAttributeName : self.active ? Gray(0) : Gray(0.5)
  };
  NSString* t = self.title.length ? self.title : @"Chromium - OS999 Edition";
  NSSize ts = [t sizeWithAttributes:attrs];
  CGFloat maxw = NSWidth(b) - 120;
  if (ts.width > maxw) ts.width = maxw;
  CGFloat tx = floor((NSWidth(b) - ts.width) / 2);
  if (self.active) {  // pinstripes either side of the title
    for (CGFloat y = 5; y < 16; y += 2) {
      [Gray(1.0) setFill];
      NSRectFill(NSMakeRect(26, y, tx - 34, 1));
      NSRectFill(NSMakeRect(tx + ts.width + 8, y, NSWidth(b) - 48 - tx - ts.width - 8, 1));
      [Gray(0.55) setFill];
      NSRectFill(NSMakeRect(27, y + 1, tx - 34, 1));
      NSRectFill(NSMakeRect(tx + ts.width + 9, y + 1, NSWidth(b) - 48 - tx - ts.width - 8, 1));
    }
    Box([self closeRect]);
    Box([self zoomRect]);
    Box([self collapseRect]);
    [Gray(0) setStroke];
    NSRect z = NSInsetRect([self zoomRect], 3, 3);
    [[NSBezierPath bezierPathWithRect:NSMakeRect(z.origin.x + .5, z.origin.y + .5, 4, 4)] stroke];
    NSRect c = [self collapseRect];
    NSRectFill(NSMakeRect(c.origin.x + 3, c.origin.y + 5, 6, 1));
    NSRectFill(NSMakeRect(c.origin.x + 3, c.origin.y + 7, 6, 1));
    if (_pressed) {
      NSRect p = _pressed == 1 ? [self closeRect] : _pressed == 2 ? [self zoomRect] : [self collapseRect];
      [Gray(0.3) setFill];
      NSRectFill(NSInsetRect(p, 2, 2));
    }
  }
  NSRect tr = NSMakeRect(tx, 3, ts.width, 16);
  [t drawWithRect:tr
          options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine
       attributes:attrs
          context:nil];
  [Gray(0.45) setFill];
  NSRectFill(NSMakeRect(0, NSHeight(b) - 1, NSWidth(b), 1));
}
- (NSInteger)hit:(NSPoint)p {
  if (NSPointInRect(p, NSInsetRect([self closeRect], -2, -2))) return 1;
  if (NSPointInRect(p, NSInsetRect([self zoomRect], -2, -2))) return 2;
  if (NSPointInRect(p, NSInsetRect([self collapseRect], -2, -2))) return 3;
  return 0;
}
- (void)mouseDown:(NSEvent*)e {
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  _pressed = [self hit:p];
  if (!_pressed) {
    if (e.clickCount == 2) {
      [self.delegate chromeCollapse];
      return;
    }
    [self.window performWindowDragWithEvent:e];
    return;
  }
  [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent*)e {
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  NSInteger hit = [self hit:p], was = _pressed;
  _pressed = 0;
  [self setNeedsDisplay:YES];
  if (!was || hit != was) return;
  if (was == 1) [self.delegate chromeClose];
  if (was == 2) [self.delegate chromeZoom];
  if (was == 3) [self.delegate chromeCollapse];
}
@end

// ---- Toolbar -------------------------------------------------------------

typedef NS_ENUM(NSInteger, OS999Glyph) { kBack, kForward, kReload, kStop, kHome };

@interface OS999ToolButton : NSControl
@property(nonatomic) OS999Glyph glyph;
@property(nonatomic) BOOL pressed;
@end

@implementation OS999ToolButton
- (BOOL)isFlipped {
  return YES;
}
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  Bevel(b, self.pressed);
  BOOL on = self.enabled;
  NSColor* fill;
  NSBezierPath* p = [NSBezierPath bezierPath];
  CGFloat cx = NSMidX(b), cy = NSMidY(b);
  switch (self.glyph) {
    case kBack:
    case kForward: {
      CGFloat s = self.glyph == kBack ? -1 : 1;
      [p moveToPoint:NSMakePoint(cx + 9 * s, cy)];
      [p lineToPoint:NSMakePoint(cx + 1 * s, cy - 8)];
      [p lineToPoint:NSMakePoint(cx + 1 * s, cy - 4)];
      [p lineToPoint:NSMakePoint(cx - 9 * s, cy - 4)];
      [p lineToPoint:NSMakePoint(cx - 9 * s, cy + 4)];
      [p lineToPoint:NSMakePoint(cx + 1 * s, cy + 4)];
      [p lineToPoint:NSMakePoint(cx + 1 * s, cy + 8)];
      [p closePath];
      fill = [NSColor colorWithCalibratedRed:0.30 green:0.69 blue:0.31 alpha:1];
      break;
    }
    case kReload: {
      [p appendBezierPathWithArcWithCenter:NSMakePoint(cx, cy) radius:7
                                startAngle:200 endAngle:500];
      p.lineWidth = 3;
      fill = nil;
      NSColor* c = on ? [NSColor colorWithCalibratedRed:0.15 green:0.35 blue:0.75 alpha:1] : Gray(0.6);
      [c setStroke];
      [p stroke];
      NSBezierPath* head = [NSBezierPath bezierPath];
      [head moveToPoint:NSMakePoint(cx + 3, cy - 11)];
      [head lineToPoint:NSMakePoint(cx + 9, cy - 6)];
      [head lineToPoint:NSMakePoint(cx + 2, cy - 3)];
      [head closePath];
      [c setFill];
      [head fill];
      return;
    }
    case kStop: {
      [p appendBezierPathWithRect:NSInsetRect(NSMakeRect(cx - 8, cy - 8, 16, 16), 1, 1)];
      fill = [NSColor colorWithCalibratedRed:0.80 green:0.15 blue:0.12 alpha:1];
      break;
    }
    case kHome: {
      [p moveToPoint:NSMakePoint(cx, cy - 9)];
      [p lineToPoint:NSMakePoint(cx + 10, cy)];
      [p lineToPoint:NSMakePoint(cx + 7, cy)];
      [p lineToPoint:NSMakePoint(cx + 7, cy + 8)];
      [p lineToPoint:NSMakePoint(cx - 7, cy + 8)];
      [p lineToPoint:NSMakePoint(cx - 7, cy)];
      [p lineToPoint:NSMakePoint(cx - 10, cy)];
      [p closePath];
      fill = [NSColor colorWithCalibratedRed:0.85 green:0.55 blue:0.25 alpha:1];
      break;
    }
  }
  [(on ? fill : Gray(0.72)) setFill];
  [p fill];
  [Gray(on ? 0.1 : 0.55) setStroke];
  p.lineWidth = 1;
  [p stroke];
}
- (void)mouseDown:(NSEvent*)e {
  if (!self.enabled) return;
  self.pressed = YES;
  [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent*)e {
  if (!self.pressed) return;
  self.pressed = NO;
  [self setNeedsDisplay:YES];
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  if (NSPointInRect(p, self.bounds)) [self sendAction:self.action to:self.target];
}
@end

@implementation OS999ToolBar {
  OS999ToolButton *_back, *_fwd, *_reload, *_home;
  NSTextField* _url;
  BOOL _loading, _secure;
}
- (BOOL)isFlipped {
  return YES;
}
- (OS999ToolButton*)button:(OS999Glyph)g x:(CGFloat)x action:(SEL)a {
  OS999ToolButton* b = [[OS999ToolButton alloc] initWithFrame:NSMakeRect(x, 5, 30, 26)];
  b.glyph = g;
  b.target = self;
  b.action = a;
  [self addSubview:b];
  return b;
}
- (instancetype)initWithFrame:(NSRect)f {
  if ((self = [super initWithFrame:f])) {
    _back = [self button:kBack x:8 action:@selector(onBack)];
    _fwd = [self button:kForward x:40 action:@selector(onForward)];
    _reload = [self button:kReload x:72 action:@selector(onReload)];
    _home = [self button:kHome x:104 action:@selector(onHome)];
    _url = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _url.bezeled = NO;
    _url.bordered = NO;
    _url.drawsBackground = YES;
    _url.backgroundColor = Gray(1);
    _url.font = [NSFont systemFontOfSize:13];
    _url.delegate = self;
    _url.cell.scrollable = YES;
    _url.cell.wraps = NO;
    _url.placeholderString = @"Search or enter address";
    [self addSubview:_url];
    [self setLoading:NO back:NO forward:NO];
  }
  return self;
}
- (NSRect)urlFrame {
  return NSMakeRect(164, 9, NSWidth(self.bounds) - 176, 18);
}
- (void)resizeSubviewsWithOldSize:(NSSize)old {
  _url.frame = [self urlFrame];
}
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  [Gray(0.87) setFill];
  NSRectFill(b);
  [Gray(0.45) setFill];
  NSRectFill(NSMakeRect(0, NSHeight(b) - 1, NSWidth(b), 1));
  // Inset well around the address field, with a lock for HTTPS.
  NSRect u = NSInsetRect([self urlFrame], -4, -3);
  u.origin.x -= 20;
  u.size.width += 20;
  [Gray(1) setFill];
  NSRectFill(u);
  [Gray(0.0) setStroke];
  [[NSBezierPath bezierPathWithRect:NSInsetRect(u, 0.5, 0.5)] stroke];
  [Gray(0.45) setFill];
  NSRectFill(NSMakeRect(u.origin.x - 1, u.origin.y - 1, u.size.width + 1, 1));
  NSRectFill(NSMakeRect(u.origin.x - 1, u.origin.y - 1, 1, u.size.height + 1));
  if (_secure) {
    NSRect l = NSMakeRect(u.origin.x + 7, u.origin.y + 10, 9, 8);
    [Gray(0.15) setFill];
    NSRectFill(l);
    NSBezierPath* s = [NSBezierPath bezierPath];
    [s appendBezierPathWithArcWithCenter:NSMakePoint(NSMidX(l), l.origin.y)
                                  radius:3 startAngle:180 endAngle:360 clockwise:NO];
    s.lineWidth = 1.5;
    [Gray(0.15) setStroke];
    [s stroke];
  }
}
- (void)setSecure:(BOOL)secure { _secure = secure; [self setNeedsDisplay:YES]; }
- (void)controlTextDidChange:(NSNotification*)note { [self setSecure:NO]; }
- (void)setURL:(NSString*)url {
  if (_url.currentEditor == nil) _url.stringValue = url ?: @"";
  _secure = NO;
  [self setNeedsDisplay:YES];
}
- (void)setLoading:(BOOL)loading back:(BOOL)back forward:(BOOL)forward {
  _loading = loading;
  _back.enabled = back;
  _fwd.enabled = forward;
  _reload.glyph = loading ? kStop : kReload;
  [_back setNeedsDisplay:YES];
  [_fwd setNeedsDisplay:YES];
  [_reload setNeedsDisplay:YES];
}
- (void)focusAddress {
  [self.window makeFirstResponder:_url];
  [_url selectText:nil];
}
- (void)onBack { [self.delegate chromeBack]; }
- (void)onForward { [self.delegate chromeForward]; }
- (void)onReload { [self.delegate chromeReloadOrStop]; }
- (void)onHome { [self.delegate chromeHome]; }
- (BOOL)control:(NSControl*)c textView:(NSTextView*)tv doCommandBySelector:(SEL)sel {
  if (sel == @selector(insertNewline:)) {
    [self.delegate chromeNavigate:_url.stringValue];
    return YES;
  }
  return NO;
}
@end

@implementation OS999StatusBar {
  NSPoint _start;
  NSRect _startFrame;
  BOOL _growing;
}
- (BOOL)isFlipped {
  return YES;
}
- (void)mouseDown:(NSEvent*)e {
  NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
  _growing = p.x > NSWidth(self.bounds) - 18;
  _start = [NSEvent mouseLocation];
  _startFrame = self.window.frame;
}
- (void)mouseDragged:(NSEvent*)e {
  if (!_growing) return;
  NSPoint now = [NSEvent mouseLocation];
  NSRect f = _startFrame;
  CGFloat dw = now.x - _start.x, dh = _start.y - now.y;
  f.size.width = MAX(self.window.minSize.width, f.size.width + dw);
  f.size.height = MAX(self.window.minSize.height, f.size.height + dh);
  f.origin.y = NSMaxY(_startFrame) - f.size.height;  // keep top edge fixed
  [self.window setFrame:f display:YES];
}
- (void)setText:(NSString*)text {
  _text = [text copy];
  [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty {
  NSRect b = self.bounds;
  [Gray(0.87) setFill];
  NSRectFill(b);
  [Gray(1) setFill];
  NSRectFill(NSMakeRect(0, 1, NSWidth(b), 1));
  [Gray(0.45) setFill];
  NSRectFill(NSMakeRect(0, 0, NSWidth(b), 1));
  [self.text drawAtPoint:NSMakePoint(8, 2)
          withAttributes:@{
            NSFontAttributeName : [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular],
            NSForegroundColorAttributeName : Gray(0.15)
          }];
  // Grow box: diagonal ridges in the corner.
  CGFloat x = NSWidth(b) - 14, y = NSHeight(b) - 14;
  for (int i = 0; i < 3; i++) {
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint:NSMakePoint(x + 4 + i * 4, y + 13)];
    [p lineToPoint:NSMakePoint(x + 13, y + 4 + i * 4)];
    [Gray(1) setStroke];
    [p stroke];
    NSBezierPath* q = [NSBezierPath bezierPath];
    [q moveToPoint:NSMakePoint(x + 5 + i * 4, y + 13)];
    [q lineToPoint:NSMakePoint(x + 13, y + 5 + i * 4)];
    [Gray(0.45) setStroke];
    [q stroke];
  }
}
@end
