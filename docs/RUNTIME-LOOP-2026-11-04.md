# Independent Runtime: First Development Loop

## Target and boundary

Ian requested playable classic games within an hour, launched from the existing
OS Pi desktop. The first loop started at 19:26:04 UTC on 4 November 2026, with
20:26:04 UTC as its one-hour checkpoint. No game is playable at this checkpoint's
current implementation. Builds and startup traces do not pass that requirement.

Work ran locally on MBP-M4Max, arm64, macOS 26.6.2, directly as requested.
Existing changes, the volume, and Systemless saves were preserved. After the
original app exited, its bundle was refreshed and reopened at about 20:28 UTC. Scoped REA work used isolated worktrees; the parent integrated
their files. Nothing was committed or pushed. Existing AGENTS.md was retained.

## Repeatable development loop

1. Read the checklist; select the earliest observed missing interface.
2. Reproduce with a bounded original-app probe. Keep guest artifacts outside Git.
3. Establish the public interface and observable behavior. Do not derive our
   implementation from Apple ROM/System code. Distinguish REA evidence from
   ordinary CPU traces and resource inspection.
4. Implement the behavior with independent synthetic tests. Unknown operations
   stop or return documented errors; do not patch game checks or fabricate success.
5. Run `BUILDFLEET_LOCAL=1 make test runtime` after changes, then:

   ```sh
   python3 scripts/runtime-checkpoint.py --indexed '/path/first-app.rsrc' '/path/second-app.rsrc'
   ```

6. Inspect `report.json` and per-target logs. Each checkpoint records the binary,
   source-file manifest, guest hashes, commands, host, timeout, and exit status.
   Exit 0 means guest ExitToShell, not gameplay acceptance. Exit 3 is a stopped
   guest; exit 4 is instruction-budget exhaustion. Verify the desktop separately.
7. Update the checklist honestly. Continue with the next dependency; retain
   failures and incomplete gates instead of resetting the success criterion.

The checkpoint script runs probes; it does not modify code or claim to be an
autonomous repair service. The active development agent performs steps 1–7.

## Implemented and tested

- Bounds-checked raw resource forks and AppleSingle/AppleDouble containers,
  enumeration, lookup, malformed inputs, and 20,000 deterministic mutations.
- Musashi-backed 68000/68020 execution, 24-bit bus handling, lazy near-model CODE
  loading, canonical shared code/resource memory, and patched-trap chaining.
- Partial Memory/Resource/Event services, manager initialization, monochrome
  drawing, cursor state, environment queries, and explicit failure diagnostics.
- Read-only PBOpen/PBRead/PBClose for ASCII sibling data files. Symlinks,
  traversal, writes, asynchronous I/O, and unsupported volume references fail.
- Indexed display records, color ports, rectangular raster operations, same-depth
  CopyBits, and palette updates. The indexed profile passes the Monkey Island
  color checks; complex regions, mixed-depth copies, and full Color QuickDraw
  remain unsupported. These drawing operations run on the CPU.
- Desktop double-click fallback and native application routing. The helper
  presents through SDL with a required accelerated backend; rasterization and
  conversion are CPU work. No full-acceleration claim is made.

All headless suites passed at the 20:14 checkpoint. Sanitizer checks also passed
on scoped modules; those are separate from visual and gameplay acceptance.
The linker warns about reducing Musashi common-section alignment from 0x8000
to 0x4000. No Linux or Raspberry Pi test was rerun during this loop.

## Original application results

The 20:14 probe used one million instructions per target and stopped earlier:

| Original guest | Executed instructions | Observed stop |
| --- | ---: | --- |
| Secret of Monkey Island | 58,145 | Guest alert requesting 256-color mode |
| Monkey Island 2 | 83,976 | Guest alert requesting 256-color mode |
| Castle of Dr. Brain | 5,577 | SetApplLimit trap A02D unsupported (initially misidentified as InitZone) |

Evidence: `build/loop-20261104/evidence/checkpoint-2014/`. Binary SHA-256:
`52a05f09f640d99045d53fb4992eccff28dfa8d6af16d5481f14b2d4edf6cc9d`.
The report contains exact resource-fork hashes and source-manifest hash.
Later changes require a fresh checkpoint before claiming the same result.

