# Desktop completion loop — 6 November 2026

## Scope and ownership

Ian requested a complete desktop with menus appropriate to the final Mac OS 9
Finder and an implementation/verification loop until finished. The active goal
is desktop work, not a resumption of game validation. Work is isolated at
`/Users/ian/projects/macos9port-desktop-loop`, based on `68f61ff`, because the
main checkout has concurrent runtime/build changes. Nothing is committed,
pushed, or integrated into the main checkout yet.

## First implementation increment

- Explicit desktop focus prevents commands targeting an old folder after a
  background click. Inactive window titles are visually distinct.
- Expanded File, Edit, View, Special, Help and Finder window-selection menus;
  command-based enablement replaces catalog-row offsets. Appearance precedes
  Help; Finder is on the right.
- Duplicate, Rename, Undo Rename, Move to Trash, Undo Trash and Put Away.
  Exclusive destinations, resource-fork sidecars, contained directory opens,
  bounded copies, and no-follow behavior are exercised by filesystem tests.
- OS999 aliases, Show Original, Favorites and bounded, case-insensitive Find
  with actionable results. These aliases are OS999 records, not classic Alias
  Manager resource files. Renaming an alias target is not yet tracked.
- Buttons view, date/size arrangement, Clipboard window, View Options, Finder
  Preferences and persistent item labels. Preferences expose existing host
  sound/startup choices. Labels use extended attributes.
- Restart resets desktop windows after confirmation, retaining host processes
  and files. It does not restart the host or a classic application.

## Evidence

`BUILDFLEET_LOCAL=1 make -j2 test all` passed in the isolated checkout; output:
`build/full-check-2.log`. Vendor/runtime links report existing common-section
alignment warnings; these are not GUI acceptance. The focused file suite has
75 checks; the session-command suite subsequently passed 39 checks including
settings, cancellation, aliases, search, rename and Trash restoration.

Source manifest: `build/desktop-r1-source.json`. This names exact SHA-256 values
for the implementation/test files before the R1 package build. R1 package and
latest suite outcome are to be appended once complete.

A real single-window capture is at `build/desktop-loop-initial.png`, captured
6 November 2026 around 14:01 CST. It exposed duplicated Finder text; the old
decorative text was removed in source. Initial preview PID 77826, window 25299,
uses only `build/desktop-loop-volume`. These IDs are historical, not liveness
proof. Background input could not press the custom SDL surface; foreground
focus through the 4.9.7 GUI bridge timed out. Local 4.9.8 input verification is
pending. No menu interaction is claimed from those dispatches.

## Remaining loop work

- Complete application-menu integration with the combined browser, terminal
  and media desktop. The current isolated runtime permits one classic guest;
  Finder/current-guest switching is implemented, not general multi-guest support.
  Audit remaining menu semantics and document unavailable host-only operations.
  Cut/Paste, Encrypt, Eject/Erase Disk and Sleep currently remain disabled.
- Verify Page Setup cancellation/settings and remaining printing paths in the
  running app. Print Window panel/preview/cancel and native PDF generation now
  have evidence; physical printer output does not.
- Verify bottom-edge pop-up tabs, restored folder layouts, multi-selection
  and transfers through real GUI input. Audit item-count limits, missing folders
  and cached layouts after external filesystem changes.
- Improve alias target tracking, bulk operations, undo consistency, list
  columns, help text and shortcuts. Test malformed aliases/search bounds and
  labels on unsupported filesystems. Current labels/Find do not imply complete
  classic metadata or filesystem compatibility.
- Repeat complete tests and sanitizer checks after fixes; package a unique
  preview, verify signature, capture it and validate real input. Retain exact
  artifact/source identities. Do not overwrite a running bundle.
- Integrate only scoped changes with ownership/conflict checks against the
  concurrently changing main checkout, then rerun combined checks. Keep the
  goal active until the desktop requirements and acceptance are satisfied.

## Behavioral references opened

