# Display modes

The logical desktop is no longer fixed at 1024 x 768. Its size is a runtime
value chosen in Monitors & Display, saved per volume and restored before the
first frame. With nothing saved the desktop is 1024 x 768, as before.

## How the size is carried

`OSPI_W` and `OSPI_H` (in `include/ospi.h`) read two weak globals,
`ospi_display_w` and `ospi_display_h`. Weak definitions in the header give
every binary one shared copy without linking a particular source file, which
matters because many test binaries link only a few sources. `OSPI_MAX_W` and
`OSPI_MAX_H` (1920 each) are the compile-time constants for anything that
needs one; `OSPI_DEFAULT_W`/`OSPI_DEFAULT_H` are 1024 and 768.

Only `display_set_size()` and `display_mode_apply()` (`src/display_modes.c`)
change the size, and only to a size in the table. `Session.sw`/`Session.sh`
equal the current size after `display_mode_apply()` and after every
`session_paint()`.

## Modes

Fifteen landscape sizes in three shapes. Each also has a portrait mode with
width and height exchanged, so there are thirty modes.

| Shape | Landscape sizes | Portrait shape |
| --- | --- | --- |
| 4:3 | 640x480, 800x600, 1024x768, 1152x864, 1280x960 | 3:4 |
| 16:10 | 960x600, 1280x800, 1440x900, 1680x1050, 1920x1200 | 10:16 |
| 16:9 | 854x480, 1280x720, 1366x768, 1600x900, 1920x1080 | 9:16 |

854x480 and 1366x768 are the usual nominal 16:9 sizes, not exact ratios. The
narrowest desktop is 480 wide (480x640 and 480x854, the phone-like ones) and
the shortest is 480 high.

## Switching

Monitors & Display has a Desktop Size group: a Shape row (4:3, 16:10, 16:9)
that chooses which five sizes are listed, Landscape and Portrait buttons that
turn the current desktop on its side at once, and the five size buttons. The
Screen size row (75% to 200%) still scales the host window.

A switch calls `display_mode_apply()`: the session takes the new size, any
open menu closes, Finder windows are clamped into view with the title bar
below the menu bar, the application switcher is brought back on screen and
the control panel window is centred again. Desktop icons, dialogs and the
Appearance panel are placed from the current size each frame. On the host
side `src/desktop_display.c` replaces the frame buffers and the streaming
texture and resizes the window to desktop size times the Screen size percent,
held inside the usable display area with the shape kept. A running classic
app window is centred and scaled again; browser windows are moved and shrunk
to fit. The mode is saved in the control-panel store under `monitors` as
`desktop_width` and `desktop_height`, beside `window_scale`.

## The narrow menu bar

Rule, applied in `menu_title_rect()` whenever the titles plus the application
menu would not fit the desktop width:

1. The clock shortens to hours and minutes, then is not drawn (this was
   already in `paint.c`).
2. The space around each title tightens from 18 to 10 pixels.
3. Titles leave the bar one at a time in this order: Help, Appearance,
   Window, Special, View, Edit.

The fruit menu, File and the application menu never leave. A title that has
left has an empty rectangle: it is not drawn, cannot be clicked and is skipped
by the Left and Right keys. Its Cmd-key shortcuts still work. With the Finder
in front all nine titles fit at 480 wide with the tighter spacing; with a long
application name in front the test sees Help and Appearance leave.

## Monitors & Display on a small desktop

The control panel window is 620 wide, wider than the two 480-wide modes. Rule:
the window keeps its full size, opens at the left edge, and the Desktop Size
rows and the Screen size row sit in its left 460 pixels, so the way back to a
larger mode is on screen without moving anything. The rest of the panel is
reached by dragging the title bar sideways; the window pans until its far
edge meets the desktop edge.

## Not verified, and not adapted

Everything above was checked headlessly (`tests/test_display_modes.c`). None
of it has been seen in a real window: the host window resize, the texture
swap, pointer mapping against a real window, the browser overlay following a
switch and a classic app surface during a switch are implemented and
untested on screen.

Windows that are wider than a 480 or 600 wide desktop and were not reworked
are cut off at the right edge in those modes: the Appearance panel (600),
the startup Extensions Manager (640), OS999 Amp and wide Finder dialogs keep
their content layout. The classic guest runtime's own screen size, `mobile/`
and `keyboard/` are unchanged.
