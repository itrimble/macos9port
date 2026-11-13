# QuickTime Installer in ospi-runtime: traces

The dated record of running Apple's QuickTime 4.0.3 Installer (68K) as guest code in `ospi-runtime`. QuickTime is the required demonstration software
(Development contract, 4 November 2026): it is separately supplied guest software, never implementation source, never bundled, never committed. Nothing
here is copied from Apple text or code; the observations are of what the program does when run. Everything is headless; no window was shown and no frame
was looked at.

## First run — 12 November 2026

Branch `qt/filemanager` (hierarchical File Manager) on top of `ppc/printing`.

**The archive.** `QT_403_68k_Mac.sit` from the public Macintosh Garden page `macintoshgarden.org/apps/quicktime-4` (the same file the Install Classic Apps
catalogue entry `quicktime-403` names), downloaded with `curl` into a scratch directory outside Git. 8,688,785 bytes, MD5
`0ad325554dfcf2950f08a7093690d417`, which matches the catalogue. Unpacked with `unar -k visible` (resource forks as `.rsrc` sidecars) into
`QT_403_68k_Mac/`: `QuickTime Installer` (data fork 34,338 bytes, resource fork 353,714), `QuickTime Install Cache` (data fork 8,344,292 bytes,
resource fork 10,393). I did not run the install pipeline's UI, so the unpack was by `unar` directly, not by the project's own installer code.

**Classification.** `build/os999-classify --data "QuickTime Installer" --rsrc "QuickTime Installer.rsrc"`: format `68k-code`, profile `B-clean-68k`,
confidence medium, runnable, application, 21 CODE resources with a CODE 0, `SIZE` with is32BitCompatible set (preferred 2,252,800, minimum 1,638,400).
The fork holds 440 resources (CODE, DATA, INIT, INLC, LDEF, CURS, ICN#, `Dcmp` 1004 and 1005, vers, SIG#, and more).

**Run.** `ospi-runtime --indexed --trace --steps 20000000 --saves <scratch> "QuickTime Installer.rsrc"`, the application folder as the read-only data
volume.

| Step | What the trace shows |
| --- | --- |
| Start | CODE 0 has one jump-table entry, to CODE 24 (22,764 bytes) at offset 0x18. The program starts there. Segment 24 begins with the bytes `A89F 'VISE'` after a 16-byte header |
| Stub | It finds its own handle (`RecoverHandle`), reads the table of its other resources, and for each of 244 of them (`INLC`, `CURS`, `LDEF`, `Dcmp` and others) loads the resource, looks at its first long for `A89F`, and, where it finds it, allocates, `BlockMove`s and `SetHandleSize`s: a decompression in place through the program's own `Dcmp` resources. It also compares the trap addresses of `$A89F` and `$A0BD` (to decide whether to call `$A0BD`, which this runtime accepts as a no-op), patches OS trap `$A0FC` and Toolbox trap `$A9A5` with its own code, and puts a hook in low memory `$33C` |
| End of the stub | It writes the 8 bytes `0000 3F3C 0001 A9F0` back into its own jump-table entry (offset 0, **segment 1**) and jumps into the entry, so `LoadSeg(1)` runs the next stage |

**Blocker 1, fixed (`src/mac_runtime.c`, test in `tests/test_mac_unloadseg.c`).** `LoadSeg` matched jump-table entries against CODE 0's ids (24) and the
fork's offset and size, so an entry the program had re-pointed at segment 1 was never rewritten and the same `LoadSeg` ran 64 times before the existing
"self-loading stub" halt. A real Segment Loader works from the entry as it stands: an unloaded entry (offset, `MOVE.W #id,-(SP)`, `_LoadSeg`) names its own
segment and offset. `mac_runtime_load_segment` now does that for unloaded entries, and takes the segment size from the live handle (the stub had grown it). Entries still in
CODE 0's form behave as before. The run after the change got past it: CODE 1 (1,139 bytes, at the moved handle) ran.

**Blocker 2, the stop.** CODE 1 is the data-initialisation stage: it loads `CODE 0` and `DATA 0` (6,343 bytes), clears the globals and runs the usual
unpack loop of the packed initial-data format. Its first opcode read finds a byte stream it does not accept and the code calls `SysError(15)`
(`$A9C9`), at 2,236,598 instructions. The runtime reports it as `The extension raised a system error (SysError).` (that wording belongs to the INIT host
and is misleading here; the call comes from the application). What I could see: the bytes the unpacker reads begin `A89F 000C`. That is the
signature of a **compressed resource**, so `GetResource('DATA', 0)` handed it the stored form where on a real machine something between the Resource
Manager and the program hands back the decompressed bytes. That something is, by the stub's patches (Toolbox trap `$A9A5`, OS trap `$A0FC` and the `$33C` hook), the
program's own decompression layer plus the Resource Manager's support for compressed resources. I did not take the stub's patched routines apart, so
which of the three the program relies on is not established.

**The missing subsystem, named:** the Resource Manager's **compressed-resource support**: resources stored with an `A89F` header, decompressed on load through
decompressor resources (`Dcmp`/`dcmp`) in the program's own fork, and the way this installer engine hooks that (patched traps and a low-memory hook that
`GetResource`, `SizeRsrc` and friends must honour). Beyond it the Installer will need, in this order and as far as I can tell without running it: its
script engine reading `QuickTime Install Cache` (8.3 MB, an archive in the installer engine's own format), the Component Manager and shared libraries of the
QuickTime extension it installs, and the Folder Manager and Gestalt answers an installer asks of a System Folder (this volume has none of the folders it
will want to write to). None of that was reached.

**Not verified.** No window, no dialog, no frame; the Installer was not seen to draw anything. The instruction counts are headless budgets, not timing. The
reading of the stub as a decompressor is from its trap pattern and the `A89F` comparisons in its code, not from a decompiled listing. The claim that
`DATA 0` itself starts with `A89F` rests on the bytes seen in the unpacker's register at the stop, not on parsing the resource from the fork by hand.
The archive's authenticity is only that its MD5 matches the one the catalogue copied from the Garden page.

## Second run — compressed resources, the load hook, the first dialog — 12 November 2026

Branch `runtime/compressed-resources`, on `main` plus the merged `handoff/qt-filemanager`. Same application, same budget style (`ospi-runtime --indexed
--steps N`), headless. The Garden archive could not be fetched again from here (the direct download answered 403, and I did not try to get around that), so
this run used the already unpacked copy on the MBP's volume (`QuickTime Installer` data fork 34,338 bytes, MD5 `9b0295cfaf665f9c24efd2f0d8275174`; its
`.rsrc` sidecar is an AppleDouble file of 353,773 bytes). The earlier run reached the same stop at the same instruction count (2,236,598), which is the only
evidence that it is the same program; I did not compare the forks byte for byte.

