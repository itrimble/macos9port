# PowerPC runtime: real-file traces

The dated record of runs of one real PowerPC application (kept outside Git, not named here) through `ospi-runtime`, moved out of
[POWERPC-RUNTIME.md](POWERPC-RUNTIME.md) when that file passed 500 lines. Each section says what ran, what stopped it, and what was not
verified. Nothing here was seen in a real window.

## Real-file evidence — 10 November 2026

First run against a real application supplied by Ian (kept outside Git under
`~/os999-guest-software/`): DesignWorkshop Lite 1.8.5, a PowerPC-only PEF.

- `--inspect` reads its `cfrg` and PEF: 3 sections (code 3,016,064 bytes,
  pattern data 443,000 unpacked from 262,659, loader 37,193), main entry in
  section 1, and 893 imports across 7 libraries (InterfaceLib 518, QuickDraw 3D
  325 weak, MathLib 22, QuickTimeLib 17, ObjectSupportLib 6, QuickDraw 3D
  Accelerator 4 weak, LightWorks Editor 1 weak). The per-library counts add up
  to the total and the names are sensible, which supports the loader-table,
  string-table and weak-library readings. The real relocation program ran to
  completion without a rejected opcode.
- Two defects found and fixed: the `cfrg` member declares 68 bytes where 56
  exist (now tolerated when the fixed part and name fit), and the entry code
  uses the string moves `lswi`/`lswx`/`stswi`/`stswx` (now implemented, with
  tests).
- `OS999_PPC_TRIAL=<instructions> ospi-runtime --inspect <app>.rsrc` runs the
  real entry point. It executes compiler-generated startup code and halts at
  the first Toolbox call: `Unsupported PowerPC import InterfaceLib::NewPtr`.

Still unverified against real files: large-repeat counts, export hashing and
pattern opcodes 3 and 4 beyond what this one application exercises. The next
step is the InterfaceLib bridge onto the existing Toolbox services.

### Bridge slice, same day

`ospi-runtime --indexed --steps 20000000 <app>.rsrc` with
`OS999_PPC_TRACE=1`, headless. The fragment loads above A5, all 893 imports
are accounted for (330 weak imports resolve to null, 473 are bound, 90
have no row or native function) and the entry point runs 610,399 PowerPC
instructions and 444 import calls before halting. Exit code 3.

Imports in call order (a count after a name means consecutive calls):

1. Runtime setup: `NewPtr` 2, `NewRoutineDescriptor` 13, `NewPtr`.
2. Toolbox initialisation: `InitGraf`, `InitFonts`, `InitWindows`,
   `InitMenus`, `TEInit`, `InitDialogs`, `InitCursor`.
3. `NGetTrapAddress` 4, then the same seven initialisation calls again.
4. `CurResFile`, `MaxApplZone`, `MoreMasters` 150, `NewRoutineDescriptor`,
   `SetGrowZone`, `NewHandle`.
5. Environment checks: `NGetTrapAddress` 8, `SysEnvirons`, `NGetTrapAddress`
   8, `SysEnvirons`, `Gestalt` 5.
6. `NewHandle` 2, `GetCursor`, `HNoPurge`, `GetCursor`, `HNoPurge`, `NewRgn`,
   `NewPtr`, `Get1Resource`, `ResError`, `HLock`, `HUnlock`, `BlockMoveData`
   2, `PBGetFCBInfoSync` 3.
7. Steps 4 and 5 a second time, then `NewHandle` 2, `GetCursor`, `HNoPurge`,
   `GetCursor`, `HNoPurge`, `NewRgn`.
8. `GetResource` and `HNoPurge` six times each, alternating; `NewRgn`,
   `NewHandle`, `NewPtr`.
9. A port of its own: `OpenPort`, `GetGrayRgn`, `PortSize`, `MovePortTo`,
   `SetOrigin`, `SetClip`, `CopyRgn`.
10. `NewHandle` 2, `SetHandleSize`, `MemError`, `BlockMoveData`, `SetPt` 2,
    `GetResource` (the `WIND` 200 resource), `GetNewWindow`.

