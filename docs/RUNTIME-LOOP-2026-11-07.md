# Runtime loop record — 7 November 2026 (memory, Toolbox gaps, real-app probing)

Dated evidence, not acceptance. Nothing here is a gameplay, windowed or device pass; every
result is headless on one host. Read with [the checklist](DEVELOPMENT-CHECKLIST.md) items R03,
R04 and G02. Items stay unchecked.

## Scope and revision

- Branch `feature/memory-integration`, code head recorded in the git log (last verified revision: the commit that adds SetPortBits), cut from `main` `a14d464`. `main` is
  untouched and nothing is pushed.
- Host: Apple silicon (arm64), macOS 26.6.2 (Darwin 25.6.0), Apple clang 21.0.0.
- Builds and tests ran locally with `BUILDFLEET_LOCAL=1` per the project override.
- Guest apps were downloaded from Macintosh Garden (`old.mac.gdn`) or taken from the existing
  volume, copied into a scratch directory outside Git, and run from copies. Glider 4.10,
  Glider Edit and Room Editor came from the Glider 4.10 floppy image (`Glider_4_10.dsk`, HFS
  extracted with `hfsutils`, forks via `unar -k visible`); Glider PRO 1.1.2 from its `.toast`.
  Monkey Island, Monkey Island 2 and Castle of Dr. Brain were already installed.

## What changed, in commit order

