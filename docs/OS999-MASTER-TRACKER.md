# OS999 master tracker

## Current integration status

Build 32 is the promoted baseline as of 12 November 2026. Local main is
`8950fd6`, the promotion of `integration/r11` (`aba87e1`). Main was pushed to
origin on 12 November (`552a235..8950fd6`, from the MBP; the Neo's synced `.git`
was missing an object). YouTube: hardware decode was verified from Chromium's
diagnostics in the standalone browser helper, and Ian heard and saw playback there,
but playback inside the OS999 desktop window is not yet verified. Builds 25–32 are summarised below and in
the build table; the Build 24 text that follows is kept as the earlier record.

Builds 25–32, in order:

- Build 25 failed on a merge conflict with the stale tracker text in the base
  of `d8f8dea`.
- Build 26 failed at the Quake stage. The watcher pinned Quake `a0448ae`, the
  bridge header had changed, and Builds 21–24 had used `06688bc` by hand. The
  CEF root was also stale (156.0.15 against the pinned 156.0.26); a
  gate-owned CEF folder fixed that on 11 November.
- Build 27 was cut off by a watcher reload.
- Build 28 passed (caption fix `c2b64bb`) and became main `7d3b652`.
- Build 29 passed (internal disk capacity `417a6d3`) and became `a9712ec`.
- Build 30 failed with "Main changed during validation". An untracked
  `abandonware/` symlink had appeared in the main checkout; it is now listed
  in `.git/info/exclude`.
- Build 31 passed (Quake III retail paks `61704f3`) and became `3aa79ea`.
- Build 32 passed (`integration/r11` `aba87e1`) and became `8950fd6`: gate pins
  now owned by the candidate, a native in-window file dialog for Movie Player,
  guest RAM as an opt-in (256 MB, 32-bit; the default stays 16 MB, 24-bit),
  an app classifier with per-app launch profiles, a PowerPC runtime with a
  custom MDEF and GWorlds, a .sit expansion dialog, a desktop menu audit, and
  the MI1 save/reload evidence document.

Since 12 November the watcher plist holds only `OS999_QUAKE_REPOSITORY`; all
other pins come from the candidate.

Evidence limits for Builds 28–32: everything is headless. Nothing in them has
been seen in a real window. MI1 save, quit, relaunch and load was proven
headlessly (`docs/RUNTIME-LOOP-2026-11-11.md`). The PowerPC test app halts at
the Printing Manager (`PrOpen`) with no window drawn. The required
demonstrations (Thurrott.com browsing, YouTube with verified hardware decode,
classic QuickTime playback) have no runtime pass.

Build 24 was the promoted baseline as of 10 November 2026. Local main contained
merge `18ca9ab`, which carries gate-validated commit `3a854b2` (tree
`145760afbf7ff452c6054b3c6b34df4a20bda0b7`, the tree in the Build 24 gate
receipt `runs/r10-integration-24`, 15 of 15 stages, 10 November 11:57). No push.

Build 20 was promoted first (`a1d2bbe`, carrying the linear Builds 13–20
chain); Builds 21–23 were promoted as source commits `99b69bb`, `94410fe` and
`97f59a7` plus records on 10 November; Build 24 followed by merge. The
Build 20 acceptance record and its open findings
([Build 20](BUILD-20-ACCEPTANCE.md)) were answered by Builds 21–23: usage log
off until chosen in Help, one 1.1.0 version string with build number, Movie
Player 999 title, CEF 156.0.26.

Build 24 windowed evidence: the gate artifact was hash-verified on the Neo
(448 of 448 files match the receipt; strict codesign passes) and launched on a
fresh disposable volume. The Monitors & Display Desktop Size group was reached
and the 16:9 shape row selected. An earlier scripted pass on the Neo saw live
switches to a portrait mode, 1280 by 960 and 1920 by 1200 redrawn at the new
size with the host window scaled to fit. Not yet seen: the smallest 480-wide
modes, relaunch persistence of a saved mode, the browser and guest windows at
non-default sizes, and the startup animation at other sizes. Known gaps:
several fixed-width panels are cut off below 640 wide, and the preview caption
clips at the panel edge (fix committed as `d8f8dea`, queued for the next gate).
A full scripted GUI session wrote no usage-log events with consent off,
confirming the Build 21 consent fix outside the test suite. Click delivery
note: a leftover third-party computer-use overlay window on the Neo swallows
screen-coordinate clicks; posting events directly to the ospi PID works.

