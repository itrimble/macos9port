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
