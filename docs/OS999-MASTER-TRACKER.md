# OS999 master tracker

## Delivery contract — 7 November 2026

One complete macOS app, all existing work integrated into local main, plus
Quake III as an application inside the desktop. No remote pushes. Use direct
local builds (`BUILDFLEET_LOCAL=1`). Leave a persistent watcher accepting
explicit ready markers for exact commits, testing before local promotion.
Use authorized Quake demo data outside Git. Preserve existing apps and saves.

## Current integration

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
with external-library rejection before issuing a passing receipt. Its final
execution on the frozen candidate and main reconciliation are still pending.

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
upstream license. Build 8 validation remains pending; this is not fleet parity.