The three extracted games were copied into new folders under the user's
`Volume/Applications`, without replacing existing folders. Resource-only apps
received an empty data-fork file so their names appear beside hidden sidecars.
`build/OS Pi Development.app` is separate from the original running bundle.
Real window captures verified opening Applications and the Monkey Island folder.
The executable click remains unverified: foreground automation was interrupted
by a macOS Python data-access permission prompt. It was not approved automatically.

REA 6.8.0 returned ZIP inventory evidence for original MI2 and Castle media:
`ev_6c5b7fceea36267f9a48ea87303134423a99e304e780a6e1304c8d42ff1d6e0d`
and `ev_8443894151b8d60612f30afd927d17c1bf89bfbb915c03ee1d21f250219f9dd9`.
QuickTime's MacBinary inspection was unsupported. These inventories do not
establish execution; original-game traces above come from our CPU adapter.

## Dependencies and remaining acceptance

Musashi revision `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd` is downloaded by
`scripts/prepare-musashi.sh`, verified against SHA-256
`8bed9e28e7e5377256d41aff80a99c9e917a24e7392d9219e1c45c40ad4000e5`.
Its MIT-style notice and bundled SoftFloat 2b notice accompany app packaging.
Source remains in ignored `build/deps/`; no Apple implementation is downloaded.
SDL2, FreeType, and the existing public-domain fonts retain their own notices.
The replacement arrow, hourglass, palette, and tests are independently authored.

Next dependencies: offscreen pixel-map allocation and File Manager metadata,
correct Castle's SetApplLimit trap mapping, then follow actual drawing,
window, menu, and audio calls. A02D is SetApplLimit; the earlier InitZone
diagnosis was wrong. Heap zones remain a separate unimplemented feature.
General heap reclamation, exception handling, sound, saves, security isolation,
full GPU acceleration, sustained gameplay, and Ian's acceptance remain open.
The runtime is not a sandbox. Chromium, Thurrott.com, YouTube, secure networking,
legacy protocols, and classic QuickTime video playback remain unimplemented.
MacSurf identity is still unresolved. See the checklist for their separate gates.

## One-hour result and continued work

The one-hour gameplay target was missed. The indexed checkpoint at 20:25:52 UTC
(`build/loop-20261104/evidence/checkpoint-hour/`) passed the games' color checks:
Monkey Island stopped at VInstall A033 after 57,815 instructions; Monkey Island 2
stopped at GetVol A014 after 83,680. Castle stopped at SetApplLimit A02D after
5,577. No game scene, sound, or playable interaction was demonstrated.

Subsequent bounded VBL callbacks and GetVol implementation passed the headless
suite. New traces reached NewPixMap AA03 in Monkey Island and PBHGetVInfo A207 (earlier file-info label was incorrect)
in Monkey Island 2. These are progress through startup, not gameplay passes.
That first bundle refresh predated those two services; the later refresh below includes them.

## Corrected memory dispatch checkpoint

`build/loop-20261104/evidence/checkpoint-2037/` records the next immutable
binary/source snapshot after adding NewPixMap and correcting SetApplLimit.
The old implementation had wrongly assigned SetApplLimit to A064 (MoveHHi).
A064 now stops explicitly; previous later MI2 traces included that incorrect
behavior and do not prove a correct path through its startup.

| Guest | Instructions | Current stop |
| --- | ---: | --- |
| Monkey Island | 57,897 | QDExtensions NoPurgePixels (AB1D, selector 12) |
| Monkey Island 2 | 72,862 | MoveHHi A064 |
| Castle of Dr. Brain | 5,655 | HFSDispatch GetWDInfo (A260, selector 7) |

The indexed InitGraf-to-OpenCPort-to-PaintRect integration test passes through
the actual trap dispatcher and verifies resulting framebuffer pixels. NewPixMap
ownership and corrected memory limits pass scoped ASAN/UBSAN checks. None of
these checks demonstrates game scenes, sound, saves, or GPU guest drawing.

