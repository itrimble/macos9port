# OS999 desktop integration continuation

## Scope and ownership

Continue the final Mac OS 9-style desktop/menu goal in
`/Users/ian/projects/macos9port-desktop-integrated`. This detached worktree
retains a deliberately uncommitted merge of release e66a061, main 8512c62,
and the R7 desktop work. Do not complete the merge or commit without Ian's
request. Preserve the original checkouts, running previews and production data.
The previous record is [R1–R8 desktop loop](DESKTOP-LOOP-2026-11-06.md).

## R9: desktop overlays above the browser

The browser compositor preserves independent rectangles for menus, dialogs,
Appearance and its contextual menu, desktop contextual menus, and media panels.
The same rectangles exclude browser GPU content, with disjoint visible regions
preventing overlapping cutouts from cancelling each other. A menu bar remains
visible when an overlay is active over fullscreen browsing. Hidden browser
frames now skip CPU/chrome painting as well as hiding the GPU layer.

Appearance and contextual menus expose their actual layout bounds. The media
hook interface gains an optional bounds callback; OS999Amp supplies its current
layout, including shaded and expanded states. Dialog bounds use the existing
shared layout function. This does not create application-menu registration.

Browser input yields to all desktop overlays, releases page focus and clears
mouse capture/drag and IME composition state. It remembers prior keyboard focus
and restores it on
the next event after the overlay closes. The headless regression links the real
browser event handler and compositor with inert SDL/Chromium transport stubs.
It checks exact occluder locations, every framebuffer pixel, focus loss/restore,
input suppression and hidden-frame preservation. Audio integration also checks
the real media hook's bounds on open/close.

Validation completed: direct local `make -j2 -W src/browser_input.c test all app`
passed (exec session 91615 terminal exit 0), producing
`build/OS999 Desktop Integrated R9.app`. Log: `build/desktop-r9-package.log`.
The expanded compositor/event harness passed ASan/UBSan with zero failures
(`build/desktop-r9-overlay-sanitizer.log`, session 29424 terminal exit 0).
Strict deep signature verification passed for R9. Source/artifact hashes are
`build/desktop-r9-source.json` and `build/desktop-r9-artifacts.json`; source
hashes were compared again after validation and matched. No test/build remains
running. The existing runtime section-alignment linker warning remains.

A standalone helper exists at the main checkout's
`build/browser/Release/OS999Browser.app`, but deep signature verification fails.
It has dangling framework links `Versions/A/A -> A`,
`Versions/A/Resources/Resources -> Versions/A/Resources`, and
`Versions/A/Libraries/Libraries -> Versions/A/Libraries` under the embedded
Chromium framework. Its executable hash/metadata and signature output are in
`build/desktop-r9-existing-browser.json` and the matching `-signature.log`.
It was only inspected, never launched, copied, repaired or packaged into R9.
Do not mistake its presence or empty version metadata for browser acceptance.

## R10: OS999Amp application-menu participation

OS999Amp registers while its player window is open. The right application menu
names/checks the active player and supports Finder/guest/player selection,
Hide, Hide Others, and Show All. Hidden and background states are distinct:
background media paints beneath Finder windows; hidden media has no window or
input target. Opening Control Panels/Appearance backgrounds the player instead
of closing it. Close Window/Close All route to a focused player rather than
closing a Finder window behind it. Hiding does not stop or recreate playback.

Focused validation passed: Finder desktop 112 checks, browser compositor zero
failures, and the real audio integration/format suite including live player
identity/playback checks across Hide. The Finder test also checks actual
framebuffer stacking with a deterministic media surface, guest arrival/exit,
and hidden state surviving synchronization. Its ASan/UBSan run passed 112/0
(`desktop-r10-apps-sanitizer.log`). This is not real-window acceptance.
The first full pass stopped on missing media-query stubs in two isolated menu
tests (session 58291 terminal exit 2). Those fixtures now supply the stub.
The full local build/package/test rerun passed (session 72308 terminal exit 0),
log `build/desktop-r10-package-final.log`. The unique packaged R10 app passed
strict deep signature verification. Source/artifact hashes are in
`build/desktop-r10-source.json` and `build/desktop-r10-artifacts.json`.