**What I got wrong last time.** The `A89F 000C` resources are not the Resource Manager's compressed-resource format. That format starts `A89F 6572` and
names a `dcmp` resource. These start `A89F 000C` and belong to the installer engine's own packer (the fork's `Dcmp` 1004 and 1005 are its decompressor's data, spelled
with a capital D, and the stub's own marker `A89F 'VISE'` sits at the head of CODE 24). The engine unpacks them itself, in a patch of OS trap `$A0FC`. Reading the
stub's code (disassembled with the repo's own diagnostic, as observation only), the patch does this: call the previous routine, and if ResErr is clear, take
the handle at offset 8 of the structure A2 points to, and if its contents start `A89F 000C`, unpack them in place with the stub's own routine, or release the handle and
set `memFullErr` if its checksum fails. A2 there is the resource's 12-byte map entry, so `$A0FC` is a hook the real Resource Manager calls after reading a resource's bytes. The stub's
`SizeRsrc` patch (`$A9A5`) answers from a table of unpacked sizes, but only for a handle whose master pointer is NIL (unloaded), which our handles never are.

**What the runtime does now** (`src/mac_rescomp.c`, `src/mac_rescomp_fork.c`, `include/mac_codefmt.h`).

* When the program installs a patch for `$A0FC`, every `A89F 000C` resource of its fork is run through that patch once, as a guest call with A2 set
  as above, ResErr cleared and every register saved around it. The results are gathered into a rebuilt copy of the fork that replaces `rt->fork`, so native readers
  (dialog items, menus, cursors, pictures) see the unpacked bytes as well as `GetResource`. That is a deliberate difference from a real machine, where the hook runs per load; the reason is that only GetResource,
  LoadResource and LoadSeg can wait for a guest call here.
* A chained call from guest code to a native stub while a guest call was running (the patch does `JSR` to the previous `$A0FC`) used to return through the wrong
  address and end the call early. That was a latent bug in `mac_runtime_trap`; it is fixed and the new test covers it.
* The previous `$A0FC` routine exists as a native no-op. `$6F4` (the cache-flush vector MPW start-up code calls above a 68000) is set to the unimplemented-trap
  address, which that code reads as "nothing to flush"; it was zero and sent TeachText's start-up code to address 0.
* A startup segment stored compressed keeps its jump-table entry as an unloaded stub, so the first call loads and expands it.

**Result.** The Installer no longer stops at `SysError(15)`. The next stop is `Process Manager` (`OSDispatch` `$A88F`, selectors `$37` GetCurrentProcess and `$3A`
GetProcessInformation), which the stub's own initialisation calls; a one-process Process Manager (`src/mac_process.c`, selectors `$37 $38 $39 $3A $3B $3C $3D`;
any other selector halts and names itself) answers it. After that the engine builds a dialog, runs `ModalDialog` in a loop (58 million traps in a 60 million
instruction budget, i.e. a busy wait for a click) and, in a headless frame at 8 seconds of guest time, the dialog reads *An error occurred while reading the installe[r]* with the code
`1009:5,-50` and an OK button. I looked at that frame (640 by 480, from `ospi-runtime-probe --frames`, not committed). So the engine runs, draws, and then fails by itself.

