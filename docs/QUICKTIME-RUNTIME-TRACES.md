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