It stops there: `Toolbox trap $A9BD behind PowerPC import GetNewWindow is not
implemented.` The 68K runtime's Window Manager has `NewCWindow` and
`GetNewCWindow` only. As an experiment that was not kept, `GetNewWindow` was
routed to the `GetNewCWindow` path; the Window Manager then refused the
window with `Only standard zoomNoGrow, noGrowDocProc, plainDBox and dBoxProc
windows (and custom WDEF ids 1-255, shown with titled chrome) are supported.`
So the next two things between this application and its first window are
old-style window creation and the window definition its `WIND` 200 asks for,
both in the 68K runtime's Window Manager and not in the bridge.

What this run does and does not show. It shows compiler-generated PowerPC
code driving 38 distinct InterfaceLib imports through the existing services
on shared guest RAM, with every Pascal frame balanced. It does not show any
drawing, any window, the menu bar, the event loop, a callback from the
Toolbox into the application, or a weak-library check (the application had
not asked Gestalt about QuickDraw 3D or called a null import by the time it
stopped). The application calls its initialisation sequence twice; whether a
real Macintosh shows the same order was not checked. `PortSize` and
`MovePortTo` did not exist in the 68K runtime and were added for step 9.

## Second real-file run: window, controls, menus — 11 November 2026

Same application, same command (`OS999_PPC_TRACE=1 ospi-runtime --indexed --steps 20000000 <app>.rsrc`), headless, run
on a copy kept outside Git. Before this change the run stopped at 472 import calls and 618,092 instructions. The
`GetNewWindow` stop recorded above had already been removed by the earlier window work: this application calls
`GetNewCWindow` for its `WIND` 200 (procID 8, 286 x 108, created hidden), which the 68K Window Manager creates; the
`GetNewWindow` ($A9BD) path was therefore not exercised by this program.

What the trace showed. After `GetNewCWindow` the application builds a pane object, then a scroll bar from `CNTL` 300
(rectangle all zero, procID 16) and sizes it. The sizing call was `SizeControl(control, -15, 16)`: the program asks for
the pane width minus 15, and the pane's width field is still 0 when the scroll bar is made. A trace of the program's own
stores confirmed the width field (an offset in a C++ pane object) is first written 0 and not touched before the
call. The Control Manager here halted with `SizeControl needs a positive width and height.`

Change: `SizeControl` ($A95C) no longer validates its size. The rectangle becomes `(top, left, top + h, left + w)` in
16-bit arithmetic, as the call is described; a zero or negative size leaves an empty rectangle, which the existing
drawing and hit-testing already treat as nothing (`box()` in `mac_control_draw.c` rejects it). The test in
`tests/test_mac_controls.c` now sizes a control to (-15, 16) and (0, 0), checks the stored rectangle, checks that
`FindControl` reports no part inside it, and sizes it back. The old "SizeControl to nothing halts" case was replaced by
`SizeControl` of a nil handle, which still halts. That this is what a real Macintosh does is my reading of the call, not
something I checked on one.

After the change the run reaches 590 import calls and 638,441 instructions, 67 distinct imports. New calls between 472
and 590, in order of first appearance: `SetControlAction`, `GetClip`, `SetRect`, `GlobalToLocal`, `OffsetRgn`, `InvalRgn`,
`ValidRect`, `HideControl`, `NewHandleClear`, `StringToNum`, `LMGetMBarHeight`, `GetMenuItemText`, `SetMenuItemText`,
`CountMItems`, `GetItemCmd`, `GetMenu`, `InsertMenu`. `GetMenu` of `MENU` 1 and `InsertMenu` succeed. The second
`GetNewControl` of `CNTL` 300 and the repeated port/origin/clip sequences run without a halt.

It stops at `GetMenu` of `MENU` 2 (the File menu). Its procID field is 19999 and the application carries a 25,617-byte
`MDEF` 19999 in 68K code; six of its other menus name the same procedure. The Menu Manager here draws only its own
standard pull-down and refuses a menu that names another definition procedure. That refusal was already there; the
message now names the MDEF (`GetMenu: MENU 2 names custom menu definition MDEF 19999, which is unsupported.`). The
exit code is 3.

