#ifndef OS999_BROWSER_IPC_H
#define OS999_BROWSER_IPC_H

/* Narrow IPC between the OS999 desktop (ospi) and its one OS999 Browser
 * helper process (checklist B04). Fixed-size records over one inherited
 * socket. Each tab is a browser with a desktop-chosen id. GPU frames travel
 * separately: IOSurface Mach ports on macOS, shared memory on Linux. */

#include <stdint.h>

enum { OS999B_TEXT = 1000, OS999B_MAX_ID = 64 };

typedef struct {
    uint32_t kind;
    int32_t id;         /* browser (tab) id, 1..OS999B_MAX_ID-1; 0 = helper */
    int32_t x, y;       /* view DIP coordinates, top-left origin */
    int32_t a, b;       /* kind-specific */
    uint32_t modifiers; /* CEF EVENTFLAG_* bits */
    uint32_t keycode;   /* macOS virtual key code / X11 keysym on Linux */
    uint32_t ch, uch;   /* UTF-16 character / unmodified character */
    char text[OS999B_TEXT];
} Os999BrowserMsg;

enum {
    /* desktop -> helper */
    OS999B_CREATE = 1,   /* text=url a=width b=height x=scale*100 */
    OS999B_DESTROY,
    OS999B_SHOW,         /* a=visible (active tab of a shown window) */
    OS999B_RESIZE,       /* a=width b=height (DIP), x=scale*100 */
    OS999B_MOUSE_MOVE,
    OS999B_MOUSE_DOWN,   /* a=button (0 left,1 middle,2 right) b=clicks */
    OS999B_MOUSE_UP,
    OS999B_MOUSE_LEAVE,
    OS999B_WHEEL,        /* a=dx b=dy (DIP) */
    OS999B_KEY_DOWN,
    OS999B_KEY_UP,
    OS999B_CHAR,         /* text: committed UTF-8 */
    OS999B_IME_SET,      /* text: composition UTF-8, a=cursor (UTF-16 units) */
    OS999B_IME_CANCEL,
    OS999B_NAVIGATE,     /* text: URL */
    OS999B_BACK,
    OS999B_FORWARD,
    OS999B_RELOAD,
    OS999B_STOP,
    OS999B_FOCUS,        /* a=0/1 */
    OS999B_EXIT_FULLSCREEN,
    OS999B_TARGET,       /* text: frame target name (Linux shared memory) */
    OS999B_SHUTDOWN,
    OS999B_APP_INSTALL = 24, /* user-requested install of current manifest */
    OS999B_APP_REMOVE = 25, /* remove current start-page shortcut; keep site data */
    /* helper -> desktop */
    OS999B_TITLE = 100,  /* text */
    OS999B_ADDRESS,      /* text */
    OS999B_LOADING,      /* a=loading b=(can_back | can_forward<<1) */
    OS999B_CURSOR,       /* a=cef_cursor_type_t */
    OS999B_STATUS,       /* id 0, text: frame statistics */
    OS999B_FULLSCREEN,   /* a=0/1 */
    OS999B_CLOSED,       /* browser finished closing */
    OS999B_OPEN_URL,     /* text=url a=1 new window, 0 new tab (popups) */
    OS999B_DOWNLOAD,     /* text=file name a=percent b=0 running 1 done 2 failed */
    OS999B_IME_RECT,     /* x,y,a=w,b=h caret bounds in view DIP */
    OS999B_READY,        /* id 0: helper initialised */
    OS999B_SECURITY,     /* id 0: a=verdict b=blocking text=message (B06) */
    OS999B_TLS,          /* per-tab: a=verified b=blocked text=reason */
    OS999B_APP_RESULT,   /* per-tab: a=success text=message */
    OS999B_CAPABILITIES  /* built-in diagnostics only; text=JSON */
};

/* Mach message carrying one presented frame (macOS only). */
#define OS999B_FRAME_MSG_ID 0x0599B002

#endif