The complete local `make test all` run passed after these changes. The owned
desktop process was stopped, its bundle refreshed without deleting the bundle,
ad-hoc signature verified, and the app reopened at about 20:38 UTC. Signing
changes executable hashes; signed bundle hashes and a matching Monkey Island
headless stop are recorded separately in `evidence/packaged-2038.json`.
The separate OS Pi Development bundle remains an older build.

Next memory work: MI2 MoveHHi targets unlocked CODE resource 6 (13,512 bytes).
A correct implementation needs a reserved upper allocation boundary, consistent
FreeMem/SetApplLimit accounting, and protection for loaded executable segments.
Monkey Island needs NoPurgePixels followed by LockPixels for its independently
allocated pixel buffer; Castle needs working-directory information. None was
bypassed with fabricated success.

## Continued execution work after the hour

The `checkpoint-2048` receipt records 1.5-million-instruction budgets against
all three original fixtures. Monkey Island executes 842,638 instructions and
reaches NewCWindow AA45 after genuine pixel-buffer allocation, locking, base
address lookup, and OffsetRect. MI2 executes relocated CODE and reaches
PBHGetVInfo A207 after 83,700 instructions. Castle passes GetWDInfo and reaches
GetCatInfo selector 9 after 5,684. All still stop before gameplay.

The full `make test all` run passed for that snapshot. Later focused checks pass
for calling the HFS predecessor trap through real 68k JSR, and for root catalog
metadata. Castle then advances to HFSDispatch selector 8 after 6,062 instructions.
Pixel memory, memory relocation, root metadata, and geometry also have scoped
ASAN/UBSAN passes. These are implementation checks, not visual acceptance.

MoveHHi currently relocates trailing movable blocks into reserved upper-heap
space. Nontrailing compaction remains unsupported. Both allocator accounting and
SetApplLimit preserve that reservation, including after disposal. Loaded CODE
has a separate execution pin protecting its published addresses from relocation,
resize, disposal, and Resource Manager release. General heap reclamation and
UnloadSeg remain open.

The desktop now uses each SDL button event's timestamp when draining queued
clicks, so fallback double-click detection does not assign every event the same
frame time. Its headless regression passes; automated GUI delivery remains
unverified. The running bundle still corresponds to the preceding signed
checkpoint until the next explicitly recorded refresh.

Diagnostic correction: `mac_cpu_read` returns zero after CPU halt. Earlier
post-halt observations of apparently empty file parameter blocks were invalid.
The trace now dumps checked raw RAM instead. Castle's catalog request really
contains a valid name output buffer, volume -1, index -1, and root directory ID 2.
Also, A207 is PBHGetVInfo (volume information), not PBHGetFInfo. Earlier labels
were corrected; no erroneous file-info handler was integrated.


## 21:10 UTC continuation: windows and System catalog

Both Monkey Island originals now pass NewCWindow and RectRgn, then halt at
color PenPat A89D (842,672 and 868,581 instructions respectively). The bounded
window service owns its records and backing pixels; it does not implement
general overlapping windows or custom WDEF callbacks. Region ownership and
window/event tests pass. No playable scene has been demonstrated.

The runtime now owns an independently generated empty System resource fork
(286 bytes in a 512-byte allocation), with read-only volume, catalog, FCB, and
read services. No Apple System bytes are included. Its lifecycle and focused
tests pass. System Resource Manager chain lookup remains unimplemented.
Castle reaches Alert 131 after 6,280 instructions. Resource-only inspection
identifies its static text as requiring System 6.0.7 or later; this is not yet
a diagnosis of which version query failed. The current live desktop bundle
has not been refreshed for these changes.


The subsequent `checkpoint-2115` receipt (actual timestamp in report) includes
SysEnvirons backed by the registered authored System volume. Castle passes the
version requirement and reaches Alert 133, ParamText `IsFloppyDisk`, after
6,663 instructions. Both Monkey Island games still stop at color PenPat.
The full suite passed before SysEnvirons; focused environment, runtime lifecycle,
and saved-trap tests pass after it. Color-pen implementation remains in its
isolated worktree and is not included in this checkpoint.