Promotion followed a scripted windowed pass on the Neo, recorded in
[the Build 20 acceptance record](BUILD-20-ACCEPTANCE.md). That pass is not
Ian's hands-on acceptance and found failures that stay open: telemetry records
by default with no consent control, version strings disagree, the host movie
window uses the QuickTime name, and the pinned Chromium is two versions behind.
Listening, gameplay, double-click, guest applications and hardware remain
untested. PowerPC code does not execute; only the 68K runtime exists.

Ten numbered builds passed the gate (9, 10, 13–20); 11 and 12 failed. Builds
13–20 were manual snapshot validations outside the watcher queue, so the
watcher counter was corrected from 13 to 20 under the state lock; the next
build is 21. The stale dashboard edits that blocked promotion were older copies
of work already in this tree; they are saved in
`../macos9port-backups/main-dirty-pre-build20-promotion-20261110`. The nested
`bluetooth` worktree moved to `../macos9port-bluetooth-feature` because this
tree now tracks a `bluetooth/` directory.

Correction, 10 November: eight branches are not ancestors of Build 20
(`feature/storage-installers`, `feature/control-panels`,
`fix/controlpanels-ramdisk-safety`, `fix/r2-documents`,
`feature/boot-startup-sequence`, `recovery/boot-startup`,
`recovery/applescript-credit`, `recovery/quicktime-credit`), but their work was
carried in by squash and recovery commits. Every file they change exists in
main, either identical or in a later version; none is missing or left at its
old version. They are not pending integrations. A line-by-line audit of the
later versions has not been done.

Builds 21 and 22 (branch `integration/r10`, not promoted) answer the Build 20
findings. Both passed all 15 gate stages on the MBP on 10 November. Build 21 is
commit `99b69bb`, tree `9920c1978887e1fa477bca04dd634dc192c6445d`; Build 22 is
`94410fe`, tree `05e45aba2a02a6908de2bf0c34daa7059b5ae46a`.

- Usage log: off until the user ticks Help, Record Local Usage Log. Seen in a
  real window on the Neo with Build 21: a fresh launch wrote nothing, turning it
  on showed the notice and a check mark and started the log, turning it off
  removed the consent file and stopped it.
- Version: one string, 1.1.0, seen in About and the startup log (Build 21).
  Build 21 did not show its number because the flag sat inside a test-only
  Makefile block; Build 22 compiles it in (236 compile lines carry the flag),
  but the About panel of Build 22 was not seen in a window.
- Host movie window is titled "Movie Player 999" and the menu reads
  "OS999 Amp..." (label seen in Build 22; movie title covered by its test only).
- CEF pin moved to 156.0.26 (Chromium 156.0.8078.88). No 157+ CEF exists.

Build 23 (`97f59a7`, tree `7fbd1214510a4d136261d97a01cf6e3d05b65771`, promoted
into local main on 10 November) answers Ian's hands-on pass of Build 22 on
10 November. It passed all 15 gate stages on the MBP. Seen in a real window on
the Neo with the gate artifact (scripted clicks, fresh disposable volume):

- The window and bundle are named "OS999"; "Stylized" is gone from the code
  the user sees.
- A Window menu sits between View and Special and lists the open windows.
- Monitors and Display has a Screen size row (75 to 200 percent); 75 percent
  resized the host window to 768 by 576 and 100 percent restored it. This
  scales the 1024 by 768 desktop. Choosing a different desktop resolution is
  not implemented; the size is assumed in about 80 places.
- The Extensions Manager lists four OS999-authored sample extensions, placed
  once per volume. Loading them at startup was not watched.
