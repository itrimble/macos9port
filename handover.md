# Integration checkpoint — 5 November 2026, evening

This section supersedes the live-state and next-step claims below.
Ian authorized integration loops, sub-agents, local commits and launching major
builds for feedback. No push is authorized. Theme handoff code is missing:
local bundle tip is 1c1b1b9, while the theme handoff requires f876cdb.
All delivered asset files are committed locally at 1236aef.

The integrated source includes prior startup/SIT/embedded-runtime work plus
icon loading, host sound preferences, startup cleanup/shortcuts, bridge pacing,
and bounded shared-lock recovery. The complete local test/build and host-sound
suite passed for R2; its separately packaged bundle passed strict verification.
Use build/OS999 Integration R2.app; do not overwrite any live app bundle.
Source hashes/artifact receipts are in build/integration-20261105/evidence.
The copied test-volume path is in test-volume-r2.txt in that folder.

Old live processes and saves were preserved. The original MI1 logo stall is
not diagnosed: guest frames stayed identical despite CPU progress. Fresh runs
of the exact binary progressed with real and dummy audio. Bridge pacing fixes
confirmed CPU waste, not a proven game-state defect. Await Ian's actual test
feedback; wallpaper selection, themes, native HiDPI and sustained audio remain
open. See docs/DEVELOPMENT-CHECKLIST.md V01–V06 and the end of runtime Part 3.

---

# OS999 session handover — 5 November 2026

## User's correction and current objective

The live game run Ian was describing uses an **old OS build**. He clarified that
the newer OS build was created outside the active goal. Do not keep diagnosing
the old desktop as though it represented the current OS. Continue the active
one-hour goal: run an implementation and validation loop toward playing the
selected classic Mac games in OS999's own clean-room runtime, prioritize games,
preserve saves and unrelated edits, and report evidence precisely. The goal is
still active.

Ian explicitly authorized controlling the game, recording audio, and using
computer-use tools. The restart should continue the working gameplay loop:
launch a fresh game under the correct OS build, observe and measure it, compare
the result with Ian's listening/visual feedback, make one bounded fix, rebuild,
and repeat on a fresh run. Do not substitute host-only probes or an old process
for live acceptance.

## What was researched and completed in this session

- Read `CLAUDE.md`, `docs/DEVELOPMENT-CHECKLIST.md`, and the latest runtime-loop
  notes. The checklist confirms the clean-room independent runtime is the goal;
  a screenshot or successful build is not gameplay acceptance.
- Identified the old live session: OS desktop PID **41604** (`build/ospi`) and
  Monkey Island runtime PID **15887**, launched with
  `/private/tmp/ospi-embedded-volume.tDTEiY/Applications/Monkey Island/Monkey Island.rsrc`.
  It remains untouched and should not be terminated during pickup.
- Identified the newer OS candidate as
  `build/OS999 Stylized.app`. Its main executable SHA-256 is
  `b1b3c65fc0dc68d480035e2fb5a71037658b62e194f6fcc655c757bc1b109375`;
  its runtime SHA-256 is
  `758ea414566f009ace191e292ee8786f9823eb616fccbdc3bd4e5734b5268de4`, matching
  the current `build/ospi-runtime`. This is newer than the old standalone
  `build/ospi` (SHA-256
  `e41553c398097ab1d07c04f427095fda9b01e2d2806e2554c579caa3358f6d1f`). The
  bundle/runtime version is verified by binary identity; its build provenance
  outside the active goal has not yet been established.
- Launched the newer OS as a separate instance, PID **24482**, window **21077**,
  with `--volume /tmp/ospi-newer-os-volume`. That volume is a 4.9 MB copy of the
  old session's test volume, so the two desktops are isolated. A screenshot
  confirmed the OS999 Stylized desktop and its `OS999 HD` disk.
- Opened the copied volume's `Applications` folder. The Monkey Island folder is
  visible, but the game has **not** yet been opened under the newer OS. Pixel
  clicks/double-clicks did not visibly select that folder; continue from a
  fresh window-state read and resolve input through the live UI. No latest-build
  game playback or audio capture has been verified yet.
