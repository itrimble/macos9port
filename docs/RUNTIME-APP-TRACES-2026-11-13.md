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
