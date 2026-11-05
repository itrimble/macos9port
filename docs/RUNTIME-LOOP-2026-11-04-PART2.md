# OS Pi runtime loop — 4 November 2026, continued

Continues [the initial work log](RUNTIME-LOOP-2026-11-04.md). The one-hour
target was missed. Gameplay, audio fidelity, save/reload and full acceleration
remain unaccepted. This log records implementation evidence, not completion.

## Shared runtime compilation

The Makefile now compiles runtime modules once into `build/runtime/*.o` and
links them into independent tests and the runtime executable. Header/recipe
dependencies and generated dependency files retain rebuild tracking. No vendor
CPU objects, guest assets or saved data were removed.

The complete suite and runtime build pass in `/tmp/ospi-object-build-tests.log`.
A dry-run with `src/mac_sound.c` marked changed schedules one runtime compile
and two links for the focused sound test and runtime. An unchanged runtime
build schedules no work. Evidence: `/tmp/ospi-incremental-build-plan.txt`.

## Monkey Island 2 dispatch correction

A800 is SoundDispatch, not SndAddModifier. The observed D0 value `00200008`
selects SndPlayDoubleBuffer. The next requirement is genuine double-buffered
PCM playback and its guest callback, not another synthesizer installation.
The development checklist has been corrected. Public interface reference:
https://leopard-adc.pepas.com/documentation/mac/Sound/Sound-136.html.

## Original Monkey Island introduction

Half-open voice loops now accept an endpoint equal to sample length, with no
clamping or guest-data edits. Volume V examples describe the trailing sample
counts consistently with this boundary. The mixer retains the later documented
full-first-pass envelope; the older attack/sustain/decay profile is not claimed.
Waveform and malformed-boundary tests pass under sanitizers and in the root suite.

A live window reached the original island introduction and title sequence.
Capture: `build/loop-20261104/evidence/mi1-loop-boundary-live.png`. This is a
real window capture on 4 November 2026; input/gameplay acceptance remains open.
The next failure was a 56,077.94 Hz note exceeding the former 48 kHz limit.
The run submitted 3,106,048 stereo frames; submission is not audible acceptance.

Added bounded area resampling that includes every crossed source contribution
when initNoDrop is requested. NoInterp uses held samples; otherwise the filter
integrates linear ramps. Derived rates use 64 bits with an explicit 768 kHz
limit. Synthetic sanitizer tests pass; an offline PCM run reaches 60 million
instructions past the prior failure. Apple-identical filtering is not claimed.

## Double buffers and interactive launches

Double-buffer PCM now owns actual buffer-ready transitions and schedules guest
callbacks outside CPU traps and the SDL audio thread. Final buffers receive a
callback without automatically restarting playback. Tests cover mixer gain,
quiet cancellation, flush preserving playback, callback registers/stack and
invalid inputs. MI2 passes startup and next requests A458 before submitting PCM.

Desktop launches now use `--interactive`, running until closed or stopped by a
runtime error. Diagnostic headless limits remain unchanged. CLI checks reject
interactive mode without a window or alongside an explicit instruction limit;
the native-launch test verifies the actual argument vector.

## Timer integration and current live checks

Implemented bounded normal/extended Time Manager records, deadline scheduling,
callback register/stack restoration and remove/reinsert resume semantics. The
host uses a monotonic microsecond clock after audio initialization; headless
runs use explicitly simulated time. Callbacks remain frame-quantized, with no
real-time precision claim. Full suite and runtime pass in
`/tmp/ospi-time-full-tests.log`. MI2 passes A458 in the live runtime but the
captured window remains black; this is not startup-screen or gameplay acceptance.
Capture: `build/loop-20261104/evidence/mi2-timer-live.png`.

MI1 passed high-rate conversion and submitted 4,795,264 stereo frames before
stopping on callback-time channel disposal. The real title/credit capture is
`build/loop-20261104/evidence/mi1-resample-progress.png`. Ian was away during
the music/input question and requested another check with the next build. No
audibility or Escape-key result was inferred. The packaged desktop helper has
not yet been refreshed with these changes.

The subsequent live MI2 capture `mi2-timer-progress.png` shows the original
Press Return copy-protection prompt. No protection input or bypass was attempted.
It later stopped at the 120-tick VBL catch-up guard after 70,888,193 instructions
and 5,521,024 submitted stereo frames. Host contention/pause versus scheduler
behavior remains unresolved; the guard was not disabled.