- No files were committed. The worktree already contains extensive unrelated
  dirty changes and untracked assets; preserve them.

## Current live state to preserve

| Process | State | Volume |
|---|---|---|
| PID 41604, `build/ospi` | Old OS desktop; leave running | `/tmp/ospi-embedded-volume.tDTEiY` |
| PID 15887, `build/ospi-runtime` | Old live Monkey Island game; leave running | Same old volume |
| PID 24482, `build/OS999 Stylized.app/.../ospi` | New OS desktop; continue here | `/tmp/ospi-newer-os-volume` |

CUA Driver session label used: `os999-latest-game-test`. Re-discover windows
and bounds before clicking; the OS desktop is a custom-drawn SDL surface and its
Accessibility tree does not expose Finder icons. The current latest-OS window
is 21077, with the Applications folder open. Verify process state first.

## The previous measurement loop and its limits

The earlier MI1 callback-ring run in
`docs/RUNTIME-LOOP-2026-11-04-PART2.md` produced a useful repeatable loop. Over
245 intervals after the first 20 seconds, producer/callback medians were
48,091/48,063 frames per second; presentation median was 60 FPS and the minimum
was 52.5. It also logged 4,096 underrun frames across six intervals (about
85 ms), plus 131 clipped samples. Ian heard the SCUMM Bar music doubled for
about 20 seconds, then mostly alright before returning to normal. That listening
report was not a loopback recording, and those telemetry figures belong to an
older runtime artifact. They are a comparison baseline only, not evidence about
PID 24482 or the latest OS build.

`docs/RUNTIME-LOOP-2026-11-04-PART3.md` records recent UI/launcher work and the
embedded guest-window bridge. The bridge was unit-tested, but prior GUI attempts
did not prove a Finder-launched game, routed game input, or window containment.
That makes the current newer-OS MI1 launch a useful next end-to-end check.

## Next steps

1. Re-read `CLAUDE.md` and `docs/DEVELOPMENT-CHECKLIST.md`; check the active goal
   and confirm PIDs 41604, 15887, and 24482 still exist. Leave the old run and
   its volume alone.
2. In PID 24482, open `Applications/Monkey Island/Monkey Island` from the
   custom desktop. Verify the resulting child command line uses the
   `OS999 Stylized.app` bundled runtime and the copied volume before assessing
   gameplay.
3. Capture an initial screenshot and start an authorized audio recording for
   the fresh run. Enable/read runtime performance and audio telemetry where
   available. Measure presented FPS and frame pacing; do not call the guest
   clock or old `mixed-fps` counter FPS.
4. Exercise the same reproducible MI1 scenes that exposed duplicated opening
   notes and SCUMM Bar ticking/music dropout. Keep Ian's live listening feedback
   separate from PCM/loopback measurements and runtime counters. Record the
   scene, elapsed host time, audio, FPS, frame-time percentiles, CPU/GPU path,
   and any stalls.
5. If the failure reproduces, inspect that exact fresh-run trace, make one
   focused change, run the relevant requested checks, rebuild the OS app, and
   repeat on a new isolated volume. Do not claim an improvement without both
   measured and listening evidence where applicable.
6. After MI1, resume LeChuck's Revenge and the other checklist targets. Keep
   browser, networking, QuickTime, packaging, and theme work behind game
   execution during the active goal.
7. Update `docs/DEVELOPMENT-CHECKLIST.md` and the runtime-loop evidence with
   exact source state, host/tool versions, artifact hashes, results, limitations,
   and the next checkpoint. Do not commit unless Ian asks.

## Working tree and build notes

The source tree has many existing modifications and untracked files. The last
known local suite passed before this handover; no tests were run in this session.
`build/ospi-runtime` and the bundled OS999 runtime currently have identical
SHA-256 hashes. Do not rebuild an app bundle in place while an instance from
that bundle is running. The root `handover.md` is new and was explicitly
requested by Ian.
