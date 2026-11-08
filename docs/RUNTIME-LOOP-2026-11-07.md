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
| `529b247` | `RecoverHandle` (`$A128`, also `$A028`): the handle that owns the relocatable block starting at A0, from the runtime\'s handle table; a pointer that is not the start of a live relocatable block (a plain pointer, an interior pointer, a handle, nil, a disposed block) returns a nil handle and `memWZErr` (-111), which is our choice because the real result is undefined. `SndNewChannel` accepts the sample-rate field of the init options (`initSRateMask`, `$0030`; Castle passes `$AC`, mono plus `$20` plus noDrop plus noInterp): the rate is only a hint, playback uses each sound\'s own header rate, and stereo, pan and compression fields are still refused (checked against the on-disk MPW interface, which defines the mask but no named rates, so I do not claim the `$20` value means 22 kHz). Castle of Dr. Brain now runs about 2.7 seconds and halts at "Double buffer start requires an idle sound channel" (`SndPlayDoubleBuffer` on a channel that is not idle), a sound-path question I have not investigated. Baseline refreshed (`baseline/games-outputs-sound`) | `test_mac_memory`, `test_mac_sound` |
| `4c93182`, `2540a27`, `10f8ca5`, `61c50d7` | Four steps of Castle of Dr. Brain (each halt was fixed in the smallest honest way, in order). **Sound:** `SndPlayDoubleBuffer` refused a channel holding a queued `ampCmd`; a real synth consumes such commands almost at once, so the command pump now runs first (`mac_sound_pump`), and a channel that is really busy (playing, waiting, paused, callback pending, double buffer active) is still refused. **`ShowHide` (`$A908`):** visibility without activation, an activate event or reordering. **Palette Manager:** the "more than 254 entries" limit counted entries; it now counts distinct non-white, non-black tolerant colours that need a device index, so a 256-entry palette with white and black fits; `pmExplicit` entries (the interface says "no reserve, no render, no record; stuff index into port") change nothing and map to their own index. Castle\'s palette uses `0x0E` (explicit plus animated plus tolerant on all 256 entries); the interface does not define that combination, so the explicit bit winning is **an interpretation I could not verify on hardware**, bounded by the fact that animating those entries still halts by name and plain animated and explicit-with-dithered stay refused. An earlier test used `14` as its example of an unsupported usage; that was changed knowingly. **Regions:** `SetEmptyRgn`, `SectRgn`, `UnionRgn`, `DiffRgn`, `XorRgn`, `OffsetRgn`, `InsetRgn`, `EmptyRgn`, `EqualRgn`, `PtInRgn`, `RectInRgn` on rectangular regions: exact when the result is a rectangle or empty, a named halt when it would need a non-rectangular region. Castle now runs to about 2.7 seconds and halts at `InvalRect` (`$A928`). The audio capture so far is silent (peak 0), so nothing is claimed about sound. Baseline refreshed (`baseline/games-outputs-regions`) | `test_mac_sound_double`, `test_mac_window`, `test_mac_palette_activation`, `test_mac_palette_manager`, `test_mac_regions` |
| `1715a3f` | Window Manager `InvalRect`, `InvalRgn`, `ValidRect`, `ValidRgn` (`$A928`, `$A927`, `$A92A`, `$A929`) and `EqualRect` (`$A8A6`), the two halts Castle of Dr. Brain reached after its region work. The calls act on the window that owns the current colour port: local to global through the pixmap bounds, clipped to the content, held as one rectangle (the update region is a single rectangle here, like every region). Invalidate takes the exact union when it is a rectangle and otherwise the bounding box (over-invalidating only redraws more); validate removes an exact strip or everything and otherwise leaves the region as it was. Complex region arguments, a non-window or basic port and bad pointers halt with a named message. New file `mac_window_inval.c` (`mac_window.c` is at 468 lines). Tests: 20 mutants, 18 killed at first, the two survivors were hidden behind an earlier check and are now killed; EqualRect 7 of 8 (the survivor is a stack-size check that a bad read catches anyway). |
| `fa4e3a1` | `CopyBits` with a palette-linked source colour table. Castle's table has `ctFlags = $4000` (256 entries, seed `$401`); the old code refused every flag but the device bit. Technical Note QD13 (opened and searched, not read end to end) says bit 14 makes each entry's `value` an index into the destination window's palette and ignores `rgb`. Implemented for a source table with `srcCopy`: pixel, entry, palette index, device index through the activated palette's map. A linked destination, another transfer mode, reserved flag bits, no activated palette, a truncated table and an index outside the palette halt and leave the destination untouched. Mutation: 16 mutants, 4 survivors at first, all killed after the test data was changed (rgb values that differ, an index above 255). |
| `04fbfba` | Doubleback return check: a C-convention callback ends with a bare `RTS` and leaves its two arguments on the stack (A7 = sp-8); a Pascal one pops them. Both are clean and the stack is restored; any other depth still halts. Castle's callback is the C form. 4 of 4 mutants killed. |
| `97434ee` | Time Manager poll: each callback runs against the poll's whole budget instead of what earlier callbacks in the same poll left. Castle's timer task is long (it does file reads and pixel loops) and was cut off mid-run with a return mismatch. A runaway callback is still stopped at the budget (the existing 10,000-instruction test still holds). 4 of 4 mutants killed; the new test fails without the change. |
| `5da36ec` | `PBGetVInfo` (`$A007`). The shared volume-info code takes the size of the block the call owns: the plain call writes only the first 64 bytes, never the HFS fields at 64 and beyond (a caller's block is often only that big). 7 of 7 mutants killed after tests for the 64-byte boundary, a name buffer inside and outside the block, and a block with too little room. |
| `f2dd086` | Fix to `5da36ec`, found by an automated security review after the commit and reproduced with ASan (`READ of size 122`, heap-buffer-overflow, `tests/test_mac_files.c`): `PBGetVInfo` range-checked 64 bytes but copied a 122-byte scratch buffer from guest RAM, so a block at the very end of RAM made the host read past its allocation. The extra bytes were never written back to the guest, so nothing leaked, but it was a real out-of-bounds read. I had not run ASan on the new tests before committing; all five new tests of this session are now ASan/UBSan clean. |
| `a588842` | `AnimatePalette` (`$AA9A`; the MPW listing puts each `INLINE` after its declaration, so `$AA99` is `AnimateEntry` and `$AA9B` `GetEntryColor`). It replaces the device colors of the window palette\'s animated entries from a color table, at once, with no redraw. The device index is the one activation chose (for the explicit-and-animated entries Castle uses, the entry\'s own index). A non-animated entry, an inactive palette, bad indices or lengths and a bad source table halt by name, all before the first device entry changes. The palette\'s own stored colors are not rewritten, so a later activation restores them: an interpretation I could not check against the Palette Manager chapter (only the interface listing is local), so it is unverified. 21 mutants: 3 survivors after the first pass were tightened (palette bound, stack message, source range); 2 equivalent survivors remain (a missing palette or a negative entry still halt at a later check). **Castle now runs its whole 20 s probe with no halt.** |
| `0062f13` | **The cause of Castle's black window: the low-memory clock globals were never written.** `Ticks` (`$16A`) and `Time` (`$20C`) stayed zero for the whole run (RAM dumps at 6 to 45 s all show zero); only the `TickCount` trap followed `rt->ticks`. Sierra's SCI interpreter reads `Ticks` directly (`GetTime` mode 0 is `move.l $16a.w,D0`) and `Time` for its seconds, so every tick- or seconds-based wait in the first room never elapsed. Found by reading the interpreter by its embedded function names, then the `GetTime` code. They are now written from `rt->ticks` at the start of both run entry points (`Time` = a base in seconds since 1904, taken once from the host clock, plus whole elapsed seconds). 11 of 11 mutants on the new test killed after the epoch check was bracketed against the host clock; two survivors are equivalent (a NULL CPU is already tolerated by the write). Monkey Island and Monkey Island 2 stay byte-identical. |
| `73f519c` | `AnimatePalette` with a NIL window, and with a window that has no palette. With a live clock Castle reaches the call early (0.57 s), while its palette-less splash window is frontmost, and passes NIL. The interface listing available here defines neither case, so both are narrow interpretations, recorded in the source and unverified: NIL selects the frontmost window, and a window without a palette has no animated entries so nothing changes. A NIL with no window, and every other bad argument, still halt by name. |
| `a5d7837` | `Random` (`$A861`): QuickDraw's `randSeed` (126 bytes below the `thePort` pointer `InitGraf` was given) advances as a Lehmer generator, `seed * 16807 mod 2^31-1`; the result is the low 16 bits as a signed integer and -32768 is returned as 0. The generator and range come from the documented behaviour; the check values are the public Park-Miller sequence from seed 1. 14 mutants: 13 killed, 1 equivalent (without the low-bound check the end-of-RAM check still halts). |
| `6a24c9b` | **Guest code now runs in supervisor mode** (`SR = $2000`; it was 0, user mode). Classic Mac OS runs applications in supervisor mode and ordinary code uses privileged instructions: Castle's heap code brackets critical sections with `move SR,-(A7)` / `ori #$0200,SR`. In user mode that is a privilege violation; the empty vector sent the game to address 0 with `A7 = $FFFFFFF8`, after about 52 s. Found with a temporary ring buffer of the last executed PCs. The new test runs the idiom through a real runtime init (3 of 4 mutants killed; the fourth, setting SR after A7, makes no difference with this CPU wrapper, and I corrected a source comment that wrongly claimed it would). |
| `TextFont`..`TextSize` commit | `TextFont`, `TextFace`, `TextMode`, `TextSize` (`$A887` to `$A88A`): each stores one field of the current port (`txFont` +68, `txFace` +70 as a byte from the high half of the stack word, `txMode` +72, `txSize` +74) and pops its argument. Nothing draws. 16 of 16 mutants killed. Found because Castle **now reacts to input**: a scripted click at 50 s makes the interpreter start drawing text and it halts at `TextFace`. |
| `0ab8bee` | **Bitmap font engine** (`mac_font.c` strike parser and lookup, `mac_text.c` calls): `GetFontInfo` (`$A88B`), `StringWidth` (`$A88C`), `CharWidth` (`$A88D`), `TextWidth` (`$A886`), `SpaceExtra` (`$A88E`), `DrawChar` (`$A883`), `DrawString` (`$A884`), `DrawText` (`$A885`). Glyphs come only from the guest's own resources: a `FONT` with id family*128+size, or the family's `FOND` association table (header 52 bytes, then a count-1 word and 6-byte size/style/id entries) naming an `NFNT`. Castle's font 200 ("SCI") is reached through FOND 200: sizes 7, 10, 14 map to NFNT 22688, 20970, 17363. I checked my reading of the strike format against NFNT 22688 in Python first (the letters A and g come out as letters) before writing the C parser. Only exact sizes are served; nothing is scaled; an absent family or size halts by name. Drawing: indexed 8-bit colour port only, clipped to portRect, pixmap bounds, visRgn and clipRgn; srcCopy, srcOr, srcXor, srcBic; plain, bold (synthesised: drawn twice a pixel apart and one pixel wider) and underline (a row below the baseline). Other styles, fractional SpaceExtra, the not and pattern modes, complex clips and basic ports halt. My choices that are not verified against a real Mac: bold's +1 advance and double strike, the underline row, srcCopy painting the glyph cell in the background colour, and a hidden pen (pnVis < 0) still moving the pen. Tests: a hand-built strike in a real resource fork, exact pixels, 22 refusal cases; 70 mutants killed after I added boundary cases for the ones that other checks had been hiding (rowWords 0, last > 255, rectH > 255, the end of the location table, kernMax). |
| `ef6f452` | **TextEdit** (`mac_textedit.c`): `TEInit`, `TENew`, `TEDispose`, `TEGetText`, `TESetText`, `TESetSelect`, `TEUpdate`, `TEClick`, `TEActivate`, `TEDeactivate`, `TEIdle`, `TEKey`, `TESetJust`, and `TEDispatch` selector 8 (`TEGetPoint`). The list came from scanning Castle's interpreter for the traps it contains (13). The `TERec` is a real guest handle, allocated through the runtime's own `NewHandle`/`SetHandleSize`/`DisposeHandle` services, because the game reads `nLines`, `lineStarts` and `lineHeight` directly. Lines break at spaces (a space hangs past the margin rather than forcing a break), at returns, or per character when a word is wider than the box; `crOnly < 0` breaks at returns only. Text uses the guest's own fonts through the face/draw API now exported from `mac_text.c` (`mac_text.h`). Selection and caret are drawn by inverting pixels, clipped to the view; the caret blinks in `TEIdle` every 20 ticks. `TEGetPoint`: `h` is the left edge of the character and `v` the bottom of its line; I chose `v` because the interpreter uses the last character's `v` as the height of a block of text (it stores it as the rectangle's bottom). **Not implemented and halting by name:** hooks (`wordBreak`, `clikLoop`, `highHook`, `caretHook`), `TEDispatch` selectors other than 8, up/down arrows, control keys other than backspace and return. **Deliberate simplifications:** `TEClick` selects at the click and does not track a drag (it cannot block waiting for the mouse); an edit redraws from the line holding the previous character to the bottom of the view; text is capped at 32,000 characters and 4,000 lines. About 65 mutants: I closed the real test gaps (wrong-kind handles, corrupt pointers, mid-word wrap, a box narrower than a glyph, the line cap, multi-line selections, the 32,000 limit); the remaining survivors are equivalent (redundant clamps and a clip that makes a skip redundant). A pixel dump caught two of my own wrong expectations (the highlight is correctly clipped to the view), and one small real flaw, now fixed: after typing, the redraw started at the line of the new offset, but a reflow can change the line before it. |
| TextEdit hardening commit | Fix to `ef6f452`, from an automated security review after the commit, reproduced with ASan (`negative-size-param` in `memcpy`): the `TERec` is guest memory, and `TEKey` and `set_text` used its guest-writable fields (selection, `teLength`, `nLines`, `hText`, and the text handle\'s master pointer) as sizes, indices and destinations of host copies. A guest could corrupt them, or call `SetHandleSize` on a `TEHandle`, and make the host copy out of bounds. `te_of` now validates the whole record on every call against the runtime\'s own handle table (both handles must be ones TextEdit made and tagged; the real size must hold the line table; length, line count and selection must agree), and all host pointers come from that table. Tests corrupt each field, swap the text handle and shrink the record; they run under ASan. 18 validations mutation-tested: 10 killed, 8 survive as equivalent (allocator guarantees and checks that `te_of` already makes redundant). Lesson, again: a structure that lives in guest memory must be treated as untrusted input by every host-side use. |
| `368cad9`, `29626ea` | **Region engine.** `mac_rgn.c` holds a region as a canonical list of rectangles (non-overlapping, sorted, touching runs and identical consecutive bands merged) with exact sect, union, diff and xor, queries, offset and bounding box, and reads and writes the documented guest encoding (`rgnSize`, `rgnBBox`, scan-line records of ascending inversion points that toggle the row above, a bottom record that cancels the last row, a final `$7FFF`; rectangles and empty regions stay the 10-byte header). Decoding treats guest bytes as untrusted and bounds everything (512 edges per line, 256 rectangles per encoded band, 2048 rectangles, 32,760 bytes). `mac_regions.c` now stores any result exactly (it used to halt on a non-rectangle), validates a record against the handle table's real size on every use, adds `CopyRgn` and `SetRectRgn`, keeps a handle's size as its capacity so alternating simple and complex results do not leak heap blocks, and halts by name on `InsetRgn` of a non-rectangle (the edge semantics for a concave shape are not something I could verify). Tests: an independent oracle (3,000 random region pairs through all four operations against brute-force pixel arithmetic, and byte for byte against a separate row-by-row encoder), hand-written encodings, a crafted input for each way a record can be malformed (decoded from exact-size heap buffers under ASan). About 80 mutants; survivors are equivalent redundant guards, plus a host leak that this platform's sanitizer cannot see (LeakSanitizer is unsupported here). |
| `d45f828`, `8da4dbe` | **What Castle needed next.** `OpenRgn`/`CloseRgn` record an axis-aligned outline from `MoveTo`, `LineTo`, `Move`, `Line` and `FrameRect` (observed ahead of the drawing services) and fill it even-odd through `mac_rgn_from_vedges`; other frames while recording, a diagonal, nested or unmatched calls and over 4,096 segments halt by name. `LineTo`, `Line` and `Move` on a colour port: an axis-aligned line paints the pen's swept rectangle through the existing `PaintRect` path (pattern, mode, clip); a hidden pen moves without painting; a diagonal halts by name. `SetStdCProcs` fills the 80-byte `CQDProcs` record (13 entries point at the stub of their `Std…` trap, so reaching an unimplemented one halts by name; the rest are NIL): Castle replaces `rectProc` with a no-op around `TEUpdate` to suppress TextEdit's erase, which my `TEUpdate` never does. `PmForeColor` and `PmBackColor` give a window port the device index and RGB of an entry of its active palette. **A bug the screenshots caught:** my first `OpenRgn` let the recorded hot-spot outlines draw, and black lines appeared across the front-door scene. `OpenRgn` now hides the pen until `CloseRgn` (as I remember the QuickDraw documentation; the interface listing here does not say, so this is an interpretation, supported by that observation). About 75 mutants plus ASan on all five touched tests. |

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
| Glider 4.10 | 0 | none in 70 s (attract screen) | headless frames, 9 Nov 2026: title screen and animation running; not played (no input scripted), audio unheard |
| Room Editor 1.06 | (dialog) | `DrawPicture` (`A8F6`) into a dialog port | headless frames, 9 Nov 2026: open dialog, its own password alert, Okay, then a hidden dialog is created, moved, shown and its items adjusted (all handled by the new Dialog Manager) until the app draws a picture into the dialog's port, which has no drawable port here. WIND 128 (procID 160, the app's own WDEF 10) is still ahead |
| Glider PRO 1.1.2 | 143 | `A260` | `PBDirCreate` (HFSDispatch selector 6) to create a "Preferences" folder; the file system has one flat directory, so this needs a hierarchy, not a one-off |
| Glider Edit 1.1.1 | 36 | idles at its menu bar after an error dialog | headless frames, 9 Nov 2026: with the new Dialog Manager, opening a (wrong-format) copy of a PRO house retyped `gliH` shows the editor's own box, *The house "Test" is locked.* (ParamText filled in; "locked" because the data volume is read-only without `--saves`), OK dismisses it and the editor returns to its menu bar. I have not seen a house open successfully: the only houses available are the wrong format |

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

## Castle of Dr. Brain, 8 Nov 2026 (headless only)

Castle runs without a halt and responds to scripted input. Everything here is a headless frame dump of the runtime's own screen from
the probe, not a window I watched, and I have not listened to any audio.

- **Unattended:** with the clock globals published (`0062f13`) the game waits about 20 s, shows the Sierra logo (24 s), then its opening
  castle scene, and by 100 s the castle's front door with two flamingos.
- **Input:** a scripted click at 50 s brings up the game's own dialog, *"Would you like to skip the introduction or watch the whole
  thing?"*, with **Skip it**, **Watch** and **Restore a** buttons and Dr. Brain's icon, its text laid out with the game's own fonts
  and TextEdit. A scripted click on **Skip it** (about x 218, y 252) closes it and the game moves to the front-door room, which sits
  idle waiting for input. Clicking the door gives a text box, *"This is the front door of Dr. Brain's castle."* The 130 s and 140 s
  runs finish with **no halt**; ASan on Castle (100 s) reports nothing.
- **Audio:** the capture is silent for the first ~20 s, then carries a varying signal (RMS about 2,000 to 10,000, changing from second
  to second) until about 85 s, then silence. That is signal-shaped output from our audio path; whether it is the right music is
  unverified.
- **What got it here, in order:** the frozen low-memory clock (`0062f13`, the cause of the black window), `AnimatePalette` with a NIL
  window, supervisor mode, `Random`, the text-state calls, a bitmap font engine, TextEdit, a region engine, `OpenRgn`/`CloseRgn`,
  colour-port lines, `SetStdCProcs`, `PmForeColor`/`PmBackColor`.
- **Not yet known:** why the logo takes ~20 s to appear (I do not know whether a real Mac is faster); whether the game's first
  moves (walking, the inventory, the menus, saving) work; anything about actual play. Reading the interpreter by its embedded function
  names (map kept outside Git) found the clock bug: `GetTime` mode 0 is `move.l $16a.w,D0`.
- **Menu bar (headless frames):** `FindWindow` now returns `inMenuBar` for a point above `MBarHeight`; new `MenuSelect`, `HiliteMenu` and
  `MenuKey` (`src/mac_menuselect.c`) track a press in the bar, draw a pull-down in the project's own 8x8 face (not a system menu font)
  and own chrome, restore the pixels under it, and return `(menuID << 16) | item`. A scripted press on **File** (x 55, y 9) shows
  *Save Game? / Restore Game? / Restart Game / Quit* with command keys, **Edit** dimmed; moving to Restore Game? and releasing opens the
  game's own Standard File dialog ("Restore Game:"), which our dialog code draws. Not done: hierarchical menus, icons in items, custom
  menu definitions and colour menus are not drawn; the pull-down is not a pixel match for a real Mac's. I have not tried the Options
  menu or a command key in the game. Also added the integer/bit Toolbox utilities (`BitAnd`.. `BitClr`, `HiWord`, `LoWord`,
  `src/mac_toolutil.c`) after Castle reached `LoWord` ($A86B); `BitShift` semantics are from memory, not from a listing I could open.
  Mutation checks on the tracker: 9 of 11 mutants killed; the 2 survivors are equivalent (a redundant `shown = 0`, a second clear of
  an already-clear highlight). UBSan caught a signed shift on menu flags with the high byte set; fixed.
- **Options menu (headless frames):** the **Options** pull-down shows *Small Window* (checked), *Medium Window*, *Large Window*, *Hide
  Desktop*. Choosing Medium Window took three fixes: `ShieldCursor` (`$A855`, validates the rectangle, changes nothing because the
  cursor is composed by the host and never drawn into guest memory), `SwapMMUMode` (`$A05D`, D0.b in = new mode, D0.b out = old mode, kept
  at `$CB2`; the register convention is from memory), and callbacks on an odd stack. THINK C builds odd-sized frames (`link A6,#-7`),
  so an interrupt can land with an odd A7; the timer, VBL and both sound callbacks refused that as "stack exceeds RAM". They now run
  word aligned just below it and restore the registers. After that the window resizes to the medium size and the game redraws the
  front-door scene at 1.5x in the frame. The sound render callback has the same change but no odd-stack test of its own. **Large Window
  still halts:** `SizeWindow exceeds supported content dimensions` (the game asks for 640x400 at top 140 before moving the window, and
  our window model keeps windows fully on the 640x480 screen). That needs off-screen window geometry, which I have not started.
- **Save and restore (headless frames and a file on disk):** **File > Save Game?** halted on Pack3 selector 1 (`SFPutFile`). It is now a
  native modal in `src/mac_stdput.c`: a reduced "save as" dialog of our own (prompt, "Save in: OS Pi Data", one editable name of 1 to 31
  characters without ':', Save and Cancel; no file list, no Eject/Drive, no "replace existing file?" alert). With the probe's new
  `--saves DIR` (the same writable overlay `ospi-runtime --saves` uses), typing `AB` and Return makes Castle create a 13,970-byte
  `AB` in that directory and carry on. Without `--saves` the game shows its own "An error occurred creating a file" alert, which is the
  read-only volume doing its job. In a second run on that directory **File > Restore Game?** lists `AB`; selecting it and pressing Open
  closes the dialog, the game's icon bar appears and nothing halts. I saved at the very start of the room, so this does not show that
  game state comes back. Mutation checks on the Save dialog: 13 of 13 killed (one needed a placement test added).
- **Large Window and Quit (headless frames):** Large Window needed `SizeWindow` to accept a window that extends past the bottom or right
  edge (the game sizes 640x400 at top 140, then moves it). A window may now be up to 640x480 and sit partly off-screen; every pixel loop
  clips to the screen, nothing is written past the screen buffer (tested with a sentinel), and `MoveWindow` carries only the on-screen
  pixels and starts the rest as paper. Growing an off-screen visible window with `updating` = TRUE still halts, because the update region
  could not name the off-screen part; moving a window to another off-screen place still halts. After the fix the game fills the screen
  with the room and its icon bar (eye, hand, ..., help). **Quit** shows the game's own "Quit for now / Keep" box, and Quit for now ends
  with `Guest called ExitToShell` and a clean exit. Restart shows no visible change that I have identified. **Hide Desktop** now halts
  one step later, `Showing a second visible window requires unsupported compositing`: it opens a second, full-screen window, and the
  runtime still shows one window at a time. Mutation checks on the window change: 6 of 8 killed after one added boundary test; one
  survivor is equivalent in effect (a snapshot copy of the full width whose extra bytes are never pasted), one was the boundary case now
  covered.
- **Clicking around the room (headless frames, no halt in 125 s):** with the default verb the doorbell gives *"This looks like every
  other castle doorbell you've seen."*, the door gives its earlier text, and a click near the top row of the window selects a
  different verb (the door then answers *"Dr. Brain's front door seems very secure."*). A click on the doorbell with that verb changes the door's
  grating in the next frame; I have not seen the whole sequence or heard any sound. Flamingo and lamp clicks showed no text in the
  frames I looked at; I did not check whether they should.
- **Standard File selectors 5 and 6, and Glider Edit (headless frames):** `StandardPutFile` (selector 5) shares the save-as dialog;
  `StandardGetFile` (selector 6) shares the open dialog. Both are centred and return a 88-byte `StandardFileReply` whose FSSpec is
  {vRefNum $FFFF, parID 2, name}; `sfReplacing` is always 0 (no replace alert), and a file filter on `StandardGetFile` halts. Mutation
  checks: 13 of 14 killed after two test fixes; the survivor is a stack-room boundary for `StandardPutFile` that changes no outcome I can
  construct. `MaxSizeRsrc` answers like `SizeResource`.
- **Past the Glider Edit open dialog (headless frames):** with a copy of a Glider PRO house retyped `gliH` (this old editor lists only
  `gliH`), choosing it ran into `PurgeSpace` (`A162`: D0 = total free, A0 = largest block, MemError left alone; both memory models), then
  SANE `FSUBX`. The runtime had only SANE conversions, so I added extended-precision `FADDX`, `FSUBX`, `FMULX`, `FDIVX` in
  `src/mac_sane_arith.c`, 128-bit integer arithmetic with all four rounding directions and no host floating point, plus `FX2S`, `FX2D`
  and `FD2X`. Zeros and normal numbers only: infinities, NaNs, denormals, division by zero, overflow and underflow halt by name, as do
  the double-precision rounding setting and an enabled inexact halt. Checked against 232 arithmetic, 228 float and 86 double-to-extended
  vectors from a separate exact reference written in Python (`tests/sane_*_vectors.h`); 20 of 21 mutants killed (the survivor is the
  sticky bit of a division quotient, which only matters for an exact tie I could not construct). After that the app reads the file and
  shows an error dialog of its own (the file is the wrong format), which needs `GetStdFilterProc` and `GetNewDialog`: the real Dialog
  Manager is the next piece.
- **A bounded Dialog Manager (`src/mac_dlgmgr.c`, headless frames):** `GetNewDialog`, `ModalDialog`, `DrawDialog`, `CloseDialog`,
  `DisposeDialog`, `GetDItem`, `GetIText`, `SetIText`, `SelIText`, `ErrorSound`, `IsDialogEvent`, `DialogSelect`, and `DialogDispatch`
  selectors `$0203` (`GetStdFilterProc`), `$0304`, `$0305`, `$0306` (default item, cancel item, cursor tracking; these selectors are my reading
  of the call sequences, not from a listing I opened). A DialogPtr is only a name for a host record: the guest's copy of the record, its
  item handles' master pointers and its strings are never trusted (a test repoints a handle and checks the write still goes to the host's
  block). Open dialogs make `FrontWindow` and `SetPort` answer for them, so an application's own event loop works; the window regions
  are real region handles. `GetStdFilterProc` hands out a guest-callable stub, so an app that calls the standard filter works. Drawn with
  our own 8x8 chrome; ^0 to ^3 expand from ParamText; no user items, control items, hidden dialogs, `NewDialog`, `SetDItem`, filter
  procedures of the app's own, or more than four open dialogs (all halt by name). 20 of 21 mutants killed after eight added checks; the
  survivor cannot change an outcome (a text-capacity bound the Pascal length byte already enforces). Glider Edit's error box is the first
  real user: its own `ModalDialog`-style loop (`FrontWindow`, `WaitNextEvent`, `IsDialogEvent`, `DialogSelect`) now runs to the OK button.
- **Room Editor with the Dialog Manager (headless frames):** choosing "The House" in its open dialog (row, then Open) loads it and shows the
  editor's own alert, *"Ye intruders desist - this house protected by the Wicked Witch of the West!"* (the house is password protected;
  I take the box to be the app's, not ours). Okay then reaches a dialog whose DLOG is hidden, so the Dialog Manager now supports hidden
  dialogs (`ShowWindow` and `HideWindow` on a dialog; `FrontWindow` and `IsDialogEvent` ignore a hidden one), `MoveWindow` and `SelectWindow`
  on a dialog, and `HideDItem`/`ShowDItem`. Next halt: `DrawPicture` (`$A8F6`) with a dialog as the current port. A dialog has no
  drawable port here, so while one is the port a short list of decorative calls and the pen and text state calls that go with them
  (default-button outline via `FrameRoundRect`, frames, erases, `DrawString`, `PenSize`, `ForeColor`, ...) are accepted and **ignored**: what the
  app draws into a dialog itself is not shown. Every other drawing call halts by name. That is the honest limit now; real ports for
  dialogs would be the next piece. 20 of 23 new mutants killed, two more killed after added checks, one equivalent. (Castle's total
  instruction count differs by tens between identical runs, so the earlier "24 fewer instructions" was noise, not a behaviour change.)
- **Glider 4.10 draws its title screen (headless frames):** the old halt, `A204`, is a Device Manager `Control` (immediate) on reference
  -4, csCode 2, one word of parameter. It is now answered with `controlErr` (-17: no driver responds), or `badUnitErr` (-21) for other
  references; nothing is faked as success and I have not identified what the call means. The game carries on, draws its opening
  picture (a folded-paper glider on a desk, "Hey!!! Fly a REAL Glider", a pencil) and shows its own alert, *"New keyboard. Switching to
  default keyboard controls."* Okay then needed `UnionRect` (`A8AB`, smallest rectangle enclosing both, empty ones included: my reading)
  and no longer stops at a menu colour table (`mctb` is now ignored: menus are black and white here). The next halt is
  `FrameRoundRect` while recording a region (`OpenRgn`): the runtime has no oval or round-rectangle rasteriser at all, and a
  region outline of one would have to match QuickDraw's own scan conversion, which I have no reference for.
- **Glider 4.10 reaches its attract screen (headless frames, 70 s, no halt):** after the keyboard alert the game builds regions with
  `OpenRgn`, `FrameRoundRect` and diagonal lines, takes `GrayRgn` (`$9EE`), calls `PaintBehind` with a complex region, `CalcVisBehind`
  (`$A90A`), `CopyMask` (`$A817`) and `CopyBits` through a colour port's `portBits`, a complex mask region and a different colour
  table. All now work, and its title screen is up: *Glider 4.0, by john calhoun, (c) 1991 Casady & Greene*, with menus **Game** and
  **Options**, a balloon and small paper gliders moving. What each piece is, and what it is **not**:
  - **Ovals and round rectangles** (`src/mac_shape.c`, `src/mac_shapes.c`): Frame, Paint, Erase, Invert and Fill for both, on 1-bit and
    indexed colour ports, and in region recording. **This is our own scan conversion, not QuickDraw's** (a symmetric midpoint-style
    ellipse sampled at row centres, widths rounded to the shape's parity): pixels near curves can differ from a real Mac.
  - **Diagonal lines in regions** are scan-converted by us the same way (one edge per row, x rounded half up) and ignore the pen size.
  - **GrayRgn** is a plain rectangle, the screen below the menu bar (no rounded screen corners). `PaintBehind` accepts a complex
    region, treats the part inside the window as its bounding box (so the update area can be larger than strictly needed) and still
    halts if the region reaches the screen outside the window (the desktop is not repainted). `CalcVisBehind` recomputes the one
    window like `CalcVis` and ignores the region.
  - **CopyMask**: srcCopy through a 1-bit mask map; the destination is untouched where the mask is clear.
  - **CopyBits** now takes a colour port's `portBits` address (the port's PixMap), crops a source rectangle that reaches past the source
    bitmap (the destination shrinks with it), applies complex mask, visible and clip regions pixel by pixel, and converts between
    different colour tables for srcCopy by nearest squared RGB distance (QuickDraw uses an inverse table of limited resolution, so a
    colour missing from the destination may map to a different entry); other modes with different tables still halt.
  Mutation checks: all mutants killed after three rounds of added tests (the last survivors were an off-by-one in a clamp, a pen-size
  reduction that only matters for round rectangles, and a region-edge case at column 639).
