# OS999 cross-worktree status — 6 November 2026

> Historical record copied from main on 7 November 2026. Revisions, paths,
> process observations and acceptance statements below describe the dated audit.
> See [the master tracker](OS999-MASTER-TRACKER.md) for subsequent integration work.

## What changed in our understanding

I have working pieces spread across several checkouts. The main checkout is
not the complete development build. The `release/1.1` worktree combines the
browser, host HTTPS transport, storage, audio player, terminal, movie window, mobile tree, and further
desktop work. That is substantial implementation progress. It is not a claim
that the integrated release has passed all acceptance requirements.

Ian confirms that Monkey Island screens opened, mouse clicks worked, and sound
played. Earlier automated input failures do not invalidate that hands-on result.
Stable audio, sustained play, save/reload, and latest-build performance still
need their own evidence.

This audit inspected source, Git history/status, documentation, and selected
existing logs. It did not build, run tests, launch applications, record audio,
or operate a GUI. Numbers below are attributed historical results, not new
measurements. No publication, commit, push, merge, or cleanup was performed.

## Scope and source map

The [recursive inventory](WORKTREE-INVENTORY-2026-11-06.md) records all discovered
Markdown paths, per-directory file counts, and registered worktree tips/status.
Traversal included hidden and ignored source folders and nested worktrees.
Generated builds, dependency caches, runtime volumes, and symlink traversal
were excluded from the broad inventory. Selected build logs were read separately.
Downloaded games, credentials, and proprietary binaries were not audited.

Relative sibling paths below resolve from this document in `docs/`.
They are local evidence links and may not exist in a fresh clone. Commit IDs
and source paths are the portable lookup keys. Worktrees changed during the
audit; the inventory is a timestamped observation, not a locked snapshot.

| Workstream | Location and revision observed | Integration state |
| --- | --- | --- |
| Main desktop/runtime | repository root, `main` at `68f61ff` | Dirty audio/probe/test work beyond committed desktop navigation |
| Combined development build | [release/1.1](../../macos9port-v1.1/), `e66a061` | Storage, browser, audio, terminal, video, mobile and desktop changes combined; no full-release acceptance established here |
| Browser | [browser-embed](../../macos9port-browser-embed/browser/README.md), `8d6a6f4` | Merged, including the keyboard-focus fix via `7583642` during this audit |
| Browser security integration | `../../macos9port-browser-security`, based on `8d6a6f4` | Dirty policy, signing, transport and browser changes; no completed combined result established |
| Storage | [storage guide](../../macos9port-storage/storage/README.md), `d7de608` | Library merged into release; desktop installer wiring remains separate |
| Host audio | `../../macos9port-sound`, `9fb1b54` | FFmpeg/SDL engine and OS999 Amp merged; dirty desktop hooks were ported |
| Networking | [networking README](../../macos9port-networking/README.md), `a8a0898` | Host transport merged via `e66a061`; separate browser policy source remains untracked |
| Finder follow-up | `../../macos9port-desktop-loop`, `68f61ff` plus dirty files | Release includes a port; source worktree remains dirty |
| Terminal | [nested checkout](../terminal/), `38b5b15` plus dirty files | Ported into release; additional release-only paste/Copy fixes |
| Video | [nested checkout](../videosupport/README.md), `38b5b15` plus dirty files | Ported into release with later parser bounds fix |
| Mobile | [mobile evidence](../ios_android/docs/MOBILE.md), `38b5b15` plus dirty files | Mobile tree ported into release; platform acceptance remains partial |
| Keyboard | [keyboard guide](../keyboard/README.md), untracked root subtree | Separate library/simulator; mobile carries its own integration; root desktop wiring not established |
| Guest file writes | `.claude/worktrees/l04-writable-files`, based on `68f61ff`, locked | Active separate work; do not equate storage-library writes with guest File Manager support |
| Test expansion | `/private/tmp/os999-test-suites`, `dfcba3b` | Two test-suite commits merged into release; some tests specify future behavior only |

Other temporary worktrees retain appearance, wallpaper, theme, menu, icon,
startup, archive, CPU and resource work. Many sit at ancestor commits with
dirty patches. Their existence is not proof that those patches are missing
from main; compare content before attempting integration or removal.