- Apple's archived Mac OS 9 Finder menu comparison:
  https://savagetaylor.com/til/TA22098.html
- Apple's Mac OS 8 Human Interface Guidelines, menu behavior inherited by 9:
  https://dev.os9.ca/techpubs/mac/pdf/HIGOS8Guidelines.pdf

These are behavioral references only. No Apple implementation, assets, or
manual text is included in the product.

## R1 build checkpoint, 14:07 CST

`BUILDFLEET_LOCAL=1 make -j2 test all app
APP_BUNDLE='build/OS999 Desktop Loop R1.app'` completed successfully. Full log:
`build/check-package-r1.log`. `codesign --verify --deep --strict --verbose=2`
passed for the bundle and its runtime. Executable SHA-256 values are recorded
in `build/desktop-r1-artifacts.json`. These are build/signature results, not
interaction acceptance.

The 4.9.8 caller-local foreground click also hung; its exact owned CLI process
was terminated, without stopping shared bridges. The original disposable
standalone preview was closed. The signed R1 bundle was opened using
LaunchServices on the same disposable volume: PID 98642, window 25352 (recheck
liveness). The latest full suite includes 39 Finder session checks and 75 file
checks. The goal remains active; the remaining work above is not complete.

The R1 real-window capture (`build/desktop-r1.png`, 14:07 CST) confirms the
Finder title overlap is fixed. A bounded foreground input attempt against
window 25352 through caller-local Peekaboo 4.9.8 exceeded 25 seconds and was
terminated. Menu and dialog behavior therefore has headless session-test
coverage only, not successful GUI input evidence. Preserve that distinction.

Next dependency-ready increment: persistent folder/window state, proper
pop-up tabs and multi-selection/drag behavior, then remaining menu capabilities
and another packaged checkpoint. Keep R1 running only on its disposable
volume; use a different bundle name for subsequent builds. Do not retry broad
or unverified GUI targets to evade the exact-window input failure.


## Second implementation increment

- Folder geometry, ordering, view mode, stacking and pop-up state persist in
  an atomic, validated volume-local state file. Closed desktop state persists.
- Real bottom-edge pop-up tabs expand upward, collapse on focus changes and
  detach when dragged. Window clamping preserves usable width and height.
- Shift selection, list ranges, keyboard range extension and marquee selection;
  selected groups remain selected while starting a drag.
- Folder transfers support moves and Option-drag copies, preserve resource
  sidecars, reject recursive self-copies and never overwrite destinations.
- Duplicate, aliases, Favorites, Trash and Put Away operate on selections.
  Batch Undo handles moves, Trash, copies and aliases. Undoing a created item
  moves it to Trash rather than permanently deleting it. Put Away itself does
  not yet create an inverse journal. A collision stops Undo with its remaining
  entries retained, so the operation can be retried safely.
- Open folder paths and cached layouts follow internal moves and renames.
  Help now describes the implemented selection, view and Undo behavior.

Focused checks before the final R2 full build: 40 interaction checks, 29 state
checks, 93 filesystem checks, 39 Finder command checks and 91 navigation checks
passed. Additional geometry/rename-state checks have since been added. Final
full build, packaging and GUI acceptance are not implied by these results.

## R2 verified checkpoint

`BUILDFLEET_LOCAL=1 make -j2 test all app
APP_BUNDLE='build/OS999 Desktop Loop R2.app'` passed; log:
`build/desktop-r2-package.log`. The first full run failed an appearance-test
cleanup assertion because it did not remove the newly persisted Finder state;
that test now verifies/removes the file and the complete rerun passed.
The current suites include 40 interaction, 35 state, 39 Finder command and
93 filesystem checks. AddressSanitizer and UndefinedBehaviorSanitizer runs of
both the filesystem and interaction suites passed with no reported errors;
log: `build/desktop-r2-sanitizers.log`.

