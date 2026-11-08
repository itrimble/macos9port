#ifndef OS999_DESKTOP_SHORTCUTS_H
#define OS999_DESKTOP_SHORTCUTS_H

/* Call after SDL has installed its native application/window menus. */
void desktop_shortcuts_init(void);
#ifdef __OBJC__
@class NSMenu;
int desktop_shortcuts_strip(NSMenu *menu);
#endif

#endif