- OS999's own files live in the volume's hidden `.os999` folder
  (`src/os999_private.c`): usage log and consent, browser profile, staged
  browser helper, Quake data and settings, and OS999's temporary files. With
  the browser open, the helper ran from `.os999/Helpers`, the profile was in
  `.os999/Browser`, the Mac's `Library/Application Support/OS999` folders were
  unchanged, and the login keychain's "Chromium Safe Storage" item was not
  modified. Whether a keychain dialog can still appear on a Mac that already
  has that item was not established; Ian's report could not be reproduced.
- Dragging a folder into itself now says so instead of the generic failure.

Still outside the volume: the volume's own default location, temporary files
made by host tools OS999 starts (curl, unar, Chromium), and the host fonts the
body text face is read from. Older data left in the Mac's Library by earlier
builds is not deleted; Quake demo data is copied across once.

| Candidate | Recorded outcome | Remaining work |
| --- | --- | --- |
| Build 32 | Gate passed: `integration/r11` `aba87e1`, candidate-owned pins, Movie Player file dialog, guest RAM opt-in, launch profiles, PowerPC runtime, .sit dialog, menu audit, MI1 evidence | Promoted as main `8950fd6`; headless only, not seen in a window; PowerPC test app halts at `PrOpen` |
| Build 31 | Gate passed: Quake III retail paks `61704f3` | Promoted as `3aa79ea`; headless only |
| Build 30 | Failed: "Main changed during validation" (untracked `abandonware/` symlink in the main checkout) | Symlink added to `.git/info/exclude`; no package |
| Build 29 | Gate passed: internal disk capacity `417a6d3` | Promoted as `a9712ec`; headless only |
| Build 28 | Gate passed: caption fix `c2b64bb` | Promoted as `7d3b652`; headless only |
| Build 27 | Cut off by a watcher reload | No result recorded |
| Build 26 | Failed at the Quake stage: watcher pinned Quake `a0448ae`, bridge header changed; CEF root stale (156.0.15 vs pin 156.0.26) | Fixed 11 November with a gate-owned CEF folder; pins now come from the candidate |
| Build 25 | Failed: merge conflict on the stale tracker text in the base of `d8f8dea` | No package |
| Build 24 | Gate passed: 30 desktop sizes in landscape and portrait, chosen in Monitors & Display and saved per volume | Promoted 10 November; unseen items and open gaps listed above; caption fix `d8f8dea` queued for Build 25 |
| Build 23 | Gate passed: OS999 name, Window menu, `.os999` storage, screen size, sample extensions, browser helpers off the keychain | Promoted 10 November; loading sample extensions at startup not watched |
| Build 21 | Gate passed: usage-log consent, 1.1.0 version string, Movie Player 999 title, CEF 156.0.26 | Superseded by Build 22 |
| Build 22 | Gate passed: Build 21 plus the build number in About | Promoted within the Build 24 chain |
| Build 13 | Gate passed: R2 desktop fixes, quiet effects, native apps, Notes, Fonts, About | Superseded by Build 20 |
| Build 14 | Gate passed: browser staging, no-keychain browser, New Folder, Get Info, terminal history | Superseded by Build 20 |
| Build 15 | Gate passed: Quake III icon and demo-data install | Superseded by Build 20 |
| Build 16 | Gate passed: Quake cursor and focus, Shut Down menu item, Console shutdown | Superseded by Build 20 |
| Build 17 | Gate passed: Bluetooth and Extensions Manager panels, Font Book, classic runtime merge | Superseded by Build 20 |
| Build 18 | Gate passed: protected guest heap for every partition size, opt-in host fonts | Superseded by Build 20 |
| Build 19 | Gate passed on the second attempt (first failed packaging tests): local interaction telemetry | Superseded by Build 20 |
| Build 20 | Gate passed: Movie chooser, OS 9 style About This Mac, telemetry change | Promoted; open findings above |
| Build 9 | Complete gate and relocated packaged Quake frame/input probe passed | Local main promotion remains pending |
| Build 10 | All 15 gate stages passed; later review found movie/audio teardown use-after-free | Diagnostic artifact only; correction carried into the next candidate |
| Build 11 | Desktop test gate failed because the direct Finder test omitted quota-helper link dependencies | No package assembled; dependency correction carried into R2 |
| Build 12 | Stopped during SDL preparation when a parallel suite exposed an obsolete About body-text assertion | No package assembled; test correction retained for a fresh numbered run |
| R2 candidate | Sound, native applications, Notes/docs and Fonts/About combined focused checks pass; mobile 596 checks and host simulator build pass | Final combined suite and a new numbered package gate pending |