`codesign --verify --deep --strict --verbose=2` passed for R2 and its runtime.
Exact source and executable hashes are in `build/desktop-r2-source.json` and
`build/desktop-r2-artifacts.json`. R2 has not been launched or visually accepted.
R1 was not replaced, the production volume was not used, and nothing has been
committed, pushed or integrated into the concurrent main checkout.

Next: finish hierarchical menu behavior and recent items; then printing and
application switching. Also retain follow-ups for alias target identity,
Undo identity checks when another process replaces a file, Put Away inverse
history, richer list columns and the real-window input verification blocker.
The goal remains active. These successful checks establish this increment,
not completion of the full desktop request.

## R3 hierarchical menus and recent items

Arrange, Label and Control Panels now open submenus. Pointer hover/click and
keyboard Right/Left, Up/Down, Return and Escape use the same hierarchy. Label
choices apply directly; mixed selections do not show an incorrect checkmark.
Control Panels offers its folder plus direct Appearance and Finder Preferences.

Recent Applications and Recent Documents record successful opens, deduplicate
paths, reopen items and clear independently. The bounded volume-local history
uses no-follow reads and atomic replacement; malformed, escaped and symlinked
paths are rejected. Clear Menu persists. Missing items are reported at open
time; painting menus does not repeatedly traverse the filesystem. Recent paths
still need the same rename/identity follow-up as aliases.

`BUILDFLEET_LOCAL=1 make -j2 test all app
APP_BUNDLE='build/OS999 Desktop Loop R3.app'` passed in
`build/desktop-r3-package-final.log`. The first full run exposed an old test
expecting a direct Control Panels command; it now exercises the actual submenu.
The submenu label test also caught a settings-dialog lifecycle error, fixed
before the final build. Finder desktop has 55 checks; real production menu
integration and navigation checks passed. Address/undefined sanitizer runs of
menu integration and Finder desktop passed (`build/desktop-r3-sanitizers.log`),
before the final Open Control Panels menu-entry adjustment. Signature verification
passed. Exact source and executable hashes: `build/desktop-r3-source.json` and
`build/desktop-r3-artifacts.json`.

R3 was launched on `build/desktop-r3-volume`: PID 35519, window 25503 (historical
IDs, revalidate). Real capture `build/desktop-r3.png`, 14:34 CST on 6 November,
shows the running desktop; it is not evidence of submenu interaction.

Peekaboo's foreground focus check exposed name ambiguity among multiple running
OS999 apps. A separately signed copy, `build/OS999 Menu Verification R3.app`,
uses unique display/name and bundle ID `com.iantrimble.os999.desktoploop.r3`.
Its PID 36500/window 25525 uses only `build/desktop-r3-ui-volume`. No unrelated
app was stopped. The unique identity cleared that focus ambiguity. Bridge
foreground clicks/keys subsequently reported dispatched but fresh captures and
the disposable filesystem did not show the intended menu/New Folder outcome.
Do not claim GUI input acceptance from these delivery receipts. Capture and
request records are retained under `build/desktop-r3-*.json/png`.

A current read of the main checkout found HEAD `751d7b7` and new cross-worktree
status documentation. The combined `macos9port-v1.1` release at `e66a061` is clean
and includes an earlier port of this desktop work plus browser, media, terminal
and networking. R2/R3 integration must compare that actual content and preserve
those features; copying this older-base desktop wholesale would regress them.
Main and release were read only during this increment.

Caller-local Peekaboo 4.9.8 also accepted an exact-window foreground click on
that uniquely identified preview. The subsequent real capture at 14:38 CST,
`build/desktop-r3-local-result.png`, still shows no opened View menu. Next input
diagnosis should use the existing `OSPI_INPUT_TRACE` instrumentation on an owned,
uniquely identified disposable preview rather than repeating unobserved clicks.
Normal printing/page setup and application switching remain implementation work;
Apple's AppKit printing documentation was located for the next increment, but no
printing implementation or printer acceptance is claimed in R3.


## R4 printing and application switching

