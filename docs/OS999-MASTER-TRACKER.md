# OS999 master tracker

## Current integration status

Build 8 remains the last promoted baseline. Later runs are separate candidates;
none of the following changes imply promotion, GUI acceptance or listening tests.

| Candidate | Recorded outcome | Remaining work |
| --- | --- | --- |
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