| Commit | Change | Main tests |
| --- | --- | --- |
| `d1ff826` | Protected heap `os999_heap`: per-guest region committed on demand, bookkeeping outside guest memory, generation-tagged handles, incremental compaction, zero on allocate and free, fault tears down one space | `test_os999_heap` (plain and ASan/UBSan) |
| `f1aa8b9` | Guest NewPtr/NewHandle/size/lock/state/FreeMem/CompactMem served from the heap over guest RAM `[0x400000,0x700000)`; low bump area keeps `[0x100000,0x400000)` | `test_mac_heap_bridge` (plain and ASan) |
| `82d824f` | Heap handles and pointers shadowed in `rt->handles[]` so menu/resource/pixel code can see them; resource bodies moved into runtime-owned heap blocks; MoveHHi on them accepted; MoreMasters and SetGrowZone; `OSPI_HEAP_STATS`; ASan probe target | bridge tests |
| `5e3f173` | SetResInfo (rename at the same ID) | `test_mac_resource_services` |
| `f87ac3d` | PaletteDispatch `$AAA2`: HasDepth `$0A14`, SetDepth `$0A13` (identified from the public MPW interface text) | `test_mac_palette_manager` |
| `6f99cdd` | Resource edit layer, WriteResource, persistence through the save overlay as a hidden file bound to a hash of the fork; Mac II / System 7.0 profile; Gestalt `qdrw` | `test_mac_resource_edits`, `test_mac_system` |
| `260c84c` | Colour cursors GetCCursor/SetCCursor/AllocCursor/DisposeCCursor | `test_mac_ccursor` |
| `25bdfdc` | Pack7 NumToString/StringToNum | `test_mac_numconv` |
| `9be4086` | Real 8-bit gray mode for the main device; HasDepth/SetDepth report it | palette manager tests |
| `6b7b6db` | FSSpec calls via `$AA52` (7 selectors); Gestalt `fs  ` bit 1 only | `test_mac_fsspec`, `test_mac_fsspec_write` |
| `837c66f` | ClearMenuBar, GetNewMBar, SetMenuBar, data-level submenus; heap-safe list resize | `test_mac_menubar` |
| `3fbcd10` | `$A830` identified as the Help Manager dispatch; reports "switched off" with the documented codes | `test_mac_help` |
| `507c06b` | Window colour tables (`wctb`) for GetNewCWindow | `test_mac_window_colors` |
| `00d65f1` | SetOrigin `$A878` (translates bounds or pmBounds, portRect and visRgn, not the clip); fixes LocalToGlobal/GlobalToLocal, which read the wrong port offsets with the wrong signs | `test_mac_setorigin`, `test_mac_quickdraw` |
| `6923cd5` | GetNewPalette `$AA92` from a `pltt` resource; NewPalette's allocator shared | `test_mac_getnewpalette` |
| `2040404` | Palette activation on the 8-bit main device: SetPalette on a visible frontmost window, ShowWindow of a window that has a palette, ActivatePalette `$AA94`; tolerant and courteous usages, 8-bit inhibit bits; unsupported usages refused before any change | `test_mac_palette_activation`, `test_mac_palette_manager` |
| `b95dc4d` (merged `29edb96`) | Apple Event Manager handler table (`$A816`): AEInstallEventHandler, AERemoveEventHandler, AEGetEventHandler, AESet/GetInteractionAllowed; handlers are recorded, never called. Built by a separate agent in its own worktree and verified here | `test_mac_appleevents` |
| `473e9be` | PICT decoder accepts 1, 2, 4 and 8-bit indexed PackBitsRect PixMaps (Glider 4.10's `PICT 140` is 4-bit); padding bits are not treated as colour | `test_mac_pict_depths` |
| `b7c6d9f` | GetClip, SetClip and ClipRect work on colour ports (clipRgn is at port+28 in both port kinds); SetPortBits `$A875` for basic ports | `test_mac_raster`, `test_mac_setportbits` |
| `301f8aa` (merged `bf35f9b`) | `_Delay` `$A03B`: waits on the runtime's tick counter by rewinding the trap and yielding the CPU slice, so it is virtual time headless and real time windowed (windowed not observed). Built by a separate agent; register convention (A0 in, D0 out) came from a search excerpt, not an opened page | `test_mac_delay` |
| `f25ae66` | `noGrowDocProc` (procID 4): the zoomNoGrow title bar without a zoom box (no zoom hit, spareFlag 0); the title-bar height rule shared in one helper instead of six copies. Menu Manager `SetItem` `$A947`: literal text replacement, key/mark/style/enable kept, header width rebuilt; empty text halts. Other procIDs and custom WDEFs still halt | `test_mac_window_nogrow`, `test_mac_menu_setitem` |
| `acfe6f8` | PICT decoder reads version-1 streams (single-byte opcodes, BitsRect `0x90` and PackBitsRect `0x98` with a plain 1-bit BitMap) and shares one scale-and-paint helper with version 2. `DrawPicture` can draw into a basic GrafPort whose bitmap is not the screen, using an exact black/white palette: a picture with any other colour halts with a named message rather than being thresholded. `UnloadSeg` `$A9F1` validates a jump-table routine address and leaves the segment resident (the call is only a purgeability hint) | `test_mac_pict_v1`, `test_mac_unloadseg` |
| `c93f5ea` | `mac_guest_call`: a native trap service can call a guest Pascal routine and resume afterwards. It builds `[return address = native stub of key 0xFE][arguments][result slot]` below the trap's stack, jumps to the routine, and recognises the routine's `RTS` into that stub, then restores A7 and PC and runs a completion callback that may chain the next call. Nested calls, odd or low targets, a missing stack, and unbalanced returns halt with a named message. First consumer will be Standard File's filter procedure and Room Editor's WDEF. Not covered: a call started through a guest-patched trap vector (the stub path), which only has a guard | `test_mac_guestcall` |
| `da132c5` | `mac_ditl`: bounded `DLOG` and `DITL` parsers (item kinds user, help, button, check, radio, control, static, edit, icon, PICT; disabled bit; even padding; exact-consumption rules; at most 40 items). Run over every `DLOG`/`DITL` in six real apps (Room Editor, Glider Edit, Glider 4.10, Glider PRO, Monkey Island, Castle of Dr. Brain; 26+25+16+82+8+18 resources) with a throwaway harness outside the repo; that run found the Help Manager item kind (`0x01`, 4 bytes) that the synthetic test had not covered. No runtime caller yet | `test_mac_ditl` |
| `ddc9fa1` | Standard File groundwork, no guest-visible change yet. `mac_files_entries`: one-pass listing of the merged data+saves volume (regular files only, sorted, with the Finder type/creator/flags the guest has set, zero otherwise); a save that shares a data file's name is listed once and reports the save, as `PBGetFInfo` does. `MacSfList`: the dialog's list logic with no CPU or drawing: case-insensitive order, selection, double-click, arrow keys, paging, scroll clamp, type-ahead (the one-second letter window is our choice, not Apple's). Two mutants are equivalent or race-only (a second guard behind `listing()`, and a file vanishing between listing and open) | `test_mac_files_entries`, `test_mac_stdfile_list` |
| `84e1add` | Native Standard File dialog: Pack3 `SFGetFile` (selector 2) and `SFPGetFile` (selector 4) as a modal drawn into the 640x480 indexed screen with our own chrome, a template from the app's `DLOG`/`DITL` (or a built-in layout for `SFGetFile` and dialogID -4000), our file list with type filter (`numTypes`, a type never set matches nothing), invisible files hidden, the app's file-filter routine called per file through `mac_guest_call` (TRUE hides), mouse and keyboard handling (row click, double click, buttons act on mouse-up, scroll arrows and page regions, arrows, Return, Escape, Cmd-period, type-ahead), `SFReply` written, arguments popped, backdrop restored. The trap rewinds and yields while the dialog is open, so with no input a run stays in the dialog. Halts with a named message: selectors other than 2 and 4, dialog hooks, dialog filter procedures, a template without items 1/3/7/8, a non-640x480x8 display, a call through a patched trap vector, a second call while open. Eject and Drive are drawn dimmed (one volume that cannot do either). Not done: scroll thumb dragging, auto-repeat, the icon item, a popup for the disk name. Also `_FSDispatch` `$A060` selector `$1A` (`PBOpenDF`), which opens like `PBOpen` | `test_mac_stdfile`, `test_mac_files` |
| `254db50` | Dialog Manager `Alert`, `StopAlert`, `NoteAlert`, `CautionAlert` (`$A985` to `$A988`) as a native modal alert, replacing the old "Alert N unsupported" halt. `ALRT` + `DITL` at the authored rectangle (moved onto the screen if it sticks out; a position word is not interpreted); buttons act on mouse-up, Return and Enter press the default button (item 2 when the stage bold bit says so); static text word-wrapped with `^0` to `^3` from ParamText; the app\'s own `ICON` items drawn. Stages must agree across all four nibbles (true of all ~60 real `ALRT`s checked); an undrawn stage, a filter procedure, a missing or malformed resource, and a non-button default item halt with a named message. Alert sounds are not played; the system stop/note/caution icons are not drawn; Escape and Cmd-period do nothing. The drawing primitives (pen, frame, button, wrapped text, icon, backdrop) moved to `mac_dialog_draw.c` and Standard File uses them (rendered frame byte-identical before and after); the event-queue helper moved to `mac_dialog.c`. **Baseline change:** Castle of Dr. Brain\'s frozen output recorded a halt at `Alert 143 unsupported: Can\'t load !??`; it now shows that alert (its own sad-Mac icon and a Quit button) and waits, so its baseline was refreshed (`baseline/games-outputs-alert`: Monkey Island and Monkey Island 2 unchanged and still byte-identical, Castle new). Castle still cannot load something; that is its own lead | `test_mac_alert`, `test_mac_dialog` |
| `bcf9ad9` | **Castle of Dr. Brain reaches its title screen.** Its THINK C-style startup does not use the Resource Manager to find its initial globals: it reads low-memory `CurApRefNum` (`$900`), walks the resource map that `TopMapHndl` (`$A50`) points to in guest memory, and reads resources from the file with `PBRead` at the map\'s offsets. The runtime left `$900` and `$A50` at zero, so the startup found no map, concluded there was nothing to initialise, zeroed its globals, and the game then tried to open a file with a NULL name ("Can\'t load !"). New `mac_appfork`: publishes `CurApRefNum` and `CurMap` (the application\'s resource file reference, 1) and `TopMapHndl` (a copy of the file\'s resource map in an unused part of guest RAM above the screen, with the file reference filled in, so no application heap address moves), and answers `PBRead` and `PBGetEOF` on that reference from the fork bytes, read-only (modes at mark, from start, from end, from mark; `eofErr`/`posErr`; asynchronous refused; writes and closes not claimed). Also `PBHGetFInfo` (`$A20C`): the same lookup and indexed listing as `PBGetFInfo`, root only (directory ID 2 or 0, else `dirNFErr`), `ioDirID` left as the caller set it. Castle now runs at about 2.4 million instructions per second and draws its title screen (headless frame viewed: "CASTLE OF DR.BRAIN MACINTOSH VERSION 1.0"). **Not verified:** any input, sound, or play; a splash is not gameplay. Monkey Island and Monkey Island 2 stay byte-identical; Room Editor, Glider 4.10 and Glider PRO stop at the same traps, with slightly different instruction counts for Room Editor and Glider 4.10 (the new low-memory values are visible to them). Baseline refreshed again (`baseline/games-outputs-appfork`: Castle new) | `test_mac_appfork`, `test_mac_files_write` |
| `f3ac615` | A full pathname that starts with the volume\'s own name now works: `PBOpen` and the other name-taking File Manager calls (shared `mac_files_read_name`) and `OpenResFile` accept `OS Pi Data:Name` (case-insensitive prefix, one prefix only) as the same file as `Name`; every other colon form, the bare volume name, `..`, and nested paths are still refused. Castle of Dr. Brain builds such paths from `PBGetVol` and was failing to open `RESOURCE.000` and the sound driver. It now opens its resource files, reads `resource.cfg`, scans the folder for `*.msg` files (finding none, correctly), opens `SoundDriver.drv`\'s resource fork, runs about 2.7 seconds and halts at `RecoverHandle` (`$A128`). Two hypotheses I tried and reverted because they changed nothing: reporting the resource-fork length in `PBGetFInfo`, and the Finder type being absent. Baseline refreshed (`baseline/games-outputs-driver`: Castle new; Monkey Island and Monkey Island 2 unchanged) | `test_mac_files`, `test_mac_resource_chain` |
| (next commit) | `RecoverHandle` (`$A128`, also `$A028`): the handle that owns the relocatable block starting at A0, from the runtime\'s handle table; a pointer that is not the start of a live relocatable block (a plain pointer, an interior pointer, a handle, nil, a disposed block) returns a nil handle and `memWZErr` (-111), which is our choice because the real result is undefined. `SndNewChannel` accepts the sample-rate field of the init options (`initSRateMask`, `$0030`; Castle passes `$AC`, mono plus `$20` plus noDrop plus noInterp): the rate is only a hint, playback uses each sound\'s own header rate, and stereo, pan and compression fields are still refused (checked against the on-disk MPW interface, which defines the mask but no named rates, so I do not claim the `$20` value means 22 kHz). Castle of Dr. Brain now runs about 2.7 seconds and halts at "Double buffer start requires an idle sound channel" (`SndPlayDoubleBuffer` on a channel that is not idle), a sound-path question I have not investigated. Baseline refreshed (`baseline/games-outputs-sound`) | `test_mac_memory`, `test_mac_sound` |

## Compatibility profile decision

Ian authorised, on 7 November 2026, presenting the guest as a Mac II-class machine running
System 7.0 (SysEnvirons `envMacII`, Gestalt `mach` = `gestaltMacII`, version `0x0700`). It is
an identity for programs that gate on it, not a claim that every System 7 service exists:
unsupported traps and Gestalt selectors still halt or return the undefined-selector error.
Consequences seen: apps now ask for the Help Manager, so it answers honestly that balloons are
off and no Help menu exists; Gestalt `help`, `stdf`, `evnt` remain unanswered.

## Evidence at `507c06b`

- `BUILDFLEET_LOCAL=1 make test-memory` exit 0; `BUILDFLEET_LOCAL=1 make test` exit 0 with 127
  test binaries and no failure line (132 at `bf35f9b`, 134 at `f25ae66`, 136 at `acfe6f8`, 137 with guest calls, 138 with the DITL parser, 140 with the Standard File groundwork, 141 with the dialog, 142 with the alert, 143 with the application fork and volume paths; unchanged binary count with RecoverHandle and the sound hint). No source or test file reaches 500 lines.
- Every new test file was mutation-checked (deliberate breakage, rebuild with a forced object
  refresh, confirm a test fails). Weak cases found that way were fixed: the pixel-depth and
  trailing-bytes cases, a source-span off-by-one in Pack7, and one redundant read-only check
  in FSSpec (equivalent mutant, documented by its author).
- Regression: Monkey Island, Monkey Island 2 and Castle of Dr. Brain give byte-identical
  `build/ospi-runtime-probe --seconds 8` output between consecutive verified revisions, and
  zero AddressSanitizer or UBSan reports in `build/ospi-runtime-probe-asan`. Against the first
  baseline Monkey Island differs by 2 instructions in 21.8 million after the System 7.0
  profile; the other two are identical.
- A real run of Glider 4.10 with `--saves` wrote a 94-byte hidden `.resedits-<fork hash>`
  file, a second run reported loading one edit, and the original `.rsrc` kept its SHA-256.

## Real applications (headless, `build/ospi-runtime --indexed --steps 20000000`)

None of these has reached its main loop. "Traps" is the count before the halt.

| App | Traps | Stops at | What it is |
| --- | --- | --- | --- |
| Glider 4.10 | 1249 | `A204` | a Device Manager `Control` (PBControlImmed) on driver reference number -4, csCode 2, csParam 3, reached from library glue. Glider's source calls `GetSoundVol`/`SetSoundVol`, which are glue in MPW, so that is the likely cause; I did not find a source that confirms it, so it is unimplemented, not guessed |
| Room Editor 1.06 | (dialog) | `GetNewDialog` `A97C` | with scripted input (probe, headless): Standard File, open a file, then its own alert (witch icon, "Okay" button; clicking Okay dismisses it), then it calls `GetNewDialog`, so it needs the real Dialog Manager (dialog records, `ModalDialog`, item access). Headless frames of the dialog and the alert were viewed. WIND 128 (procID 160, the app\'s own WDEF 10) is still ahead |
| Glider PRO 1.1.2 | 143 | `A260` | `PBDirCreate` (HFSDispatch selector 6) to create a "Preferences" folder; the file system has one flat directory, so this needs a hierarchy, not a one-off |
| Glider Edit 1.1.1 | 36 | waits in its own alert (`A985`) | it asks Gestalt `stdf` (Standard File 3.0), gets "undefined", and shows an alert (viewed in a headless frame): "Sorry, this program can\'t run on this computer because it needs the new filesystem (system 7.0 or higher)", button "Oh ...". Moving on needs the System 7 Standard File calls (Pack3 selectors 5 to 8) and an honest `stdf` Gestalt answer |

The window-colour change cleared Glider 4.10's `wctb 128` (content, frame and text black;
parts 3 and 4 are documented as reserved). A pixel dump of the screen at the halt is entirely
palette index 255, including outside the 512x342 window, so the game itself paints the whole
screen black and the real run cannot show the colour work. The unit tests carry that evidence.

## Corrections and findings during the loop

- **LocalToGlobal / GlobalToLocal were wrong** (found while doing SetOrigin). They read `port+12/+14`
  (bounds bottom and right; the file's own `bitmap()` helper and the raster code put bounds at
  `port+8`) and applied the opposite signs. Their test had copied the same offsets. Both are
  corrected, colour ports now find their bounds through the PixMap, and the test says why. The
  three games' output did not change, so none of them depended on the old behaviour.
- **SetOrigin and the clip region:** two public sources agree it does not move the clipRgn.
- **`pltt` layout** verified against all three real palettes in the test apps (exactly
  `16 + 16*count` bytes); their header and private slots hold junk, which is deliberately not copied.
- **Palette activation refused its own old test:** the test that pinned "ShowWindow of a window with a
  palette halts" used a palette whose entries have usage 14 (animated and explicit bits). Activation
  correctly refuses that, so the test's expectation survived but its stated reason was wrong; it now
  pins the true reason. A first draft of ShowWindow showed the window before refusing; validation now runs
  first so a refused palette leaves the window hidden.
- **Real palette data:** Glider 4.10's palette is 16 `pmTolerant` entries with tolerance 0; Castle of
  Dr. Brain's mixes in explicit entries (usage `0x0a`), which activation refuses.
- **A mutation harness blind spot:** a mutant that crashes prints no failure line. Mutation runs now
  also look at the exit status; the unbounded-palette mutant was caught by a stack-smash abort.

## Limitations and things not verified

- Everything is headless. No windowed or device run, no screenshots, no audio listening.
- The protected heap's compaction never ran in any real app (none frees a movable block ahead
  of another); it is covered by unit tests only. Guest loads and stores still reach RAM
  directly, so fault containment applies to the translate calls, not to every guest access.
- Handles beyond the 512-entry table work for the memory traps but are invisible to other
  managers. SetGrowZone stores the procedure but never calls it. The bump area for runtime
  records is 3 MB (0.34 to 1.38 MB used by the installed games, measured before resource
  bodies moved into the heap, so current use is lower).
- Colour cursors install the 1-bit image only; colour pixels are validated and kept, not drawn.
  The `crsr` layout was checked against 25 real cursors in two apps, not against a fetched spec.
- Pack7 parsing rules for malformed input are an implementation choice, not verified on a ROM.
- FSSpec: FSpOpenRF, FSpDirCreate, locks, CatMove and ExchangeFiles are not implemented;
  Glider Edit never reached an FSp call, so they are tested only with hand-built frames.
- SetResInfo cannot change an ID. Resource edits persist only for the application fork.
- GetNewCWindow does not yet create and attach the palette that the Window Manager is documented to
  build from a `pltt` with the window's ID. Palette activation covers the single 8-bit device and
  `pmCourteous` / `pmTolerant` entries only (tolerance is treated as satisfied by the exact colour; no
  tolerance metric is applied); dithered, animated and explicit entries, deactivation on close or
  window change, and update events beyond invalidating the window are not implemented. Complex
  (non-rectangular) regions are unsupported, so SetOrigin halts on one.
- Apple Events: handlers are recorded and never called (nothing sends an event). Descriptors,
  AEProcessAppleEvent, replies and Gestalt `evnt` are absent. The wildcard lookup order, the missing-handler
  error for Remove and the table capacity (64) are conventions, not taken from the interface text.
- PICT: only PackBitsRect indexed PixMaps with default packing and srcCopy; direct-colour PixMaps, other packing
  modes, other opcodes and complex clips are refused with a message. SetPortBits refuses colour ports. ClipRect,
  GetClip and SetClip on a colour port were checked with a hand-built port, not with a real CGrafPort from an app.
- Subsystems that are entirely absent: the Device Manager (Control/Status/Open on drivers), custom WDEFs, Apple Event delivery, Standard File,
  pop-up and hierarchical menu drawing, balloon help, GWorlds backed by handles.

## Next reproducible steps

1. Glider 4.10: identify the Device Manager call on refnum -4 (csCode 2) from a public source, then
   decide whether it is a small answer or a driver subsystem. Palette activation is done; a windowed run
   and screenshot are still owed once it reaches its loop.
2. Glider PRO: a hierarchical file system for `PBDirCreate` (HFSDispatch selector 6) and its siblings.
3. The real Dialog Manager (`GetNewDialog` `A97C`, `ModalDialog`, `DrawDialog`, dialog item get/set, `DisposeDialog`): Room Editor\'s next halt, built on the DITL parser and `mac_dialog_draw`. Then Pack3 selectors 5 to 8 (`StandardGetFile`) and the `stdf` Gestalt answer for Glider Edit, and Room Editor\'s custom WDEF mechanism (now possible through `mac_guest_call`). Castle of Dr. Brain now reaches its title screen headlessly: next is a windowed or scripted run to see whether it takes input, plays sound, and starts the game.
4. Exercise compaction with a real fragmenting program, or a purpose-built guest.