MI1's precise disposal request closes its same caller-owned, idle channel with
quietNow true from its callback. Logical close now stops playback immediately,
rejects subsequent channel use, and reclaims internal storage after callback
return. Caller-owned records remain intact. Synthetic normal/double callbacks
pass sanitizer checks; busy synchronous disposal remains unsupported.

The complete suite/runtime/desktop build passes in
`/tmp/ospi-dispose-full-tests.log`. The original MI1 offline PCM replay reaches
100 million instructions without a runtime failure, past its 67,650,268
disposal stop. The desktop bundle was refreshed and strictly signature-verified;
receipt: `build/loop-20261104/evidence/packaged-20261104T230223Z.json`.
A fresh bundled live MI1 run was started for Ian's requested audio/input retry;
those results remain unverified.

## Hands-on audio and function keys

Ian confirmed audible sound from the updated bundled MI1 run: imperfect but
not bad. This establishes audibility, not faithful mixing, sustained playback,
input acceptance or gameplay. The run continued through the original credits
and into a black transition without a logged halt at the time of capture.

The host event translator previously dropped every function key. It now maps
F1–F15 to their classic virtual key and function-character values; modifiers
remain in the event modifier field. Focused conversion tests and runtime build
pass (`/tmp/ospi-function-keys-build.log`). The running bundle has not been
replaced mid-session. Mapping references are SDL's public Darwin key table
and the public Event Manager interface constants, not Apple implementation.

## Bounded VBL backlog recovery

The former 120-tick fatal guard has been replaced with a 120-tick per-poll
work limit. Overdue ticks and pending callbacks remain queued; the existing
10,000-instruction callback cap and malformed/runaway callback failures remain.
Synthetic tests verify exact delivery after a 305-tick pause, unsigned tick
wraparound, bounded dormant-task scanning, and continuation of simultaneous
callbacks. Agent ASAN/UBSAN checks pass. Live MI2 recovery still needs a fresh
run; the currently open MI1 process retains its original build.

Full suite and runtime build pass in `/tmp/ospi-vbl-input-full-tests.log`.
These post-commit changes remain uncommitted and are not yet in the app bundle.


## QuickDraw coordinate conversion

A871 was identified as GlobalToLocal, reached by the fresh MI1 run after about
52 million instructions. Added GlobalToLocal and LocalToGlobal using the current
port's bitmap-bounds origin, matching the documented point coordinate systems.
Focused tests cover negative/nonzero origins, signed points, inverse conversion
and stack cleanup; `build/test_mac_quickdraw` passes. The app was rebuilt,
strictly signature-verified and a fresh MI1 live replay started. Its next result
is pending. Ian reports audible sound and a black angled in-game pointer; pointer
shape fidelity is still unverified.


Ian reports MI1 audio is now faster and less stuttery, with some static still
audible. The current live replay advances through the original title credits
after the coordinate traps, without a runtime error at the latest check. Capture
`mi1-global-local-latest.png` shows the title credits. Sustained playback, input
and playable-scene acceptance remain open.


The live MI1 window has now advanced beyond the title credits to the original
campfire scene. A fresh capture after a scene click shows Guybrush, the lookout
and the response text “I see.” This is direct visual evidence of a scene and
response, though Peekaboo reported the click effect as unverifiable because the
custom-drawn surface has no pressable accessibility element. Do not yet claim
repeatable mouse control, full gameplay or save/reload. Evidence: `mi1-after-
scene-click.png`; foreground Escape remains unverified.


A fresh post-click capture shows the same campfire scene with “Good luck.” where
the prior capture showed “I see.” Peekaboo did not confirm dispatch or focus, so
this before/after change is evidence of a game response but not a repeatable
input guarantee. Do not retry without a fresh target receipt. Evidence:
`mi1-after-foreground-click.png`.