R2 interface effects default off, startup chime off and system gain 20 percent;
explicit saved preferences survive. Effect toggles no longer mute music/movie
output. Its separate full desktop/movie suite and mobile 592 checks plus host
simulator build passed. Combined native-app and Notes regression checks pass,
including 51 document checks and core/movie-window tests. The Notes producer's
mobile routing suite passed 596 checks. These are source/headless results.

The next build number must be allocated under the integration state lock.
Preserve Build 11's failed receipt and Build 10's needs-repair history. Current
receipts live under `~/Library/Application Support/OS999/Integration/runs/`.
Main promotion, actual font/About interaction, listening, sustained gameplay,
Bluetooth hardware and native mobile-device acceptance remain separate gates.

## Last promoted checkpoint — Build 8 (historical)

Build 8 passed the complete gate and its source was fast-forwarded into local
main as `f19ead673186885108b2717d20b79e28ded77151`. The tested tree is
`b3daf8be1f5f609ea7dcc17b6f7654a9d8c2d204`. Subsequent documentation-only
status notes do not change that artifact or its validation receipt. No push.

The artifact and receipt are in
`~/Library/Application Support/OS999/Integration/runs/initial-integration-8/`.
Desktop/runtime, tailnet, 29 TLS cases, keyboard 64,063/pad 56/SDL 12 checks,
mobile 592 checks and the ARM64 host simulator build passed. All eight SDL
clients link the private genuine SDL2 build. The package contains 28 host
libraries; strict signatures and a relocated minimal-PATH seed passed with
675 loaded images confined to the copied bundle and system locations.

The relocated preview uses `/tmp/os999-package-smoke-sy6swlvx/OS999 Build 8.app`
and its sibling disposable Volume. PID 68103 was observed running, and the
real desktop/disk window was visually inspected. R6 and existing saves remain
untouched. This is not browser interaction, gameplay, listening, or device
acceptance. Real storage fixtures were skipped; Quake demo data extraction and
native iOS/Android execution remain unresolved.

Dashboard and watcher now run from main under their existing launchd services.
Dashboard tests (5), watcher tests (10), HTTP assets and foreign-Host rejection
pass. At that checkpoint the watcher was idle for explicit ready commits and
its next number was 9. Later reservations are described above. Failed Build 7
remains recorded. Original main edits are retained
in stash `3e965471d1e0890b8874585528194ccabcebb3cd` and the verified backup
`../macos9port-backups/main-promotion-20261107-173017`; 82 colliding source files
were preserved there before promotion. Local evidence and nested worktrees
remain in place, excluded locally rather than deleted.

SnapDog dashboard captures run on the Mac mini; integration stays on this Mac.
Ian approved temporarily disabling Open in Editor to bypass stale editor state;
restore it after capture. Capture completion is recorded separately.

## Delivery contract — 7 November 2026

One complete macOS app, all existing work integrated into local main, plus
Quake III as an application inside the desktop. No remote pushes. Use direct
local builds (`BUILDFLEET_LOCAL=1`). Leave a persistent watcher accepting
explicit ready markers for exact commits, testing before local promotion.
Use authorized Quake demo data outside Git. Preserve existing apps and saves.

## Candidate assembly before final validation

- Candidate: `integration/complete-20261107`, worktree
  `/Users/ian/projects/macos9port-complete`, based on main `cd8b557`.
- Backup: `/Users/ian/projects/macos9port-backups/20261107-codex-pre-integration`
  (625 MB; all registered worktree diffs, untracked files, branch and Quake bundles).