The inventory captured release at `7583642`; a final read found `e66a061`.
This later tip includes the host networking merge. No complete test receipt for
that final tip was verified. The networking checkout retains an untracked
`browser/` policy snapshot, so the host transport merge does not establish that
all browser security changes were merged.

## Classic games, audio and performance

- **Monkey Island:** user-observed screens, clicking, sound and scene progress.
  Prior reports describe startup doubled notes, intermittent ticking, and
  delayed music. Do not describe it as merely a successful process launch.
- **Older measured baseline:** [handover](../handover.md) and
  [runtime Part 2](RUNTIME-LOOP-2026-11-04-PART2.md) record 245 intervals after
  the first 20 seconds: 60 FPS presentation median, 52.5 minimum, 4,096 audio
  underrun frames across six intervals (about 85 ms), and 131 clipped samples.
  These results belong to an older runtime, not today's release or live game.
- **New probe work:** [runtime progress](../.ralph/progress.md) records a
  deterministic probe using production audio polling/rendering and scripted
  input. The SCUMM Bar route reaches the bar at about 162 guest seconds.
  The author reports structured music and recurring sequenced rests rather
  than reproduction of the audible ticking. This is a bounded diagnostic,
  not proof that real-time playback is fixed.
- **Log inspected:** `.ralph/evidence/mi1-bar.log` ends at 260,005 guest ms,
  12,480,000 captured audio frames, with result 4 and the explicit message
  "Probe budget exhausted" / "gameplay unverified." Do not label that a
  successful game-acceptance run. Bridge-probe action logs also exist; no
  completed real-time audio verdict was established by this audit.
- **Monkey Island 2:** [runtime Part 3](RUNTIME-LOOP-2026-11-04-PART3.md)
  records copy-protection passage and several opening scenes, four-plus
  minutes of opening sequence after callback-budget changes, and Space/Escape
  input. Dummy audio was used in that run. Free play and sustained sound remain
  unverified. Castle of Dr. Brain still has a recorded startup resource failure.

Guest drawing remains CPU rasterization with accelerated presentation.
Uploading a framebuffer does not establish GPU QuickDraw. Guest File Manager
in this main source still documents read-only sibling access; a writable
storage library does not itself make a game's Save command work.

## Browser and networking

The root [browser README](../browser/README.md) describes an older standalone
snapshot. The browser-embed README contains later desktop evidence:

- On M4 Max/macOS, embedded YouTube AV1 playback recorded 60 decoded FPS,
  two dropped frames in approximately 65 seconds, and 58–60 presented FPS.
- The earlier standalone VP9 run identified `VideoToolboxVideoDecoder` and
  platform decoding at 3840×2160. Ian reported clear sound and clicked Skip Ad.
  That decoder result does not establish AV1 hardware decoding in the later run.
- Recorded embedded interactions include a native select menu, a saved data
  download, tab creation/switching, synthetic Japanese IME composition,
  fullscreen/Escape, popup windows and Cmd-T address focus.
- Linux arm64 and x86_64 container builds/tests and Xvfb rendering are recorded.
  They do not establish real Raspberry Pi graphics, decode or performance.
- Version-policy code blocks outdated Chromium builds. The branch's latest
  keyboard-focus restoration fix was merged into release during this audit.

Open browser acceptance includes the Thurrott navigation route, ten-minute
playback per target host, host CPU/GPU load, frame pacing, A/V synchronization,
complete sandbox/process audit, real IME interaction, and physical Pi evidence.
Earlier "no tabs/downloads/fullscreen" notes are historical, superseded by the
later feature evidence in that same browser workstream.

The networking README reports 29 passing host transport cases, including a
sanitizer run, and standalone browser checks rejecting unsafe launch flags,
untrusted certificates and plaintext navigation while loading trusted HTTPS.
Transport uses libcurl, verified TLS identities, destination callbacks,
redirect/byte/time limits and cancellation. It does not implement guest MacTCP,
Open Transport, UDP, inbound servers or a complete network sandbox.
Client-certificate selection/import and redistribution of required dylibs remain
open. The prior Keychain stall cleared, but the author did not observe why.
Host transport was committed as `a8a0898` and merged into release as `e66a061`
during this audit. The separate browser-security worktree is still integration
work, not an accepted combined security result.