- macOS Page Setup uses a working copy of print settings so cancellation does
  not replace active settings. Print opens the host panel for plain text below
  64 KiB; Print Window prepares the visible front window. Symlinks, binary data,
  oversized documents and invalid window bounds are rejected. Other platforms
  retain disabled printing until a host backend exists.
- The application menu names the foreground application and offers Finder,
  the current classic guest, Hide, Hide Others and Show All. Finder windows
  can be drawn above a background guest without terminating it. Hidden guests
  no longer intercept pointer input. Cmd-Tab has a desktop handler; the host
  may reserve that shortcut. General multi-guest operation is still absent.
- Production Finder tests now include 88 checks, including printing requests,
  limits, app focus/hide transitions and guest hit testing. Address/undefined
  sanitizers passed those checks (`build/desktop-r4-sanitizers.log`). These
  state tests are not acceptance of a running classic guest switch.
- `make test-print-pdf` compiled the current AppKit backend and passed 13
  checks. A 200-line document produced multiple pages with both endpoint lines
  retained in the parsed PDF; a window image produced one page; an invalid crop
  was rejected. No physical print job is sent by this test. Current log:
  `build/desktop-r4-native-pdf.log`.
- The complete local `make -j2 test all app` pass is recorded in
  `build/desktop-r4-package.log`; bundle signature verification passed. The
  unique R4 preview has a separate name/identifier and disposable volume.

### Real-window evidence and input diagnosis

The R3 trace showed a button at a previously tracked pointer position. SDL2's
Cocoa source uses its tracked mouse position for a focused-window button event:
https://raw.githubusercontent.com/libsdl-org/SDL/SDL2/src/video/cocoa/SDL_cocoawindow.m
Explicit `peekaboo move --profile linear --duration 300ms --steps 10` to the
exact window target before clicking resolved the tested coordinate mismatch.
Capture again between motion and click; do not trust dispatch alone. Unique
app names/IDs still avoid the separate Peekaboo focus ambiguity among previews.
Do not replace event-local desktop coordinates with global mouse polling.

R4 real captures on 6 November 2026:
- `build/desktop-r4-view-verified.png`, 14:58 CST: open View menu.
- `build/desktop-r4-arrange-verified.png`, 14:59 CST: nested Arrange submenu.
- `build/desktop-r4-print-hovered.png`, 15:00 CST: enabled Print Window action.
- `build/desktop-r4-print-panel.png`, about 15:01 CST: native Print dialog,
  one-page folder preview and No Printer Selected.
- `build/desktop-r4-print-cancelled.png`, 15:02 CST: desktop resumed after Cancel.

Cancel's AX click reported a final-identity error because its dialog closed.
The following inventory showed the Print window gone and the real desktop
capture confirmed the outcome. It was not retried. No printer was configured
or used. Page Setup and actual guest switching still need real-window checks.

The current owned preview is `build/OS999 Menu Verification R4.app`, PID 57656,
window 25644, volume `build/desktop-r4-ui-volume` (revalidate these IDs). Its
bundle ID is `com.iantrimble.os999.desktoploop.r4`; input trace is
`build/desktop-r4-input-trace.log`. Owned older previews 98642, 35519 and 51975
were individually identity/child checked and closed. Other apps and guest
processes were preserved. No game validation was resumed.

Printing API references opened and checked against the installed AppKit headers:
https://developer.apple.com/documentation/appkit/nspagelayout
https://developer.apple.com/documentation/appkit/nsprintoperation/init(view:printinfo:)

Final R4 build after the host-shortcut help caveat and trailing-whitespace tidy
passed (`build/desktop-r4-final-build.log`); deep/strict signature verification
passed again. Full tests and sanitizer evidence precede only those help/whitespace
edits. Current source/artifacts are bound by `desktop-r4-source.json` and
`desktop-r4-artifacts.json`; the independently named running verification copy
retains its prior source manifest and own Info.plist/executable hashes in
`desktop-r4-ui-source.json` and `desktop-r4-ui-artifacts.json`, all under build/.
No commit, push, normal-volume modification or release integration occurred.