- Existing all-features source: `aca680a`. Squash integration is in progress;
  original branches are preserved. New commits use Ian <ian@example.com> and
  no attribution trailers. The old eleven commits are not rewritten or imported
  as candidate ancestry.
- Initial conflict resolution preserves the newer app-scoped menus, expanded
  Finder model and writable File Manager; carries main's text/page navigation
  and runtime fixes. Full local build/tests passed for the R6 checkpoint;
  a final gate follows the source-size refactor and audio diagnostic correction.
- Initial local build log: `build-logs/integration-baseline.log`.
- Watcher is installed under launchd; ten regression tests pass. It waits
  while main is dirty. Final pinned build inputs still require configuration.
- R6 is packaged and running from an isolated copied location; see evidence below.
  No merge into main or remote push has occurred.

## Integration ledger

| Work | Source | State |
| --- | --- | --- |
| Existing all-features build | aca680a | Candidate imported; R6 full local suite passed |
| Main scrolling/runtime | cd8b557 and dirty snapshot | Committed baseline included; dirty delta accounted for, shared audio corrections preserved |
| Desktop R28–R37 | desktop-integrated backup | Expanded Finder/desktop carried; backup-delta audit reconciled; tests pass |
| Browser follow-up | d26d955..5f5c159 | Test-hook/evidence updates carried; historical browsing evidence is not new artifact acceptance |
| Browser security remainder | a6da09c versus snapshot 58ea86f | Browser implementation matches a6da09c; newer README retained; exact-source build/policy tests pass |
| L06 installer | 7b1a15e | Integrated with renamed-Applications handling; archive/native fixture checks pass |
| Guest networking phase 1 | 782b52f | Sources integrated; isolated link-variable fix; guest networking tests pass |
| Mobile follow-up | ios_android backup | Missing keyboard/trackpad/pinch backup delta recovered; isolated headless/simulator checks pass; final gate and device acceptance open |
| Quake desktop embedding | external a0448ae4305ff10a78a4fffac4fb1506cf2b8ea1 | Host/engine protocol integrated; synthetic bridge checks pass; compatible demo/playability not verified |
| Complete standalone packaging | browser/helper/assets/dylibs | R6 nested signature and relocated minimal-PATH seed pass; desktop observed |
| Persistent watcher | explicit-ready queue | Installed; ten tests pass; no promotion yet; final gate input configuration pending |

## Acceptance boundaries

Run all desktop/runtime, TLS, tailnet, wallpapers, browser, mobile and keyboard
checks against the integrated snapshot. Verify the packaged app outside its
checkout, with a disposable volume. Record visible terminal/browser/wallpaper,
media/Finder and embedded Quake results separately from headless checks.
Keep listening, acceleration and physical-device evidence distinct.

Not part of this milestone: new GPU QuickDraw, PowerPC, classic QuickTime,
guest networking phase 2, missing desk accessories, Control Strip/dock, or the
undelivered theme engine. Existing mobile source is integrated; physical-device
parity is not implied by the Mac artifact.

## Historical receipt correction

The prior all-features receipt names source `8313cf9`, one dirty file, and
truncated hashes, while the branch tip is `aca680a`. Its browser helper resides
outside the bundle. It is historical evidence, not the receipt for this delivery.

## Recovery checkpoint — 7 November 2026

The interrupted dashboard work resumed in the complete candidate. The local
Python service and browser UI now expose 160 tracked gates, component acceptance,
roadmap/dependencies, compatibility, performance placeholders, System Folder
anatomy, searchable evidence, and build receipts. SSE and HTTP checks pass;
foreign Host headers are rejected. Evidence-boundary regression checks pass.
Browser-control host unavailable: visual and interactive UI acceptance pending.
Start with `python3 scripts/os999-dashboard.py --port 8080`; regenerate the local
Mermaid bundle with `sh scripts/build-dashboard.sh`. Dependencies remain in build/.

Watcher recovery is integrated: ten regression tests pass; submissions serialize,
retry build numbers ascend, and unexpected source output blocks promotion.
The packaging gate is added; full execution remains pending. The persistent
launchd watcher is installed and its heartbeat advances while waiting on dirty
main. Initial recovery had no queued runs. Build 7 subsequently failed as recorded below;
main promotion and remote push have not occurred.
The dashboard also runs under launchd on loopback port 8080.