Blocker. Going further needs custom menu definition procedures: running the application's 68K MDEF for the size,
draw, choose and pop-up messages from `GetMenu`/`CalcMenuSize`, `DrawMenuBar`, `MenuSelect`, `MenuKey` and `PopUpMenuSelect`,
from a PowerPC process. That is a subsystem of its own (the CDEF dispatch in `mac_cdef.c` is the nearest existing piece),
not a one-trap addition. Treating the MDEF as absent and drawing standard menus was not done: it would change what the
program shows and chooses without saying so.

What this run does and does not show. It shows window creation, a pane/scroll-bar construction sequence and the start of
menu setup running as compiler-generated PowerPC code on the shared services. It does not show a frame: the window is
created hidden and nothing has been drawn into it, so there is no headless frame dump to point to. It does not show an
event loop, a callback from the Toolbox into the application, `WaitNextEvent`, or any visible window. Nothing was seen
in a real window.

## Third real-file run: custom menu definitions — 11 November 2026

Same application and command as the second run (`OS999_PPC_TRACE=1 ospi-runtime --indexed --steps 20000000 <app>.rsrc`), headless, on a copy kept
outside Git (sha256 of the data fork `bfe75af7...f72e`, of the resource fork `995486d1...3700b`). Before: 590 import calls, 638,441 PowerPC
instructions, halt `GetMenu: MENU 2 names custom menu definition MDEF 19999, which is unsupported.`

What changed in the runtime. A menu whose MENU resource names a menu definition procedure now runs that 68K code (`src/mac_mdef.c`,
`src/mac_menuselect.c`, `src/mac_menu_records.c`). `GetMenu`, `GetNewMBar` and `CalcMenuSize` send the size message and deliver their result
only when the procedure returns; `MenuSelect` and `PopUpMenuSelect` send size, (pop-up), draw and then one choose message per time slice. The
call is a guest call from inside the trap service, so a PowerPC caller reaches it through the 68K trap stub like any other blocking trap; the
first test of that path from PowerPC code is `build/test_mac_ppc_mdef`. Standard menus take exactly the paths they took before. Details are in
the header comment of `src/mac_mdef.c` and `include/mac_mdef.h`.

What the application needed beyond that, in the order the trace hit it (each stop is the halt message the next change removed):

| Stop | Cause | Change |
| --- | --- | --- |
| `PaletteDispatch selector 0x0c19 is unsupported.` | the MDEF asks `GetGray` for the colour of a disabled item | `GetGray` added: half way between the background and black, white for a dark background. That rule is my reading of the call, not checked against a real Macintosh |
| `unhandled A-line trap a898` | `GetPenState` on the Window Manager port, a plain `GrafPort` | `GetPenState` and `SetPenState` for plain ports (the 18 bytes at `pnLoc`) |
| `unhandled A-line trap a8b5` | `ScriptUtil` selector `$82000000` (`FontScript`) | a Roman-only Script Manager (`src/mac_script.c`): `FontScript`, `IntlScript`, `Font2Script`; any other selector halts naming itself |
| `Script Manager selector 82020006 is unsupported.` | `Font2Script` | added |
| `Font family number is outside the supported range.` | the MDEF's own font is a `FOND` with ID 32700 (and `NFNT` 32716, 32728) named `.MDEF Font`; the Font Manager refused families above 255 | families up to 32767 are looked up (a `FOND` is how a family above 255 is reached) |
| `unhandled A-line trap aa14` | `RGBForeColor` while the current port was the plain Window Manager port | with colour QuickDraw in use the definition now runs in a screen-sized colour port (opened once, by the existing `OpenCPort` service) |

An unintended side effect, noted so a diff of traces is not misread: answering the Script Manager trap makes the application's check for it
succeed, and it then makes two more `Gestalt` calls (imports 208 and 408), so every later import number is two higher than in the second run.

