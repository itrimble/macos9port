/* iOS: haptics through UIImpactFeedbackGenerator, and safe-area / density
 * reports for the bridge. Apple has no public hinge API, so the shell decides
 * cover versus inner from the screen's shape (mobile_posture_detect) and the
 * Control Strip's Posture tile covers the rest. */
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#include "os999_mobile.h"
#include "mobile_platform.h"

/* A keyboard case's trackpad pinch reaches apps only through a gesture
 * recognizer that accepts the indirect pointer; screen pinches stay with
 * SDL's touches (OS999's own two-finger recognizer). */
@interface Os999Pinch : NSObject <UIGestureRecognizerDelegate>
@end

@implementation Os999Pinch {
    CGFloat last;
}
- (void)pinch:(UIPinchGestureRecognizer *)g {
    if (g.state == UIGestureRecognizerStateBegan) last = 1;
    if (g.state != UIGestureRecognizerStateBegan && g.state != UIGestureRecognizerStateChanged) return;
    if (last > 0 && g.scale > 0) os999_mobile_report_magnify((float)(g.scale / last));
    last = g.scale;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)a
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)b {
    (void)a; (void)b;
    return YES;
}
@end

static Os999Pinch *pinch_target;
static UIImpactFeedbackGenerator *soft, *rigid, *heavy;
static UIEdgeInsets last_insets;
static CGFloat last_scale;

static UIWindow *key_window(void) {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *w in ((UIWindowScene *)scene).windows)
            if (w.isKeyWindow) return w;
    }
    return nil;
}

/* Keyboard haptic vocabulary (keyboard/docs/HAPTICS.md) onto impact styles:
 * crisp types are Rigid, rounder ones Soft, force and mode changes Heavy. */
static void ios_haptic(int type, float strength, float sharpness, int duration_ms) {
    (void)type; (void)duration_ms;
    UIImpactFeedbackGenerator *g = sharpness >= 0.75f ? rigid : (sharpness <= 0.45f ? heavy : soft);
    [g impactOccurredWithIntensity:strength];
    [g prepare];
}

void mobile_ios_start(void) {
    soft = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleSoft];
    rigid = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleRigid];
    heavy = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
    [soft prepare];
    [rigid prepare];
    os999_mobile_set_haptics(ios_haptic);
    /* Hardware keyboards (keyboard cases, folios, Bluetooth): report attach and
     * detach as they happen, so the on-screen keys give way at once rather
     * than at the first key press. */
    if (@available(iOS 14.0, *)) {
        os999_mobile_report_hw_keyboard(GCKeyboard.coalescedKeyboard != nil);
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserverForName:GCKeyboardDidConnectNotification object:nil queue:nil
                    usingBlock:^(NSNotification *n) { (void)n; os999_mobile_report_hw_keyboard(1); }];
        [nc addObserverForName:GCKeyboardDidDisconnectNotification object:nil queue:nil
                    usingBlock:^(NSNotification *n) { (void)n; os999_mobile_report_hw_keyboard(0); }];
    }
    os999_mobile_report_tablet(UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad);
    mobile_ios_poll();
}

void mobile_ios_poll(void) {
    UIWindow *w = key_window();
    if (!w) return;
    UIView *view = w.rootViewController.view;
    if (!pinch_target && view) {
        pinch_target = [Os999Pinch new];
        UIPinchGestureRecognizer *g = [[UIPinchGestureRecognizer alloc] initWithTarget:pinch_target
                                                                                action:@selector(pinch:)];
        g.allowedTouchTypes = @[ @(UITouchTypeIndirectPointer) ];
        g.cancelsTouchesInView = NO;
        g.delegate = pinch_target;
        [view addGestureRecognizer:g];
    }
    UIEdgeInsets in = w.safeAreaInsets;
    CGFloat scale = w.screen.nativeScale;
    if (UIEdgeInsetsEqualToEdgeInsets(in, last_insets) && scale == last_scale) return;
    last_insets = in;
    last_scale = scale;
    os999_mobile_report_insets((int)(in.left * scale), (int)(in.top * scale),
                               (int)(in.right * scale), (int)(in.bottom * scale));
    /* iPhones put about 153-163 pixels per inch on each point scale step;
     * iPads about 132. */
    BOOL pad = UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad;
    os999_mobile_report_display(pad ? 1 : -1, (float)(scale * (pad ? 132.0 : 153.0)));
}