## R5: identity-checked Undo and Recent Items relocation

Renames now use the same batch journal as drag moves, copies, aliases and Trash.
The obsolete single-item Undo fields and unreachable duplicate command handlers
were removed. Journal entries capture the device, inode and kind of the primary
item and the presence/device/inode of its resource sidecar using no-follow,
directory-relative lookup. Undo refuses a missing/replaced item or sidecar,
retains the remaining journal and tells the user why nothing was changed.
Content edits to the same object are not treated as replacement. These checks
are not an atomic guarantee against a hostile process racing the subsequent
rename, nor do they detect inode reuse after deletion.

Put Away now records an inverse: Undo returns the restored item to Trash with
fresh origin metadata, so a later Put Away remains possible. Trash may allocate
a collision-free name. This supersedes the earlier missing-inverse limitation.

Recent Items now follows internal leaf/folder rename, move, Trash and Undo
through the same path-retarget hook as folder windows. Updated paths are saved
immediately and survive session recreation. External moves and identity-based
alias/recent resolution remain unfinished; path prefixes are matched only at
component boundaries.

Production-session regression checks cover replacement data files, replacement
resource forks, symlink replacement during Rename Undo, restoring the original
object and retrying Undo, Put Away/Undo/Put Away, and recent document reopening
after a containing-folder rename and session recreation. The first new Recent
Items reopen test reused an intentionally invalid resource-fork fixture and
failed because the runtime rejected it; the corrected test uses the separate
plain-text fixture. The first failed logs are retained.

R5 focused checks: Finder interaction 78/0, Finder desktop 88/0. ASan/UBSan
interaction checks passed 78/0 (`build/desktop-r5-sanitizers-final.log`).
The new `build/OS999 Desktop Loop R5.app` passes deep/strict signature checking.
It was not launched or GUI-tested; the R4 owned preview remains untouched.
No commits, pushes, release integration or normal-volume changes occurred.

The final full `BUILDFLEET_LOCAL=1 make -j2 test all app
APP_BUNDLE='build/OS999 Desktop Loop R5.app'` completed successfully
(`build/desktop-r5-package-final.log`). Source hashes for all tracked/untracked
source, headers and tests plus Makefile are in `build/desktop-r5-source.json`;
bundled executable hashes are in `build/desktop-r5-artifacts.json`. The initial
build emitted the existing runtime section-alignment linker warning; the final
incremental log has no warnings. No new GUI acceptance is inferred from tests.


## R6: aliases follow renamed and moved originals

Alias handling is now isolated in `src/finder_alias.c`. Newly created aliases
use an OS999-ALIAS-2 record with the original path and filesystem identity
(device/inode; macOS also records creation seconds/nanoseconds). Open and Show
Original first check the saved location, then search inside the volume for that
identity. A replacement with the old filename is not substituted for the
original. Renames, containing-folder renames, moves and Trash/Put Away are
covered, including changes made through the file backend without session
retargeting. No Apple Alias Manager format or implementation is used.

Directory traversal never follows symbolic links and is bounded at 16,384
entries and 32 directory levels. Searches happen only for explicit resolution;
folder reloads read the alias record without searching. A missing original still
shows as an alias and Open reports an alias error, rather than displaying the
record as text. Numeric identity fields reject overflow and malformed data.

Limitations: legacy OS999-ALIAS-1 records retain their path-only behavior; new
aliases do not track copies to another filesystem, volumes whose device identity
changes, or originals outside the bounded search. Non-macOS identity currently
lacks creation time, so inode reuse is not guarded there; Linux execution was
not performed. There is no atomic protection against concurrent hostile path
replacement between resolution and opening. This is OS999 desktop behavior,
not complete classic Alias Manager compatibility.