After: 1,032 import calls, 670,344 PowerPC instructions (744,760 on the 68K counter, which includes the definition's code), 2,254 traps. Seven
`MENU` resources that name MDEF 19999, and the `MENU` 20 that names three submenus, are loaded; the eight `GetMenu` calls and eight `InsertMenu`
calls of the menu bar setup complete, including the sizing of the custom menus by the real 68K procedure. It halts, exit code 3, with:

    AppendResMenu menu is invalid.

Blocker. The call is `AppendResMenu(GetMenuHandle(50), 'FONT')`. `GetMenuHandle(50)` is nil: `MENU` 50 ("Font Submenu", no items) is the
submenu of `MENU` 20 (its first item has command 27 and mark 50, and items 2 and 3 name `MENU` 11 and 9), and the application never calls
`GetMenu(50)` or `InsertMenu(..., -1)` before this point. Either the real Menu Manager makes a menu's submenus reachable by ID when the
parent is inserted, or the program loads them later and has an ordering bug. I could not tell which: it needs the application's PowerPC
code disassembled around the call (`lr=00a11730`), and I did not guess. As an experiment that was not kept, `GetMenuHandle` was made to load a
missing ID from its `MENU` resource and register it as a hierarchical menu. With that the run went on to 1,162 import calls and drew a
45,486-byte `PICT` (`DrawPicture`: 440 x 302 output, 132,000 pixels painted, headless), then stopped at `Toolbox trap $AB1D behind PowerPC
import GetGWorld is not implemented.` The 68K runtime has no offscreen graphics worlds (`NewGWorld`, `GetGWorld`, `LockPixels` and so on); that is a
subsystem of its own and is the next large piece after the submenu question.

Not verified. No window was opened for this run: it is `--steps` headless, the screen was never looked at, and `DrawPicture` painting 132,000
pixels says nothing about what they look like. The menu sizes and the draw and choose messages were exercised only against a synthetic
procedure in the tests; the application's own procedure was run for the size message (and whatever it calls) and has not yet been sent a draw
or a choose message by this application. No pop-up or `MenuSelect` has been driven with the real procedure. The first window is not drawn.


## Fourth real-file run: submenus, offscreen worlds, a first long stretch — 12 November 2026

Same application, command and copy as the third run (headless `--steps`, `OS999_PPC_TRACE=1`). Before: 1,032 import calls, 670,344 PowerPC
instructions, halt `AppendResMenu menu is invalid.` After: 2,554 import calls, 929,567 PowerPC instructions (1,068,948 on the 68K counter, 5,228
traps), halt, exit code 3:

    Unsupported PowerPC import InterfaceLib::PrOpen (called from 00940130).

The submenu question. The public Menu Manager text for `InsertMenu` (read this session) says a menu inserted with ID -1 is how a submenu enters
the menu list, and says nothing that makes `GetMenu`, `GetNewMBar` or the parent's `InsertMenu` do it. So the application must load submenus
itself, and the trace showed it looping over every item of every menu with `GetItemCmd`. The cause was ours: `GetItemCmd` and `GetItemMark` stored
one byte, but the C form of both calls takes a `short` pointer and the application reads two bytes, so the command 27 of the submenu items never
compared equal. Both now store a 16-bit value (the character in the low byte); `GetItemStyle` stays a byte. With that the application calls
`GetMenu` and `InsertMenu(..., -1)` for its submenus (1,742 imports at that point). The earlier experiment that loaded a missing ID inside
`GetMenuHandle` was wrong and is gone. I checked the 16-bit width from the C interface (from memory) and from the effect on the real trace; I did
not find a page that states the width.

Changes between 1,742 and 2,554 imports, in the order the trace hit them:

| Stop | Change |
| --- | --- |
| `$AB1D` `GetGWorld` not implemented | offscreen worlds, see below |
| `SetWRefCon` on a dialog | `GetNewDialog` makes a dialog record outside the window registry; `GetWRefCon`/`SetWRefCon` now work on one (the refCon is at +152) |
| `TEDispatch` selector 9 | `TEGetHeight`: the height of the lines from `startLine` up to `endLine`, `lineHeight` each. That the range is half-open is from how programs call it (`nLines`, 0), not from a page |
| import `TESetClickLoop` | library glue that stores the routine in the TERec; a `clikLoop` is now allowed (nothing calls it: `TEClick` does not track a drag). `TESetWordBreak` likewise, but a `wordBreak` still halts |
| `TECalText` ($A9D0) | recalculates the line starts |
| import `EqualString` | library glue over the string compare (ASCII case folding only) |
| import `MathLib::num2dec` | the float and fixed styles of the decimal record; layout, rounding and the dropping of trailing zeros in float style are from memory and have not been compared with the original |
| `CopyBits mixed pixel depths unsupported.` | `CopyBits` between 1, 8, 16 and 32 bits per pixel, srcCopy only, through RGB (below) |
| `CopyBits nondefault port color mapping unsupported.` | in a converting transfer a 1-bit source is coloured with the destination colour port's foreground and background |
| `$A952` `DeleteMenuItem` | removes the item; the enable bits of later items move up |

Offscreen worlds (`src/mac_gworld.c`, `$AB1D`, selector in D0). `NewGWorld`, `UpdateGWorld`, `DisposeGWorld`, `GetGWorld`, `SetGWorld`,
`GetGWorldPixMap`, `GetPixBaseAddr`, `LockPixels`, `UnlockPixels`, `PixMap32Bit`, `QDDone`, `GetGWorldDevice`, `AllowPurgePixels`, `NoPurgePixels`,
`GetPixelsState`, `SetPixelsState` and the four `*Changed` calls. A GWorld is a colour port made by the existing `OpenCPort` service whose PixMap
points at `NewPtr` storage; depths 1, 2, 4, 8, 16 and 32 can be created (16 is 1-5-5-5, 32 is 8-8-8-8 with an unused first byte), rows padded to 4
bytes, other depths and empty or oversized bounds give `paramErr` or `memFullErr`. Limits: no GDevice of its own (the main device stands in), flags
ignored, pixels cleared to zero, and the Memory Manager here is a bump allocator, so disposed blocks and port records are not reused. Drawing into a
GWorld works where QuickDraw already draws on a colour port (8-bit indexed); at other depths QuickDraw halts with its own message, so those worlds can
be created, filled and copied but not drawn on. The routine numbers other than the ones already in the bridge table (`$0F`, `$16`, `$17`, `$00`-`$06`)
are from memory. The PowerPC bridge has rows for all of them.

`CopyBits` (`src/mac_copybits.c`) now accepts 16- and 32-bit direct PixMaps and mixed depths. Only srcCopy; anything else between different formats
halts naming the mode. A destination at 8 bits gets the nearest colour of its table (squared distance on a 5-bit-per-channel grid), at 1 bit white or
black by luminance. Same-format 8-to-8 and 1-to-1 transfers take the old path unchanged.

What the 2,554 calls include: 29 `CopyBits` calls, one `DrawPicture`, three `GetNewCWindow`. No `NewGWorld` was reached before the stop, so the
offscreen code has been run by the real application only through `GetGWorld`; everything else about it is tested synthetically.

Blocker. `PrOpen` is the Printing Manager (`PrOpen`, `PrintDefault`, `PrValidate`, `PrStlDialog`, `PrError`, ...). The application opens it while
setting up its page-layout state. That is a subsystem of its own, and what a real Macintosh with no printer chosen answers (what `PrError` holds
after `PrOpen`, which resources it needs) is not something I know, so I did not stub it. The first window is not drawn; no `ShowWindow` or
window-update call was reached.

Not verified: everything here is headless; no frame was looked at. The 29 `CopyBits` calls completed, but their pixels have not been inspected.
Tests exist for the offscreen worlds (`build/test_mac_gworld` from the 68K side, `build/test_mac_ppc_gworld` from PowerPC code), the converting `CopyBits`
(inside `test_mac_gworld`), `DeleteMenuItem`, `TEGetHeight`, `TECalText`, `GetItemCmd`'s width, `EqualString` and `num2dec` (`build/test_mac_ppc_libs`).
`TESetClickLoop`, `TESetWordBreak` and the dialog `refCon` change have no test of their own; only the real application has run them.


## Fifth real-file run: printing, a long stretch of start-up, and a folder it cannot find — 12 November 2026

Branch `ppc/printing` from main `8950fd6`. Same application and copy, headless `--steps`, `OS999_PPC_TRACE=1`. Two ways of running it matter now:
alone (`dwl.rsrc` in an otherwise empty directory, `--saves` pointing at a scratch directory) and from a full copy of the application folder.

| Run | Imports | PowerPC instructions | Stops at |
| --- | --- | --- | --- |
| Before (fourth run) | 2,554 | 929,567 | `Unsupported PowerPC import InterfaceLib::PrOpen` |
| Alone, no `--saves` | 2,597 | 1,017,487 | a note alert (`ALRT` 4003) waiting for a click |
| Alone, `--saves` scratch | 57,138 | 15,104,713 | the same alert, after a long stretch of start-up |
| Full folder copy | 2,597 | 1,017,487 | the same alert |

Printing Manager for "no printer" (`src/mac_print.c`, trap `$A8FD`, selector long on the stack; bridge rows in `mac_ppc_sigs_toolbox.c`). What the
public Printing Manager chapter (read this session; my words) establishes: the routine selectors (`PrOpen` `$C8000000`, `PrClose` `$D0000000`,
`PrError` `$BA000000`, `PrSetError` `$C0000200`, `PrDrvrOpen` `$80000000`, `PrDrvrClose` `$88000000`, `PrintDefault` `$20040480`, `PrValidate`
`$52040498`, `PrStlDialog` `$2A040484`, `PrJobDialog` `$32040488`, `PrJobMerge` `$5804089C`, `PrOpenDoc` `$04000C00`, `PrCloseDoc` `$08000484`,
`PrOpenPage` `$10000808`, `PrClosePage` `$1800040C`, `PrPicFile` `$60051480`), that errors are reported through `PrError`, and a list of result
codes of which `PAPNoPrinter` (-4101) is the one tied to "no printer found, closed or not selected". What it does not establish, as far as I could
read it before my Firecrawl credits ran out: the exact code a real Macintosh gives from `PrOpen` when nothing is chosen in the Chooser. So -4101 is
the most conservative documented code, not an observed one. Behaviour: `PrOpen` and `PrDrvrOpen` set `PrintErr` ($944) to -4101; every routine that
needs a driver sets the same, shows no dialog, returns FALSE or nil and leaves the caller's records untouched; `PrClose`, `PrDrvrClose` clear it;
`PrError` returns it; `PrSetError` stores it. Nothing is spooled and nothing pretends to print. The application calls `PrOpen` twice, checks
`PrError` each time, and carries on, which is the graceful path.

Other stops, in order (each removed by the change beside it):

| Stop | Change |
| --- | --- |
| `ParamText requires a Pascal string, not NIL.` | The public page says to pass empty strings for unused parameters and does not cover NIL. The application passes NIL, so a NIL now leaves that parameter as it was. This is an inference from its use, not from a page |
| import `HGetFInfo` | `HGetFInfo` and `HSetFInfo` glue over `PBHGetFInfo`/`PBHSetFInfo` |
| import `MathLib::dec2num` | exact inverse of `num2dec`'s record (host `strtod`) |
| import `IUEqualString` | 0 when equal ignoring case, else 1 (ASCII folding only) |
| `Only unpacked indexed-8 color PixMaps are supported.` | the application makes a 1-bit offscreen world and erases a rectangle in it; rectangle-like calls on colour ports now work at 1, 2 and 4 bits (pixels packed and unpacked, the port's foreground and background mapped to the PixMap's table by nearest colour). Text, lines and pixel patterns on those depths still halt |
| `DisposeGWorld: the pointer is not a GWorld` | `DisposeGWorld(NIL)` does nothing (the application makes that call; the public text does not cover it) |
| `Toolbox trap $A9C7 ... DateToSeconds` | `DateToSeconds`, the inverse of the existing `SecondsToDate` (no time zone) |
| import `GetIndPattern` | glue over `GetResource('PAT#')` |
| import `HCreate` | `PBHCreate` plus `PBHSetFInfo` for type and creator; `HDelete` too |

The stop. `ALRT` 4003 is the application's generic error alert: "could not be completed, because ^0 (^2)". Its text in this run: "DesignWorkshop was
unable to find the "Textures" folder." (from the full-folder copy) or "could not read the Material Resource" (alone, after `PBDirCreate` made an
empty `Textures` in the save overlay). The application keeps its materials in a `Textures` folder of about fifty files beside it; the File Manager
here has no real directory tree. Folders are an approximation (`mac_fsspec.c`: a folder made by `PBDirCreate` gets an ID and files are looked up
flat), so a host directory next to the application is invisible. That is the large missing subsystem: **a hierarchical File Manager over real host
directories** (catalog IDs for directories, `PBGetCatInfo` enumeration, lookups by directory ID, per-directory file creation). The alert blocks
headlessly (nothing clicks OK), which is why the run ends at the instruction budget. No window was shown: the alert is native and the application's
`ShowWindow` was not reached.