- **Driving Glider 4.10 (headless frames, scripted mouse only):** the **Game** menu pulls down (New Game? #N, Load House, End Game, Open
  Saved Game..., Save Game, Save Game As?, Quit; the greyed ones are dimmed) and **Options** (Controls, Sound?, Air is Invisible, Depth
  Restore is On, High Scores, Help). **High Scores** draws its notepad page (a default entry, "Ozma of OZ!" 214050, and empty rows).
  Choosing **Controls** shows nothing I could see in the next 16 s (no dialog calls were made); not investigated.
  **New Game?** first halted at `PenMode(44)`. Apple's *Color QuickDraw* chapter (the PDF at developer.apple.com, which I downloaded and
  read) says "to specify a transparent pattern, add the transparent constant to the patCopy constant": 36 + 8 = 44. It is now supported
  on colour ports: the pattern's set bits paint the foreground, its clear bits (the background colour) leave the destination alone, and a
  foreground equal to the background paints nothing (the arithmetic transparent rule). The next halt was `PenMode(1)`, a source mode used
  as a pen mode: the same Apple/Inside Macintosh table of Boolean transfer modes gives patCopy/srcCopy, patOr/srcOr, patXor/srcXor,
  patBic/srcBic and the notPat/notSrc forms identical actions, so 0 to 7 now act as 8 to 15 on colour ports (they used to halt). The
  other arithmetic modes ($20-$27 except as above), ditherCopy and hilite still halt, naming the mode. Then **the game asks for the
  system font** (family 0, size 12, which is Chicago): the runtime only finds fonts in the guest's own resources, so it halts naming
  "Font family 0 size 12 style 0" (the message now says which face). Providing a system font means rendering the public-domain
  ChicagoFLF in `fonts/` into a bitmap strike: done next (below).
