# Desktop menu audit (V08)

Audited 11 November 2026 on branch `desktop-menus`. Scope: the Finder menus
(999 fruit menu, File, Edit, View, Special, Help, Window). The guest Menu Manager
(`src/mac_menu*.c`) and the Appearance picker are out of scope.

Rule: an enabled item has an effect (state, volume change or a deliberate
dialog) or is already in the state it asks for; a disabled item says why.
Choosing a disabled item's key equivalent in the Finder shows a "Not Available"
note with the reason (`CMD_MENU_UNAVAILABLE` = 725, `src/desktop_menu_status.c`).
Keys are left alone while a browser, guest or media application is active, a
dialog is open, or the item has no key. Reasons live in one table,
`desktop_menu_reason()`.

`tests/test_desktop_menus.c` (run with `-v` for the full table) walks every menu
item in 14 contexts, 1011 probes, on a temporary volume: no windows, disk icon,
Trash icon, folder window with nothing / a file / a folder / everything selected,
text window, Trash window, dialog open, console, alias, list view, note.
Enabled items must change something; disabled items must refuse to activate and
carry a reason.

## What the audit found (before this change)

| Item | CMD | Before | After |
|---|---|---|---|
| Tailscale, Quake III Arena, Quake III Demo (999 menu) | CMD_TAILSCALE, CMD_QUAKE3, CMD_QUAKE3_DEMO | handlers existed but the rows were always dimmed, no reason | enabled; Quake shows its own "put pak0.pk3 here" dialog when data is missing |
| Special > Sleep | CMD_SLEEP | always dimmed, no handler | dims the desktop until the next input |
| Edit > Cut | (was CMD_NONE, no key) | dimmed, silent | CMD_CUT, still dimmed on purpose (files are never moved through the clipboard); Cmd-X explains |
| Edit > Copy in a terminal with nothing selected | CMD_COPY | enabled, silent | dialog: select terminal text first |
| Edit > Paste into a terminal or note with an empty clipboard | CMD_PASTE | enabled, silent | dialog: clipboard is empty |
| View > Reset Column Positions with default columns | CMD_RESET_COLUMNS | enabled, no change | dimmed until a column has been dragged |
| View > as Window / as Pop-up Window | CMD_AS_WINDOW, CMD_AS_POPUP | no check mark, so a repeat looked like a no-op | check mark shows the current mode |
| every dimmed item | | no reason anywhere | reason table; key equivalent shows it |

## Current state

Works = has an effect in a context where it is enabled. "Dimmed" items are
listed with the context that dims them.

| Menu | Item (CMD) | State | Dimmed when / why |
|---|---|---|---|
| 999 | About This Mac, Control Panels and each panel, Appearance, Font Book 999, Favorites, Recent lists, Terminal, OS999 Console, Chromium Browser, OS999 Amp, Shut Down | works | never dimmed (modal dialog dims everything) |
| 999 | Tailscale, Quake III Arena (+ Demo) | works (Quake not run in tests) | |
| File | New Folder (CMD_NEW_FOLDER) | works | search-results window |
| File | New Note | works | browser or guest in front |
| File | Open (CMD_OPEN), Get Info (CMD_GET_INFO) | works | nothing selected (Get Info also works on the window) |
| File | Close Window, Close All | works | no windows |
| File | Label (and 8 labels), Duplicate, Make Alias, Add to Favorites | works | no selection |
| File | Put Away (CMD_PUT_AWAY) | works | selection not inside the Trash window |
| File | Find..., Find Again, Show Original | works | Find Again: no search yet. Show Original: not a single alias |
| File | Rename..., Move to Trash | works | no selection; Trash, System Folder; items already in Trash |
| File | Print, Page Setup..., Print Window... | works when a printer connection exists | headless/no host printer, or nothing printable |
| File | Encrypt (CMD_ENCRYPT) | not implemented | always dimmed: encryption does not exist yet |
| File | Install from Garden, New Terminal, New OS999 Console, Quit | works | |
| Edit | Undo (CMD_UNDO) | works | nothing to undo |
| Edit | Cut (CMD_CUT) | deliberately unavailable | always |
| Edit | Copy Names / Copy Text / Copy (CMD_COPY) | works | no selection, empty text window |
| Edit | Paste (CMD_PASTE) | works in notes and terminals | other windows |
| Edit | Clear (CMD_CLEAR) | works (moves to Trash) | no selection, or already in Trash |
| Edit | Select All, Show Clipboard, Preferences... | works | Select All: no items/editable note |
| Edit | Clear Scrollback, Toggle Terminal Theme | works | no terminal in front |
| View | as Icons / Buttons / List, Clean Up, Arrange (6 sorts), View Options... | works | no folder window; desktop only allows icons |
| View | as Window / as Pop-up Window, Reset Column Positions | works | desktop surface; columns not moved |
| Special | Empty Trash... (CMD_EMPTY_ASK) | works: confirmation, then delete; "Trash is empty" otherwise | |
| Special | Eject (CMD_EJECT), Erase Disk... (CMD_ERASE) | not offered | always dimmed: OS999 HD is the only volume and the startup disk |
| Special | Sleep, Restart..., Shut Down, Startup Animation, Preview Startup, Interface Sounds, Supported Audio Formats... | works | |
| Help | OS999 Help, Keyboard Shortcuts, What Works Today, Record Local Usage Log | works (dialogs) | |
| Window | Hide / Hide Others / Show All, Application Switcher, application and window list | works | Hide/Show All dimmed when there is nothing to hide |

## Not covered

No real window was used. Everything above is headless. Dialog wording, dimmed
appearance, the key notice and the Sleep dimming were not seen on screen. Quake
launch, Terminal (host shell) and Tailscale were enabled but not run in the
sweep; printing was not exercised (no printer connection in tests). Media
panel items were checked with a fake hook, not the real player. Cut, Encrypt,
Eject and Erase Disk remain unimplemented by decision.