Not verified: everything is headless and no frame was looked at. The -4101 code is the open point above. Tests: `build/test_mac_print` (68K side:
every selector, records untouched, refusals), `build/test_mac_ppc_libs` (PowerPC side: `PrOpen`, `PrError`, `PrClose`, `dec2num`, plus the earlier
`EqualString` and `num2dec`), a low-depth drawing case in `build/test_mac_gworld`. `HGetFInfo`, `HSetFInfo`, `HCreate`, `HDelete`,
`IUEqualString`, `GetIndPattern`, `DateToSeconds`, the `ParamText` NIL rule and `DisposeGWorld(NIL)` have no test of their own; only the real
application ran them.

## Sixth real-file run: the folder is found — 12 November 2026

Branch `qt/filemanager` (hierarchical File Manager, commits `5c62cb5` and `666dfac`) on top of `ppc/printing`. Same application, run from a full copy of its
folder (Textures folder of 231 entries and the other sample folders beside it), `--saves` pointing at an empty scratch directory,
`OS999_PPC_TRACE=1 ospi-runtime --indexed --steps 60000000 <app>.rsrc`, headless.

| Run | Imports | PowerPC instructions | Stops at |
| --- | --- | --- | --- |
| Fifth run, full folder copy | 2,597 | 1,017,487 | the `NoteAlert` for the missing Textures folder |
| Sixth run, full folder copy | 82,641 | 17,748,937 | `Toolbox trap $A921 behind PowerPC import SendBehind is not implemented.` |