| | Before | After |
| --- | --- | --- |
| Stop | `SysError(15)` at 2,236,598 instructions, 4,011 traps | dialog wait in `ModalDialog` after about 3.0 million instructions |
| Why | `GetResource('DATA',0)` returned `A89F 000C` bytes | none of the engine's resources are packed any more when it reads them |

**The next blocker.** `-50` is `paramErr`. The last File Manager call before the dialog is a `PBHOpen` with `ioNamePtr` empty, `ioVRefNum` -1 and `ioDirID` 0 (open
traces with `--trace` now print the call: `open vref=-1 dirid=0 perm=1 name=''`). Before it, the engine opened its own data fork for writing (`permErr` -54 on the read-only volume, normal), opened and
read 8 bytes of `QuickTime Install Cache`, and closed it. I do not know where the empty name comes from. The WordPerfect installer, which uses the same engine, reaches the same call right after `PBHGetVInfo`
by volume index returned `nsvErr` for index 3, which suggests it is searching volumes for something (this runtime shows two) and gives up with an empty name; for QuickTime I have not shown that. Treat both as hypotheses.

**Not verified.** Nothing beyond that dialog: no click was sent, the Install Cache was not unpacked, no file was written. The instruction counts are headless budgets. The
unpacking is the engine's own code; I did not check its output against a reference. `SizeRsrc` answers (the stub's `$A9A5` patch) were not exercised.

## Third run — what the -50 was, and the next stop — branch `qt/installer-volumes`

Same application and the same MBP copy (`QuickTime Installer` data fork 34,338 bytes, `.rsrc` 353,773 bytes), run from a scratch copy, headless, `ospi-runtime-probe --trace`.
The method this time was to find the instruction that issues the failing call (a temporary program-counter watch in the CPU wrapper, not committed, plus the repo's
`ospi-dasm` over a RAM dump) and read the engine's own routines around it, as observation only.

