# Desktop capacity continuation — 6 November 2026

This continues [the integration record](DESKTOP-INTEGRATION-2026-11-06.md).
All edits remain in the isolated desktop-integrated worktree. Main, release,
shared previews and games are untouched. No commits, pushes or publication.

## R22: Find and clipboard consistency

Find results now grow beyond 48, using the same owned-window storage as ordinary
folders. Exactly-full result buffers are no longer falsely marked partial;
the search needs evidence of another match or a traversal limit/error. Searches
remain bounded to 32,768 results, 131,072 visited entries and depth 32. Partial
results are labeled and explained. Allocation failure or an unavailable root
preserves the previous result window. Paths are still volume-relative and
directory traversal does not follow symlinks.

Copy Names computes and allocates the complete selected text before requesting
a host clipboard write. Output, imported clipboard text and text windows use
owned buffers when they exceed the inline 4 KiB space. The clipboard/text bound
is 3 MiB, enough for the displayed folder's supported names. Oversized/unreadable
clipboard input is reported instead of silently displayed as complete. Short
replacements release the previous large buffers; window close/destruction also
releases them. The existing short edit/paste buffer is unchanged.

The clipboard viewer and Copy from text windows retain the complete content.
Drawing stops below the visible clip. Text printing explicitly refuses content
at or above 64 KiB, preserving the existing print buffer and sending nothing.
This does not establish large-text rendering performance or a physical print.

## Validation and pending work

The 137-result fixture copies more than 4 KiB and checks viewer/import round trips,
failed repeat search retention, exactly 48 matches, invalid-name rejection in the
command test, self-replacement, the 3 MiB import boundary, recovery after an
oversized import, and rejection of oversized printing. All 215 capacity checks
pass, including AddressSanitizer/UndefinedBehaviorSanitizer (exec 63919, exit 0).
Finder desktop 112 checks, menu integration and desktop-command checks pass.
Desktop build passed (37042); final boundary build passed (55542).

Receipts: `desktop-r22-build.log`, `desktop-r22-boundary-build.log`,
`desktop-r22-boundary-tests.log`, `desktop-r22-integration-tests.log`,
`desktop-r22-sanitizer.log`, and `desktop-r22-source.json`, all under `build/`.
Full suite and separate R22 package passed (exec session 93225, exit 0), log
`build/desktop-r22-full-package.log`. Real storage fixtures skipped because the
fixture variable is unset. Source hashes match, and the bundle passed deep/strict
signature verification. `desktop-r22-validation.json` records bundle hashes.

Native clipboard delivery and real GUI rendering remain unverified. The broader
goal still needs application focus/stacking and menu/desktop GUI acceptance.
Long host filenames remain unsupported and explicitly yield partial listings.
Do not equate headless checks or packaging with complete Mac OS 9-style behavior.


## R23: Application cycling and guest close-box routing

The keyboard dispatcher now handles Command-Tab before the browser receives
keys. Forward/reverse cycling includes every running Finder, classic guest,
OS999Amp and Browser identity; explicit selection unhides the destination.
It skips unavailable apps, suppresses repeat switching, consumes the matching
Tab release even after modifier release, and respects menus/dialogs. Host focus
loss resets the capture latch. Host interception of Command-Tab remains a
platform limitation; the application menu is available as an alternative.

The guest title-bar close box now uses the existing guest Close command callback
instead of stopping the runtime. It brings the guest forward and retains it on
busy/unavailable delivery. This does not demonstrate an actual guest save prompt,
window close or exit; no guest/game was run. Help describes all application types.

Behavior reference opened: Apple's archived [Application Switcher key commands](https://savagetaylor.com/til/TA25752.html), describing forward/reverse cycling
for Mac OS 8.5, 8.6 and 9. The floating/tear-off switcher remains unimplemented.

Application switching tests pass 82 checks; menu integration and browser
composition pass. Desktop build passed (exec 70119); focused test session 79824
exited 0. Logs: `desktop-r23-build.log`, `desktop-r23-tests.log`; source snapshot
`desktop-r23-source.json`. An initial automated edit guard rejected an assumed
field declaration before modifying it; the subsequent build found no test rule.
That setup error was corrected before the successful build/tests above.

