#import <AppKit/AppKit.h>
#include "desktop_shortcuts.h"

int desktop_shortcuts_strip(NSMenu *menu) {
    int changed = 0;
    for (NSMenuItem *item in menu.itemArray) {
        changed += desktop_shortcuts_strip(item.submenu);
        if (!(item.keyEquivalentModifierMask & NSEventModifierFlagCommand)) continue;
        BOOL close = item.action == @selector(performClose:) &&
                     [item.keyEquivalent.lowercaseString isEqualToString:@"w"];
        BOOL quit = item.action == @selector(terminate:) &&
                    [item.keyEquivalent.lowercaseString isEqualToString:@"q"];
        BOOL minimize = item.action == @selector(performMiniaturize:) &&
                        [item.keyEquivalent.lowercaseString isEqualToString:@"m"];
        if (close || quit || minimize) {
            /* SDL delivers the keystroke to the virtual desktop. Letting
             * AppKit also perform it closes or minimizes the host. Cmd-M is
             * Finder Make Alias, not the native macOS Minimize shortcut. */
            item.keyEquivalent = @"";
            changed++;
        }
    }
    return changed;
}

void desktop_shortcuts_init(void) {
    desktop_shortcuts_strip(NSApp.mainMenu);
}
