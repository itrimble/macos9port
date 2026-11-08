#include "browser_window_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OS999_ENABLE_TEST_HOOKS
/* Test hook (OSPI_BROWSER_SCRIPT): pushes real SDL events into OS999's normal
 * event queue at fixed times, so automated checks exercise the same routing
 * as a person. Steps: "ms:click:x,y" "ms:drag:x1,y1,x2,y2" "ms:key:Name" "ms:cmd:Name" "ms:text:abc"
 * "ms:ime:abc", separated by ';'. Coordinates are logical OS999 pixels. */

static void to_window(SDL_Window *win, int x, int y, int *wx, int *wy) {
    int ww = OSPI_W, wh = OSPI_H;
    SDL_GetWindowSize(win, &ww, &wh);
    *wx = x * ww / OSPI_W;
    *wy = y * wh / OSPI_H;
}

static void push_motion(SDL_Window *win, int x, int y, int held) {
    SDL_Event e;
    int wx, wy;
    to_window(win, x, y, &wx, &wy);
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = SDL_GetWindowID(win);
    e.motion.state = held ? SDL_BUTTON_LMASK : 0;
    e.motion.x = wx; e.motion.y = wy;
    SDL_PushEvent(&e);
}

static void push_button(SDL_Window *win, int x, int y, int up) {
    SDL_Event e;
    int wx, wy;
    to_window(win, x, y, &wx, &wy);
    memset(&e, 0, sizeof e);
    e.type = up ? SDL_MOUSEBUTTONUP : SDL_MOUSEBUTTONDOWN;
    e.button.windowID = SDL_GetWindowID(win);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = up ? SDL_RELEASED : SDL_PRESSED;
    e.button.clicks = 1;
    e.button.x = wx; e.button.y = wy;
    SDL_PushEvent(&e);
}

static void push_click(SDL_Window *win, int x, int y) {
    push_motion(win, x, y, 0);
    push_button(win, x, y, 0);
    push_button(win, x, y, 1);
}

/* Press at (x1,y1), move in steps to (x2,y2), release: resizes via the grow box. */
static void push_drag(SDL_Window *win, int x1, int y1, int x2, int y2) {
    push_motion(win, x1, y1, 0);
    push_button(win, x1, y1, 0);
    for (int i = 1; i <= 8; i++)
        push_motion(win, x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8, 1);
    push_button(win, x2, y2, 1);
}

static void push_key(SDL_Window *win, const char *name, Uint16 mod) {
    SDL_Keycode k = SDL_GetKeyFromName(name);
    if (k == SDLK_UNKNOWN) return;
    for (int up = 0; up < 2; up++) {
        SDL_Event e;
        memset(&e, 0, sizeof e);
        e.type = up ? SDL_KEYUP : SDL_KEYDOWN;
        e.key.windowID = SDL_GetWindowID(win);
        e.key.state = up ? SDL_RELEASED : SDL_PRESSED;
        e.key.keysym.sym = k;
        e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
        e.key.keysym.mod = mod;
        SDL_PushEvent(&e);
    }
}

/* Text and IME events go straight to the browser's handler: sdl2-compat
 * (SDL2 on SDL3) crashes when such events are pushed into its queue. */
static void push_text(Session *s, SDL_Window *win, const char *text, int ime) {
    /* SDL text events hold at most 31 bytes; longer typing arrives in pieces. */
    if (!ime) {
        size_t cap = sizeof ((SDL_TextInputEvent *)0)->text - 1, len = strlen(text);
        while (len > cap) {
            size_t n = cap;
            while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;  /* UTF-8 boundary */
            char part[sizeof ((SDL_TextInputEvent *)0)->text];
            memcpy(part, text, n);
            part[n] = 0;
            push_text(s, win, part, 0);
            text += n;
            len -= n;
        }
    }
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = ime ? SDL_TEXTEDITING : SDL_TEXTINPUT;
    if (ime) {
        e.edit.windowID = SDL_GetWindowID(win);
        snprintf(e.edit.text, sizeof e.edit.text, "%s", text);
        e.edit.start = 1;
    } else {
        e.text.windowID = SDL_GetWindowID(win);
        snprintf(e.text.text, sizeof e.text.text, "%s", text);
    }
    browser_window_event(s, win, &e);
}

void bw_script_tick(Session *s, SDL_Window *win) {
    static const char *script;
    static uint32_t start;
    static int started;
    if (!started) {
        started = 1;
        script = getenv("OSPI_BROWSER_SCRIPT");
        start = SDL_GetTicks();
    }
    if (!script || !*script) return;
    unsigned at = 0;
    char action[16], arg[96];
    if (sscanf(script, "%u:%15[^:]:%95[^;]", &at, action, arg) != 3) { script = NULL; return; }
    if (SDL_GetTicks() - start < at) return;
    fprintf(stderr, "os999-browser script %u %s %s\n", at, action, arg);
    if (strcmp(action, "click") == 0) {
        int x = 0, y = 0;
        if (sscanf(arg, "%d,%d", &x, &y) == 2) push_click(win, x, y);
    } else if (strcmp(action, "drag") == 0) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (sscanf(arg, "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) push_drag(win, x1, y1, x2, y2);
    } else if (strcmp(action, "key") == 0) push_key(win, arg, KMOD_NONE);
    else if (strcmp(action, "cmd") == 0) push_key(win, arg, KMOD_LGUI);
    else if (strcmp(action, "ctrl") == 0) push_key(win, arg, KMOD_LCTRL);
    else if (strcmp(action, "text") == 0) push_text(s, win, arg, 0);
    else if (strcmp(action, "ime") == 0) push_text(s, win, arg, 1);
    const char *next = strchr(script, ';');
    script = next ? next + 1 : NULL;
}

#else
void bw_script_tick(Session *s, SDL_Window *win) { (void)s; (void)win; }
#endif