The black frame was a scene transition. A later live capture shows the original
MI1 docks scene, Guybrush, the SCUMM verb panel and “Walk to” text:
`mi1-after-black-check.png`. This establishes execution into a real game scene.
Earlier captures show dialogue changes following click attempts, but Peekaboo
receipts leave click delivery indeterminate. Repeatable input, sustained play,
save/reload and hands-on gameplay acceptance remain open.
A human gameplay report supersedes the earlier uncertainty about mouse input:
Ian moved Guybrush through the docks to the SCUMM Bar, clicked Open door, and
observed the door open with its correct sound effect. Background music then
stopped while the game slowed; other sound effects still played. This is a
specific gameplay/audio regression to reproduce. The live runtime remained
running at about 25–34% CPU after eight minutes. That initial one-second macOS
`sample` showed guest execution and time waiting for a Metal drawable, but no
guest channel state. The opt-in telemetry added below closes that observation
gap. It still does not prove that every reported dropout has the same cause.
Metal currently accelerates presentation only; QuickDraw and framebuffer
conversion still run on CPU.


## MI1 live pacing and sound-channel trace

The next signed app added opt-in `OSPI_AUDIO_TRACE=1` telemetry: displayed
frames per interval, guest instructions/ticks, queued host PCM frames and each
active guest channel. The running window is PID 11322; trace: `/tmp/ospi-mi1-
traced-live.log`. Full `make test` completed successfully in
`/tmp/ospi-audio-trace-full-tests.log`; the focused video check and strict app
signature check also pass.

After the first 20 seconds, 565 measured intervals covered 577.2 seconds and
32,861 displayed frames: 56.9 FPS weighted across the run. The median interval
was 60 FPS and the 10th percentile was 57 FPS; 24 intervals fell below 45 FPS.
Severe stalls included 4 frames in 2.246 seconds, 34 in 1.977 seconds, and 1
in 1.135 seconds (0.9 FPS). During the 2.246-second stall, the host audio queue
was empty at the next producer check while all three guest channels still
reported playing and had queued commands. This confirms host output starvation
can coincide with pacing stalls; it does not prove every reported music dropout
has the same cause.

A one-second `sample` of this run recorded 20 of 40 main-thread samples in
`SDL_RenderPresent` waiting for a Metal drawable and 12 in guest execution.
That is a profile sample, not a frame-time attribution, but it gives the next
optimization a concrete target. The runtime currently executes at most 10,000
guest instructions per presentation-loop iteration, rather than pacing by a
measured 68020 cycle budget. MI2's documented 16 MHz requirement is a useful
calibration point; the minimums do not prescribe a frame rate. Next: decouple
guest CPU/audio cadence from drawable waits, then repeat MI1's scene, sustained
music, and FPS measurements and exercise MI2 at the same 68020 profile.


## Cycle pacing attempt and current live run

The first 16 MHz interactive attempt dispatched A-line traps from Musashi's
instruction hook and halted on an invalid Resource Manager guest address. The
root cause was trap dispatch from the hook, which changes CPU state while
Musashi is between fetch and execution. The pinned open-source Musashi build
now routes A-line opcodes through its existing illegal-instruction callback
inside the generated 1010 handler. `scripts/prepare-musashi.sh` applies this
small, reproducible source patch after verifying the pinned archive checksum.

The fresh signed MI1 build now runs with cycle batches at the 16 MHz target.
An 87-second capture showed mostly 58–60 presented FPS and measured guest
throughput near 16 MHz (sample intervals ranged about 14.4–16.1 MHz). By 4.8
seconds three guest channels were active and 49,664 PCM frames had been
submitted; subsequent trace intervals generally kept 1,024–1,536 frames
queued. This is evidence of PCM production and host queue health in this
capture, not proof of uninterrupted music or human audibility. The user
supplied an audio reference at
<https://www.youtube.com/watch?v=DoVi1__jNsI&t=2242s> (Mac version begins at
37:23), but direct video fetch was throttled; it has not been independently
heard or compared. Manual gameplay, save/reload, and longer audio acceptance
remain open.

Continue by comparing the same soundtrack segment when the video can be
accessed, and repeat measured FPS, guest MHz, queue-starvation, manual input,
and save/reload checks during a longer play session.

Ian later listened to the live SCUMM Bar run. The opening sounded as if every
note doubled for roughly 20 seconds; afterward the background music sounded
mostly acceptable, though not perfect. The supplied screenshot shows the
SCUMM Bar scene and verb panel. Host queue telemetry alone cannot explain the
perceived doubled notes. Added bounded `OSPI_SOUND_NOTE_TRACE=1` diagnostics
for the first 4,096 sampled note commands, including channel, command, note,
phase and rate, to identify overlapping note starts on the next fresh run.
This symptom has not yet been reproduced with the new note-event trace.

