# Runtime traces: TeachText 7.2 and the WordPerfect 3.5e installer

Two real classic applications that failed to launch in Build 35, run as guest code in `ospi-runtime` on the branch `runtime/compressed-resources`. Both are
named here only as specimens; they are separately supplied guest software, copied from the MBP's volume into a scratch directory, never committed. The QuickTime
Installer has its own record in `docs/QUICKTIME-RUNTIME-TRACES.md`. All of it is headless (`./build/ospi-runtime --indexed --steps N`, plus
`ospi-runtime-probe` frame dumps); no real window was involved. Nothing here says either program works.

## TeachText 7.2 (68K)

| | Before | After |
| --- | --- | --- |
| Classifier | `68k-code`, profile `B-clean-68k`, runnable | same |
| Loader | `CODE entry point is outside its segment.` before a single instruction | loads, runs 40 instructions and 7 traps |
| Stop | none (refused at load) | `LoadSeg could not expand CODE 1: CODE resource 1 is compressed (header version 8, expands to 26668 bytes) and needs decompressor 'dcmp' 0, which is in none of the open resource files. The stock decompressors belong to the System file; OS999 does not ship it.` |

**Why it was refused.** CODE 1 and CODE 2 are stored compressed with the Resource Manager's extended header (`A89F 6572`, header version 8, decompressor
`dcmp` ID 0). The loader checked each jump-table entry's offset against the *stored* size of the segment, so an entry that lies inside the expanded segment looked
to be outside it. The loader now checks at the expanded size the header declares.

**What runs now.** The startup segment is CODE 32, an uncompressed 196-byte stub. It asks `GetTrapAddress` and `Gestalt` about the machine and then, on any
CPU above a 68000, jumps through the low-memory vector at `$6F4` after moving code. That vector was zero; it now holds the unimplemented-trap address, which is
what that start-up code reads as "nothing to flush". Past that, the stub calls `LoadSeg` for CODE 1, which is where the program stops.

**What is missing, precisely.** The `dcmp` resource with ID 0 is not in TeachText's own fork. Resource ID 0 is one of the stock decompressors, which live in the
System file. OS999 does not ship a System file and the runtime does not re-create the stock decompressor's algorithm, so TeachText 7.2 cannot go further here until a
`dcmp` 0 is somewhere in the resource chain. If one is supplied by the user's own files, the runtime will find it by ID and run it as guest code (see below); that path is
tested only with a toy decompressor written for the test. **The calling convention it uses was taken from public descriptions of the interface and is unverified against a real
stock decompressor, because none was available.** That is the largest unknown in this change.

## WordPerfect 3.5e installer ("Install Corel WordPerfect 3.5e", MindVision engine)

| | Before | After |
| --- | --- | --- |
| Classifier | `malformed`, profile `D-unsupported` ("CODE 0 jump table has an unsupported or inconsistent layout") | `68k-code`, profile `B-clean-68k`, runnable, with a note that CODE 0 stores 480 of the 2,416 jump-table bytes it declares |
| Loader | `Unsupported or invalid CODE 0 layout.` | loads; the engine's stub runs |
| Stop | none (refused at load) | draws its own dialog *An error occurred while reading the installe[r]* with the code `1009:5,-50`, then waits in `ModalDialog` |