File tests and ASan/UBSan checks pass 140/0, including moved originals,
replacement old paths, parent-folder renames, Trash/Put Away, broken/legacy
aliases, numeric overflow, malformed paths and symlink escape rejection.
`build/desktop-r6-sanitizers.log` retains the sanitizer result. Production-session
Open/Show Original and broken-alias checks were added to the desktop test.
No new GUI input, game validation, release integration or publication occurred.

Final complete local test/build/package run passed after the search-count bound
and desktop test changes (`build/desktop-r6-package-final.log`), including 100/0
desktop checks and 140/0 file checks. R6 deep/strict bundle signature verification
passed. Final source and executable hashes are in `build/desktop-r6-source.json`
and `build/desktop-r6-artifacts.json`. The R6 bundle was not launched or visually
accepted; the running R4 preview was not changed.

Integration reconnaissance: the clean release checkout is still e66a061; its
e0530b6 desktop port carried only 72 added/20 removed lines in 12 files, so it
is a partial earlier port, not the full loop implementation. Main has advanced
to 8512c62 in another session. Preserve both checkouts. The next integration
must use an isolated worktree and retain browser/media/terminal/networking and
newer runtime work; no integration change has been made yet.


## R7: Finder list columns and persistent layout

List view now displays Name, Kind, Size and Date Modified. Clicking a header
selects its ordering; clicking it again reverses it, with an arrow showing the
direction. Divider drags resize neighboring columns within minimum bounds.
Reset Column Positions resets the widths without changing ordering and is
available only in list view. Widths scale with window size; very narrow columns
clip text instead of painting over neighboring values. Size currently reports
the data fork, not the combined data/resource-fork allocation.

OS999-FINDER-3 persists divider positions and reverse ordering per folder.
Version-2 state remains readable. Invalid/overflowed saved positions are
rejected. Closing/reopening and session recreation preserve these preferences.
The initial full run exposed an old integration test whose far-right header
coordinate assumed Kind; its click now targets the actual Kind column. The
failed initial log is retained, and the corrected complete run passed.

Validation: `build/desktop-r7-package-final.log` records the full direct local
`BUILDFLEET_LOCAL=1 make -j2 test all app` pass, with folder view 51/0, Finder
state 41/0 and Finder desktop 100/0. `build/desktop-r7-sanitizers.log` records
41/0 state checks under ASan/UBSan, including production input drag and reset,
persistence, version-2 compatibility and malformed version-3 positions.

Real-window verification uses a separately named/signed copy,
`build/OS999 List Verification R7.app`, PID 86118/window 26124 at capture time,
and only `build/desktop-r7-ui-volume`. The volume was preconfigured to open
Documents in list view and contains disposable text fixtures. The original
R7 package and prior R4 preview were not overwritten while running. Peekaboo's
pinned GUI bridge reported all three required capture/input grants live.

- `build/desktop-r7-list.png`: all four columns visible.
  *Documents list in the real R7 window, captured 6 November 2026 at 15:22 CST.*
- `build/desktop-r7-size-sorted.png`: largest document moved to the first row.
  *Real Size-header click result, captured 6 November 2026 at 15:22 CST.*
- `build/desktop-r7-resized.png`: Name column widened with size sorting retained.
  *Real divider-drag result, captured 6 November 2026 at 15:23 CST.*

No Linux execution, browser/media/terminal integration, game acceptance,
commit or push occurred. Remaining desktop acceptance remains open.

The real View → Reset Column Positions action also restored the default widths
while retaining Size ordering (`build/desktop-r7-reset.png`).
*Documents after the real menu reset, captured 6 November 2026 at 15:25 CST.*
Deep/strict signature checks passed for both the original R7 package and unique
verification copy. `build/desktop-r7-source.json` binds the source; executable
and Info.plist hashes for both bundles are in `build/desktop-r7-artifacts.json`.
The R7 preview remains open with the disposable volume; revalidate its PID and
window ID before further input. The column GUI evidence does not prove the
remaining desktop interactions or integration gates.