**What the -50 was.** The `PBHOpen` with an empty name, `ioVRefNum` -1 and `ioDirID` 0 is the engine opening *its own file*. The routine around it builds a file
specification record by hand (volume reference word, parent directory long, Pascal name at offset 6) from two globals and then opens its data fork through that record,
without checking the build. The globals are filled in the engine's start-up: the name is copied from low-memory `$910`, the application-name global (a Str31 the
system holds for the running program), and the volume reference comes from a `GetVol`-style call (-1 here, the OS999 HD, which is right). `$910` was zero in this
runtime, so the record named nothing, and an open of an empty name in directory 0 is a parameter error. The name is now published when the runtime learns the
application's file name (`mac_files_set_app`, `src/mac_fcbinfo.c`), and the probe, which never told the File Manager the name, now does (`mac_files_set_app_path`, shared with `ospi-runtime`).
The open now reads `open vref=-1 dirid=0 perm=1 name='QuickTime Installer'`, resolves against the default directory (the application's folder, directory ID 2) and succeeds.
I read the empty-name / directory-0 case as `paramErr` from the public description of the call, not from a reference machine.

**What it was not.** The volume-index probing is correct as it stood. `PBHGetVInfo` by `ioVolIndex` answers index 1 (OS999 HD, reference -1) and index 2 (the authored System volume,
reference -2, `mac_system_volume.c`) and `nsvErr` for index 3, which is how a program learns the volume list has ended; the engine walks it to the end and then moves on. Reference -1 is
what `GetVol` returns for the default volume. No File Manager volume-semantics change was needed, and none was made.

**Past the -50, in the order the engine hits things** (each is a fix with a headless test):

| Stop | What was missing | Change |
| --- | --- | --- |
| Script Manager selector `8404000C` | `GetScript(script, verb)` | `smScriptLang` (verb 28) of the Roman script answers langEnglish (0); any other verb halts naming itself |
| Script Manager selector `84020008` | `GetEnvirons(verb)` | `smRegionCode` (verb 40) answers verUS (0); any other verb halts naming itself |
| `SetWTitle` on the installer's dialog | the window service only knows registered windows | a dialog keeps the title in its WindowRecord (`titleHandle`, `titleWidth`); `src/mac_dlgtitle.c`. Dialogs have no title bar here, so nothing is drawn |
| `NewControl` on a dialog | "dialogs are not supported" | controls of an open dialog are created in its port window (the same hidden window its DITL controls already use) and found through the DialogPtr by `NewControl`, `GetNewControl`, `DrawControls`, `UpdateControls`, `FindControl` |
| trap `$A96D` | `Draw1Control` | draws one control if it is visible in a visible window |

The Script Manager selector and verb numbers were checked in the fifth run below against Apple's public Script interface (`GetScript` selector `$8404000C`, `GetEnvirons` `$84020008`, `smRegionCode` 40, `smDoubleByte` 36); verb 28 of `GetScript` is `smScriptLang`, not an encoding, and the answer (0) is unchanged.

**The stop: the List Manager.** After the engine has loaded its main dialog (`DLOG` 1000), added a control and got and set a user item, it calls `Pack0`
(`$A9E7`) with selector `$0044`, which is `LNew`, with a vertical scroll bar and no horizontal one: it is creating a list (the component list of the custom install,
by the resources around it). The runtime has no List Manager at all (no `ListRec`, no cell storage, no `LDEF` calls, no `LClick`/`LDraw`/`LAddRow`/`LSetCell` family), and the
engine ships its own `LDEF` resources, so a real one has to call guest list definitions. That is a subsystem of its own, not an iteration, and I stopped there. The halt is
`unhandled A-line trap a9e7 at 001177da` after 3,855,480 instructions (1,133 ms of guest time).

| | Before this run | After |
| --- | --- | --- |
| Stop | `1009:5,-50` dialog, then `ModalDialog` wait at about 3.0 million instructions | halt on `Pack0` `LNew` at 3,855,480 instructions |
| The `-50` | `PBHOpen` of an empty name | gone; the engine opens `QuickTime Installer` (perm 1), then `QuickTime Install Cache` (perm 3 refused with `permErr` -54 on the read-only volume, then perm 1) and reads it 54 times |

**Not verified.** No installer window was drawn: a frame at 1 s of guest time shows only the menu bar rule. `LNew` is the stop, so "Easy Install" / "Custom Install" were never seen
and nothing was clicked. The meaning of the engine's globals comes from its code and from the public names of the low-memory variables; I did not decompile it. One `Read` returns `rfNumErr` (-51)
just before the volume walk, which I did not explain. The title of a dialog is stored but not shown.

## Fourth run — the List Manager, and the Welcome window — branch `qt/installer-volumes`

The stop of the third run was `Pack0` `LNew`. The runtime now has a List Manager (`src/mac_list.c`, `src/mac_list_ui.c`, `src/mac_list_int.h`), written from the public interface in this project's own
words: a ListHandle is a guest handle to an 88-byte ListRec in the public layout; the selectors (LNew, LDispose, LAddRow/LAddColumn/LDelRow/LDelColumn, LSetCell/LGetCell/LAddToCell/LClrCell,
LSetSelect/LGetSelect, LDraw/LUpdate/LDoDraw, LClick, LScroll/LAutoScroll, LActivate, LSize, LCellSize, LSearch, LNextCell, LRect, LLastClick, LFind) are real; any other selector halts naming itself.
The standard definition draws natively (this project's own look); a custom `LDEF` runs as guest 68K code through `mac_guest_call` (messages init, draw, hilite, close), with `lDataOffset` and
`lDataLen` indexing a guest `cells` handle that is rebuilt after every change. Scroll bars are standard Control Manager controls outside the view rectangle. `tests/test_mac_list.c` drives all of this
headless, including a hand-assembled LDEF, and checks the PowerPC bridge rows for every `L*` name against the 68K argument sizes.

Stated differences and limits: the cell data is a host table (the ListRec's `cellArray` offset table does not exist, so a program that reads it directly is unsupported); the default cell height is 12 pixels when the
program passes 0; scrolling redraws the visible cells instead of copying pixels; `LClick` handles one press (no drag selection, no scroll-bar thumb tracking, which halts by name; arrows and page areas step once);
`LSearch` with the program's own search procedure halts, and the default match is exact bytes; `LActivate` toggles the scroll bars but does not change how selected cells look. The selector numbers and argument orders were verified in the fifth run below.

Three more things the engine asked for after `LNew`, each with a test: `MoveControl` on a dialog's button, check box or radio button item (`src/mac_dlgtitle.c`, the item's rectangle moves like `SetDItem`);
`SizeWindow` on a dialog (backdrop replaced, regions reset, the port window follows); and the earlier `NewControl`-on-a-dialog route now carries the list's scroll bar.

**Result (headless, `ospi-runtime-probe`, QuickTime Installer from the MBP copy).** The engine now gets through its start-up, builds its main dialog with its custom list definition (`LDEF` 1040, init message run as guest code),
and waits in `ModalDialog` (about 3.89 million instructions). The frame at 6 s of guest time (640 by 480, from `--frames`, not committed) shows the **Welcome to QuickTime 4** window: the engine's own styled
heading and body text, a lavender panel, a bordered frame with a **Continue** button, a second (empty, rounded) button and a line of text in the small bitmap font, "This installation was created with Installer VISE from MindVision Software".
It is not clean: a white rectangle covers part of the first line of the body text, and the empty rounded button has no label. A scripted click on Continue (`2.0 click 510 385`) loads the next dialog (`DLOG` 1021) and the run halts
at **trap `$A83E` (`TEStyleNew`)**: styled TextEdit, the next missing subsystem (no style table, no `TEStyleInsert`/`TESetStyle`, no per-run fonts in the TextEdit service). "Easy Install" / "Custom Install" has not been
reached, and was not seen.

| | Before | After |
| --- | --- | --- |
| Stop | halt on `Pack0` `LNew` at 3,855,480 instructions | waits in `ModalDialog` on the Welcome window; after Continue, halt on `TEStyleNew` at about 3.89 million |
| Drawn | menu bar rule only | the Welcome dialog (see above) |

**Not verified.** The visual defects above are as seen in one headless frame, not explained. The Continue click is a scripted press through the probe, not a real mouse. Nothing past the Welcome screen has run.

## Fifth run — verified selectors, font substitution, multistyled TextEdit, the licence window — branch `qt/installer-volumes`

**List Manager numbers, verified.** All 26 selectors, their argument orders and result types match Apple's archived *More Macintosh Toolbox* List Manager reference
(`developer.apple.com/library/archive/documentation/mac/MoreToolbox/MoreToolbox-268.html`, the routine summary), cross-checked on the pages for `LActivate`, `LSetSelect`, `LNew`, `LAddRow` and
`LDelRow`. `LFind` is that document's `LGetCellDataLocation` and `LDoDraw` its `LSetDrawingMode` (same selectors). Two behaviours I had wrong or had left open are now as documented: a count of 0 in
`LDelRow`/`LDelColumn` deletes them all, and an invalid start does nothing (it halted before). Insertion at `rowNum` with the first new row as the result, a position past the end appending, and
scroll bars outside the view rectangle all agree with the same pages. Still from memory and not in what I read: `LSearch`'s default match (exact bytes here), `LNextCell` wrapping, the double-click
timing test, and the 12-pixel default cell height. The Script Manager verbs were checked against Apple's public `Script.h`: `GetEnvirons` verbs 4 (`smEnabled`), 6, 18, 20, 22, 36 (`smDoubleByte`) and 40
(`smRegionCode`) are now answered for a Roman-only machine, and `GetScript` verb 28 is `smScriptLang`.

**Font substitution (`src/mac_font.c`).** The classic Font Manager does not refuse a face that is not installed, so `mac_font_find` no longer does: a missing size takes the nearest size of the
family the program carries (larger first at equal distance, searched among sizes 1 to 127 because a FONT id is `family * 128 + size`), a family with no strike at all takes OS999's stand-in system face
(ChicagoFLF through `mac_sysfont`, at the requested size clamped to 8 to 36). A strike the program carries but that is malformed is still an error, and nothing is scaled: a nearer size is drawn at its own
height. `RealFont` answers from the new `mac_font_find_exact`, so it says no for anything that is only a substitute. `FMSwapFont` has no implementation in this runtime (it halts as an unhandled trap), so
there is nothing there to keep consistent. WordPerfect's `Font family 3 size 9` stop is gone.

**Multistyled TextEdit (`src/mac_testyle.c`, `src/mac_te_int.h`, `src/mac_textedit.c`).** `TEStyleNew` ($A83E) builds a TERec with `txSize`, `lineHeight` and `fontAscent` -1 and the style handle in
`txFont`/`txFace`; the style record, style table, style runs, line-height table and null style are real guest handles in Apple's public layouts (the numbers are from Apple's public `TextEdit.h` and the
TextEdit chapter of *Inside Macintosh: Text*). TEDispatch selectors implemented on multistyled records: 1 `TESetStyle` (with `doToggle` and `addSize`), 2 `TEReplaceStyle`, 3 `TEGetStyle`,
4 `TEGetStyleHandle`, 6 `TEGetStyleScrapHandle`, 7 `TEStyleInsert`, 10 `TEContinuousStyle`, 11 `TEUseStyleScrap`, 13 `TENumStyles`; 0, 5, 12 and 14 halt by name. Layout, line breaking, hit testing,
`TEGetPoint`, `TEGetHeight`, the caret and highlight use per-line heights from the table; drawing makes one call per run in that run's face and in the nearest palette colour of its RGB. Typing takes the
style before the caret or the null scrap's. Limits: 2000 runs, 512 styles; a face beyond bold and underline draws plain; `TEStylePaste` and `TESetStyleHandle` are not implemented. `tests/test_mac_testyle.c`
covers all of it headless with a hand-built two-size font. Also needed: `SetCTitle`/`GetCTitle` on a dialog's button items, and an update event for a dialog that has repainted itself (so a program that
draws in an event loop, not inside `ModalDialog`, repaints), with `DialogSelect` now repainting only when the dialog's state changed and giving the program's user items their turn on an update.