**Why it was refused.** CODE 0 declares a jump table of 2,416 bytes but the resource is 497 bytes: one real entry (to CODE 20, the engine's stub) and then a block that
starts `A89F 00A6`, the engine's packed payload. A real Segment Loader copies what the header says and the stub rewrites it. The loader and the classifier now share one
reader (`include/mac_codefmt.h`): a table stored shorter than declared is accepted if at least one whole entry is there and everything declared fits the A5 world; entries from the first
`A89F` word on are not validated, the stored bytes are copied, the rest is zero, and `LoadSeg` still honours entries the program rewrites into the unloaded form beyond the stored part.

**What it does after that.** It is the same engine as the QuickTime Installer, so the same fixes apply: its patch of OS trap `$A0FC` unpacks its `A89F 000C` resources
(run once, eagerly, when the patch is installed) and it calls a one-process Process Manager. It then reaches the dialog above in about 1.76 million instructions. I looked at a
headless frame at 8 seconds of guest time: the dialog is drawn, an OK button is present, nothing was clicked.

**The next blocker.** `-50` is `paramErr`. The last File Manager call is a `PBHOpen` with an empty name, `ioVRefNum` -1 and `ioDirID` 0, issued right after `PBHGetVInfo` with
`ioVolIndex` 3 answered `nsvErr`. My guess, not verified, is that the engine walks volumes by index looking for something and gives up with an empty name; this runtime presents two
volumes. The data fork of the installer is 25 MB; none of it was read.

## What was not verified

* Neither program was seen to do anything beyond what is written above. No click, no install, no file written.
* The instruction counts are headless budgets, not timing.
* For the engine's unpacking I rely on its own code running; I did not compare its output with a reference unpacker.
* The Resource Manager compressed-resource path (`A89F 6572` and a `dcmp`) is verified only against synthetic forks and a toy decompressor, not a real one.

## The `-50` explained, and the next stop (branch `qt/installer-volumes`)

Both installers use the same engine, so one cause explained both. Full reasoning and the table of fixes are in the third run of `docs/QUICKTIME-RUNTIME-TRACES.md`.

**What the -50 was.** The `PBHOpen` with an empty name and directory 0 is the engine opening its own file through a file specification record it builds from two
globals: the name copied from low-memory `$910` (ApName, the running application's name) and the default volume. `$910` was empty in this runtime. It is now set from the
application's file name; the probe did not tell the File Manager the name at all, so both `ospi-runtime` and the probe now share `mac_files_set_app_path`. The volume walk that ends in `nsvErr` for index 3
is correct (index 1 OS999 HD, index 2 the System volume, 3 past the end); nothing in volume indexing changed.

**WordPerfect 3.5e installer, after the fix.** The same ordering: it opens `Install Corel WordPerfect 3.5e` (perm 1, directory 0, resolved to the default directory) and runs on to the same
`DLOG` 1000, a `NewControl` on that dialog, and then `Pack0` selector `$0044` (`LNew`, vertical scroll bar) at 2,103,904 instructions (616 ms guest time), where it halts:
`unhandled A-line trap a9e7`. Before: the `-50` dialog and a `ModalDialog` wait at about 1.76 million instructions. The same Script Manager, `SetWTitle`-on-a-dialog, controls-on-a-dialog and
`Draw1Control` gaps were hit and closed on the way (the QuickTime record has the table).

**The missing subsystem: the List Manager** (`Pack0`, `$A9E7`: `LNew` and the rest of the `L*` family, with calls into the program's own `LDEF` code). Neither installer's main window ("Easy Install" / "Custom Install")
has been drawn; the frame at the stop is blank apart from the menu bar rule. Nothing was clicked.

**Regression check.** Monkey Island (`Monkey Island.rsrc`) and Monkey Island 2 (`LeChuck's Revenge.rsrc`), probe copies in `~/os999-guest-software/from-mbp-20261111/`, 12 seconds of guest time each, before and
after: identical instruction counts (32,611,248 and 37,422,033) and byte-identical frames at 0, 3, 6, 9 and 12 seconds. That is a headless probe comparison, not play.

## The List Manager, and where each installer stops now

The List Manager exists now (see the fourth run in `docs/QUICKTIME-RUNTIME-TRACES.md` for what it covers, its limits and what is unverified). Headless results from `ospi-runtime-probe`, scratch copies, nothing committed:

| Installer | Before | After |
| --- | --- | --- |
| QuickTime 4.0.3 Installer | halt on `Pack0` `LNew` | draws the Welcome to QuickTime 4 window and waits for input; after a scripted Continue it halts on trap `$A83E` (`TEStyleNew`, styled TextEdit) |
| WordPerfect 3.5e installer | halt on `Pack0` `LNew` | halts at 633 ms of guest time: `Font family 3 size 9 style 0 is not in the guest application's resources (no scaling or system font is provided)`. Family 3 is the system file's small UI face, which this runtime does not ship; the installer's own fork does not carry it. After `LNew` it also needed `SizeWindow` on a dialog (added) |

For WordPerfect the stop is a font policy question, not a missing manager: the runtime refuses to substitute a face, and the faces it may bundle are limited by the clean-room rules. Nothing was drawn (the frame is blank).
"Easy Install" / "Custom Install" were not reached in either installer. MI1 and MI2 are unchanged: identical instruction counts (32,611,248 and 37,422,033) and byte-identical frames at 0, 3, 6, 9 and 12 seconds against a build of the commit before the List Manager.