Integration repaired desktop selection/open and navigation merge regressions:
174 desktop-surface checks pass. Full suite and exact-source browser build pass.
Quake host and engine bridge regression checks pass; original Arena demo fails
the engine's Team Arena checksum requirement. Acquired Team Arena installers
remain outside Git and have not yielded verified compatible data. Gameplay is open.

Ian inspected localhost:8080 on 7 November and approved its appearance as an MVP
("looks pretty good to me for an MVP"). SnapDog captures of all six dashboard
views are requested and in progress; this feedback is dashboard visual acceptance,
not OS integration, runtime compatibility, or full parity acceptance.

R4 relocation failed before startup because Brotli retained an unresolved
@rpath dependency. R5 then exposed SDL2-compat's dynamic SDL3 dependency. Both
failures are retained; no user volume was used. R6 resolves both failures.


## R6 artifact checkpoint and final gate preparation

`build/OS999 Integration 20261107 R6.app` contains 29 bundled host libraries
and 46 Mach-O objects. Strict nested signing passes. The copied app at
`/tmp/os999-r6-relocation-8kjyz0y5/OS999 Integration 20261107 R6.app` passes
`--seed` with minimal PATH; dynamic-loader evidence contains no Homebrew loads.
Its disposable volume is the sibling `Volume`, and preview PID 84753 remains
separate from prior apps/saves. Root observed the desktop, OS999 HD, Documents,
Notes and View menu. That observation does not establish input, browser, media,
listening, Quake gameplay, or platform acceptance.

Evidence: `build-logs/resume-artifact-r6.json`, `resume-validation.json`,
`resume-relocatable-tests.log`, `resume-packaging-tests.log`, and `r6-preview.log`.
Real storage fixtures were skipped because OS999_STORAGE_FIXTURES was unset.
The recovered mobile delta restores keyboard/trackpad gestures and continuity,
plus compatible Finder standard-icon reset and font fixes. Isolated recovery
checks pass; native iOS/Android builds and real-device acceptance remain open.
The subsequent source split keeps authored source/test files under 500 lines;
Makefile is 461 lines with unchanged recipes in config/extra-tests.mk. The
refactor full suite passed. Tailnet fixtures and 29 loopback TLS cases pass.
The audio diagnostic now describes effect suppression, not a paused device;
shared media output remains available. No listening improvement is claimed.

The final gate builds the browser from candidate source with a matching shared
CEF pin, and builds the external Quake engine from its immutable commit in a
run-owned worktree. It checks browser source, full bundle files and symlinks,
engine commit/binary/protocol, tests, signatures, and a bounded relocated seed
with external-library rejection before issuing a passing receipt. Build 8
completed this gate and main reconciliation; see the current checkpoint above.

`make app` creates the base desktop/runtime/Quake bundle only. The complete
browser, transitive-library, provenance, and relocated-launch gate is
`scripts/integration-gate.sh`; plain packaging is not a complete-build receipt.


## Frozen gate 7 dependency failure

The gate on tree `5eb6e823612949701d154529ef2e1f3c6e8b478d` passed the
browser/engine builds, desktop/runtime suite, tailnet, 29 TLS cases, and keyboard
64,063 plus pad 56 checks. SDL keyboard injection then crashed inside installed
SDL2-compat 2.32.74: its text-event conversion returns NULL to SDL3_PushEvent.
No pass receipt or Build 7 app was emitted. Logs remain in
`build-logs/final-gate-7.log` and `build-logs/keyboard-sdl-crash.log`.

The complete gate now builds genuine SDL2 2.32.10 from its verified official
archive into a private ignored prefix. This deliberate compatibility pin leaves
Homebrew and live R6 unchanged. It forces SDL client relinking, verifies actual
library paths and installed-file hashes, records provenance, and bundles the
upstream license. Build 8 validation passed; this project dependency pin is not fleet parity.