- **System font for guest family 0 (`src/mac_sysfont.c`, `include/mac_sysfont.h`).** A program that asks for the system font (family 0, plain)
  and does not carry it now gets a strike rendered from the project's public-domain ChicagoFLF through FreeType (`FT_LOAD_TARGET_MONO`), for
  characters 32-126 plus the font's missing glyph, packed in the NFNT layout `mac_strike_parse` reads, so StringWidth/DrawString/TextEdit
  work unchanged. **This is not Apple's Chicago** and not an authored bitmap: shapes and advances are the outline's at that pixel size
  (measured: at 12 px 'I' advances 6 with a 2x9 stem, 'W' advances 12; the ascent is FreeType's rounded ascender, leading 0). Sizes 8 to 36
  are served, nothing else, and only family 0 style 0; other families still halt. The file is found from `mac_sysfont_set_path`, then
  `$OSPI_SYSTEM_FONT`, then beside the executable (`../Resources/Fonts`, `../fonts`, `fonts`), then `fonts/` in the working directory;
  without FreeType or the file it halts by name. Strikes are cached for the process. Link lines for the runtime tests now carry `$(FT_LIBS)`.
  Mutation checks on the test: bit position, row offset, advance rounding and size bound were each killed (two survived an ink-count-only
  test; I added a per-row check for the 'I' stem). Headless: Glider 4.10's **New Game?** now draws its first room (a table, a Mac, a clock);
  the next halt was `$A02F` (PostEvent).
- **PostEvent (`$A02F`, in `src/mac_events.c`).** A0 = event code, D0 = message (the register ABI is from memory, not from a source I opened);
  D0 returns 0, or -1 (evQErr) when the 64-entry queue is full or the code is 0 or above 15 (the whole A0 register is tested, so a value
  with high bits is refused rather than truncated). The event takes the current mouse, modifiers and tick count and is queued behind
  earlier ones. Tests: order, wrap-around of the ring, full queue, bad codes, GetNextEvent filtering; mutants for null, upper bound, full
  and `where` killed (a mask-to-word mutant was equivalent once I stopped masking). Headless: Glider 4.10 **New Game?** now starts a game
  (glider and two reserve gliders on screen at 45 s, no input beyond the menu click) and then returns to its attract screen, with no halt in
  90 s. I did not steer the glider, so gameplay is not verified.
- **Steering attempt (headless).** The probe script gained a `keyup` action (the existing `key` action posts a keyDown and the key map keeps
  it down until a keyUp). With the right arrow (virtual key 124) held from 40 s for 6 s, then the left arrow, Glider 4.10's glider did not
  move across the room: it sat at the left wall, fell and respawned (reserves went 2 to 1). So its default controls are not the arrow keys,
  or it reads input I do not provide. **Options > Controls** (menu drag at 105,9 to 105,25) still shows nothing; the trace after it shows
  only GetKeys/event polling and drawing, no dialog or window creation. The control scheme is unknown; I have not found it.
- **Parity:** Monkey Island and Monkey Island 2 are byte-identical to the previous baseline; `make test` exits 0; ASan on Castle (90 s with
  the menu script) and on the new and touched menu, window and utility tests reports nothing.

## Next reproducible steps

1. Glider 4.10: identify the Device Manager call on refnum -4 (csCode 2) from a public source, then
   decide whether it is a small answer or a driver subsystem. Palette activation is done; a windowed run
   and screenshot are still owed once it reaches its loop.
2. Glider PRO: a hierarchical file system for `PBDirCreate` (HFSDispatch selector 6) and its siblings.
3. The real Dialog Manager (`GetNewDialog` `A97C`, `ModalDialog`, `DrawDialog`, dialog item get/set, `DisposeDialog`): Room Editor\'s next halt, built on the DITL parser and `mac_dialog_draw`. Then Pack3 selectors 5 to 8 (`StandardGetFile`) and the `stdf` Gestalt answer for Glider Edit, and Room Editor\'s custom WDEF mechanism (now possible through `mac_guest_call`). Castle of Dr. Brain now reaches its title screen headlessly: next is a windowed or scripted run to see whether it takes input, plays sound, and starts the game.
4. Exercise compaction with a real fragmenting program, or a purpose-built guest.

- **Glider PRO (headless, 20 s).** Halts at `$A260` with D0 = 6 (HFSDispatch **DirCreate**): the call before it is GetCatInfo (selector 9, answered).
  The name is "Preferences", ioVRefNum -2, ioDirID 0: it is making a Preferences folder. Not implemented, deliberately: the guest volume
  and the save overlay are a flat namespace (`mac_files_read_name` rejects ':' and names are single components), so a folder registry
  with nothing behind it would be a fake. Real folders need a decision about the overlay layout (host subdirectories under `--saves`, a
  directory-ID table, parID handling in create/open/GetCatInfo/enumeration); that is the next real piece of work for this app. Halts of
  unhandled A-line traps now print D0, so a dispatching trap's selector shows in the message (`make test` 0; Monkey Island and Monkey
  Island 2 identical to baseline; `test_mac_cpu` clean under ASan).