Full R23 suite/package passed (exec 52888, exit 0), log
`desktop-r23-full-package.log`. Real storage fixtures skipped (unset fixture
variable); source hashes match and deep/strict bundle signature checking passed.
`desktop-r23-validation.json` records bundle hashes. GUI acceptance is pending. Next major work: retain application activation
order consistently across painting, browser occlusion and hit testing. Current
non-frontmost layering and Hide/Quit fallback still use fixed priorities.


## R24: Application stacking and modeless panels

Finder, guest, player and browser now share saved activation order for painting,
hit testing and browser occlusion. Hide and foreground Quit/exit return to the
last visible running application, falling back to a hidden application when
necessary. Finder icon/window selection survives application changes and a
background guest exit. Inactive Finder windows no longer draw active title stripes.

Appearance and the player accept exposed clicks without blocking every other
application. Appearance retains its window while yielding keyboard focus to
Finder windows; its popup remains modal. Command-Tab works with modeless panels.
Browser Quit and closing the player return to the previous application without
stopping background playback. These are synthetic integration checks, not guest
application or real browser acceptance.

Focused checks pass: application stack 29, Finder desktop 112, switching 82,
Appearance 148, browser composition and audio engine. The stack also passes
ASan/UBSan (exec 76101). An old Finder test supplied a contradictory legacy
focus argument; it now activates the guest through the real switching path.
Final focused runner 47590 exited 0. The first full run (61281) failed two obsolete modal-panel assertions in
`test_desktop_controls`. Updated integration checks now verify desktop blur and
exposed-panel refocus. Full suite and separate R24b package pass (10788, exit 0);
source hashes match and deep/strict signature verification passes. Real storage
fixtures skipped (unset variable); `desktop-r24-validation.json` binds the bundle.

`build/desktop-r24-source.json` binds 375 source files before that run.

Remaining: floating/tear-off Application Switcher, drag/marquee composition
across guest/browser surfaces, and scoped real GUI acceptance. No games or guest
software were launched. Source checks do not establish complete OS 9 parity.


## R25: Floating Application Switcher and drag composition

The application menu now opens a floating switcher, and dragging that menu out
onto the desktop tears it off. The palette lists running applications, activates
and unhides them, supports Option-click hiding of the prior app, title dragging,
right-edge resizing, vertical collapse, name toggling, small/large icons and
Option-Shift orientation changes. Original letter badges identify the apps; no
Apple assets were copied. The behavioral reference is the Apple archived article
linked above. Options are currently session-local; cross-session persistence and
compatible document delivery remain follow-ups. Dropping files on the palette
must not move them into a folder underneath it.

Palette interactions preserve the active application until an app is selected.
It paints and intercepts clicks above Finder, guest, media and Browser surfaces.
Finder drags retain motion/release ownership across those surfaces. Drag ghosts
and selection outlines render after the guest, and the browser compositor masks
only their edge pixels, preserving transparent interiors. Trash highlighting is
idempotent across repeated overlay draws. Host focus loss cancels capture.

Application Switcher tests pass 40 checks, including options, hidden-app return,
modal refusal, canceled close, tear-off, screen bounds, capture and repeated
outline rendering. ASan/UBSan passes (9760, exit 0). Browser composition checks
verify palette focus retention, drag motion/release delivery and exact masks.
Final focused build/tests pass (50828, exit 0). Logs: `desktop-r25-focus-build.log`,
`desktop-r25-focus-tests.log`, `desktop-r25-sanitizer.log`. The first build/test
pass also passed but preceded the palette focus-routing correction.

Full suite and separate R25 package pass (exec 35339, exit 0). All 380 source
hashes match; deep/strict signature verification passes. Snapshot and bundle
receipts: `build/desktop-r25-source.json`, `build/desktop-r25-validation.json`.
Real storage fixtures skipped (unset fixture variable). Real-window GUI acceptance remains
pending. Games, guest software, existing previews and other worktrees were not
operated or modified. No commits or pushes.


### R25 real-window observation