**Result (headless, probe, MBP copies).** QuickTime: Continue on the Welcome window, then the **Software License** window: "ENGLISH" language row, a heading in bold, the licence text in styled runs (bold
"1. License." run inside plain text), a scroll bar and the four buttons Print?, Save As?, Disagree, Agree, all with this runtime's stand-in face in place of the Geneva-family fonts the installer asks for.
It waits there. A scripted click on Agree then halts at **trap `$A9ED`, selector 6, parameter 0: `IUGetIntl(0)`**, the International Utilities package (Pack6) reading the `itl0` number and date format
resource; the runtime has no International Utilities and no System file to read it from. That is the next subsystem, and the nearest thing to the Easy/Custom Install window that was reached: it is not
drawn. WordPerfect: past its font stop, past the splash (its own picture, drawn from a PICT with a long comment the decoder now skips) to its **Your License** window, where the licence text area wraps
one letter per line (cause not found) and the splash picture is not erased from behind the new window; it waits there.

**The Welcome window defects.** Looked at, not fixed. DLOG 1000's DITL has no items at all; the engine builds the window itself (a picture, controls it creates with `NewControl`). So the white rectangle over
the first body line and the unlabelled rounded button are engine-created items; the trace shows no `SetCTitle` and no `SetIText` for them during the Welcome stage. I did not establish what they are.