What changed, in the order the application asked for it. It first calls `PBGetFCBInfoSync` on its own resource file to learn where it lives (a parent
directory ID and a volume). That call had no handler for the data volume, so the answer was an error and the application went on to `PBDirCreateSync`, made an
empty "Textures" and failed to read its materials. `PBGetFCBInfo` (selector 8) now answers for the application's resource file (reference 1, reported as
the file the runtime was started from, in the root directory) and for any file the service holds open, with the directory ID of its folder. With that, the
trace shows `HGetFInfo` with a large directory ID (a table ID, not the root's 2), then 272 `PBGetCatInfoSync` calls (the enumeration of the Textures folder
by index), 34 `PBReadSync`, one `PBOpenSync`/`PBGetEOFSync`/`PBCloseSync` and no alert. The save directory stayed empty: nothing was created.

The stop is the Window Manager: `SendBehind` (`$A921`) is not implemented. That is outside this change and is the next blocker, not a File Manager one.

Not verified: no window was shown and no frame looked at; the application's use of the textures after enumerating them is not checked (only that it read
34 blocks and carried on). `PBGetFCBInfo`'s flag bits are not claimed (only "resource fork" and "writable" are set, on a guess from the System volume's
implementation). The `PBGetFCBInfo` layout is the one `mac_system_volume.c` already used; the trace did not dump the block the application built, so the
input fields (reference number, index) were taken on trust from that layout, and the result is that the application proceeded, not a byte-level check.
