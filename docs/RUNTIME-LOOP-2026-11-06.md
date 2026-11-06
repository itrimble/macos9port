# Runtime Loop — 2026-11-06

Evidence record for the MI1 audio/pacing gate (checklist R03, R05, G01, G02).
Host: Apple Silicon Mac, macOS 26.6.2, Apple clang 21, SDL2 2.32.74,
FreeType 26.7.20, Python 3.15.2. Builds are local with `BUILDFLEET_LOCAL=1`
as Ian instructed for this project. Game data is Ian's installed MI1 copy at
`~/Library/Application Support/OS Pi/Volume/Applications/Monkey Island/`
(fork `Monkey Island.rsrc`, 290,897 bytes); it is not in Git. Raw logs,
WAVs and frame dumps live in the untracked `.ralph/evidence/` folder.

## New tooling

- `build/ospi-runtime-probe` (`make probe`, `src/runtime_probe.c`): headless,
  deterministic runner. Fixed 60 Hz guest clock, 16 MHz cycle budget split
  into N slices per tick, scripted input (`T down|up|click|move X Y`,
  `T key VKEY ASCII`), the production Sound Manager poll/render chain
  rendered at exactly 800 frames per tick into a WAV, PPM frames every S
  seconds. Same fork and script give the same bytes. 60 guest seconds take
  about 2.5 s of wall time.
- `build/ospi-bridge-probe` (`src/bridge_probe.c`): real-time driver that
  spawns the production `ospi-runtime --window --indexed --interactive
  --bridge` exactly as `native_launch.c` does, pushes the same script through
  the bridge, reads frames back, and ends the guest with the CLOSED state. The
  runtime's own `OSPI_AUDIO_TRACE` / `OSPI_PERF_TRACE` lines and an optional
  `OSPI_AUDIO_WAV` producer capture go to the given log.
- `scripts/audio-analyze.py` (levels, silent gaps, click candidates,
  spectrogram) and `scripts/audio-trace-summary.py` (per-second producer and
  callback rates, underruns, ring minimum).
- `OSPI_SOUND_NOTE_TRACE=1` now also prints `sound-voice` lines for soundCmd
  (80) and bufferCmd (81) installs, with the bytes of voices up to 32 bytes.

## Route into the SCUMM Bar (deterministic)

Script `.ralph/evidence/mi1-bar.script`: Escape at 22 s skips the credits;
the lookout cutscene plays itself; docks with verbs at ~104 s; five clicks at
the right edge walk along the pier; click (465,235) at 123.5 s, the door at
(218,255) at 135 s, Open at (42,393) at 150 s, the door again at 151 s, then
the open doorway at 160 s. The bar scene with pirates is on screen from ~164 s
and the bar music starts at 162.07 s guest time. Frames:
`.ralph/evidence/mi1-frames-bar/`.

## What the deterministic bar render shows

`mi1-bar.wav`, 260 guest seconds. Three sampled channels, init 0x8c
(mono, noInterp, noDrop). Every note is `freqDurationCmd` (40); the
sequencer re-installs the voice with `soundCmd` (80) whenever the instrument
changes (1,035 installs in 200 s). Rests are an 8-byte all-0x80 voice with
loop 1..6. Grace notes of 6–12 ms precede main notes (12+42+126 = 180 ms
groups); per Inside Macintosh: Sound ("Playing Notes", Sound-54), a
`freqDurationCmd` starts the sample from its beginning and loops between the
header's loop points, which is what the implementation does. The only
large sample steps are stair-step edges from the noInterp path. Full-band
silences of 70–166 ms recur with the song's 24.58 s period, so they are
sequenced rests. The deterministic path does not reproduce ticking.

## Real-time finding: periodic guest tasks were starved

Bridge run 1 (`mi1-bridge-runtime.log`, pre-fix): 0 ring underrun frames in
222 s, presenter ~50 fps, yet the lookout dialogue took about twice as long
as in the deterministic run (docks at ~220 s instead of 104 s).

Cause: `mac_runtime_run_cycles` gave the VBL and Time Manager polls a budget
of cycles/4, and both pollers return without doing anything for budgets
under 10,000 instructions. The real-time worker's slices are 1–2 ms
(16,000–32,000 cycles), so VBL and timer tasks fired only on the occasional
larger slice. Reproduced deterministically: `--slices 16` (16,667 cycles per
slice) left the screen black for 110 s; `--slices 4` was fine.

Fix: floor the service budget at `MAC_RUNTIME_SERVICE_MIN` (10,000) in
`src/mac_runtime.c`. Regression `small_cycle_slices` in
`tests/test_mac_vbl.c` fails on the old code and passes on the new one.
`BUILDFLEET_LOCAL=1 make -j4 test` exit 0 after the change
(`.ralph/evidence/make-test-after-service-floor.log`).

Bridge run 2 (`mi1-bridge-runtime-fixed.log`, post-fix): docks at ~95 s,
SCUMM Bar sign at ~115 s, town street from ~130 s — the same pace as the
deterministic probe (real time ran ~9 s ahead, so the fixed script's door
clicks missed; run 3 uses a shifted script). 1,024 underrun frames total
(21 ms, one interval at 218 s) in 227 s.

Bridge run 3 (`mi1-bridge-runtime-fixed2.log`, post-fix, walk actions
shifted 9 s earlier): docks at ~95 s, door opened at ~126 s, SCUMM Bar scene
on screen from ~150 s, bar music continuous from 150 s to the 230 s stop
(frames in `mi1-bridge-frames-fixed2/`, producer PCM in
`mi1-bridge-producer-fixed2.wav`). Sequenced rests recur as in the
deterministic render. Ring underruns: 1,536 frames (32 ms) at host second 128
while the door opened, and 4,096 frames (85 ms, one full ring) at second 219
where the worker's own trace shows a 1,101 ms interval and 14.15 MHz, i.e. a
host scheduling stall. Presenter in bridge mode: ~50 fps, p50 19.8 ms, because
the old pacing added the frame copy to a 17 ms wait.

Follow-ups applied after run 3 (verified only by unit tests so far):
`src/runtime_pacing.h` paces the bridge presenter to fixed 16.667 ms deadlines
(`test_runtime_video` covers the schedule), and the guest worker asks for
`SDL_THREAD_PRIORITY_HIGH` so a brief host stall is less likely to empty the
85 ms ring. `src/desktop_guest.c` now owns the guest-window drag clamp
(`guest_window_drag`), covered by `test_drag_containment`.

Bridge run 4 (`mi1-bridge-runtime-run4.log`, pacing + priority applied,
190 s, same shifted script): 0 underrun frames in 187 intervals, bar music
from 150 s, presenter median 60.0 fps (min 58.7, max 60.9), frame time
p50 16.52 ms (15.25–17.10), p95 22.45 ms, p99 24.12 ms, worst 33.4 ms.
These are host presenter timings in bridge mode on this Mac, not GPU
execution or input latency.

## Limitations

- No human has listened to these captures yet. The ticking Ian reported on
  5 November is not reproduced by either path on this build; whether the
  pacing fix changes what he hears is PENDING_HUMAN_ACCEPTANCE.
- The producer WAV is pre-ring PCM. Delivered audio is characterised only by
  the callback underrun counters.
- Sword Master scene needs a save file; the runtime has no file writes (L04).