## Decoupled pacing and opening-music follow-up

The guest CPU/audio loop now runs on its own SDL thread; the main thread owns
SDL events and frame presentation. Runtime access and framebuffer snapshots
are mutex-protected. The isolated `Monkey Island (Decoupled Test)` process
(PID 93927) measured a 20-second sample averaging 16.14 MHz guest execution
and 59.9 FPS presentation, with no intervals below 14 MHz or 45 FPS. A
five-second macOS `sample` found the presentation thread mostly waiting for a
Metal drawable; guest execution remained independent. This is host presentation
acceleration and pacing evidence only, not GPU-accelerated QuickDraw.

Ian's latest listening report narrows the remaining audio issue: the opening
SCUMM Bar music sounded doubled for about 20 seconds, then mostly acceptable,
though imperfect. Per-pump duration and submitted-frame counters were added to
audio telemetry. The existing live process predates those counters, so they
have not yet been observed in a fresh run. Do not infer an audio cause from
the old log, its 16 MHz rate, or the host queue alone. Next, retain the user's
live session, start a fresh labeled run when available, collect the new pump
counters and bounded note trace during the same opening, and correlate both
with Ian's listening report.

Validation after the telemetry edit: `BUILDFLEET_LOCAL=1 make test` passed all
listed C tests; `BUILDFLEET_LOCAL=1 make runtime` reported up to date;
`git diff --check` passed. macOS 26.6.2, arm64, Apple clang 21.0.0, SDL2
2.32.74. Runtime artifact SHA-256:
`8a22bba30fe1b47705c73d56ca177f71d44843cbe2bd1e665ab2a649c4f62d66`.
At this record the source changes were still uncommitted; dirty-diff SHA-256:
`51beae23bc82b70ffd7ee66bbed8059d44779455c37fa977042d2b235743c616`.

## Audio trace with queue-depth sampling

The previous per-call timer rounded each sub-microsecond pump down to zero.
The trace now accumulates raw performance-counter ticks, reports fractional
average/max pump duration, captures queue-depth extrema/average, and records
the negotiated CoreAudio rate. Sound-note events now include guest time and
the command's half-millisecond duration.

Fresh `Monkey Island (Queue Trace)` process PID 22588, launched from the same
MI1 resource with `OSPI_AUDIO_TRACE=1 OSPI_SOUND_NOTE_TRACE=1`. CoreAudio
reported 48,000 Hz, stereo, 512-frame buffer. Once sound began, sampled
intervals mixed 48,640–49,152 frames/sec; queue averages were 2,046.5–2,047.4
of the 2,048-frame target. Each interval had 2–6 empty-queue observations
among roughly 37,000–82,000 pump checks, indicating brief empty samples but no
sustained queue starvation in this run. Guest intervals remained near 16 MHz
and presentation near 60 FPS. The first 20 seconds of sound-note events had
30 note starts; none matched the same pitch across separate channels within
10 ms, and none repeated the same pitch on one channel within 10 ms. This
rules out that narrow duplicate-command pattern in this capture; it does not
prove the mixed waveform matches the original or that the opening music is
perceptually correct. Comparison to the supplied YouTube reference remains
unavailable because fetch is throttled. Ian's reported doubled opening notes
still need his listening acceptance against this fresh run.

Ian's latest live report: in the SCUMM Bar, the opening sounded as though each
note played twice for about 20 seconds, then the background music became mostly
acceptable, though imperfect. A subsequent fresh trace showed `clip-frames=0`
and `peak-abs=28568` in the first ten-second intervals, with queue averages
near 2,047/2,048 frames. That rules out clipping in those intervals only; it
does not explain or reproduce the reported doubled notes. The first note events
in that run began around guest time 2.7 seconds on three different channels;
this is not evidence of duplicate commands or of correct audible music.

The same capture's presented FPS returned to 60 after intentional long host
pauses. A >250 ms worker-loop gap now rebases the guest clock and drops catch-up
debt. Tests cover the threshold and ensure a paused interval does not trigger
catch-up; this protects resumed sessions, but is unrelated to the SCUMM Bar
audio symptom. Continue with a fresh, uninterrupted SCUMM Bar run and compare
the user-reported segment while collecting note events and actual host queue
health. The sound remains unaccepted.