At 21:13 UTC the exact owned desktop PID 95256 was stopped, both bundle
executables refreshed, the bundle ad-hoc signed and strictly verified, then
reopened. `packaged-2115.json` records signed binary hashes. A direct packaged
helper probe passes the original 256-color check and stops at PenPat; desktop
interaction and playable graphics remain unverified. No user saves were touched.


## 21:16 UTC: color pen and real window observation

Owned old-style color pen patterns now update real CGrafPort PixPat data.
Focused root tests pass, including resulting indexed PaintRect/EraseRect pixels;
the isolated module also passed ASAN/UBSAN. Both original Monkey Island games
advance to ReserveEntry AA3E (850,979 / 876,885 instructions). No guest code
or alert was bypassed.

A real windowed MI1 probe selected SDL's Metal presentation backend. Its captured
640x512 window shows a gray field with dark top/bottom strips, not a game scene.
The separate failure dialog reports AA3E, matching Ian's latest screenshot.
Private evidence: `monkey-colorpen-window.png`, `monkey-colorpen-window.log`,
and `monkey-colorpen.log`. The test process exited with status 3 before cleanup;
no unrelated process was stopped. GPU guest rendering remains unimplemented.

Castle's `IsFloppyDisk` alert is traced to an empty Drive Queue at low memory
0x30A after a successful volume query. It searches drive entries for the returned
volume drive ID; it has not classified the mounted directory as a floppy.
A coherent logical drive queue is being implemented separately.


## Continued startup work: reservations, drives, and memory

ReserveEntry now maintains client ownership metadata used by RGB matching and
SetEntries. Recoverable reservation range/conflict errors are available through
QDError. Root palette, reservation, and raster tests pass; isolated sanitizer
suites pass. Both Monkey Island originals subsequently pass genuine empty
menu-bar drawing and stop at PaintBehind A90D (854,096 / 880,015 instructions).
Nonempty menus and custom menu color tables still require implementation.

The runtime now registers logical nonremovable read-only drives for its authored
System volume and mounted app-data directory. Queue IDs and block capacities
match volume queries; no raw disk driver is claimed. Root queue, volume, file,
System-volume, and lifecycle tests pass. Castle passes IsFloppyDisk.

Bounded PurgeMem/CompactMem succeed when the requested contiguous free space
already exists, without moving or purging data. Insufficient free space still
halts explicitly because general heap reclamation is unimplemented. Root and
isolated ASAN/UBSAN memory checks pass. Castle now reaches GetNewCWindow AA46
after 134,120 instructions. Exact trace: `castle-memory.log`. No playable target
or audio/save result has been observed. The desktop bundle remains the 21:13
checkpoint until a separately recorded refresh.


## Window and point integration checkpoint

PaintBehind now redraws clipped owned chrome and preserves content pixels while
recording exact rectangular update unions. It rejects exposed desktop damage
and complex unions before mutation. Proc2 plain dialog frames and resource-backed
GetNewCWindow are integrated; focused root suites and isolated ASAN/UBSAN pass.
WIND data is parsed from the supplied guest resource, with bounded scratch stack
restoration and explicit rejection of custom definitions/positioning.

SetPt is now implemented with its Pascal argument order, signed word coordinates,
checked destinations, and overlapping-argument snapshots. Its root tests pass.
Monkey Island 1/2 reach NewMenu A931 at 854,242 / 880,163 instructions.
Castle creates its first hidden WIND129 plain dialog, then requests a second
identical hidden window and stops at the current single-window limit after
134,137 instructions. Menu creation and multiple-window ownership remain next.

The complete regression suite passed for the preceding reservation/drive/memory/
empty-menu snapshot (`/tmp/ospi-loop-2127-tests.log`, checkpoint-2131 receipt).
A fresh full suite for the subsequent window/SetPt integration is running; its
result must be recorded separately. No gameplay, sound, saves, or full acceleration
acceptance is implied by these API tests or startup instruction counts.