## R8: isolated integration with the release desktop

Work now continues in `/Users/ian/projects/macos9port-desktop-integrated`.
The original desktop-loop checkout remains the verified R7 source. Release
checkout e66a061 and main checkout 8512c62 were only read. The new detached
worktree started from e66a061, merged main 8512c62 with `--no-commit`, then
three-way imported the R7 desktop sources against common base 68f61ff. It has
an intentionally pending, uncommitted merge; do not run merge --continue or
commit without Ian's request. All textual conflicts have been resolved.
`build/desktop-integration-import/` retains before/base/incoming files and its
61-file import report. No original checkout or running bundle was changed.

Integration preserves terminal, browser, audio player, movie, storage and TLS
sources plus newer main runtime/input tracing fixes. Media command IDs moved to
700/701 to avoid the Finder settings command. Terminal modifier bits remain
compatible; Finder Shift/Option use their matching bits. Movie/audio opening
was moved into the extracted Finder opener. Close All/Restart close terminal
and movie resources before removing windows. Trash drop sounds are retained.
The oversized combined Makefile was split into `config/desktop-tests.mk`; the
checklist's historical baseline was moved intact to a linked file to keep
edited files below 500 lines.

The initial combined Finder test passed 100/0. Terminal/menu tests exposed a
shared test-volume fixture now restoring prior Finder state, and obsolete fixed
menu positions. Fresh terminal fixtures and command-based menu assertions fix
those assumptions; terminal now passes 118 checks and menu integration passes.
The combined desktop executable builds. Two full-suite reruns exposed obsolete
modifier bits and fixed Sound menu indices in older tests; both were corrected.
The full combined `make -j2 test all app` passed in
`build/desktop-r8-package-final2.log`, including the browser menu compositor.

Remaining integration gates include browser/media application-menu registration,
hiding/focus/render ordering, media-hook acceptance, GUI input and packaging of
the Chromium helper. Finder/guest application switching alone does not cover
those applications. No games were run or accepted. Installed dependencies in
the combined build include FFmpeg 9.0.2_1 and Homebrew curl 8.23.0, matching
current Homebrew formula versions; no dependency upgrade was required.

R1–R7 artifact paths above refer to the original
`/Users/ian/projects/macos9port-desktop-loop/build/`, not this new worktree.
R8 artifacts live under this integration worktree's build directory.

R8 browser composition now saves parent and child menus independently, using
full-frame backup capacity and matching GPU cutouts. The macOS overlay now
subtracts the union of cutouts through disjoint visible regions; overlapping
parent/child or window/menu holes no longer cancel under even-odd masking.
The compositor test uses the real frame function, real menu geometry, and a
stub browser surface. It checks every pixel for seven menu/fullscreen cases,
plus overlapping, duplicate, clipped, and empty holes. It is not Chromium or
GPU visual acceptance. Packaging passed (`desktop-r8-compositor-final.log`),
strict deep signature verification passed, and the final full suite passed
(`desktop-r8-tests-final.log`). Source/artifact hashes are in
`desktop-r8-source.json` and `desktop-r8-artifacts.json` under build/.
Initial ASan/UBSan run stalled in SDL's constructor error dialog before main
(sample retained); only that test process was terminated. The headless harness
now stubs unused SDL functions and omits SDL linking. Normal and ASan/UBSan
reruns pass; `desktop-r8-compositor-sanitizer-final.log` has zero failures.
The app is unchanged by this harness adjustment; no build/test remains running.


R8 remaining issues (continued in [R9 integration](DESKTOP-INTEGRATION-2026-11-06.md)):
browser chrome/CPU frames can cover modal/Appearance/media
panels because overlay painting precedes browser painting; browser input guards
only menus/dialogs. Browser/media registration and complete cross-app stacking
remain unfinished. The current browser `hidden` argument only hides its GPU
layer, not CPU/chrome. No integrated R8 GUI acceptance has been claimed.