**Not verified.** Everything above is headless frames and traces from the probe, not a real window. The Agree click is a scripted press. No real stock font was available, so the stand-in face's look
here is not what a real Mac shows. The `IUGetIntl` selector and parameter are read from the trap's stack at the halt; what the engine does with `itl0` is not known.

## Sixth run — International Utilities, the install-type window, and the disk alert — branch `qt/installer-volumes`

**International Utilities (`src/mac_intl.c`, Pack6 `$A9ED`).** The stop of the fifth run was `IUGetIntl(0)`. Selector numbers were checked against Apple's public `TextUtils.h` and `Script.h`:
6 `IUGetIntl` (`GetIntlResource`), 0 `IUDateString`, 2 `IUTimeString`, `$E` `IUDatePString`, `$10` `IUTimePString`; those five are implemented, and every other selector (the string comparisons `$A`, `$C`,
`$1A`, `$1C`, `IUMetric`, `IUSetIntl`, the long-date and script-order calls) halts naming itself. `IUGetIntl` answers the program's own `itl0`/`itl1` (id 0) when its resource fork carries them; otherwise it hands out
a US English pair that this runtime builds when first asked, from the documented field layout (`IntlResources.h`: the 32-byte `Intl0Rec`, the 332-byte `Intl1Rec`), with OS999's own values (period, comma and semicolon
separators, `$`, month-day-year with the century, a 12-hour clock with " AM"/" PM", English day and month names, abbreviation length 3). Nothing is copied from an Apple System resource. Dates (seconds since 1 January
1904, as the program holds them) are formatted from whichever `itl0` is in use for short dates and times; long and abbreviated dates come from the built-in `itl1` only, and halt for a program's own (the long-date separator
rules are not implemented). `itl2` and the other ids halt by name. `tests/test_mac_intl.c` covers the built-in records, caching of the handle, short, long and abbreviated dates for three real dates (including a leap day), 12-hour times, a program's
own resources taking precedence, the P forms with an explicit handle, an `itl0` edited in place, and each refusal.

**Three smaller stops on the way** (all after the Agree click): a dialog's button, check box, radio button, static text or user item with an empty rectangle was refused, though a program may create one and move it later (it now
draws nothing and is never hit); `SetCtlMin` above the current maximum halted, though the Control Manager does no such validation (it is stored as given and the value settles on the maximum; the existing test now says so);
and the stack word names in the unhandled-trap message from the fifth run found the first of these.

**Result (headless, probe, MBP copy).** After Agree the QuickTime Installer draws its **install-type window**: three check boxes "Minimum" (audio and video playback), "Full" (selected; recommended for QuickTime Pro users and
media creators) and "Custom" (advanced users, uninstall capability), each with its explanatory line, and the buttons **Quit** and **Continue**. It waits for input. That is the "Easy Install"/"Custom Install" stage of this installer, in
its own wording. Defects visible in the frame: a stray vertical scroll bar (the list control, never hidden) overlaps the descriptions at the right, an unlabelled rounded button sits below them, and the licence window's heading
and first lines still show behind the new window (the screen behind a closed window is not repainted). A scripted click on Continue (with Full selected) shows an alert: **"The selected disk named ?? does not have enough [room]"**,
with an OK button: the engine asks about the destination disk and gets "??" for its name and too little room. The next subsystem is therefore what the engine reads about the installation disk (volume name and free
space through the File Manager), not another manager.

**WordPerfect's licence window, looked into.** The report was "wraps one letter per line". It does not. `TECalText` sees a destRect 441 pixels wide (4,5 to 243,446) and 7,899 characters, which wraps normally. The column of
single letters at the left is the left margin of the text, uncovered: right after the one `TEUpdate`, the engine clips to the text area and draws a 53 KB `PICT` (431 by 229, srcCopy, the Corel artwork with the words "Your
License") across it, which hides everything but the margin. The engine's own order is TEUpdate, then the picture. Whether a real Mac shows the picture behind the text (so the text would be drawn after it, by a later `TEUpdate`
on an update event we never send for this dialog) or the picture belongs elsewhere I did not establish; the `PICT` decoder accepts only srcCopy bitmaps, so a transfer mode is not the explanation. No fix was made and no test
written for a narrow wrap, because there is none to reproduce. The splash (a separate dialog drawn with `DrawPicture`) is disposed with `DisposeDialog`; nothing repaints the screen behind a disposed dialog, which is why its
pixels remain where the new window does not cover them.

**Not verified.** All headless. The Agree and Continue clicks are scripted. The built-in `itl` values are this project's own choice of US conventions, not a comparison with a real Mac. What the engine does with the
`itl0` handle after getting it is not known. The `??` disk name and the space shortfall are as seen in one frame; I did not trace which File Manager call produced them.

## Seventh run — the disk alert, the System Folder, and the progress window — branch `qt/install-disk`