- **Room Editor 1.06: DrawPicture into a dialog.** The editor's password alert is followed by a dialog whose code draws PICT 300 with `DrawPicture`
  ($A8F6) while the dialog is the current port. Dialogs here have no drawable port (they are painted from their DITLs), so `DrawPicture` is
  now accepted and ignored with the other decoration calls: **the picture is not shown**. That is a stated approximation, not support; a real
  dialog port is still to do. `test_mac_dlgmgr` pins the new behaviour (clean under ASan; `make test` 0; Monkey Island and Monkey Island 2
  identical). Headless frame at 60 s: the editor's "New Room" window with the Wicked Witch alert. The next halt is **ModalDialog filter
  procedures are unsupported**: running a guest filter procedure from inside ModalDialog (there is a guest-call helper, `mac_guestcall.c`)
  is the next piece.

- **ModalDialog filter procedures (`src/mac_dlgmgr.c`, call helper `mac_guestcall.c`).** A guest filter `FUNCTION f(dlg; VAR event; VAR itemHit): BOOLEAN`
  now runs: each pass of ModalDialog offers one event (or a null event when idle, so filters that blink or poll see idle calls) through
  `mac_guest_call`, with an EventRecord and a zeroed itemHit in scratch below the stack (above the callee's frame). TRUE returns the filter's
  itemHit as the answer without the dialog toggling anything; FALSE lets the dialog handle the event as before; the trap is re-executed
  (rewind + yield) otherwise. The result Boolean is read from the high byte of the result word (the Pascal layout). A filter address that is
  odd, in low memory or outside RAM halts. The boolean/guest-return plumbing was driven by hand in the test (no 68k code is executed there):
  mutants for the boolean byte, skipped dialog handling, scratch overlapping the frame, the null event, toggling on TRUE and the rewind PC
  were killed; "dialog already gone" in the completion is defence in depth only (disposing a dialog during ModalDialog already halts), so
  that mutant survives by design. Decoration tables moved to `src/mac_dlgswallow.c` to keep `mac_dlgmgr.c` under 500 lines (498).
  Headless: Room Editor 1.06 now shows its **Room Background Info?** dialog with the tile palette after the password alert and runs 60 s with no
  halt; the PICT in the earlier dialog is still not shown and I did not click through to see whether the editor itself works. `make test` 0, ASan
  clean on `test_mac_dlgmgr`, Monkey Island and Monkey Island 2 identical, Castle refreshed.

- **Room Editor 1.06: Okay in the background dialog, then the tools window.** A scripted mouse down/up on **Okay** closes the dialog and the
  editor asks for `GetNewCWindow` 128 (its tools palette, "Room Navigator"). That WIND is 274 bytes with a 5-byte Pascal title and 250 stray
  bytes after it (bytes of a longer title). The loader used to halt on any trailing bytes beyond the System 7 positioning word; it now ignores
  them (halt message also names the id and size now). That is my reading of the format: a real Mac reads the length-prefixed title and nothing
  else that would matter here, but I did not verify what a real Mac does with the stray bytes (the positioning word included). A mutant that
  rejects long resources again is killed by the new `stray_bytes` test; `make test` 0, ASan clean on `test_mac_window_resource`, Monkey Island and
  Monkey Island 2 identical, Castle refreshed. Next halt (headless): that window's procID is 160, a **custom floating-window WDEF**
  (`Only standard zoomNoGrow, noGrowDocProc, plainDBox and dBoxProc windows ... custom WDEFs are unsupported`). Running a guest WDEF (or
  providing a floating-window frame ourselves, which the editor may rely on for hit-testing) is a real subsystem; not started.

- **Decision on Room Editor's tools window (procID 160).** I listed the app's resource types: it carries a **WDEF** resource, so this is a guest
  window definition. Running one means calling its draw/hit-test/calc-region/grow messages from the Window Manager, a real subsystem; I did not
  start it and the halt stays. The same app also needs more than one visible window (document plus tools palette), as Castle's Hide Desktop does
  (`Showing a second visible window requires unsupported compositing`, `mac_window.c`). So **multi-window compositing** (z-order, per-window
  clipping, update regions through the existing PaintBehind/CalcVisBehind pieces) is the shared blocker for both; that is the next piece to
  scope. Glider 4.10 again: a headless run with mouse sweeps and the keys A, S, Z, X, comma, period, space and the up/down arrows (held one
  at a time, released) changed frames but I could not tie any of it to glider motion, so its controls are still unidentified.

- **Stacked visible windows (`src/mac_window_stack.c`).** Design: visible windows share the one screen; each keeps the screen snapshot taken when it
  was shown (its backdrop), so they behave like a stack. A window may be **shown** (or created visible in front) only when it is first in the
  WindowList; it may be **hidden or closed** while it is the frontmost visible window (its backdrop is restored, as before), or while it sits
  wholly under the frontmost window's frame, in which case no pixel changes and its backdrop (what lay behind it) is copied into the front
  window's backdrop over its frame so that hiding the front window later reveals the right thing. Anything else (showing a window that is not
  frontmost, creating one behind a visible window, hiding or closing a window that sticks out from under the front one) still halts with a
  message: that needs real clipping and compositing. This is my design, not a copy of the Window Manager's. The two old halts that said "second
  visible window" are gone. Tests: `test_mac_window` and `test_mac_multi_window` now check the screen byte for byte after each step (hide covered,
  hide front, close covered; 12 mutants over the guards, the cover test, the backdrop patch and the restore conditions all killed), ASan clean,
  `make test` 0, Monkey Island and Monkey Island 2 identical, Castle baseline refreshed (instruction count 416905336, noise as before).
  Headless: **Castle's Hide Desktop** now runs: the menu bar and desktop go black and the game window sits centred (320x200-ish) on black, no
  halt in 120 s. I did not script the reverse (Show Desktop) and a windowed run is untested. Room Editor still stops at its custom WDEF tools
  window; clipping between windows that overlap partly is still unimplemented.

- **Castle after Hide Desktop (headless).** After Hide Desktop the menu bar is gone and my attempts to bring it back with the mouse (move to the top
  edge, press, drag down) changed nothing visible, so Show Desktop is **not verified**; the game itself kept running 120 s with no halt. I ran
  all seven existing Castle scripts (room, room2, bell, bell2, door, move, input) for 150 s each: no halts. Further progress needs puzzle play
  driven by reading frames, which I have not done; the checklist row now says Hide Desktop blacks out the desktop and Show Desktop was not reached
  (still 499 lines).

- **Glider PRO: DirCreate refused, then the Sound Manager (headless).** I scoped real folders and decided against building them now: the flat
  guest/overlay namespace would need a directory-ID table, parID handling in create/open/GetCatInfo/delete/rename/enumeration and host
  subdirectories under `--saves`, a change across files that are already over the size limit (`mac_files_write.c` is 516 lines). Instead
  `HFSDispatch` selector 6 (**DirCreate**) is now answered with `wrgVolTypErr` (-123, "not an HFS volume") and nothing is created: an honest
  statement that this volume has no folders. Glider PRO handles it itself: it shows its own box, "A File Error Loading/Saving Preferences ...
  (error = -123)" (my first try with -54 showed that box too; -123 is the code that says what is actually true), and, after a click on Okay,
  carries on. `test_mac_files` checks the -123 result and that no `Prefs` directory appears on the host (two mutants killed; ASan clean;
  `make test` 0; Monkey Island and Monkey Island 2 identical). **Next halt:** `$A800` with D0 = `022C0018` (a Sound Manager dispatch selector I
  cannot name from memory; the existing double-buffer service answers only `00200008`). Before implementing it I need the selector table from
  Apple's Sound Manager documentation (curl a PDF; Firecrawl is out of credits); I did not open one this iteration, so I am not guessing.

- **Glider PRO: Sound Manager volume, Scrap Manager, SetResLoad (headless, after the preferences box).** I downloaded Apple's *Sound Manager* PDF
  and read the volume section: `GetDefaultOutputVolume(VAR level)` is `_SoundDispatch` selector `$022C0024`, `SetDefaultOutputVolume(level)` is
  `$02300024`, the level is a long of two 0-$0100 words. Glider PRO uses the same routine numbers with the low word `$0018` (`022C0018`,
  `02300018`); I did not find why, and I decoded its call site (one VAR long, an OSErr result, the low word divided by 36 and clamped to 7) to
  confirm it is the getter, so `src/mac_sound_volume.c` answers both low words alike: Get reports `$01000100` (full volume, no system volume is
  modelled), Set is accepted and ignored. Next halt `$A9FD` = **GetScrap** (signature from the opened MPW listing): `src/mac_scrap.c` is a small
  host-side scrap (ZeroScrap, PutScrap, GetScrap; 8 types, 64 KB each) where GetScrap of an absent type returns noTypeErr (-102, from my memory)
  and GetScrap of a present type halts by name (it would have to resize the destination handle). Then `$A99B` = **SetResLoad**: accepted and
  ignored (`src/mac_resload.c`; resources are always loaded here, a stated difference). Tests: `test_mac_sound_volume`, `test_mac_scrap` (corrupt
  pointers, odd/short stacks, bounded tables; mutants killed except two that are equivalent because a later memory write halts anyway), ASan
  clean, `make test` 0, Monkey Island and Monkey Island 2 identical, Castle refreshed. **Next halt:** `Monochrome drawing into the indexed
  screen is unsupported` (a 1-bit drawing path into the 8-bit screen); not started.

