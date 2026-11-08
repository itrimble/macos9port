#import <AppKit/AppKit.h>
#include "desktop_shortcuts.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL shortcuts %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    @autoreleasepool {
        NSMenu *root = [[NSMenu alloc] initWithTitle:@"Root"];
        NSMenu *window = [[NSMenu alloc] initWithTitle:@"Window"];
        NSMenuItem *parent = [root addItemWithTitle:@"Window" action:nil keyEquivalent:@""];
        parent.submenu = window;
        NSMenuItem *close = [window addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
        NSMenuItem *quit = [root addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
        NSMenuItem *minimize = [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"M"];
        NSMenuItem *other = [window addItemWithTitle:@"Other" action:@selector(copy:) keyEquivalent:@"w"];
        NSMenuItem *hide = [root addItemWithTitle:@"Hide" action:@selector(hide:) keyEquivalent:@"h"];
        NSMenuItem *control = [window addItemWithTitle:@"Control Close" action:@selector(performClose:) keyEquivalent:@"w"];
        control.keyEquivalentModifierMask = NSEventModifierFlagControl;
        close.keyEquivalentModifierMask = quit.keyEquivalentModifierMask = minimize.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        CHECK(desktop_shortcuts_strip(root) == 3);
        CHECK(!close.keyEquivalent.length && !quit.keyEquivalent.length);
        CHECK(close.action == @selector(performClose:) && quit.action == @selector(terminate:));
        CHECK(!minimize.keyEquivalent.length && minimize.action == @selector(performMiniaturize:));
        CHECK([hide.keyEquivalent isEqualToString:@"h"]);
        CHECK([control.keyEquivalent isEqualToString:@"w"]);
        CHECK([other.keyEquivalent isEqualToString:@"w"]);
        CHECK(desktop_shortcuts_strip(root) == 0);
        CHECK(desktop_shortcuts_strip(nil) == 0);
        [window release]; [root release];
    }
    printf("Native shortcuts: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