A separate `OS999 Desktop Preview R25.app` uses the tested R25 desktop binary
(SHA256 `347a3ce794309deb7724f7d1075705ba902b5bc52ad0051430e931866c3975e7`)
and unchanged R24b resources/runtime; this is not the final R25 package receipt.
Its unique bundle ID is `com.iantrimble.os999.desktoploop.r25`, initial PID 75880,
window 26614, and volume `build/desktop-r25-ui-volume`. It launched without
becoming active. Preserve it until live inventory confirms ownership/state.

Real captures `desktop-r25-initial.png`, `desktop-r25-appmenu.png` and
`desktop-r25-after-synth.png` show the seeded Finder desktop. They do not show
an opened application menu or the new palette. Peekaboo CLI 4.9.8 selected GUI
bridge 4.9.7; background click and explicit background synthOnly both returned
`INTERACTION_FAILED`, no pressable AX element, effect unverifiable. A fresh
capture after each attempt showed unchanged UI. No foreground escalation,
keystrokes, guest/game launch or other app interaction was performed. Default
bridge discovery selected the same host. Real palette/menu acceptance remains
blocked on a safe working input route; source and headless results remain valid.

*The three real captures show only the isolated preview window on 6 November
2026 at approximately 6:10–6:11 PM America/Chicago.*


## R26: Switcher preferences and safe rejected drops

Switcher position, size, open/closed state, icon size, names, orientation and
collapse state now save on normal session destruction and restore on startup.
Gesture state is never restored. Preferences use bounded strict parsing, regular
file checks, no-follow/nonblocking reads, and an exclusive temporary file with
flush/fsync and atomic rename. Invalid input preserves the current state. A temp
collision fails without removing the existing file. A destination symlink is
replaced atomically without changing its target. An unopened palette writes
nothing. File-drop rejection now explains the missing document-delivery handler
and confirms that files were not moved; arbitrary guest drops are not implemented.

Focused build and tests pass (1797, exit 0): switcher 83, Finder state 41,
Finder interaction 78 checks. Sanitizers pass (90844, exit 0). Logs are
`desktop-r26-build.log`, `desktop-r26-tests.log`, `desktop-r26-sanitizer.log`.
Full suite/package passed (1060, exit 0). All 381 source hashes match and
deep/strict signature verification passes. `desktop-r26-source.json` and
`desktop-r26-validation.json` bind the evidence. Real storage fixtures skipped
because the fixture variable was unset.

### Foreground verification route correction

After background routing failed, a scoped R25 app switch was explicitly attempted
for the desktop test. It succeeded and a real capture confirmed the preview was
frontmost. Both the ordinary foreground click and an exact-PID/window click with
`--no-auto-focus` were refused before the menu opened. Peekaboo's focused-window
lookup still resolved the shared display name and reported multiple matching
apps. Fresh screenshots after both attempts showed the unchanged desktop.
No other instances were closed or modified. The R25 preview's CFBundleDisplayName
was still OS999 Stylized despite its unique CFBundleName and identifier.
A separate R26 preview now sets both names uniquely to resolve the ambiguity;
its GUI outcome is pending. This supersedes the earlier no-foreground-attempt
statement, which described the initial background-only attempts.


### R26 observed GUI outcomes and next handoff

R26's unique CFBundleDisplayName resolved the Peekaboo name ambiguity. Initial
clicks were delivered but did not reliably act at the newly requested location.
The preview was normally quit and relaunched with `OSPI_INPUT_TRACE=1`; its trace
confirmed window point (985,42) arrived as SDL client point (985,10), with a
1024x768 logical window and 2048x1536 drawable. Coordinate scaling was correct.
Do not change desktop input mapping on the basis of the earlier click failures.

A real screenshot confirms the application menu open. Down then Return selected
Application Switcher and opened its floating palette. Explicit `peekaboo move`
to the target, a fresh observation, then exact-PID/window foreground click made
the mouse controls work: the zoom box hid names and narrowed the palette.
Earlier apparent menu/zoom failures are not established desktop defects; they
acted like delivery at the previous pointer location. A `move --no-auto-focus`
call was refused before dispatch; the scoped foreground move succeeded with
normal exact-target focus. Always preserve focus guards and observe each change.

