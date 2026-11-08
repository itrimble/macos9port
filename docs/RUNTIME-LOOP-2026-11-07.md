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
- **Parity:** Monkey Island and Monkey Island 2 are byte-identical to the previous baseline; `make test` exits 0; ASan on Castle (90 s with
  the menu script) and on the new and touched menu, window and utility tests reports nothing.

## Next reproducible steps

1. Glider 4.10: identify the Device Manager call on refnum -4 (csCode 2) from a public source, then
   decide whether it is a small answer or a driver subsystem. Palette activation is done; a windowed run
   and screenshot are still owed once it reaches its loop.
2. Glider PRO: a hierarchical file system for `PBDirCreate` (HFSDispatch selector 6) and its siblings.
3. The real Dialog Manager (`GetNewDialog` `A97C`, `ModalDialog`, `DrawDialog`, dialog item get/set, `DisposeDialog`): Room Editor\'s next halt, built on the DITL parser and `mac_dialog_draw`. Then Pack3 selectors 5 to 8 (`StandardGetFile`) and the `stdf` Gestalt answer for Glider Edit, and Room Editor\'s custom WDEF mechanism (now possible through `mac_guest_call`). Castle of Dr. Brain now reaches its title screen headlessly: next is a windowed or scripted run to see whether it takes input, plays sound, and starts the game.
4. Exercise compaction with a real fragmenting program, or a purpose-built guest.