Same MBP copy (`QuickTime Installer` 34,338 bytes, `.rsrc` 353,773 bytes, `QuickTime Install Cache` 8,344,292 bytes), headless `ospi-runtime-probe`, scratch copies, scripted clicks. The probe now takes `--disk VOLUME`
like `ospi-runtime` does (it measures the guest disk against that folder), and `--trace` now prints what a volume-information call returned (volume reference, drive and driver reference numbers,
block counts and size, free blocks, the name), the name in a catalogue call, `FindFolder` requests and `ParamText` strings. The method was the third run's: trace, then read the engine's own code around the call
(`ospi-dasm` over a RAM dump, observation only).

**What the "??" was.** The File Manager was not the cause of the name. The engine builds a table of volumes at start-up (one 66-byte entry per volume, from `PBHGetVInfo` by `ioVolIndex` 1, 2 and 3: OS999 HD, the System volume, then
`nsvErr`, which ends the walk) and takes the name, the capacity and the free space from those answers (`ioNamePtr` was right: `OS999 HD`). After Continue it asks `PBHGetVInfo` again, this time
with `ioNamePtr` NIL, `ioVRefNum` -1 and `ioVolIndex` 0, and reads `ioVDrvInfo` (+66) and `ioVDRefNum` (+68), then `ioVNmAlBlks` (+46) and `ioVFrBlk` (+62), each multiplied by `ioVAlBlkSiz` (+48). When the free bytes
are short it calls `ParamText` with the volume's table entry as the first string, and then builds the sentence itself: it reads the four **`DAStrings`** handles at low memory `$0AA0`, `$0AA4`, `$0AA8`, `$0AAC`, replaces `^0` to `^3` in
the text of STR# 1009 with their contents, disposes the handles and zeroes the array, and puts the result in the alert with `SetIText`. This runtime kept `ParamText` only in a host copy, so `$0AA0` held NIL and the engine
substituted an empty string: `named “” does not have enough free space`. The quotes are Mac Roman `$D2` and `$D3`, which this runtime's stand-in face has no glyphs for, so they drew as `?` and the line read `named ?? does not`.
The second cause was real: in the probe the disk was not attached, so `ioVFrBlk` was 0 and there was never room.

| | Before (`e205a5d`) | After |
| --- | --- | --- |
| Alert text, probe without `--disk` | `named ?? does not have enough` (an empty name between two `$D2`/`$D3` quotes) | `named ?OS999 HD? does not have enough free space` (the quotes still draw as `?`: a font gap, not fixed) |
| `ioVFrBlk` with the disk attached (`--disk`) | not possible in the probe | about 2.0 GB free: 64,605 blocks of 32,256 bytes in the first run (the host was nearly full, so the capacity was host-limited and moved with the host's free space during the day) |
| `ParamText` | host copy only; `$0AA0` stays NIL | four guest handles at `$0AA0` (`src/mac_dialog.c`), replaced on each call; a NIL parameter leaves its slot; a slot the program disposed and zeroed is refilled |

**The File Manager answers, as they are.** `PBHGetVInfo` for the OS999 HD: `ioNamePtr` gets `OS999 HD` when the caller gives a pointer; `ioVRefNum` -1; `ioVDrvInfo` 1 (the drive queue entry of the data volume) and `ioVDRefNum` 0;
`ioVNmAlBlks` and `ioVAlBlkSiz` are the internal disk's capacity clipped to 65,535 blocks and just under 2 GiB (`volume_classic_view`, 512-byte steps), `ioVFrBlk` the free space in those blocks. These were correct
already and are unchanged; a new test checks that the product stays at or under `$7FFFFE00` and that free is never above total. No `PBXGetVolInfo` (`HFSDispatch` selector 18, `FSDispatch` `$A060`) was called: the engine only does that when it
decides the file system supports volumes over 2 GiB, and this runtime's `Gestalt('fs  ')` does not claim that. The engine takes `ioVDrvInfo` 1 and 2 for floppies (it skips its capacity test for them); it did not
matter for this volume, and I did not change the drive numbers.

**The System Folder.** With the disk check past, the next alert is *Installation requires a System Folder on the selected disk*. The engine first asks `Gestalt('fold')` (undefined here before) and, with no Folder Manager, finds the
System Folder in `ioVFndrInfo[0]` (+90) of the `PBHGetVInfo` record, the directory ID of the blessed folder. This runtime returned 0. Changes:

* `PBHGetVInfo` (the 122-byte record only) returns the directory ID of the volume's `System Folder` in `ioVFndrInfo[0]`, making that folder in the **save overlay** first if the program has one and it is not there yet (the data
  folder is never written; with no overlay the answer is 0 and nothing is made). This is a side effect on any program that calls `PBHGetVInfo`: Monkey Island's save folder now gets an empty `System Folder`
  (its probe frames and instruction counts did not change, see below).