The owned preview was quit normally (no force). Its preference file recorded
`1 814 52 190 0 1 0 0`. A background relaunch and real window capture confirmed
the open palette, position and hidden names restored. No guest/game ran.
UI receipt: `build/desktop-r26-ui-validation.json`. Real captures include
`desktop-r26-keyboard-switcher.png`, `desktop-r26-names-hidden.png`, and
`desktop-r26-restored.png`; these are partial GUI acceptance, not full parity.

*These captures show only the isolated R26 preview: keyboard-opened switcher at
6:25 PM, hidden names at 6:26 PM, and restored state at 6:27 PM on 6 November 2026,
America/Chicago. Capture timestamps and hashes are recorded in the UI receipt.*

Latest preview identity at observation: PID 95420, process start identity
1794011242109052, window 26686 at (640,206), 1024x800 including title bar. It is
not frontmost after background relaunch. Bundle ID:
`com.iantrimble.os999.desktoploop.r26`; display name `OS999 Desktop Preview R26`.
Volume: `build/desktop-r26-ui-volume`; trace: `desktop-r26-input-restart.log`.
Revalidate all live identities before input. R25 preview PID 75880 was preserved;
older previews/runtime sessions remain untouched. No builds/tests are pending.

Remaining desktop acceptance: real tear-off/drag/resize/orientation/collapse and
modifier behavior; Finder/menu actions across windows, media and Browser; native
clipboard/print interactions. Document delivery into guest applications remains
unsupported and explicitly rejected without moving files. Do not mark V09 done.


## R27: Live switcher controls and release endpoints

The R26 preview was revalidated (PID 95420, window 26686) and brought forward
through its unique identity. Real single-window captures confirm showing names,
title dragging, right-edge resizing, collapse, expansion, close and application
menu tear-off. Receipt `build/desktop-r27-ui-validation.json` binds these checks
to the R26 preview/source, not the R27 endpoint correction.

*The R27-run captures show only the R26 desktop preview on 6 November 2026,
approximately 6:29–6:38 PM America/Chicago. Exact timestamps and image hashes
are recorded in the UI receipt.*

The modifier-click CLI rejected focus options, then rejected app/window selectors
before dispatch. Its supported route uses the exact screenshot receipt, explicit
global coordinates and `--foreground --modifiers option`. That call dispatched,
but the app performed an ordinary name toggle, not the small-icon toggle. This
is not modifier acceptance. Do not infer real-key failure or patch input mapping
from this synthetic result. A fresh observation followed each attempt; no other
apps or guest programs were operated.

Live drag/resize endpoints appeared short of the requested release point.
Inspection found that the switcher ignored release coordinates and used only
motion events. A regression with missing/coalesced final motion failed twice
before the fix (52249, exit 1; `desktop-r27-before-fix.log`). Release now applies
its final drag/resize position before clearing capture, except behind a modal.
Focused build/tests pass (4176, exit 0): switcher 84 checks and browser composition.
Full suite/package passed (84015, exit 0). All 381 source hashes match and the
bundle passes strict/deep signature verification. Receipts:
`desktop-r27-source.json`, `desktop-r27-validation.json`. Real storage fixtures
skipped because OS999_STORAGE_FIXTURES was unset. The R26 preview is unchanged and does not contain this
fix. GUI endpoint retesting and modifier/multi-app acceptance remain pending.


### R27 endpoint-preview attempt and current tool state

R26 PID 95420 was normally quit through its exact process-start identity. Its
volume is preserved. A separate R27 preview uses both unique display/name fields,
bundle ID `com.iantrimble.os999.desktoploop.r27`, and the corrected desktop binary
SHA256 `7b7e8be058dd72a4a4ef894b5effdbe5e6deae2b8c6876333296b3d6075d341d`.
The disposable `build/desktop-r27-ui-volume` was copied from the owned R26 volume
and explicitly seeded with switcher settings `1 400 200 190 0 0 0 0` for measurement.
This setup is not evidence of restored user preferences.