## Latest live trace review

The still-running `Monkey Island (Resume Check)` process (PID 38219) is using
the latest committed runtime. Its log includes deliberate host pauses, so it is
not an acceptance run. Across 423 one-second-ish intervals after the pause,
submitted audio varied from 5.3k to 58.4k frames/sec (median 36.2k), despite
CoreAudio and the default USB output both reporting 48 kHz. Average queued
depth remained near 2,047/2,048 frames; clipping was usually zero but reached
23 frames in an interval. Guest execution and presentation were generally near
16 MHz and 60 FPS after resuming. This discrepancy makes the current audio
rate counter and queue-depth samples inadequate to prove steady playback.

The first 22 seconds of guest-time note events in that run contained 23 note
starts and no same-pitch starts within 500 ms. Several different pitches began
on separate channels together, so the trace alone cannot identify which notes
should be audible. These guest-command observations do not prove that the
mixer waveform or host playback matches the reference.

The latest build now reports SDL queue-balanced consumption (`consumed-fps`)
and an estimated shortfall against the nominal 48 kHz rate (`deficit-est`), in
addition to submitted frames. The estimate is valid only in rows labeled
`queue-balance=valid`; confirm it with a clean run before drawing conclusions.
A silent SDL callback-mode probe on the default CoreAudio device measured
47,896 callback frames/sec over 8.135 seconds at 48 kHz. This checks the host
callback clock, not the queued-audio path used by OS Pi. Separate SDL queue
probes with delayed polling underfed, so they cannot isolate device consumption
from probe-thread scheduling. A dummy-video attempt also could not reach runtime
audio because its renderer failed OS Pi's accelerated-renderer/vsync check.
The running PID 38219 predates the new queue-balance metrics. Next, capture a
fresh uninterrupted SCUMM Bar run and compare those metrics while keeping Ian's
audible comparison as a separate gate.

## Monkey Island 2 VBL retest

The current source includes the 120-tick-per-poll VBL work limit, which keeps
overdue work queued instead of halting. I launched the installed local MI2
resource with the latest `build/ospi-runtime` and `OSPI_AUDIO_TRACE=1`, while
SIGSTOP-preserving the MI1 session (PID 38219). The fresh MI2 process (PID
65811) reached the original copy-protection prompt, accepted Return, and
advanced to its manual lookup screen. It remained live for 115 seconds with no
VBL catch-up halt. This verifies prompt input and recovery past the earlier
failure point, not gameplay; no manual answer or bypass was attempted.

Across 112 guest intervals, guest execution measured 15.98–17.1 MHz (median
16.45 MHz). Presentation ranged 54–60.9 FPS, with no interval below 45 FPS.
Across 95 valid queue-balance intervals after startup, consumed rate measured
46.2–49k frames/sec (median 48.0k) against the negotiated 48 kHz device. The
nominal deficit estimate was zero in half of those intervals and peaked at
1,792 frames in the rest; no clipping was recorded. This is stronger steady
state audio-queue evidence than the older `mixed-fps` counter, but the manual
lookup screen does not exercise the game's soundtrack or establish audible
fidelity. The live state is preserved at that screen for Ian's manual input.
Trace: `/tmp/ospi-mi2-vbl-retest.log`; screen capture:
`/tmp/ospi-mi2-after-return.png`.

## MI1 queue-balance follow-up

Ian listened to MI1 in the SCUMM Bar and reported that the opening sounded as if
each note played twice for about 20 seconds; afterward the background music was
mostly acceptable, though imperfect. He then reported the scene had become slow
and background music had stopped, while a door interaction sound still played.
This separates intermittent/music sequencing from the one-shot effect but does
not identify the cause.

Two fresh current-binary runs were captured with note and queue tracing. The
shorter run (PID 77102, 185 seconds) had queue-balance consumption mostly around
35–37k frames/sec. In the longer run (PID 79395, 678 seconds), queue-balance
consumption was mostly 35–36k frames/sec through host time 300 seconds, then
mostly 46–48.4k frames/sec from 300–400 seconds. At the latest sample it was
48.2k frames/sec; presenter reported 60 FPS and guest execution 16.32 MHz.
Queue depth averaged 2,047/2,048 frames and clipping was zero in that sample.
The transition's cause is unknown. A 256-frame batching experiment was made
while retaining 64-frame chunks when callback-driven sound was pending; its
trace did not show an early improvement, so the source change was reverted and
the original 64-frame implementation rebuilt. Do not attribute the later rate
change to that experiment.