- **Basic port over the 8-bit screen (`src/mac_raster.c`).** Glider PRO draws its own alert frame with the basic (black-and-white) QuickDraw
  calls into a basic GrafPort whose bitmap is the 8-bit screen (baseAddr = screen, rowBytes 640). That used to halt ("Monochrome drawing into the
  indexed screen"). It now draws bytes: pattern bit 1 is the palette's darkest entry, bit 0 its lightest (ties: first dark, last light, as the
  window chrome picks), Boolean pattern modes act on the pixel values (my reading of Apple's Color QuickDraw chapter, which says Boolean modes on indexed
  devices operate on indexes), InvertRect complements the index byte, nothing dithers. Bitmaps wider than their rows, running past the
  screen, or on a depth other than 1 or 8 halt. `test_mac_raster` has the indexed section (rect paint/erase/invert/frame, the four modes and
  the not-mode, clipping, palette choice, ties, corrupt ports); 12 mutants killed; ASan clean; `make test` 0; Monkey Island and Monkey Island 2
  identical; Castle refreshed. Headless: Glider PRO now shows its own boxes: "Okay, with your monitor & color depth, there isn't enough memory ...
  the music won't be loaded" (Whatever to dismiss), then "Couldn't Load a Resource ... (program error = 4)". The trace shows no resource
  lookup failing (all `snd ` 1029-1045 found), so the program is judging memory from something we report (free memory / largest block / a
  failed NewHandle or purge): the next thing to find; not started.

- **Glider PRO: standard cursors, HandToHand/PtrToHand, ForeColor/BackColor (headless).** The "Couldn't Load a Resource (program error = 4)" box
  was real and ours: the startup routine calls `GetCursor(1)` (the I-beam), which the System supplies on a real Mac; the runtime only made the
  watch cursor. It now makes the standard cursors 1 I-beam, 2 crosshair, 3 plus and 4 watch (shapes authored here, centre hot spots; ids 0 and 5
  are still "not found"). The "isn't enough memory ... the music won't be loaded" box is the app's own decision and still appears; I did not find
  what it measures. Next halts, in order: `$A9E1` **HandToHand** (and its neighbour PtrToHand `$A9E3`, `src/mac_handcopy.c`; register
  conventions from my memory: A0 handle in, A0 copy out, D0 OSErr; purged -112, forged/unknown -109/-111, no room -108), then `$A862`
  **ForeColor** (`BackColor` `$A863`) on a basic port (`mac_raster.c`) and on a colour port (`mac_color_raster.c`), with QuickDraw's eight colour
  constants (33, 30, 205, 341, 409, 273, 137, 69; from my memory) taken as pure colours and matched to the nearest palette entry
  (`src/mac_qdcolor.c`); any other value halts. Tests: `test_mac_cursor`, `test_mac_handcopy`, `test_mac_raster`, `test_mac_color_raster`
  (corrupt handles/pointers/sizes, full handle table and heap, bad constants, colour vs basic ports); mutants killed except two equivalent ones
  in HandToHand (the size cap and the odd-handle test are shadowed by later checks); ASan clean; `make test` 0; Monkey Island and Monkey Island 2
  identical; Castle refreshed. **Next halt:** `PICT header requires matching version2 fixed bounds (opcode 0c00, byte 40)`: a PICT whose version-2
  header carries bounds that do not match the picture frame; not examined yet.

- **Glider PRO: extended version 2 PICT header, and a bigger handle table (headless).** Its PICT 1997 is an *extended version 2* picture (Version
  -2); the decoder only knew version 2 (Version -1 with fixed bounds). I downloaded Apple's *Imaging With QuickDraw* appendix A and read the
  header section and Listing A-5: after `$0C00` come 24 bytes: Version `$FFFE`, a reserved word, best horizontal and vertical resolution as
  Fixed, the optimal source rectangle, a reserved long (the real file has four reserved bytes there although the listing shows two). It now
  decodes these when both resolutions are 72 dpi (other resolutions halt by name: no scaling by dpi), the reserved fields are zero and the
  source rectangle is not empty; the picture frame is what is drawn (the source rectangle, which in the document's own example differs from the
  frame, is not used). Tests: `test_mac_picture` with a truncation sweep, nine malformed headers, a second HeaderOp; seven mutants killed;
  ASan clean. Next halt: `No handles for color port`: the 512-entry guest handle table was full (Glider PRO loads hundreds of `snd ` resources
  and OpenCPort needs eight slots); `MAC_RUNTIME_HANDLES` is now 4096 (every user of it takes the macro; `make test` 0, Monkey Island and Monkey
  Island 2 identical, Castle refreshed). **Next halt:** `NewCWindow requires initialized 640x480 indexed display and bounded content
  rectangle`: the app opens a window whose rectangle falls outside our 640x480 limits (negative origin, off-screen, or too small); not examined yet.

- **Glider PRO reaches its title screen (headless).** The window it asks for (WIND 130: 20 pixels high, full width, plainDBox, hidden) tripped
  a minimum size I had set (32 x 64) that has no reason behind it: the chrome and backdrop code clip to the screen. NewCWindow and SizeWindow now
  take any rectangle with at least 1 x 1 pixels inside the screen (zero-size, inverted or off-screen rectangles still halt; the old pinned
  minimums in the tests were replaced by a table of accepted and refused rectangles; 8 mutants killed). Then `MoveWindow(front = TRUE)` on a hidden
  window halted: Apple's *Macintosh Toolbox Essentials* (MoveWindow, opened) says front TRUE calls SelectWindow if the window is not active; this
  runtime's SelectWindow does nothing for a hidden window, so the move now just moves it (hidden: no activation, no reordering, no event;
  visible: activated as before; one mutant, killed). Result, from a **headless frame at 90 s with no halt**: Glider PRO draws its title picture
  (a greyscale illustration with paper gliders, drawn through our indexed path) and its own dialog "A problem came up: There are no houses on this
  drive! ... Error #: 0": it looks for a house file (the game's levels are separate 'gliH' files in its Houses folder) and we have none, plus the
  earlier "not enough memory ... music" box. `make test` 0, ASan clean on the window tests, Monkey Island and Monkey Island 2 identical, Castle
  refreshed. Not verified: that the picture is drawn right (I looked at one downscaled frame), the 'no houses' cause (a missing house file or
  the missing folders, since DirCreate is refused with -123), anything about play, audio, or a windowed run.