The complete window/SetPt regression run passed (`/tmp/ospi-loop-2138-tests.log`).
At 21:39:06 UTC the owned desktop PID71484 was replaced with that signed,
strictly verified build and reopened. Receipt:
`packaged-20261104T213906Z.json`. A direct signed-helper probe passes AA3E and
stops at NewMenu A931; GUI game acceptance remains unverified.

Subsequently the main tree integrated an eight-record owned window registry.
It retains hidden windows and WindowList links independently from service
selection and QuickDraw's current port. Only one visible window is supported;
showing overlapping visible windows still fails explicitly. Root multi-window,
window, event, and region tests pass; six isolated sanitizer suites pass.
Castle now creates three real windows and reaches NewPalette AA91 after
134,167 instructions. This registry change is newer than the packaged build.

## Menu, palette objects and picture loading integration

Integrated independently authored menu records/title rendering, Palette Manager
objects and hidden-window associations, dBoxProc frames, GetPicture, and GetEOF.
Focused root tests passed for menu records, palette objects, dialog frames,
resource services, file services, and the authored System volume. Isolated
sanitizer results were reported separately by the scoped workers.

The first integrated checkpoint exposed a palette dispatcher return-convention
error despite passing direct-service tests. Corrected the dispatcher and added
a test through mac_runtime_trap. Preserve checkpoint-menu-palette as failed
integration evidence; its Castle AA91 stop is superseded by the corrected probe.
Both Monkey Island games pass menu startup/GetEOF and stop at A013 FlushVol
(1,044,604 and 1,011,632 instructions respectively). Corrected Castle passes
four windows, hidden palette association and GetPicture; stops at A91D at
PC00115168 after134,604instructions/70traps. No playable scene has been verified.
Palette activation, picture drawing, menu interaction, audio and saves remain
incomplete. SDL Metal presentation is not accelerated guest rendering.

Final root `BUILDFLEET_LOCAL=1 make -j2 test` passed, including the real
trap-dispatch palette test (`/tmp/ospi-menu-palette-final-tests.log`). Its first
fixture omitted mac_traps_init; that test setup was corrected before the passing
rerun. Corrected target receipts are in checkpoint-menu-palette-fixed.
At21:55:28UTC the exact owned desktop process was stopped, both bundle binaries
refreshed, the bundle ad-hoc signed and strictly verified, and the desktop
reopened. Receipt: packaged-20261104T215528Z.json. Desktop clicks and playable
game scenes were not verified; no guest saves were altered.

## FlushVol, keyboard state and rectangle intersection

Correction: A013 is FlushVol, not GetFPos; the previous report mislabeled it.
The trap identifier in recorded receipts was correct. Public File Manager
reference 2-143 establishes volume flush behavior. Added selection by default
volume, reference, logical drive and colon-prefixed volume name. Both mounted
volumes reject guest writes and retain no dirty buffers; flushing these immutable
volumes requires no host-wide sync. Invalid references, absent default volume,
async requests and bad bounds return errors. Focused file tests passed.

MI1 then reached GetKeys A976. Added a persistent key bitmap updated before queue
capacity checks; key releases survive overflow. GetKeys snapshots supported
host keys plus the five aggregate classic modifier positions, leaves queued
events intact, and validates its output. Focus loss clears key state. Extended
keyboard layouts/keypads and distinct left/right modifier reporting remain
incomplete. Focused event tests passed. MI1 next reached SectRect A8AA; added
signed rectangle intersection, empty-result zeroing and source/destination
alias handling, with synthetic boundary/alias tests.

Original MI1 now reaches CopyBits at PC00108180 after7,633,254instructions,
1494traps. It stops because the current CopyBits path insists on256-entry
color tables; the supplied source table has ctSize=ffff. Do not bypass this
check without establishing the correct palette/seed semantics. MI2 reaches
OpenResFile A997 atPC006de25c after1,177,892instructions/778traps. Logs:
/tmp/ospi-sect-mi1.log and /tmp/ospi-flush-target-1.log. No scene/playability proof.