Real-window verification used a uniquely named/signed copy,
`build/OS999 Applications R10.app`, and disposable `build/desktop-r10-ui-volume`.
It opened an original silent two-second WAV fixture; this is not listening or
long-session playback evidence. Initial direct executable launch captured but
Peekaboo refused focus/input. Only that owned process (30362) was terminated;
Launch Services relaunch resolved targeting. Current preview PID 31790, window
26188, bounds (640,209,1024,800); re-inventory before further input.
The player is now behind the Finder window, no menu open. Do not replace this
running bundle. R4/R7 previews and production data were left untouched.

*`build/desktop-r10-active.png`: OS999Amp active in the real R10 window,
6 November 2026, 16:06 CST.*

*`build/desktop-r10-menu.png`: real application menu with OS999Amp checked,
Finder available, and Hide/Hide Others enabled, 16:07 CST.*

*`build/desktop-r10-finder-front.png`: after selecting Finder through that menu,
the Finder window is in front and OS999Amp remains visible behind it, 16:08 CST.*

Companion JSON files retain exact-window receipts and snapshots. This verifies
menu opening and Finder selection/stacking. Hide/show, guest switching, browser
ordering, and other media controls are not yet real-window accepted. Peekaboo's
pinned GUI bridge/local runtime grants were checked at 16:00 CST. The first
observation wrapper mishandled a null error payload; it was corrected, and no
refused action was treated as successful input. No build or test remains running.

Remaining interaction limitations include media's foreground modal input model,
clicking an exposed background player beside a guest, restoring focus with an
Appearance panel still open, and persistent application identity after closing
the player window while playback continues. Browser registration/full stacking
remain separate unfinished work; do not declare a complete application switcher.

## R11: close, reopen and quit the player

The media hooks now separate window close, application activation, and quit.
Closing a player window retains its registered application and audio player;
selecting OS999Amp reopens that same window state/player without rescanning the
playlist or restarting playback. Hide remains distinct from close. File > Quit
changes to Quit OS999Amp while it is focused; Cmd-Q invokes the player shutdown
callback and leaves the desktop running. Session destruction also quits the
owned player before freeing its Session, preventing a retained owner pointer.

Appearance/Control Panels stay open behind a focused player and stop consuming
its input. Selecting Finder restores their input; opening the player no longer
closes them. This addresses the R10 media/Appearance focus limitation, but does
not resolve guest/browser layering or the foreground media modal input model.

Media application tests were extracted into `tests/test_media_app.c`, linked
into the existing audio test binary, to keep edited files under 500 lines.
Focused tests passed: Appearance 121/0 and the real audio suite, including
playing-player identity through close/reopen, Cmd-Q shutdown, desktop survival,
relaunch after quit, teardown while the player window is closed, and panel
focus restoration. Logs: `desktop-r11-focused-build.log` and
`desktop-r11-focused-tests.log`. R11 sanitizer/GUI acceptance is not yet claimed.

Initial R11 packaging and full tests passed (sessions 54374 and 60735 terminal
exit 0; logs `desktop-r11-package.log` and `desktop-r11-tests.log`).
`build/desktop-r11-source.json` records the exact dirty source snapshot.

### Native keyboard failure and R11B correction

Real R11 preview PID 40353/window 26212 launched on disposable
`build/desktop-r11-ui-volume`. A real Cmd-W exited the entire host process;
subsequent window inventory and process inspection confirmed it was gone.
Do not retry input against that window or report lifecycle GUI acceptance.
The initial press with no-auto-focus was refused without dispatch; removing
that contradictory flag allowed the chord and exposed the host-close failure.