The GUI bridge at `~/Library/Application Support/Peekaboo/bridge.sock` stopped
answering: inventory returned BRIDGE_UNAVAILABLE, and two diagnostic handshakes
timed out at 1.0 seconds. Socket and GUI-host PID 11909 still exist; no host or
shared service was restarted. A parsing script initially raised AttributeError
on the failed inventory's null data; preview launch had already succeeded.
Local CLI permissions report Screen Recording, Accessibility and Event
Synthesizing granted. Broad local app inventory timed out; exact process/window
inventory, verified focus and classic capture succeeded with `--no-remote`.

Current R27 preview: PID 9624, start identity 1794012014861139, window 26714,
bounds (640,206), 1024x800. The real capture shows the palette at the fixture's
(400,200) logical position. A local foreground drag from global (1120,448) to
(1280,548) dispatched but produced no visible movement. This does not verify
R27's release endpoint fix and does not establish a desktop regression.
A follow-up bridge handshake still timed out. Do not blindly repeat input.
Revalidate the process, window, focus and tool host before resuming GUI work.

*`desktop-r27-endpoint-before.png`, `desktop-r27-endpoint-ready.png` and
`desktop-r27-endpoint-after.png` show only the corrected R27 preview around
6:44–6:45 PM on 6 November 2026, America/Chicago; no movement is observed.*

Full suite/package and source/signature receipts remain valid. No builds/tests
are pending. GUI endpoint, modifier/orientation and multi-app acceptance remain
open; older previews and the main runtime session were not modified.


## R28: media documents dropped onto the Application Switcher

The switcher now exposes application-row hit testing independently of title-bar
controls. Dropping one regular supported media file onto the running OS999Amp
row calls the real player through a result-reporting hook. Success records the
recent document; decoder failure displays an error. Source files and Finder Undo
history are not moved or cleared. Missing files, links, folders, multiple items,
unsupported applications and unavailable hooks do not invoke playback. Palette
chrome and modal-dialog drops do nothing, without falling through to folders
underneath. Help describes the supported single-file case.

This uses the player's existing pathname-opening interface after a no-follow
identity check. It does not introduce descriptor-based asynchronous decoding or
claim protection against a concurrent host pathname replacement after that check.
Search-result and alias drops are not added. Generic guest document delivery is
still unavailable; no classic applications or games were run.

Focused switcher checks passed (101 checks before the added Undo assertion,
exec 52415, exit 0), and the desktop built. Audio integration passed with the
real generated MP3 delivered through the switcher (exec 13695, exit 0), using
SDL's dummy audio driver: this is decoding/playback-state evidence, not listening
or live mouse acceptance. Logs: `desktop-r28-build.log`, `desktop-r28-tests.log`,
`desktop-r28-audio-build.log`, `desktop-r28-audio-tests.log`. All are under `build/`.
Full suite and separate R28 package passed in exec 35524 (exit 0), including
102 switcher checks. Deep/strict signature verification passed; all 381 source
hashes match. `desktop-r28-source.json` and `desktop-r28-validation.json` bind
the source and bundle. Real storage fixtures were skipped (variable unset). GUI automation was not retried;
the owned R27 preview and shared GUI host were left alone. V09 remains open.


### Completion audit: remaining Finder behavior, not just disabled commands