* `Gestalt('fold')` answers 1 (`gestaltFindFolderPresent`). The engine then uses `FindFolder`: it asked for `macs`, `ctrl`, `desk`, `extn`, `font`, `temp` and `pref`, and created only `pref`.
* `FindFolder` (`src/mac_alias.c`, `src/mac_files_dircreate.c`) now places the folders as the public layout does: Preferences, Control Panels, Extensions, Apple Menu Items, Startup Items, Fonts and Shutdown Items inside the System
  Folder; Temporary Items, Trash, Desktop Folder and PrintMonitor Documents at the root. Before, every one was a root folder. `createFolder` makes the System Folder above it too. All of it is in the overlay.
  `kOnSystemDisk`, 0, -1 and -2 still mean this one namespace.

**Past the disk and System Folder checks.** The engine runs: the *Internet Connection* window (DLOG 5100: "14.4 Modem" to "Intranet/LAN", "Allow Multiple Simultaneous Streams"), the *QuickTime Pro registration* window (DLOG 5102, three
edit fields), then the **install progress window** (DLOG 1008): "Your QuickTime components are being ..." with a number, `23`, drawn, and a Cancel button. The first two took one scripted click on Continue (515, 385) each; nothing was typed.
Two more things stood in the way and are fixed, each with a test:

| Stop | Cause | Change |
| --- | --- | --- |
| `Unsupported SANE FP68K selector` at the progress window | the progress arithmetic uses `FDIVD` (`$0806`), `FC2X` (`$300E`) and `FCMPX` (`$0008`); the halt did not name the selector | the message names it (`$xxxx`); arithmetic with a double, single, integer, long or comp source (`$0800`, `$1000`, `$2000`, `$2800`, `$3000` plus 0, 2, 4, 6), comp to extended, and `FCMPX`/`FCPXX`, which set Z for equal and N for less in the condition codes (the signed-branch reading; the exact X, V and C bits of Apple's table were **not checked**) |
| `unhandled A-line trap a873` (`SetPort`) on the next alert | every `NewCWindow` copies a 300 KB backdrop into the native arena, which never gives memory back; a dialog's port window needs one; after about a dozen windows the arena was dry and `NewCWindow` answered NIL, which `SetPort` reported as an unhandled trap | a closed window's backdrop is kept and handed to the next window (`backdrop_spare`, `src/mac_window.c`); `SetPort` on a dialog whose port window cannot be made halts with its own message |

**What was written, and where.** The probe ran with `--saves` at a scratch directory and `--disk` at a scratch copy of the installer's folder; the guest volume's data folder is read-only to the program. The tree the installer made, in the save overlay only:

```
System Folder/                       (made by PBHGetVInfo; FindFolder 'macs' finds it)
  Preferences/                       (FindFolder 'pref' with createFolder)
    QuickTime Preferences            (0 bytes, created through the FSp calls)
```

Nothing else changed in the scratch tree (the capacity code also keeps a `.os999/capacity` file in the volume root, in the scratch copy). The MBP's own volume was only read (`scp`).

**The stop: file names with Mac Roman characters.** On the progress window the engine asks `PBGetCatInfo` (`HFSDispatch` 9, `ioDirID` 2, `ioFDirIndex` 0) for **`QuickTime™ Folder`** (the `™` is `$AA`), the folder it installs into at the root of the
disk, to see whether it exists. It gets `paramErr` (-50), shows *Error creating folder.* with code `1008:9,-50`, and waits for OK. By reading the code, the cause is the File Manager's host-name rule: only ASCII 32 to 126 is accepted, in at least eight places
(`mac_dirs_name.c` `component_ok`, `mac_files_read_name`, `mac_files_save_name_ok`, `mac_fsspec.c`, and the listing filters in `mac_volume.c`, `mac_files_info.c` and `mac_files_write.c`); a real Mac answers `fnfErr` for a name that is not there, and a
`PBDirCreate` of the same name would then have to work. Letting a program use Mac Roman names means a two-way mapping to host names (UTF-8, and macOS's normalisation of file names) in every one of those places and in the listings. That is a File Manager
naming subsystem of its own, not an iteration, and I stopped there. The installer has not copied one file out of the Install Cache: it opens and reads `QuickTime Install Cache` as before, and nothing of QuickTime itself was written.

**MI1 and MI2.** Probe against a build of `e205a5d`, 12 seconds each: identical instruction counts (32,611,248 and 37,422,033) and byte-identical frames at 0, 3, 6, 9 and 12 seconds. Their data folders in
`~/os999-guest-software/from-mbp-20261111/` were hashed before and after (13 files, `shasum`) and are unchanged.

**Not verified.** Everything is headless and from the probe; the clicks are scripted. The meaning of the engine's table fields (`+0x22` total bytes, `+0x26` free bytes, `+0x3a` volume reference) is from its code, not from a decompiled
listing, and the claim that `$0AA0` is `DAStrings` rests on the public name of that low-memory global and on the engine's own use of it. The compare condition-code mapping is unchecked against Apple's table, as said. The progress
number `23` is as drawn in one frame; I did not establish what it counts. Mac Roman quotes still draw as `?`. No real window, no real mouse. The `-50` on `QuickTime™ Folder` is attributed to the name rule by reading, not by a probe of a non-ASCII name on its own.