- **Glider PRO: full-colour title screen (headless, 100 s, no halt).** The "no houses" dialog is the app's own and honest: the trace shows only two File
  Manager calls (GetCatInfo, then the DirCreate we refuse with -123), no house enumeration at all, and no house file exists on this machine (`find`
  under ~/projects found none; only the app and its resource file are in scratchpad). After a scripted click on that dialog's Okay the game went on
  and needed, in order: `PaletteDispatch` selector `$0002` = **RestoreDeviceClut** (MPW listing: `$7002`; resets the device table and forgets the active palette;
  gd nil or the main device), `PaintBehind(NIL, rgn)` (a NIL start window is a no-op, like `CalcVisBehind(NIL)`), **GetForeColor/GetBackColor** (`$AA19/$AA1A`),
  **Index2Color/Color2Index** (`$AA34/$AA33`, `src/mac_colorindex.c`; Color2Index's pointer-argument convention is from my memory) and the **application font**
  (family 1; Geneva on a real Mac) which is now served from the same ChicagoFLF stand-in as family 0, so its glyph shapes are not Geneva's. Result: the colour
  title screen (illustration, "Glider PRO by john calhoun", paper gliders, a Game and an Options menu, and the text "House: Slumberland") on screen
  with no halt. Tests: `test_mac_palette_manager`, `test_mac_paintbehind`, `test_mac_color_raster`, `test_mac_colorindex`, `test_mac_text` (corrupt pointers,
  stacks, indices, depths; mutants killed except one equivalent odd-pointer check); ASan clean; `make test` 0; Monkey Island and Monkey Island 2 identical;
  Castle refreshed. This is one downscaled frame: I have not checked the picture pixel by pixel, nor clicked into the game, nor listened to anything;
  windowed behaviour is untested.

- **Glider PRO menus (headless, scripted mouse).** From the title screen (after the three dialog clicks): **Game** shows New Game (dimmed, Cmd-N), Two Player
  Game (dimmed), Open Saved Game? (dimmed), Load House? (Cmd-L), Quit (Cmd-Q); the dimmed items fit "no houses". **Options** shows Room Editor (Cmd-E), High
  Scores? (dimmed), Preferences? (Cmd-P), Demo? (Cmd-D). Choosing **Load House?** and **Demo?** (menu drag, release on the item at y 74) left the title
  screen unchanged for 25 s with no halt (not investigated: either the item missed, or the app waited for a file dialog or a house we do not have).
  Choosing **Preferences?** halts: `Dialog user items are unsupported.` The Preferences dialog draws custom items with guest procedures (DITL user
  items); running them needs a dialog GrafPort that guest drawing can target, and dialogs here have none (they are painted from their item lists; see the
  parked "real dialog GrafPort" item, which this and Room Editor's PICT item both need). That is the next real subsystem; not started in this pass.

- **Dialog user items accepted (inert), Glider PRO Preferences opens.** I scoped a real dialog GrafPort (a colour port per dialog whose PixMap is the screen, user-item
  draw procedures called through `mac_guest_call`, the native repaint in the modal loop re-running them) and decided it is too large for one safe step: the
  modal loop repaints the whole dialog from its item list on every pass, which would erase anything the guest draws, so real drawing needs a dirty-flag repaint
  design and a rewrite of the swallow logic, in a file that is at the size limit. The smallest honest slice: a DITL **user item** (type 0) no longer halts
  `GetNewDialog`; it is accepted, **never drawn and its draw procedure never called**, and if enabled it still counts as a hit like any other item. Headless result:
  Options > Preferences? now opens a dialog (four icons: an eye, an ear, a hand and a brain-like shape, with "All Defaults" and "Okay"); a click on the first
  icon changed nothing visible and caused no halt; Okay closes it and the title screen comes back. `test_mac_dlgmgr` now expects dialog 130 to open and dispose
  cleanly; `make test` 0; ASan clean; Monkey Island and Monkey Island 2 identical; Castle refreshed. What is **not** shown: whatever the Preferences items draw
  with their procedures (panels, boxes), and the real dialog port (Room Editor's PICT in its dialog is still not shown).

- **Glider PRO's built-in Room Editor opens (headless).** Options > Room Editor (menu drag at 105,9 to the first item) needed two things: `SetWTitle` (`$A91A`;
  `src/mac_window_title.c`: copies the Pascal string into the window title and redraws a visible titled window's bar; a length that runs off RAM or a bad pointer
  halts and changes nothing; window record's own titleHandle is not updated) and `TextSize(0)`, the default size (taken as 12 points for every family:
  **from my memory of the Font Manager, not a document**; in `mac_text_face`). Result in a headless frame at 100 s, no halt: a window titled
  "Slumberland - No rooms" on a grey field with the text "Nothing to show", and a new "House" menu in the menu bar. Tests: `test_mac_window` (round trip with
  GetWTitle, redraw, hidden window, plainDBox, six bad strings, a non-window) and `test_mac_text` (size 0 is 12: a family with no 12-point strike names size 12);
  mutants killed except one equivalent (drawing a hidden window is already a no-op); ASan clean; `make test` 0; Monkey Island and Monkey Island 2 identical;
  Castle refreshed. Not tried: the House menu (creating a room), saving, or any editing; no house file exists to open.

- **Glider PRO's editor: House menu, New House, and the next real subsystem (headless).** The editor's **House** menu: New House? (Cmd-N), Save House (dimmed), House
  Info? / Room Info? / Object Info? (dimmed), Cut / Copy / Paste / Delete Room and Duplicate Object (dimmed), Bring To Front, Send To Back, Go To Room?, Map Window,
  Tools Window, Coordinate Window. **New House?** shows our native Standard Put File dialog ("Name for New House:", "Save in: OS Pi Data", "Untitled House"). Clicking
  **Save** makes `FSpCreate` succeed (a 0-byte file "Untitled House" appears in the `--saves` overlay), then the game calls **`$A20A` (HOpenRF, open the new file's
  resource fork)** and the runtime halts: `unhandled A-line trap a20a`. A Glider PRO house is a resource file; saving one needs **writable resource forks** on the
  save overlay (CreateResFile/HCreateResFile, open for writing, AddResource/WriteResource/ChangedResource/UpdateResFile/CloseResFile, and reading them back with
  OpenResFile for Load House). Today resource forks are read-only exports of the application volume (`mac_resource_files.c`: "No writeback"). That is the next piece
  of real work for the Glider PRO loop (create a house, save, reload, New Game); it is a Resource Manager writer plus overlay storage for the fork (a sidecar
  `<name>.rsrc` file under `--saves`, written atomically), and I did not start it in this pass.

- **Glider PRO: New House saves further (headless): resource-fork sidecars, CreateResFile, HGetVol, CmpString, ResolveAliasFile.** Following the New House > Save click the
  game now gets through: **PBHOpenRF/PBOpenRF** (`src/mac_files_rf.c`: a file's resource fork is its `<name>.rsrc` sidecar; created empty on first open on the save overlay, then an
  ordinary file with the data fork's one-writer rule, staging and quota; on the application volume only an existing export opens, read-only; names over 26 characters
  cannot have a fork), **PBHGetVol/PBHSetVol** (`mac_files_hvol.c`: the one volume, root directory 2), **CreateResFile** (`mac_files_crf.c`: writes an empty resource fork,
  256-byte header + 30-byte map, from my memory of the format; the test round-trips it through this project's own parser; a fork with bytes is left alone with ResErr
  -49), **CmpString** (`mac_cmpstring.c`: ASCII case folding only; the diacritic option is not modelled; semantics from memory), and **AliasDispatch ResolveAliasFile**
  (`mac_alias.c`: an existing file resolves to itself, a missing one is -43; other alias selectors halt). `make test` 0; ASan clean on `test_mac_files_rf` and
  `test_mac_files`; 14 mutants killed, one equivalent; Monkey Island and Monkey Island 2 identical; Castle refreshed. **Next halt:** `HighLevelFSDispatch selector 13 (FSpOpenResFile)`:
  opening the new house as a *Resource Manager* file. The overlay now has `Untitled House` (0 bytes) and `Untitled House.rsrc` (286 bytes, an empty valid fork). What is left is a
  **writable Resource Manager**: FSpOpenResFile/OpenResFile of overlay forks, AddResource/WriteResource/ChangedResource/UpdateResFile/CloseResFile with a map serializer (today
  resource files are read-only exports). That is the next large piece; not started.

- **Glider PRO: FSpOpenResFile, custom-WDEF windows, windows behind a window, HiliteWindow (headless).** `FSpOpenResFile` (HighLevelFSDispatch selector 13) now opens the
  overlay sidecar ("Untitled House.rsrc") as a **read-only-in-memory Resource Manager file** (the resource chain looks in the `--saves` overlay first, then the application
  volume's export; the file is marked writable for a later write-back; OpenResFile by name uses the same lookup; the result word is the reference or -1 with the error in
  ResError). **Not done: writing resources back** (AddResource / WriteResource / UpdateResFile and the map serializer): that is still the large missing half. Then the editor
  asked for windows: **custom window definitions** (procID = id*16+variant, e.g. 2064 = WDEF 129) are not run: they are accepted for ids 1-255 and shown with this runtime's
  titled chrome (a stated approximation: the program's own frame, grow box and hit-testing are not provided; GetWVariant-style queries see procID 4); **NewCWindow behind a
  given window** (inserted directly behind it in the WindowList; behind an unknown pointer halts; visible-behind-visible still halts as it needs compositing); and
  **HiliteWindow** (`$A91C`: the record's hilited byte and the chrome follow the flag, no event). Test updates: procIDs that used to be pinned as refused (16, 160, 176, 99) are now
  accepted or replaced by 7/15/4096; `test_mac_fsspec` no longer lists selector 13 as unimplemented. Mutants killed except two equivalent ones; ASan clean; `make test` 0;
  Monkey Island and Monkey Island 2 identical; Castle refreshed. **Next halt (headless):** `$A954` = **NewControl**: the editor's windows carry controls (scroll bars, buttons); there is
  **no Control Manager in this runtime at all** (no NewControl/DrawControls/TrackControl/SetCtlValue...). That is the next large subsystem, and it also blocks the writable-resource work from
  being exercised: the New House flow stops there after opening the house file.

- **Control Manager subset (headless).** Glider PRO's editor halted at `NewControl` (`$A954`): the runtime had no Control Manager. New `src/mac_controls.c` + `src/mac_control_draw.c`:
  NewControl, GetNewControl (CNTL layout from memory: rect, value, visible, max, min, procID, refCon, title), DisposeControl, KillControls, Show/HideControl, Move/SizeControl,
  Get/SetCTitle, Get/SetCtlValue, Get/SetMinCtl/MaxCtl, Get/SetCRefCon, Get/SetCtlAction (stored, never called), HiliteControl, DrawControls, UpdateControls, TestControl, FindControl;
  DrawGrowIcon (`$A904`) validates the window and draws nothing (no window kind we create has a grow box). Standard procIDs 0, 1, 2, 16 (+8 useWFont) only, drawn natively with
  the dialog pen (this project's own look). **Not done:** TrackControl and DragControl halt by name; custom CDEFs halt by name. Stated differences from a real Mac: the procID lives in
  the record's contrlData long (contrlDefProc is nil); redraw erases to the lightest palette entry, not the window background; windows in front of the owner are ignored when drawing.
  Handles are checked against the runtime's own table (type, master pointer), the owner against the window registry, lists are bounded at 512 and a loop halts. Test
  `tests/test_mac_controls.c` has 16 corrupted-input cases (forged/odd/stale/foreign-type handle, moved master pointer, bad owner, bad window, bad rect/title pointers, max<min, bad stack,
  bad destination, zero size, bad FindControl pointer, cyclic list), the part boundaries of a scroll bar, a GetNewControl from a synthesized fork, and the supported and refused procIDs.
  Mutants killed except one equivalent (the odd-handle test: the table lookup rejects odd handles anyway). ASan clean; `make test` 0; MI and MI2 identical; Castle refreshed.
  **Next halt (headless):** Glider PRO's New House flow now halts in `NewControl` from `GetNewControl` id 129: **procID 2050 = the application's own CDEF 128** (CNTL 128 and 129 both).
  Running a program's CDEF means executing guest code through `mac_guest_call` for draw/hit-test/calc messages: that is the next design step, and it is also what the parked guest-WDEF item needs.

- **Guest control definitions (CDEF) run (headless).** A control whose procID is id*16+variant (id >= 2) now runs the program's own CDEF resource: new `src/mac_cdef.c`. Each
  Control Manager call builds a job (a list of messages: initCntl 3, drawCntl 0, testCntl 1, dispCntl 4; numbers from memory) and each `mac_guest_call` completion starts the
  next; the last runs the call's finish step (writes the result, e.g. the new handle or the part code) and pops the arguments. The CDEF handle is a pinned resource handle in
  contrlDefProc with the variant in the high byte (as on a real Mac); contrlData belongs to the CDEF. The owner window's port is current during the calls and restored after.
  Part codes from testCntl are clamped to 1..254. One job at a time: a Control Manager call made by a running CDEF halts. Standard controls take the same path natively.
  Behaviour change: FindControl now returns a control only when its part is non-zero and it is visible and active. **Bug found and fixed on the way:** `call_filter` (ModalDialog filter
  procedures) passed theDialog/event/itemHit in the reverse stack order (and its test pinned the wrong order); a real Pascal filter would have read itemHit as the dialog.
  New services the CDEF needed: GetPenState/SetPenState on colour ports, GetItem, GetItemCmd/Mark/Style/Icon (GetItemIcon's 2-byte VAR is from memory), CalcMenuSize, GetMCEntry (always
  nil: no menu colour tables), and CountMItems/Get* accept a raw MENU resource handle (strict paths still refuse it). Tests: `tests/test_mac_cdef.c` (message order and argument layout, results,
  clamping, port restore, hidden/inactive skipping, dispose/kill, nested halt, 9 corrupted-input cases), additions to the menu-records, colour-pen and dialog tests. Equivalent mutants:
  the 520-step bound (lists are capped at 512), the CDEF size floor (checked twice).
  **Glider PRO (headless):** CNTL 128/129 (procID 2050, CDEF 128) are created, initialised and drawn by Glider's own CDEF; the run goes on to the application's own `GetCIcon` (`$AA1E`,
  colour icons 'cicn'), the next halt. **Not seen in a window.** The CDEF's drawing is only as good as our pen, text and menu calls; no frame has been checked yet.

- **Colour icons and the Glider PRO room editor on screen (headless).** `GetCIcon` ($AA1E), `PlotCIcon` ($AA1F) and `DisposeCIcon` ($AA25): new `src/mac_cicn.c`. The 'cicn' layout (PixMap 50,
  mask BitMap 14, icon BitMap 14, iconData 4, mask bits, bitmap bits, colour table, pixels) was checked against Glider's 906-byte icons (82 + 128 + 128 + 56 + 512). GetCIcon keeps the
  resource's bytes in a guest handle; PlotCIcon re-parses and bounds-checks them every time because the program can write them. Supported: unpacked chunky indexed 1/2/4/8-bit icons up to
  256x256, a mask (rowBytes 0 = opaque), nearest-neighbour scaling, nearest-palette-entry matching (first of equals), the colour port's clip (`mac_color_target`, new in mac_color_raster.c;
  complex clip regions halt as before), indexed-8 colour ports only. The 1-bit iconBMap is ignored. A missing id gives nil and ResError -192. Bug found by the new test and fixed: a nil
  current port returned -1 without halting. Test `tests/test_mac_cicn.c`: 16 header-field corruptions, every truncation, limits and row floors on both sides, 11 corrupted-handle/guest-memory cases,
  masking, scaling (square and not), clipping, ties, value-vs-position tables, a table value over 255. All mutants killed except the nil-port and colour-port checks, which a later check
  also catches (equivalent in effect). ASan clean; `make test` 0; MI and MI2 identical; Castle refreshed.
  **Glider PRO (headless frames, one run):** the New House flow now reaches the editor: "Untitled House - No rooms", a Tools window with a 16-icon colour palette, the pop-up control (Glider's own
  CDEF, "Blowers"), a Map window with a grid and scroll bars; a scripted click on a tool icon moves the selection frame and its caption changes ("Floor Vent"), so mouse input reaches the app. The
  House menu opens (New House, Save House, House/Room/Object Info, Cut/Copy/Clear/Duplicate, Bring To Front, Go To Room, window toggles); it has **no "New Room"**. A scripted double-click on a Map
  cell did nothing visible: not yet understood (double-click timing under scripting, or the Map window's click handling). The run no longer halts: it idles in the event loop. Not checked: that the
  editor window overlaps are right, text positions, or any window in a real session.

- **FindWindow sees every window; Glider PRO creates a room (headless).** Root cause of the dead Map window: `FindWindow` tested only the FRONT window, so a click on any window behind it
  (the Map window behind the main one) came back as "nothing" and the app did nothing. New `mac_window_hit_test` (registry file) walks the visible windows front to back, returns the first
  hit part and leaves that window selected (no hit: the front window is selected again). With it, the scripted double-click on a Map cell opens Glider's own alert "Do you wish to create a
  New Room at this location?" (Cancel / Create; frame seen). Then the New Room path needed: **PtrAndHand** ($A9EF; A0 ptr, A1 handle, D0 count; via SetHandleSize; source copied aside;
  resource, code, forged, purged and locked handles refused; register interface from memory), **SetItemStyle/Mark/Cmd/Icon** ($A942, $A944, $A84F, $A940), **Count1Resources** ($A80D,
  current file only). Tests: FindWindow across two and three windows (front, back, title bars, none, menu bar, hidden), PtrAndHand (13 cases), the Set* calls including out-of-range items
  and bytes after the block, Count1Resources. Surviving mutants: hidden-window skip (mac_window_hit already refuses hidden windows), and in PtrAndHand the zero-count early return, the
  16 MB sum guard and the grown-pointer recheck (each also caught by a later check). ASan clean; `make test` 0; MI and MI2 identical; Castle refreshed.
  **Next halt (headless):** after Create, Glider opens its Room Info dialog (DLOG 1003, 19 items) and the Dialog Manager halts with "Dialog control items are unsupported": the DITL has a
  **control item (type 7, CNTL 128 = the CDEF pop-up)**, two PICT items, user items and edit text. The native dialogs have no real GrafPort, so a CDEF control cannot draw in one; this is the
  parked "real dialog GrafPort" step and is now on the critical path (creating a room, then Save House). The idle loop of the editor runs ~2.6M instructions per virtual second in the probe;
  whether the windowed desktop burns a core there was NOT checked.

- **Dialog control items: Glider PRO's Room Info dialog works and a room is created (headless).** A dialog whose DITL has control items (type 7, CNTL id in the item data) now owns a
  HIDDEN registered window (proc 1, `MacWindowState.port_only` 1 = dialog hidden, 2 = shown) covering the dialog rectangle with the dialog's coordinates as its local ones: it is only the
  controls' owner and colour port (new `src/mac_dlgwindow.c`; drawing and region helpers moved to `src/mac_dlgdraw.c`, shared struct in `include/mac_dlg_int.h`, which also brought
  `mac_dlgmgr.c` from 498 to 454 lines). Each control item becomes a real control from its CNTL with the item's rectangle; GetDItem returns the control handle; a CDEF runs in the port window.
  `GetNewDialog` ends through the CDEF job (initCntl, drawCntl, then the result and the stack pop). The native repaint overwrites the controls, so after every Dialog Manager call that
  completed (and after each filter step of ModalDialog) the controls get drawCntl again (`mac_dlgwin_flush`), and the pending yield is cancelled so the host never sees the repainted-but-not-
  redrawn screen (frames are taken at yields: they showed no pop-up until this was fixed; a windowed run would have flickered). Also: **Get/SetCtlValue and HiliteControl on a
  button, check box or radio item handle** (a real dialog's items are controls; Glider calls SetCtlValue on its checkbox), and the Dialog Manager's QuickDraw "swallow" no longer eats the CDEF's
  own drawing. Not done: dispCntl is NOT sent when a dialog is disposed (the controls are freed natively), clicks on a control item do nothing (TrackControl is not implemented), PICT items and
  user items still draw nothing, the dialog is placed at its DLOG rectangle (0,0 here) instead of being centred.
  **Seen (frames, headless):** Room Info shows Room Name edit text, Location, Floor/Suite, 'Number of Objects', 'First Room' checked, 'Bounds' dimmed, Cancel/Okay and **'Tiles: [Simple Room v]'**,
  the pop-up drawn by Glider's own CDEF. Clicking Okay (346,248) closes it and the editor title becomes 'Untitled House - Untitled Room (1, 61)'. **House > Save House** then runs Glider's own
  'Checking House File' pass and reports 'You have no stars in the house!': a house needs a star object before it can be saved (a Glider rule, not ours), so nothing new is written
  ('Untitled House' 0 bytes and an empty 286-byte '.rsrc' from New House are all that exist in the saves dir). To get further the editor needs a star placed: that means the Tools pop-up
  ('Blowers') must be clickable (TrackControl/pop-up menus).
  Tests: `tests/test_mac_dlgwindow.c` (standard and custom control items, hidden dialogs, ShowWindow/HideWindow, DrawDialog repaint, DisposeDialog cleanup, item-handle control calls, refusals,
  the swallow exemption); mutants killed except four equivalents (the control item's data-length check is also enforced by the DITL parser, the hidden-flush check is defensive, cancel-yield is
  not observable through the API, and dropping the hidden dialog's draw steps is redundant with the port_only gate). ASan clean; `make test` 0; MI and MI2 identical; Castle refreshed.

- **TrackControl (pop-up convention) and PopUpMenuSelect: Glider PRO's Tools pop-up works (headless).** Glider calls `TrackControl(ctl, pt, -1)` on its pop-up CDEF; with actionProc -1
  the CDEF is sent testCntl and then autoTrack (message 8, param = the part found; from memory), and the result is the part. Only that case exists: a standard control, an action procedure or
  none still halt by name. The CDEF's autoTrack calls `PopUpMenuSelect` ($A80B, new in `mac_menuselect.c`): the menu is drawn so that item popUpItem lies at (top, left), moved to fit the
  screen, tracked like MenuSelect (the pointer decides the highlighted item; releasing over an enabled item returns menuID<<16|item, else 0; no bar title is touched; it never
  switches to bar menus), with a top frame row (bar menus hang from the bar and have none; the test found that). `InsertMenu` now adopts a raw MENU resource handle (publishing sizes, as GetMenu
  would). **Seen (frames, headless):** clicking 'Blowers' opens the nine-category pop-up (Blowers, Furniture, Prizes, Transport, Switches, Lighting, Appliances Etc., Enemies, Clutter); releasing
  on 'Prizes' redraws the palette with a star; the star tool (a click needs ~0.4 s held: a 0.2 s click was not taken, cause not investigated) shows 'Magic Star' and a click in the room places
  an object (a dashed 32x32 outline: the object's picture is not drawn, and the room still says 'Nothing to show'). With an object present, House > Save House runs the check silently
  (no 'no stars' message), then calls PBWrite once (A003, d0 0x4be) and SetEOF/SetFPos, and never closes the file; the file in the saves dir stays 0 bytes (writes go to the staging copy,
  which is only published at close). Not investigated whether Load House can then see it. **Game > Load House?** opens a dialog that is blank except for Cancel/Okay: the house list is
  a user item (and probably List Manager), and user items are inert. NEXT is therefore user items with draw procedures (needs the dialog port window for every dialog, not only those with control
  items) and the List Manager, or committing staged writes earlier.
  Tests: popup tests in `test_mac_menuselect.c` (placement, clamping on all four edges, item range, highlight, release over enabled/disabled/separator/outside/bar title, restore of every pixel,
  refusals, re-entry, menu deleted under the tracker), TrackControl cases in `test_mac_cdef.c`, raw-menu adoption in `test_mac_menu_records.c`. Equivalent mutants: the initial highlight of
  the pop-up item (the tracker recomputes it from the pointer at once), re-checking `resource==1` in adoption, and keep_going on native steps. ASan clean (it found a wrong loop bound in my
  own test edit); `make test` 0; MI and MI2 identical; Castle refreshed.

- **Dialogs get a real port when the program draws in them (headless).** Glider's Load House dialog (DITL 1000) is 24 user items that the program fills by drawing straight into the dialog's
  port with QuickDraw; our Dialog Manager discarded all of it ('swallowed'). Now `SetPort(dialog)` creates the dialog's port window on first use (`mac_dlgwin_ensure`; when the colour window
  machinery is not up the old swallow stays: `mac_dlgwin_available`) and makes it the current port, so QuickDraw, PlotCIcon and the rest draw in dialog coordinates. The app sees the port WINDOW
  from GetPort, not the dialog pointer (stated difference). DisposeDialog restores a sensible current port. The waiting loops of ModalDialog/DialogSelect (and the filter step) used to repaint the
  whole dialog every step, which also wiped the program's own drawing: they now repaint only when the state the native drawing depends on changed (`mac_dlg_state_hash`: items' text, values,
  hidden/disabled, edit selection, default item, hidden flag, ParamText, pressed state). **Seen:** the Load House dialog now shows the program's own drawing (the down-arrow cicn); its 12 house slots
  are empty and the header PICT (item 3) is blank: the program found no houses (its list is built at launch from a folder scan; our application volume exposes no house files, and the file in the
  saves overlay is only published at close), and PICT items are still not drawn. Tests: SetPort(dialog) creates/reuses/removes the port window and current port, fallback without the
  colour machinery, repaint-only-on-change (a pixel drawn into the dialog survives waiting passes and is repainted after a ParamText change). Survivors are equivalent or not observable through the
  API: the explicit current-port assignment (NewCWindow already makes the new window current), most hash terms (every other state change also calls draw() itself), the `drawn` flag.
  The old swallow path is now only reachable without the colour machinery. ASan clean; `make test` 0; MI and MI2 identical; Castle refreshed.

- **Glider PRO finds its houses; written files are visible before close (headless).** The launch scan of Glider PRO's house list uses `PBGetCatInfo` with an index (HFSDispatch selector 9, `ioFDirIndex > 0`), reads the Finder type/creator ('gliH'/'ozm5') and builds an FSSpec with `FSMakeFSSpec` from the returned `ioFlParID`. We answered paramErr to the indexed form and dirNFErr to the FSSpec, so the list stayed empty. Now: indexed and by-name `PBGetCatInfo` go through the same file lookup as `PBHGetFInfo`, `ioFlParID` is 2 (the flat volume's root), a directory ID of 0 means the default directory, other IDs give dirNFErr (-120), a bad name buffer gives paramErr. `PBHGetVolParms` (selector $30; from memory) returns a 20-byte version-2 record with no capability bits (a short request gets a short answer, absurd or negative sizes and bad buffers give -50, vRefNum 0/-1/-2 accepted, others -35), and `SysBeep` ($A9C8) pops its word and plays nothing (no sound). New `mac_files_flush.c`: an open writable file's staging copy is now published (fsync, Finder info carried over, rename over the committed file, then a fresh staging copy so the open file keeps writing at its mark) before lookups, indexed listings, read-only opens and `FlushVol`, and only when it changed since the last publish. **Seen (headless):** New House → add room → star → Save House → Quit leaves a 1214-byte `Untitled House` (xattr 'gliHozm5'); after renaming it to 'Slumberland' and relaunching, the title screen shows the house name, the Game menu is enabled and New Game plays that house (frames 109–126 s change; the glider falls and the game returns to the title with no input). **Not working:** the Load House dialog grid is still empty and its header PICT blank (cause unknown, no halt), the editor's room background and object picture are not drawn, the prefs/high-scores alerts still appear (FSMakeFSSpec -35 on vRefNum -2, FSpDirCreate -120, DirCreate -123: no Preferences folder in the flat model), dialogs are not centred. Tests: `test_mac_files_flush` (publish on lookup/listing/FlushVol/read-open/close, no recommit when unchanged, finder info kept, two writers, a vanished staging file), the indexed catalog block and the volparms block in `test_mac_files`, `test_mac_sysbeep`. Mutation survivors are equivalent or unobservable here: fsync/directory fsync (durability, not visible in a test), the writable filter and the no-overlay guard (no other open file is writable/overlay-less), read-error vs EOF in the copy loop. ASan clean on the six touched tests; `make test` 0; MI and MI2 identical; Castle baseline refreshed (counts only differ).

- **Glider PRO's Load House dialog lists the house; PICT items draw; TextBox (headless).** The empty grid was not a missing house: the program draws its house names from the ModalDialog *filter procedure* when it is handed an update event, and our ModalDialog never produced one. Now, after the native repaint of a dialog that has a port window (the program called SetPort on it), the filter is offered one update event (what 6, message the dialog pointer) in place of the next null event, and not again until the native drawing repaints (`Dlg.update_due`). `BeginUpdate`/`EndUpdate` on a dialog pointer map to its port window (`mac_dlgwin_update`: brackets the drawing; the dialog's whole rectangle is the visible region, so the update region is not consulted); `DrawDialog` from inside a filter is allowed while ModalDialog waits (Close/DisposeDialog still halt) and does not owe a second update. New `mac_textbox.c`: `TextBox` ($A9CE; word wrap at spaces and carriage returns, a too-wide word split, left/centre/right (systemDefault = left), line height ascent+descent+leading, clipped to the box, pen untouched; spacing and justification are from memory). New `mac_dlgpict.c`: a DITL PICT item draws its PICT resource scaled to the item rectangle on the 8-bit screen (version 1 and indexed version 2 only; a missing or undecodable picture stays blank). **Seen (headless frame, 112 s):** Game > Load House? shows the "Select House..." header picture and the house name 'Slumberland' selected (inverted) in the first slot. **Still missing:** the 32x32 house icon above the name (the program draws it some other way: not traced), the orange frame around the grid is the program's own, Okay/Cancel/slot clicks and the Load itself are untested, the editor's room background/object pictures, prefs/high-scores alerts, dialog centring. Tests: update event once/never without a port window/never when hidden, BeginUpdate nested/unmatched/popped, DrawDialog inside the update, PICT item pixels and the blank cases, TextBox wrap/CR/justification/clip/bottom/hidden pen and seven refusals. Mutation survivors are equivalent: PICT clip against the left screen edge (dialogs never reach it), TextBox bottom (the clip rectangle hides the extra line), `span > 0` (the clip is empty anyway), the negative-length test (the RAM check catches the same input). ASan clean (dlgwindow, dlgmgr, text); `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **AddPt / SubPt; Load House click-through (headless).** Clicking a house in Glider PRO's Load House dialog halted on `SubPt` ($A87F, the mouse-down handler subtracts points); `AddPt` ($A87E) is its sibling. Both are in `mac_geometry.c`: `dst := dst +/- src` per coordinate modulo 2^16, source by value, destination by address, NIL or out-of-RAM destination halts without popping. **Seen (headless, 108-118 s):** click on the slot then Okay closes the dialog and returns to the title screen, which shows 'House: Slumberland'; no halt. This does not yet prove the house was (re)loaded: it was already the current one. Tests: five value cases (including wrap), four bad destinations, a short stack, neighbouring opcodes; six mutations all killed. ASan clean; `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **Load House loads the picked house; user items; System-volume spec calls and approximated folders (headless).** Clicking a house in Glider PRO's Load House did nothing: the slots are enabled *user items* (DITL type 0), and ModalDialog only returned buttons, check boxes, radio buttons and edit text. An enabled user item is now hit by the mouse-down itself (ModalDialog and DialogSelect). With two houses in the saves dir ('Slumberland' and a copy 'Other House', xattr gliHozm5), the click selects 'Other House' (inverted), Okay opens it, and the title reads 'House: Other House' with no error dialog. Getting there needed the high-score file path (after a load the program makes a folder 'G-PRO Scores' + 0xC4 on the System volume and a score file per house): `FSMakeFSSpec` accepts vRefNum -2 when the runtime has its authored System volume; a folder name may carry Mac Roman characters 128-254 (never a host name: any host-touching call still refuses such a name with -37, and `exists` answers -43); `FSpDirCreate` now answers: the new folder's ID is 3 + FNV-1a(name) mod 64, so it is the same every launch and needs no table; a name held by a file or the volume gives dupFNErr (-48), a nested folder dirNFErr (-120), a bad result pointer paramErr. **Approximation:** folders are not real directories. A file whose parent is a folder ID (3 to 66) lives in the one flat save namespace as "d<id>-name" (so the 31-character limit counts the prefix: such names are at most about 27 characters), the spec keeps the folder ID, and files with such names are never listed as root files; IDs outside 3..66 stay dirNFErr. A root file literally named like "d12-x" would alias a folder file (not guarded). Existing tests that used parent 3, 5 or 9 as an unknown directory now use 100; the old pin that FSpDirCreate halts is gone. Also fixed on the way: `Spec` is now zeroed before it is filled (the root spec's folder field was uninitialised). **Seen (headless frames, 120 s):** title 'House: Other House'; the saves dir gains 'd62-Other House' and 'd62-Slumberland' (score files). **Not done:** the house icon is still not drawn, 'Untitled House' still needs renaming by hand (Save House As / PBRename not traced), the folder ID's file listing and any program that enumerates a folder (PBGetCatInfo with a folder ID) are unsupported. Tests: new `test_mac_fsspec_folders` (System volume, high-character names, DirCreate rules, files in a folder apart from a root file of the same name, rename/delete, name limit, listing hides folder files), user-item press in `test_mac_dlgmgr`. Mutations: 22 of 25 killed; survivors are equivalent (dir 2 is the root anyway, `exists` on a high-character name is also -43 from the classifier, the second listing routine is not reached by the indexed catalog). ASan clean; `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **New Game on a loaded house (headless frames, evidence only; no source change).** After Load House > 'Other House' (a copy of the editor-made 'Slumberland'), Game > New Game (script `gp-lh3.script`, menu at 116-120 s) starts the game: the status strip reads 'Untitled Room', 2 gliders, 0 stars, and the New-Game message sheet shows the program's own text ('Enter New-Game Message here ...', 'There is 1 star in the house. Get every star to win.', 'Hit anything to begin') drawn through `TextBox`. At 140 s the play area is black with a thin vertical bar and a small flower-like sprite at the bottom left; the star the room should hold and the paper glider are not visible. **Not concluded:** whether the black room is right for a room made with no background in the editor (no reference), and why the star and glider sprites are missing: the gameplay window of the trace has only 7 DrawPicture, 36 CopyBits and 8 CopyMask calls and no halt, and nothing is swallowed, so the next step is to compare the CopyMask/CopyBits sources and masks against the program's pictures (PICT/cicn ids drawn). Headless only; not gameplay acceptance.

- **PBDirCreate, folder lookups and working directories on the approximated folders; the launch preferences alert is gone (headless).** Glider PRO's launch asked for its preferences folder with `PBDirCreate`, which still answered wrgVolTypErr (-123), and showed 'A File Error Loading/Saving Preferences'. Now (new `mac_files_dircreate.c`): `PBDirCreate` (HFS selector 6; checks -46 without a save overlay, -35 unknown volume, -120 parent other than the root, -37/-50 names and pointers, -48 a file holds the name) gives the folder the same hash ID as `FSpDirCreate` and creates a zero-length marker file `d<id>-.f` in the save overlay, so the folder persists and is found again; `PBGetCatInfo` by name of a made folder returns a directory record (attribute 0x10, dirID, parent 2; a file of the same name wins; an unmade name is -43); `PBOpenWD`/`PBGetWDInfo`/`PBCloseWD` of a made folder use the reference -(100 + id), which `FSMakeFSSpec` takes as vRefNum (the name is then in that folder) and the PB create/open/delete/rename/get-info calls take as ioVRefNum (the name becomes `d<id>-name`). vRefNum -2 (the authored System volume) is accepted by those calls too; the System volume's own handler leaves non-'System' names and the WD calls of made folders to the files service. **Seen (headless frames):** with a fresh saves dir the launch no longer shows the preferences alert (the program finds no 'Glider Prefs' there, which is normal for a first run); the Load House run still loads 'Other House' with 'House: Other House' on the title; the saves dir gets the marker `d32-.f` and the score files. The 'Okay, with your monitor & color depth ... the music won't be loaded' alert still appears at launch (memory model; not looked at). **Limits, stated:** WD references and folder IDs are stateless ranges (3..66, -103..-166), so an unmade folder's reference is not an error for `FSMakeFSSpec`; listings and `PBGetCatInfo` by index inside a folder are not supported; folders do not nest; marker/file names starting 'd<digits>-' are reserved. `test_mac_files` now pins DirCreate without an overlay as -46 (was -123). Tests (`test_mac_fsspec_folders`): DirCreate rules and bad pointers, folder info and 'a file made later wins', WD open/info/close and refusals, files created/opened/deleted through the WD with the host name checked, FSMakeFSSpec through a WD. Mutations: 18 of 26 killed; the 8 survivors are three mutations whose text did not match (not run), the DirCreate marker-create error pass-through (needs a full quota to see), WD range +3 and CloseWD's output (equivalent in results), and the two System-volume hand-off conditions (the test rig has no real System volume service; the Glider run covers them). ASan clean on the six file tests; `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **Security review fix (heap over-read in the System volume handler).** An automated review of 7711aeb found that the working-directory hand-off in `mac_system_volume_service` read `ioDirID` at `pb+48` through the raw host pointer after validating only 30 bytes of the guest parameter block, so a block placed in the last bytes of guest RAM read past the end of host memory. It now uses the bounds-checked `mac_cpu_read(c, p+48, 4)`. Regression test: a 30-byte block at the end of RAM for HFS selectors 1 to 9 on vRefNum -2 in `test_mac_system_volume`; under ASan the old line is a heap-buffer-overflow, the fixed one is clean. `make test` 0.

- **Memory partition from the SIZE resource: Glider PRO launches without the 'not enough memory' alert; correction of the sprite claim (headless).** **Correction first:** the earlier entry said the star and the paper glider were not visible in gameplay. That was one frame taken at an unlucky moment. Frames taken every second from 124 to 146 s show the paper glider flying and falling, the star drawn in the room, and the glider count going 2, 1, 0: the sprites are drawn. (Whether a black room is right for a room made with no background is still unchecked.) **The alert:** Glider adds up what it needs (2,643,856 bytes for a 640x480 screen at 8 bits) and compares it with `FreeMem`; our guest heap is a fixed 3 MB, 1,218,008 bytes of it used at launch, so `FreeMem` was 1.9 MB, the check failed and the program refused to load its music. Its SIZE resource (-1) asks for a 4,096,000-byte partition (minimum 3,072,000). New `mac_partition.c`: the heap end is `lo + preferred size` from the SIZE resource (10 bytes: flags, preferred, minimum), clamped to leave the stack 192 KB below the globals (`stack_base - 0x30000`), 8-byte aligned, never below the old 0x700000. A program whose preferred size fits the old 3 MB (Monkey Island 1.2 MB, Monkey Island 2 2 MB, Castle 1.2 MB) gets exactly the old heap, which is why parity is unchanged. The stack base is now computed before the heap is attached. **Seen (headless frames, clean launch, script gp-clean.script):** title at 8 s with no alert; Game > Load House lists 'Other House' and 'Slumberland'; click and Okay give 'House: Other House'; Game > New Game plays: message sheet, then the paper glider flies and the star is drawn. Nothing here shows the music loaded or played (no audio claim). Tests: `test_mac_partition` (preferred size above, at and below the default, the ceiling clamp, a ceiling below the default, alignment, nonsense and over-64 MB sizes, a short/long/absent SIZE, NULL fork). Mutations: 9 of 9 killed after adding the over-long SIZE case. UBSan found a signed shift overflow in my first version (a byte promoted to int); fixed. ASan clean on the new test (not rerun on Castle this iteration); `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **Glider PRO's editor flow from a clean launch: deferred control redraws, dialog port bits for CopyBits, LoadScrap/UnloadScrap (headless).** With the launch alerts gone the editor flow (script `gp-nh-c2.script`: one alert click, House > New House, name dialog 'Untitled House' > Save, Create the room, Tools > Prizes > star, place it, House > Save House) hit two new halts, then a third at Quit. (1) 'Nested guest calls from a native service are unsupported': the Prizes pop-up dialog's ModalDialog *filter* (a guest call) answers its update event with `DrawDialog` and `SetCtlValue`, and redrawing a CDEF control needs another guest call. A control redraw requested while a guest call is running, for a control of a dialog's port window, is now deferred: the dialog is marked as owing its controls a redraw and `mac_dlgwin_flush` draws them once the call has returned (`mac_dlgwin_owe_redraw`; in a window that is not a dialog's the nested call still halts). (2) 'CopyBits mixed pixel depths': the program copies a picture into `&theDialog->portBits`; the DialogPtr here is a native record with empty bits, so CopyBits (and CopyMask) now map `dialog + 2` to the dialog's port window's bits (`mac_dlgmgr_redirect_bits`). (3) `LoadScrap` ($A9FB) and `UnloadScrap` ($A9FA), called at Quit: LONGINT noErr, no arguments (signature from memory; the scrap is host memory so there is nothing to load). **Seen (headless frames):** the room is created, the Prizes palette and star tool work, Save House shows 'Checking House File: 1 'Untitled' Rooms', and Game > Load House lists 'Untitled House'; Game > Quit writes 'Untitled House' (1214 bytes, type gliH/ozm5) and the preferences file 'Glider Prefs' (226 bytes) into the approximated folder (`d32-Glider Prefs`), then ends cleanly. The house is named at New House time (a name dialog), so no rename is needed; the earlier rename-by-hand was an artefact of the launch-alert path. The editor still shows 'Nothing to show' for the room. Tests: deferral and its draw after the call (`test_mac_dlgwindow`), the bits redirect (dialog, reuse, other addresses, no colour windows, no Dialog Manager), Load/UnloadScrap (result, no pops, scrap kept, bad stacks). Mutations 8 of 12 killed; the survivors are equivalent or unreachable (the `def` test sits after the native-control branch so it is always true, the `bits < 2` test and the availability test change nothing observable, the Dialog Manager NULL test cannot be reached with a guest call active, a second dialog would be needed to see the window match). ASan clean on the four touched tests; `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.

- **Keyboard play, and edit-text selections (headless).** In the clean New Game flow (script `gp-key2.script`: Return starts the game, then arrow keys 0x7C / 0x7B held for 1.5 to 2 s) the keyboard reaches the game: Return dismisses the New-Game sheet, the paper glider flies and falls, the star is drawn, the glider count drops from 2 to 1, and the game ends with the program's own dialog 'Your score of 5000 is #1 on the top ten high scores for Other House. Enter your name'; then 'Getting #1 on the high scores entitles you to change the high score banner'. (Headless frames only; I did not check whether the 5000 points came from the star or from the arrow keys, nor that the glider steered.) The name field showed 'Your NameIan': the program selects the whole default text (`SelIText(dlg, item, 0, 32767)`) so typing replaces it, but `SelIText` here ignored its range and left a caret at the end. Now `SelIText` stores the range (characters, ordered, clamped, 32767 = to the end), the selection is drawn inverted, and typing replaces it, Backspace/Delete removes it, and other keys leave it alone; with no selection the caret is at the end as before (new `mac_dlgedit.c`, which also keeps `mac_dlgmgr.c` under 500 lines). **Seen:** the default 'Your Name' highlighted, then 'Ian' typed in its place; Okay moves on to the banner dialog and the score file is written (`d62-Other House`). **Still wrong, from the same frames:** these dialogs sit at the top-left corner (DLOG rectangle, not centred) over a plain blue backdrop. Tests: `test_mac_dlgedit` (13 cases: full, partial, reversed, negative and past-the-end selections, Backspace/Delete on a selection, none, empty text, control and non-ASCII keys, a full field with and without a selection) and a `SelIText` range table in `test_mac_dlgmgr`. Mutations: 14 of 15 killed; the survivor (`erasing` reset after a selection delete) is equivalent. ASan clean; `make test` 0; MI and MI2 identical; Castle same modulo counts, baseline refreshed.