The note trace recorded guest commands, not the final waveform, and overlapping
same-pitch events can arise across channels. It cannot confirm or refute the
reported doubled sound perceptually. The active process (PID 79395) was started
from the experimental binary before reversion; it was left running to preserve
Ian's game state. A newly launched process is needed to exercise the rebuilt
original batching implementation. No audio fix or full acceleration is
accepted; GPU-backed guest QuickDraw, audio fidelity, save/reload, and Ian's
hands-on acceptance remain open.

Validation: `BUILDFLEET_LOCAL=1 make runtime` linked the reverted source
successfully (the linker emitted its existing section-alignment warning).
The full test suite passed before reverting; tests were not rerun because the
final source matches the prior committed implementation.

### Later live sample

At host time 857–858 seconds, the same pre-revert process again reported only
35.6–38.2k consumed frames/sec; one interval ended with zero queued frames and
12–15 empty-queue observations. Guest execution remained 15.74–16.71 MHz and
presentation 60 FPS. This disproves treating the earlier 300–400 second
48k-rate period as a sustained recovery. A five-second macOS `sample` captured
the main thread waiting in Metal `nextDrawable` for vsync, while the guest
worker mostly slept at its 1 ms pacing point and spent a minority of samples
in guest CPU execution; audio pumping and mixing were sparse. This does not
show CPU saturation or identify why queue-balanced output rate varies. Sample:
`/tmp/ospi-mi1-after-audio-scheduling.sample`; trace:
`/tmp/ospi-mi1-audio-batch256.log`. The live PID remains on the experimental
binary; it was not restarted or altered to preserve the running game state.

## MI1 callback audio output

The queue-balance trace suggested a host playback-rate problem rather than
clipping alone. Two standalone probes used SDL2/CoreAudio at 48 kHz: a bounded
`SDL_QueueAudio` silence queue drained at about 33.5–35.3k frames/sec, while an
SDL callback probe counted 47,956 frames/sec over 10.2 seconds. This isolated
the shortfall to the queued-audio path on this host and motivated callback
output with a PCM ring. No guest code runs on the callback thread.

The runtime now uses an 8,192-frame single-producer/single-consumer PCM ring,
primes to 4,096 queued frames (about 85 ms at 48 kHz), and lets the SDL callback
copy frames into CoreAudio. The guest worker remains the sole producer and
continues to poll Sound Manager callbacks. The callback only copies ring data,
updates atomic cursors, and counts underruns.

On the final rebuilt runtime, a fresh MI1 window reached the docks/Goat-sign
scene. From host time 20 seconds through about 105 seconds, 104 audio intervals
measured producer median 48,031 frames/sec and callback median 48,069
frames/sec. The callback recorded 1,536 underrun frames in total (32 ms), with
no more than 1,024 in any one interval; the mixer recorded 75 clipped samples
in total across those intervals. Guest clock median was 16.29 MHz, range
15.49–17.0 MHz; presentation median was 60 FPS, minimum 52.5. These counters
are runtime evidence, not a human listening verdict. Ian's check of doubled
notes, stutter, sync, and the new audio latency is still pending. Save/reload
and full GPU acceleration are also unverified.

Live trace: `/tmp/ospi-mi1-callback-ring-final.log`; screenshot:
`/tmp/ospi-mi1-callback-ring-final.png`. Earlier 2,048-frame callback ring was
insufficient and recorded recurring underruns; the 4,096-frame target is the
current experiment. Validation: `BUILDFLEET_LOCAL=1 make test runtime` passed
all listed C tests and linked the runtime. The linker emitted its existing
common-section alignment warning. Source and live MI1 evidence use the final
callback-ring build; no source commit has been made yet.

Validation after these instrumentation changes: `BUILDFLEET_LOCAL=1 make
test` passed all listed C tests, `BUILDFLEET_LOCAL=1 make runtime` linked
successfully, and `git diff --check` passed. macOS 26.6.2 arm64, Apple clang
21.0.0, SDL2 2.32.74. Runtime artifact SHA-256:
`7f2df4fde98558d8e0df7b28fa9f629c90c0bba673babb76f820f2ffdfa52eed`.