Validation: full regression suite passed in /tmp/ospi-flush-keys-tests.log.
Fresh final-source event, geometry and file suites also passed after the
SectRect and GetKeys NIL-diagnostic edits. MI1 receipt is retained in
checkpoint-flush-keys; exit3 records the unsupported CopyBits path, not success.
The desktop bundle was refreshed and strictly signature-verified after tests;
timestamped packaged-*.json records the exact binaries. No GUI interaction or
scene acceptance was claimed for this update.

## First original-game image: equal-seed CopyBits and CalcVis

Trace established that MI1's empty source CLUT and populated destination CLUT
both have seed1024 with srcCopy. QD13 explicitly specifies direct pixel-value
transfer when these seeds match. Implemented that path after validating both
headers and supported flags; other transfer modes or unequal seeds still require
complete tables and verified color equivalence. Tests place an eight-byte empty
CLUT at the end of guest RAM, verify exact copied pixels, and reject unequal
seeds, other modes, truncated headers and palette-linked flags without mutation.
Source: https://leopard-adc.pepas.com/technotes/qd/qd_13.html (contract prose only).

MI1 then requested CalcVis A909. Implemented visibility from the owned global
content region, clipped to display/menu bar and translated to local coordinates.
Preserves update region, event queue, active port and pixels. Hidden windows
receive empty visibility. Unsupported visible overlap and BeginUpdate mutation
still fail explicitly. Focused window and CopyBits tests passed.

A real single-window capture at22:06:41UTC shows the original Lucasfilm Games
logo: build/loop-20261104/evidence/mi1-copy-seed-window.png. The underlying
window is dimmed by the failure alert; this is image execution proof, not
palette correctness, interactive gameplay or audio acceptance. Native helper
PID59888 exited after capture; its exec session returned3. The headless probe
stops at SndNewChannel A807, PC0010af2e, after7,706,156instructions/1563traps.
The sampled-sound initialization uses synth5/init008c and a guest callback.
Actual channel ownership, command processing, callbacks and audio output must
be implemented; returning a fake successful channel is not acceptable.

CopyBits sanitizer validation passed with AddressSanitizer and
UndefinedBehaviorSanitizer in /tmp/ospi-test-copybits-sanitized. The original
MI1 checkpoint receipt is checkpoint-copy-calcvis; its exit3 is the observed
Sound Manager stop. REA section6 independent scopes resumed in isolated trees:
cpu_adapter for sampled-sound/channel behavior and resource_loader for MI2's
additional resource-file request. Neither worker may edit the shared root.

Full regression suite passed in /tmp/ospi-copy-calcvis-tests.log. Desktop bundle refreshed and strictly signature-verified at 20261104T221006Z; the corresponding packaged receipt retains binary hashes. The captured logo is from the separately launched helper, not a verified desktop double-click.

## Window geometry advances Castle to picture drawing

Implemented bounded SizeWindow A91D and MoveWindow A91B for owned windows.
Resize preserves overlapping content, restores uncovered background and updates
the owned regions. Move preserves local coordinates while translating global
regions and visible pixels. Unsupported complex update unions and invalid
geometry fail before mutation. Castle now creates its hidden 384 by 198 window
at global position (128,141), then stops at DrawPicture A8F6, PC00115184,
after 134,661 instructions and 74 traps. Evidence: /tmp/ospi-move-castle.log.

The complete regression suite passed in /tmp/ospi-window-geometry-tests.log.
Window tests also passed with AddressSanitizer and UndefinedBehaviorSanitizer;
the existing CPU object files were not instrumented. The installed bundle still
contains the earlier CopyBits/CalcVis snapshot. A foreground automated double
click at Applications produced no visible change; desktop launch remains
unverified. No playable-game or acceleration acceptance is claimed.

## Monkey Island 2 opens its music resource file

Integrated read-only Resource Manager fork chains with owner-specific resource
handles, current-file/search order, preload, reload, reopen and close. Sibling
exports use bounded ASCII case-insensitive matching through the pinned directory;
symlinks, path escape and ambiguous names fail. Limits are eight extra forks,
16 MiB per fork and 4096 resources per fork. Native resource forks, writeback and
external File Manager records remain unsupported. Menu/cursor helper lookups
and executable CODE loading remain application-pinned.