The [SDL Cocoa event/menu source](https://github.com/libsdl-org/SDL/blob/main/src/video/cocoa/SDL_cocoaevents.m)
was opened on 6 November: its native menu assigns Cmd-W to performClose and
Cmd-Q to terminate. The desktop now clears only those conflicting native menu
key equivalents after SDL window creation, leaving mouse-selectable host menu
actions and unrelated shortcuts intact. Linux uses a no-op platform hook.
The native AppKit menu regression passed 7/0, including nested menus, retained
actions, unrelated shortcuts, nil menus and idempotence. Source lives in
`desktop_shortcuts_macos.m`, `_stub.c`, and `desktop_shortcuts.h`.

R11B packaging and the full suite passed (sessions 24653 and 64981, exit 0;
logs `build/desktop-r11b-package.log` and `build/desktop-r11b-tests.log`). Both
`OS999 Desktop Integrated R11B.app` and the unique `OS999 Lifecycle R11B.app`
passed deep, strict signature verification. `desktop-r11b-artifacts.json`
records their file hashes; all files in `desktop-r11b-source.json` matched
the tested source when rechecked at 16:25 CST on 6 November.

The lifecycle preview is PID 47528/window 26236, using disposable
`build/desktop-r11b-ui-volume`. Foreground keyboard and mouse setup could not
verify exact window focus. Background Cmd-W also returned indeterminate
delivery. Fresh captures after each keyboard attempt showed the player still
open and the host alive; this is not a successful close or keyboard regression
pass. Preserve the receipts and `desktop-r11b-close-background-observed.png`.
Real Cmd-W/Cmd-Q acceptance remains outstanding. Do not retry uncertain input
without a fresh observation, or close the native window as a substitute for
testing the internal player lifecycle. R10 preview remains intact.

## R12: browser application identity and Finder stacking

Browser now joins the application menu with active-app naming and a checkmark.
Hide, Hide Others and Show All include it. File Close/Close All address browser
windows while Browser is active; Quit Browser stops its helper and windows
without quitting the desktop. Closing all browser windows retains application
identity; selecting Browser opens a new blank window. Switching to Finder
releases browser keyboard focus. Foreground Finder window rectangles are
preserved in CPU composition and excluded from the browser GPU layer, and
browser hit testing yields to those Finder windows.

The compositor/event regression now exercises application hiding, switching,
close/reopen/quit, retained Finder windows, pixel preservation and overlapping
Finder/browser input. It passed with zero failures; the desktop binary builds.
Exact source snapshot: `build/desktop-r12-source.json`. Initial focused Finder
regression also passed 112/0. Full validation passed as exec session 88300,
exit 0, with log `build/desktop-r12-tests.log`. After that checkpoint, Cmd-W
was corrected to close an empty browser window when no helper tab is connected;
the application entry persists. The final build/regression passed (session 60340,
exit 0),
logs `desktop-r12-final-build.log` and `desktop-r12-final-tests.log`, and source
is recorded separately in `desktop-r12-final-source.json`.
No R12 bundle or GUI acceptance is claimed yet.

Follow-ups still include Appearance/media/guest application ordering, browser
menu command scope, and real keyboard/input acceptance.
The isolated regression uses inert transport, so it proves local routing and
composition, not Chromium focus, helper health or rendered web content.

## R13: application-scoped menu dispatch

A shared command-scope check now applies to menu enablement and Session command
dispatch. Finder file, selection, print, Trash, list and label actions cannot
operate on background Finder windows while Browser, OS999Amp or the guest is
focused. Global launch and application switching remain available. Browser's
New Folder entry becomes New Window and creates a browser window. Its Copy,
Paste, Select All and Undo menu actions route to Chromium key messages with
explicit Command modifiers, or to the address editor when it owns input.
The address editor supports reversible text edits through Undo, including
paste, typing and deletion; Copy requires its text selection.

Menu integration and compositor tests passed before the final address-Undo
addition. Final targeted validation passed (exec session 17759, exit 0), with
`desktop-r13-validated-build.log` and `desktop-r13-validated-tests.log`: desktop
build, menu integration, browser routing/composition, Finder and real audio.
`desktop-r13-source.json` binds the source. The tests check disabled commands,
unchanged background Finder window/selection state, application-specific menu
labels, browser window creation, Command-modified page events, address-field
copy/paste/selection, and Undo/redo. No live Chromium clipboard/editing or R13
GUI acceptance is claimed. The complete suite last passed at R12; do not label
that older receipt as full R13 validation.

Menu follow-ups include application-specific Open/Print/Find, accurate empty-
window edit enablement, guest-owned menu actions, and modal-dialog Quit behavior.
These and the stacking/GUI work below still prevent completion of the goal.

## R14: modal Quit and browser composition cancellation

Cmd-Q in a modal Session dialog no longer bypasses the disabled application
menus and exits the whole desktop. Lower- and uppercase Q retain the pending
dialog and command; Escape cancels the choice normally. Regression coverage
includes Finder, Browser and OS999Amp focus.

Browser interruption now sends OS999B_IME_CANCEL to the active tab before
clearing composition/capture state, both for desktop overlays and application
hide/switch. The existing overlay regression checks one cancellation and no
repeat on subsequent blocked events. The application-hide case also checks
cancellation. This closes the previous local-state-only implementation gap;
real input-method behavior inside Chromium still needs GUI verification.

Source is bound by `build/desktop-r14-source.json`. Build and focused menu/browser
validation passed as exec session 59571, exit 0, logs `desktop-r14-build.log`
and `desktop-r14-tests.log`. Full tests and separate R14 packaging passed as
exec session 49759, exit 0, log `desktop-r14-full-package.log`. The real-storage
fixture test skipped because OS999_STORAGE_FIXTURES is unset.
`build/OS999 Desktop Integrated R14.app` passed deep, strict signature
verification. Source hashes matched after validation; bundle hashes and receipts
are recorded in `build/desktop-r14-validation.json`. No build/test remains
running. GUI acceptance remains unverified.

GUI bridge permissions were refreshed and granted for capture, accessibility
and event synthesis. The old R11B PID 47528 is no longer present in `ps`; its
window handle must not be reused. This later observation does not establish
when or why it exited, and does not resolve the earlier Cmd-W GUI uncertainty.

## R14 GUI preview and shared-desktop interference

Created `build/OS999 Desktop Preview R14.app`, a copy with unique bundle ID
`com.iantrimble.os999.desktoploop.r14`, and verified its deep/strict signature.
Launched it with disposable `build/desktop-r14-ui-volume` and an original silent
two-second WAV. Observed PID 69755/window 26474, bounds (640, 204, 1024, 800).
Revalidate all identifiers before later input; preserve this preview and volume.

`desktop-r14-player.png` shows the player File menu: Finder-specific actions
are disabled and Quit reads Quit OS999Amp.
*Captured from the actual R14 preview window on 6 November 2026 at 16:40 CST.*

Cmd-W returned indeterminate focus setup, not verified keyboard delivery.
Subsequent capture showed a Terminal window in front of the player. Background
menu clicking found no pressable AX element; foreground synth clicking refused
because iTerm2 was frontmost. Explicit application switching later reported
confirmed activation, but the following capture showed additional window moves,
an opened Applications folder and terminal command output that these checks did
not request. Do not attribute those changes to a tested action or call this a
lifecycle pass. Input stopped to avoid interfering with other shared-desktop
activity; no running process was terminated and no unrelated window minimized.

Latest observation: `desktop-r14-activated.png`, 16:45 CST; snapshot
`ps1_f56118a42cc64276fc4ec5c635f1e893`. All tool receipts use the
`desktop-r14-` prefix in ignored build output. No CUA or game validation was
resumed. Source/build receipts from the previous section remain unchanged.

## R15: Appearance, browser and guest foreground ownership

Appearance stops consuming input when Browser or the classic guest is active,
and when Finder is hidden. Deactivation dismisses its contextual menu but keeps
the panel state. Finder activation restores panel input; Hide Finder suppresses
its painting and bounds as well. Browser composition no longer preserves the
panel above a foreground browser. Background Appearance paints with Finder's
windows, preventing the final overlay pass from repainting it over a guest.

The main loop publishes actual guest window bounds. Browser CPU/GPU composition
preserves a foreground guest rectangle, and browser hit testing yields there.
Clicks on exposed browser/guest windows publish application focus requests so
the next synchronization does not restore stale focus from the previous app.
Tests use synthetic pixels and transport; no guest executable or game ran.

Initial validation passed: Appearance 144/0, browser composition 0 failures,
Finder 112/0, real audio suite and desktop build. The final paint-order change
adds a real host paint-pipeline regression for retained Appearance beneath a
synthetic guest and restoration above it when Finder is selected. Final build
and targeted guest/browser/Appearance/audio tests passed (exec session 86967,
exit 0),
logs `desktop-r15-final-build.log` and `desktop-r15-final-tests.log`. No build/test remains running. `desktop-r15-source.json` records the source.
Main and paint files are each 499 lines: split future additions. No R15 GUI or
package acceptance is claimed; the R14 preview remains untouched.

## R16: browser menu availability without a window

Browser publishes its window count and local editor capabilities after input
and frame updates. Close/Close All and Edit commands are disabled with no
browser windows; the application remains registered and New Window/Quit stay
available. A disconnected window can be closed but has no page-edit commands.
Its local empty address field permits Paste, while Select All requires text,
Copy requires selected text, and Undo requires a saved edit. The shared command
guard also rejects direct dispatch of unavailable actions, preserving the
background Finder window and clipboard.

Connected-page selection and undo are still owned by Chromium; these menu
commands remain available for dispatch when a tab/connection exists. This does
not claim an exact CEF selection/undo capability query or live clipboard proof.

Menu integration, browser composition/routing and desktop build passed (exec
session 35913, exit 0), logs `desktop-r16-build.log` and `desktop-r16-tests.log`.
Regressions cover empty-window shortcuts, rejected direct commands, disconnected
window Close, local empty-field editing, and retained application registration.
`desktop-r16-source.json` and `desktop-r16-validation.json` bind source/binary
and results. No R16 full-suite, package or GUI acceptance is claimed. The R14
shared preview was not touched.

## R17: guest Close/Quit ownership and atomic shortcut delivery

When the classic guest is focused, File Close and Quit route through a bound
native-runtime command callback. Quit is labeled with the guest application
name. The host does not close Finder windows, quit itself or force-close the
runtime; the guest receives Command-W or Command-Q and retains responsibility
for dialogs, saving and exit. Close All is disabled because this bridge does
not expose a guest window inventory. Without a command callback, Close/Quit
are disabled rather than falling back to host actions.

The bridge enqueues key down/up together under its existing bounded lock.
Insufficient capacity queues neither event, preventing a stranded Command key;
lock failure and non-ready guests report Application Busy. The SDL2 modifier
wire value is checked against the installed SDL headers in the key test.

Focused bridge, key mapping, menu integration and desktop build passed (exec
session 39928, exit 0), logs `desktop-r17-build.log` and `desktop-r17-tests.log`.
Tests cover blocked/dead peers, a nearly full input ring, both shortcut edges,
menu labels, unchanged host windows, desktop survival, and unavailable callback
behavior. These are delivery/routing checks, not proof that a real guest saves
or quits. No guest executable/game or shared preview was used.

`desktop-r17-source.json` binds the source. Full tests and separate R17 packaging
passed (exec session 15481, exit 0), log `desktop-r17-full-package.log`. Real
storage fixtures skipped because OS999_STORAGE_FIXTURES is unset. The separate
`build/OS999 Desktop Integrated R17.app` passed deep, strict signature checking.
Source hashes still match; `desktop-r17-validation.json` records bundle hashes
and receipts. No build/test remains running. GUI/guest acceptance is unverified.
A further native-key audit found Cmd-M still assigned to host Minimize while
Finder uses it for Make Alias; resolve that conflict in the next source pass.

## R18: Make Alias versus host Minimize

The native shortcut adapter now removes the Command-M key equivalent from
performMiniaturize menu items, alongside the existing host Close/Quit conflicts.
It retains the clickable native action, unrelated Hide/Copy equivalents and
non-Command combinations. The prior R11 test intentionally retained Minimize;
that was incorrect because Finder already uses Cmd-M for Make Alias.

Native AppKit menu tests passed 9/0, including uppercase key equivalence,
nested menus, retained actions and unrelated modifiers. Menu integration also
creates an actual OS999 alias via session_key Cmd-M. Desktop build and tests
passed (exec session 47130, exit 0; `desktop-r18-build.log`, `desktop-r18-tests.log`).
Separate packaging passed (session 98361, exit 0; `desktop-r18-package.log`),
and `build/OS999 Desktop Integrated R18.app` passed deep/strict signature checks.
Source hashes matched after validation; `desktop-r18-source.json` and
`desktop-r18-validation.json` record source and bundle identities. No build/test
is running. Full suite last passed at R17; native Cmd-M GUI delivery remains
unverified and the shared R14 preview was not touched.

## R19: Refresh open Finder folders

Idle desktop ticks now check open folder metadata and listed files/resource
sidecars once per second. External additions, removals and size/date changes
reload the folder while retaining selection, keyboard anchors, view and layout.
Refresh defers during mouse gestures and overlays. Missing/renamed folders close
without discarding their saved preferences; a disconnected volume retains its
windows. Search results remain explicit snapshots. The current 48-item ceiling
and path-based listing reads still need a separate pass; this is not an atomic
filesystem snapshot or hostile-race containment claim.

Focused refresh (36), saved-state (41) and interaction (78) checks passed with
zero failures, plus desktop build. Full tests and separate R19 packaging passed
(exec session 62559, exit 0; `desktop-r19-full-package.log`). Real storage fixtures
skipped because OS999_STORAGE_FIXTURES is unset. The R19 bundle passed deep/strict
signature verification, and source hashes still match. Source and bundle receipts
are `desktop-r19-source.json` and `desktop-r19-validation.json`. No GUI input or
real guest execution occurred; the shared R14 preview remains untouched.

## R20: Descriptor-based folder listings

Folder listings, file metadata and adjacent application-resource reads now use
an owned directory descriptor. A shared no-follow component walk serves listing,
refresh and Finder mutations. Icon decoding opens sidecars without following
links and validates the opened file; FIFO/device/directory sidecars are rejected.
Tests retain an open folder across rename and reject root/intermediate symlinks.
This does not make the directory contents an atomic filesystem snapshot, and
other file-read paths still need their own audit.

Reload now builds a temporary listing and only replaces the visible contents on
success. Read/allocation failure retains selection and anchors and leaves the
refresh stamp invalid for retry. Directory enumeration reports read errors.
Focused icon tests, 140 Finder file checks and 39 refresh checks passed; desktop
build passed (session 79570, exit 0). Logs: `desktop-r20-build.log` and
`desktop-r20-tests.log`. `desktop-r20-source.json` records the source snapshot.
Full tests/package passed (exec session 76062, exit 0), log
`desktop-r20-full-package.log`. Real storage fixtures skipped (unset fixture
variable). Source hashes match, and the separate R20 bundle passed deep/strict
signature verification; `desktop-r20-validation.json` records bundle hashes.
No R20 GUI validation occurred; shared previews and games remain untouched.

Next capacity pass: the 48-item ceiling spans Window item storage, selection
snapshots and undo journals (105 references across 22 source/header files in
`build/desktop-capacity-audit.txt`). Expand them together; preserve ownership
through window moves/closure/destruction and test bulk actions past 48 items.

## R21: Larger folders and bulk Undo

Ordinary folder listings now grow from the inline 48 entries to owned storage,
up to 32,768 entries. The info line shows a plus sign when the listing is partial
because of that bound or an unsupported long filename. Item rendering, menus,
keyboard navigation, drag selection and actions share the storage accessor.
Refresh preserves selected names with sorted lookup and only swaps the listing
after successful allocation/read. Closing, reordering and destroying windows
retain/free the correct owner; failed initial folder loads now return failure.
Trash confirmation counts beyond the former 48-entry ceiling.

Bulk actions reserve journal capacity before changing files; extended selection
snapshots and Undo cover the entire displayed selection. The 137-file fixture
checks range selection, scrolling, selection retention, reverse sorting, Trash,
Undo, duplication, reordering, closure and partial-list indication. All 170
checks pass, including AddressSanitizer/UndefinedBehaviorSanitizer (session
56881, exit 0). Focused refresh 39, interaction 78, folder view 51 and desktop
command tests pass; build session 7682 exited 0. Logs: `desktop-r21-build.log`,
`desktop-r21-tests.log`, `desktop-r21-sanitizer.log`. Source snapshot:
`desktop-r21-source.json`. This tests 137 files, not performance at the upper bound.

Full suite and separate R21 package passed (exec session 47596, exit 0), log
`desktop-r21-full-package.log`. Real storage fixtures skipped (unset fixture
variable); source hashes match and deep/strict bundle signature checking passed.
`desktop-r21-validation.json` records bundle hashes. GUI remains unverified.
Find results still have
their separate 48-result bound, and Copy Names retains its bounded clipboard
buffer; address these in the next capacity pass. The 96-byte name storage and
full application/GUI acceptance remain unfinished.

## R22 continuation

Larger Find results, complete Copy Names and clipboard viewing continue in
[desktop capacity continuation](DESKTOP-CAPACITY-2026-11-06.md). Focused and
sanitizer tests and full R22 suite/package pass. No GUI
acceptance or goal completion is claimed.

## Remaining work

- Finish application lifecycle, hide/show semantics and
  cross-application focus/stacking, including Finder and the classic guest.
- Verify real integrated GUI interactions, including overlay placement, media
  controls, browser helper packaging, and returning keyboard focus.
- Verify real IME cancellation when an overlay interrupts browser input; the
  headless regression now proves cancellation-message dispatch, not guest IME behavior.
- Continue the broader desktop acceptance audit; R1–R8 tests and captures do not
  establish complete OS 9-style desktop behavior or release readiness.
- No games were resumed, no installation/publication occurred, and no commits
  or pushes were made. Local package/test success is not GUI acceptance.

## Desktop files and icon placement (R34–R37)

A separate desktop model backs background files without consuming a window slot.
It shares Finder selection, rename, labels, duplicate, Trash/Undo, text printing,
keyboard navigation, context menus and file transfers. The backing `Desktop Folder`
is hidden in the root listing; an overflow footer/context command opens its full
contents, and keyboard selection reveals an off-screen item in that folder.

R34 fixed desktop Get Info enablement and outdated fixture assumptions; full run
68384 passed with 393 source hashes. R35 added saved file-icon positions, group
movement, Clean Up and Arrange, retaining stored coordinates across resize.
R36 added spatial navigation and desktop-specific view options. Full run 60635,
the strict-signed package, and 398 source hashes passed. Storage fixtures skipped.

R37 adds movable disk/Trash icons, shared saved-position hit testing and painting,
drag outlines, Escape cancellation and keyboard navigation. Atomic position-store
checks pass (52); the initial drag checks pass (43). Updated navigation, drag and
regression checks plus the app build are running. Disk-to-Trash reports that the
active desktop volume cannot be ejected; no host disk is unmounted.
Native GUI acceptance and the remaining desktop audit are still open.