A period first-hand [Mac OS X/9 comparison](https://suddentechnology.com/xreview/xfeatures.html),
opened 6 November, describes six manual Arrange choices: name, modification date,
creation date, size, kind and label. Its Finder section also describes customizable
labels, spring-loaded folders, grid spacing and desktop-file behavior. This is
behavioral reference evidence, not Apple implementation source. Apple's archived
[Views Control Panel note](https://savagetaylor.com/TIL/TIL06845.pdf) corroborates
older label sorting; it is System 7 evidence, not proof of the exact OS 9 menu.
No proprietary binary/source was inspected. The REA skill routes ordinary source
analysis to repository tools; no engine or configuration setup was needed here.

Current source audit confirms `menu.c` exposes only four Arrange choices and
`folder_view.c` implements only four comparators. `Item` has modification time,
not creation time; persisted preferences reject sort modes above `SORT_SIZE`.
The next implementation must cover menu dispatch, data population, comparison,
View Options, saved-state validation and round-trip tests together. On hosts
without a creation timestamp, do not substitute metadata-change time and call it
creation time. Unknown dates must have an explicit ordering policy.

`finder_settings.c` has fixed label names and only sound/startup preferences.
No spring-loaded-folder implementation or arbitrary desktop-file representation
was found in the audited Finder/desktop modules. Those are completion gaps, not
reasons to claim parity after switcher testing. The existing applications,
printing, host-volume operations and live GUI acceptance gates remain separate.


## R29: complete the six Arrange modes

Added Date Created and Label to Arrange, sorting dispatch, View Options and
saved-folder-state validation. Existing sort IDs remain unchanged. Creation
sorting is newest-first with unknown timestamps last; label sorting follows
label IDs (None through Project 2). Ties use the existing case-insensitive name
comparison with a case-sensitive final tie-break. The macOS listing and search
paths use host birth time; other hosts currently report creation time unknown,
not metadata-change time. Search results also load labels through the existing
no-follow label reader. Finder-only command guards cover both new commands.

The focused fixture covers known/unknown dates, label order and ties, checked
menu state, guest-focus rejection and save/reopen restoration of both modes.
Existing submenu mouse tests now account for the inserted Date Created row.
Exec 5565 passed the desktop build, 57 folder-view checks, 43 Finder-state
checks and menu integration. Follow-up exec 39006 passed 49 Finder-state
checks and menu integration, including host birth-time population, search labels
and both View Options choices. `build/desktop-r29-source.json` records the source hashes;
`desktop-r29-build.log` and `desktop-r29-tests.log` hold results when available.
R29 full suite/package passed in exec 63850 (exit 0), log
`build/desktop-r29-full-package.log`. All 381 manifest source hashes match and
deep/strict signature verification passed; `desktop-r29-validation.json` records
the bundle. Real storage fixtures skipped (variable unset). R29 has no GUI
acceptance yet. The spring-folder draft added during the wait was not linked. No preview or guest was launched this pass.


## R30: spring-loaded folder navigation

Added a 600 ms folder-hover timer during Finder file drags, including collapsed
pop-up tabs and desktop volume icons. Source identity is retained by folder path
and resolved after window reordering. The drag tracks its original folder
windows and only the newly created windows it owns. Release keeps the destination
window while closing temporary intermediates. Escape cancels the drag; focus loss
and session destruction also restore temporary windows before saving state.
Originally collapsed folders/pop-ups are restored on cancellation. Dialogs,
application changes, panels and the switcher block spring opening. Folder aliases use the existing bounded resolver and no-follow directory check.
Search-result sources remain unsupported, as in ordinary Finder dragging.

The new module and tests were drafted without linking them while R29 ran, then
wired after R29's full receipt completed. Exec 32005 passed the desktop build and 37 spring-folder checks; follow-up
exec 71035 passed 63 checks including palette occlusion, hover reset, unowned
window preservation and quit cleanup. Exec 45369 passed 70 spring-folder checks,
102 switcher checks and browser composition, including folder-alias coverage.
A final guard also clears mouse capture when a modal/application interruption
cancels spring navigation; its stronger assertion is included in the full run. The fixture exercises delay, nested drop/source retention,
Undo, Escape, unsigned timer rollover, existing pop-up restoration and focus-loss
cleanup. Initial results are in `build/desktop-r30-tests.log`; compilation is in
`desktop-r30-build.log`. `desktop-r30-source.json` binds 384 source files. Full suite/package passed in exec 21545 (exit 0), log
`build/desktop-r30-full-package.log`. All 384 source hashes match; deep/strict
signature verification passed. `desktop-r30-validation.json` binds the bundle.
Real storage fixtures skipped (variable unset). No live GUI acceptance is claimed for R30. R30 is the latest
fully tested packaged checkpoint. No existing preview or runtime was changed.


## R31 draft: customizable label names and colors

While R30 validates, `finder_labels.c`, its header and a focused test are drafted
but are not in CORE or the running R30 source manifest. Compiler syntax checks
pass with the two planned command IDs supplied explicitly; this is not a linked
feature or a passing runtime test. The draft supports seven editable UTF-8 label
names (up to 63 bytes), six-digit RGB colors, Defaults, Cancel and per-volume
atomic persistence. Labels opened from Preferences return to that dialog with
its unsaved checkbox edits intact. Foreground ink is chosen from color luminance.
The parser is bounded and rejects malformed records, symlinks and nonregular
files; storage uses an exclusive temporary file followed by rename.

After exec 21545 completes and its receipt is recorded, wire commands 493/494,
CORE/test targets, label getters, menu titles, dialog-5 drawing/input/layout and
session-load initialization. Main's text-input routing must allow dialog 5.
Existing `finder_label_color` moves out of `finder_settings.c` to avoid duplicate
symbols. Replace fixed black label ink in icon/list/settings rendering. Add the
Labels button to Preferences and Edit Labels to the Label submenu. Run the new
fixture, existing settings/menu/render checks and the desktop build; do not call
the draft complete or overwrite R30's immutable validation snapshot.


### R31 integrated checkpoint and GUI attempt

The preceding draft is now wired. Exec 28667 passed the desktop build, 67 label
checks and menu integration. Exec 31246 passed 68 label checks, 57 folder-view
checks and 112 Finder desktop checks after replacing the ink heuristic with
sRGB relative-luminance contrast selection. Mid-gray 909090 now uses black
(6.58:1) rather than white (3.19:1). Menus show current names and color chips;
Preferences offers a Labels button, and Defaults remains an uncommitted edit.
All source files remain below 500 lines. Full suite/package runs in exec 42758,
log `build/desktop-r31-full-package.log`; `desktop-r31-source.json` binds 387 files.

Peekaboo CLI 4.9.8 matches the opened official latest release. GUI host 4.9.7
remains pinned to preserve other jobs; its handshake and permissions recovered.
A separate unique R31 preview was signed and launched on a copied R27 test volume:
PID 99107, start identity 1794014622248432, window 27056, bounds (640,210,1024,800).
Exact capture confirms desktop rendering, not label interaction. Bridge Move
refused focus without dispatch. Local exact-window focus timed out indeterminately.
A fresh capture/inventory remained nonfrontmost; background click reported no
pressable AX element and another capture showed no menu. No input was retried
blindly. System Events dictionary lookup failed; an activation script requesting
PID 99107 returned `69755, true`, a different older-preview PID. Input stopped.
Final exact R31 inventory still says nonfrontmost and the capture is unchanged.
This activation does not establish R31 focus and may have focused the older preview.
No shared host was restarted and no game/runtime guest was run.

*`build/desktop-r31-final.png` shows only the R31 desktop preview at 7:27:15 PM
on 6 November 2026, America/Chicago; the Labels editor was not opened.*

`desktop-r31-ui-validation.json` records the preview hash and failed interaction
route. R31 live editor/spring-folder acceptance remains open. Full exec 42758 exited 2: isolated Appearance linking lacked the new label-name
accessor. Appearance and navigation fixtures now provide explicit fixture-only
label names; production persistence/rendering remain covered by integrated tests.
Exec 3028 passed 148 Appearance and 91 navigation checks. Full rerun passed
in exec 48561 (exit 0), log `build/desktop-r31-full-rerun.log`. All 387 source
hashes match and deep/strict signature verification passed. Receipt: `desktop-r31-validation.json`. Storage fixtures skipped; live acceptance remains open.

### R32 spring-folder preferences
Finder Preferences saves enablement and a 100–2000 ms opening delay per volume.
The slider supports clicking, dragging, final release coordinates and arrow keys;
Escape cancels drafts. Atomic persistence rejects malformed files and symlinks.
Focused checks pass (61 preferences, 70 spring folders, 68 labels); full suite and
strict-signed R32 package pass (exec 91057), bound to 390 matching source hashes.
R33 alias drops pass 101 interaction/70 spring checks; GUI, desktop files and grid positions remain unfinished.