Focused root resource-chain and Resource Manager tests passed after integration.
The original MI2 checkpoint now stops at Pack4 A9EB, PC006df3e4, after 1,184,735
instructions and 787 traps. Its receipt is checkpoint-resource-chain. The isolated
trace confirms 68 resources opened from iMuse Setups and sound resource 999 read.
This is resource loading evidence, not music output or gameplay acceptance.

## Castle decodes its original startup picture

Added bounded indexed PICTv2 PackBits decoding, palette mapping, scaling and
rectangular clipping. The complete stream is validated in host memory before
any guest framebuffer writes. Unsupported opcodes, malformed rows and undefined
color indices fail explicitly. Drawing is on the CPU. Synthetic tests cover
screen palette selection, reserved colors, offscreen mapping, stretched output,
clipping, truncated streams and failure atomicity. The isolated strict build
passed AddressSanitizer and UndefinedBehaviorSanitizer; focused root tests pass.

The original Castle trace records 76,032 pixels painted from its 17,820-byte
PICT resource into a 384 by 198 destination. The integrated checkpoint
checkpoint-picture-integrated stops later at Alert 143, "Can't load !??",
PC00110a00, after 266,477 instructions and 1654 traps. File diagnostics show
INTERP.TXT opened and read; the loading failure remains under investigation.
This is raster execution evidence, not a verified visible scene or gameplay.

## Desktop icon launch verified

At about 22:26 UTC, Peekaboo background window-targeted double-clicks opened
Applications, Monkey Island, then its executable icon. The desktop spawned
the bundled independent helper (observed PID97370) with the installed MI1
resource fork. Its private log /tmp/ospi-runtime-wERFcp confirms Metal
presentation and the known A807 stop. The helper exited before window capture;
no new visible-game claim is made. Receipt: desktop-launch-20261104T2226Z.json.
Foreground global clicks had produced no visible change; the successful route
was background window-targeted events. This verifies launch routing only.

The full integrated Resource Manager/PICT regression suite passed in
/tmp/ospi-resource-picture-tests.log. The desktop bundle remains the earlier
CopyBits/CalcVis snapshot while subsequent sound and SANE work is integrated.

## Numeric conversions and sampled sound

Implemented bounded Pack4 SANE conversions using integer bit operations,
including binary32/integers to 80-bit extended, truncation and integer rounding.
NaNs, unsupported encodings/operations and exception callbacks fail explicitly.
Synthetic sanitizer tests and focused root tests pass. MI2 reaches SndNewChannel
at PC006dfb50 after 1,269,828 instructions (checkpoint-sane-integrated).

Integrated sampled mono channels, caller-owned queues, PCM resampling, voice
installation, note/duration commands, guest callbacks and bounded SDL output.
Strict isolated sanitizer tests and root sound tests pass. Legacy note amplitude
packing follows a public 1992 API example; no Apple implementation was copied.
The deterministic offline MI1 probe next rejects a loop endpoint equal to sample
length; no silent clamping was added. Hardware audio acceptance remains open.

The first window run submitted 2048 frames then hit the VBL backlog guard.
Measured CoreAudio initialization/opening took 3017 ms, exceeding the 120-tick
guard. Host audio setup now precedes the guest wall-clock baseline; VBL limits
and callback delivery remain unchanged. Evidence: /tmp/ospi-sound-window.log
and /tmp/ospi-cpu-loop/build/sound-check/audio_timing.log. Screenshot
mi1-sound-host-stop.png shows the logo under the stopped-window dimming.

Final hardware-window run passes startup timing, then rejects voice header
001dcc5c: length3806, loop810..3806, base72. Log /tmp/ospi-sound-final-window.log
records2048 submitted frames; audible output remains unverified. Full suite
passes in /tmp/ospi-audio-final-tests.log. Headless checkpoint-sound-integrated
records MI1 instruction-limit exhaustion (no PCM consumer), MI2 A800 and Castle
Alert143. These remain failures of the overall gameplay acceptance gate.
Bundle refreshed, signature-verified and reopened at22:35:58UTC; receipt packaged-20261104T223558Z.json, desktopPID15846. Owned probes stopped.