## Storage, terminal and media

[Storage FEATURES](../../macos9port-storage/storage/FEATURES.md) records native
container/archive parsers, Disk Copy/NDIF/UDIF, HFS reading/writing, HFS+ reading,
fork/metadata preservation, path containment, quotas and crash-recoverable saves.
Unsupported compression uses an explicitly permitted `unar` fallback or returns
an unsupported result. Recorded fuzzing covers 300,000 mutants over 12 formats
with zero findings after a JSON-reader hang was fixed. Linux x86_64 and arm64
validation is recorded; macOS and physical Pi boxes remain unchecked there.
The desktop still uses its old installation path. L06 wiring and guest writes
remain distinct from the completed library feature list.

The sound branch adds a shared FFmpeg/SDL host audio engine, media player and
OS999 Amp with a ten-band EQ and playlist. Source and tests are integrated in
release. This audit did not establish listening quality or format-matrix results
for the combined build. Host media playback does not fix the guest Sound Manager.

Terminal source includes a PTY backend, VT rendering, console and desktop UI.
Release commits fix Copy routing and control-character paste handling, including
ESC removal. No terminal was exercised by this audit. A host shell is not a
classic guest program or a sandboxed command environment.

Video support is a host movie parser/window for uncompressed 32-bit raw frames.
Audio tracks and broad codec support are not implemented by that reader.
Release adds an overflow-safe sample-description bounds check. Original classic
QuickTime/player execution remains a separate unfulfilled requirement.

## Mobile, keyboard, assets and reference material

The mobile status document records 426 headless checks, iPhone Air/iPad Air
simulator launches, an Android APK, Pixel Tablet emulator rendering, and a
Pixel Fold emulator with taps, posture changes, trackpad motion and cover-screen
continuity. No physical phone was verified. iOS touch, real multi-touch,
physical foldables, and iOS classic-program launching remain gaps. The generic
flip emulator reports posture late; wallpaper BMPs contribute to a roughly
139 MB bundle. These are recorded branch results, not reruns against release.

The keyboard guide reports 64,115 headless checks and Linux sanitizer coverage,
with separate SDL dummy-driver tests. Haptics, pressure, listening quality and
physical touch need device evidence. Showcase images are renders, not proof of
hardware operation. Its "real foldables" section includes speculative device
profiles; do not convert those into confirmed hardware specifications.

Assets include 59 wallpapers, icon/cursor packs with higher-resolution variants,
and interface sounds. Asset resolution is not proof that the desktop renderer
uses HiDPI. Sixteen local color themes are implemented; supplied theme-engine
handoff claims reference a different delivery tip and are not implementation
evidence. The supplied desktop mockups and terminal/file-format prose under
`build resources from ian/` remain references, not shipped features or verified
citations. `quake/` is a separate third-party source drop, outside the root build;
its presence does not establish an OS999 Quake port or release license approval.

## Test evidence and documentation boundaries

The existing `.ralph/evidence/baseline-make-test.log` ends with `exit=0`.
The owning progress note records 87 binaries and a dirty-tree snapshot.
Later source modifications are not covered automatically by that earlier run.
The release Makefile wires storage, audio, terminal and movie tests, but this
audit did not establish a complete passing result for `e66a061`.

The Makefile's standalone phase-test group compiles QuickTime, browser IPC,
network policy, PEF/CFM, browser media gateway and release-gate tests without
production objects. Passing those specifications cannot prove the represented
features work. Actual module tests, game execution, device evidence and user
acceptance must remain separate.

Root README, checklist, handover and browser snapshot scope now point here.
Historical runtime records and concurrent `.ralph` logs remain intact. Nested
worktree READMEs can inherit older claims; consult their module-specific notes
and this source map. `AGENTS.md` was intentionally left unchanged under Ian's
earlier explicit instruction; its old app name/Systemless-only language must
be read alongside the newer project `CLAUDE.md` and current build definitions.

Next documentation checkpoint should follow an exact integrated build receipt:
record the source/dirty-tree hash, artifact, host, commands, actual result and
remaining limits. Do not copy a branch's passing status onto the combined release.
