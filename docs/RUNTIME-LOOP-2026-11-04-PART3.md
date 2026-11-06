# Runtime Loop — 2026-11-04, Part 3

## MI1 music report

Ian reports the SCUMM Bar music was audible. In the latest listening report on
the rebuilt runtime, the opening sounded as though each note played twice for
roughly 20 seconds; the background music then sounded mostly alright, though
imperfect, before returning to its usual sound. This is a listening report, not
a loopback capture. The prior note-command trace logged 26 events during its
first 20 guest seconds and no same-pitch reissue within 250 ms; it does not
measure rendered PCM or cover this listening session. The prior callback trace
also recorded a 754 ms guest scheduling gap, a 7 FPS interval, and 34,304
callback underrun frames near startup. These are leads, not a demonstrated
cause or fix. Later intervals recovered. Keep startup ring-buffer priming,
callback underruns, scheduling gaps, guest audio commands, and rendered PCM
distinct while reproducing the first 20 seconds. Full audio fidelity and GPU
execution of guest QuickDraw remain unverified.

Follow-up process inspection found two live runtimes on this resource. CUA
identified the OS Pi app's Monkey Island window; PID 74631 is its child runtime.
PID 8926 was a standalone diagnostic run with stdout/stderr attached to
`/tmp/ospi-mi1-callback-ring-final.log`, so I stopped that test process and left
the OS Pi app and its game session running. Its last trace interval showed
16.03 MHz guest execution, 60 FPS presentation, 48,398 callback frames/sec,
zero underruns, and a 1,536-frame ring minimum. This removes a real concurrent
playback/load confound for further listening, but does not prove that it caused
the doubled notes. Ian's latest listening report came from the SCUMM Bar after
the diagnostic runtime had been stopped: the opening sounded doubled for about
20 seconds, then mostly alright before returning to its usual sound. That
confirms the symptom persists without the extra runtime, but does not identify
its cause.

## Rebuilt Finder bundle and current MI1 run

PID 74631 was the app's MI1 session using the disposable
`build/loop-20261104` game copy. No save file exists in that folder. After
closing it, I ran `BUILDFLEET_LOCAL=1 make app` directly. The new bundled
runtime and standalone runtime now have the same SHA-256:
`3e1a78c009cb4aae776484b6c9e48dffe7f7491e20996467cfc33d69a224ff7c`.
`codesign --verify --deep --strict --verbose=2` reports that the app is valid
on disk and satisfies its designated requirement. `open 'build/OS Pi.app'`
launched the Finder app and displayed the OS Pi desktop.

The rebuilt runtime then launched the same disposable MI1 resource directly
with `OSPI_AUDIO_TRACE=1`; this measured the current runtime, but did not prove
that double-clicking the game from the OS Pi desktop works. Ian has now
listened to this rebuilt runtime and reports the opening music duplication;
the sound-quality gate remains open. Over the first
minute, guest execution stayed around 15.9–16.2 MHz and presentation around
59–60 FPS after startup. After the audio ring filled, trace intervals showed
about 47.6–50.6k producer frames/sec, 47.6–49k callback frames/sec, zero
underrun frames, and zero clipped frames. Initial audio startup had an empty
ring before priming; the later stable intervals had a 1,024–2,560 frame queue.
These counters do not establish how the music sounds. Ian listened to this
rebuilt run and reported doubled notes for about 20 seconds before the music
improved and later returned to normal. The audible startup issue remains
unresolved even though later trace intervals had zero underruns and zero
clipped frames.

The initial CUA clicks used coordinates from a resized preview and landed above
the Applications icon. Using the full-resolution window screenshot, a click
selected Applications and a double-click opened the folder. This confirms the
OS Pi Finder accepts those mouse events. A later coordinate attempt opened
Castle of Dr. Brain's folder instead of Monkey Island's; no game was launched
from Finder, so application launch remains unverified. A Cmd-W recovery closed
the OS Pi app window, and the app was relaunched; the existing MI1 runtime was
left running. Ian's audio acceptance also remains open: he reports that the
opening music sounded doubled for about 20 seconds, then mostly alright before
returning to its usual sound.

## PCM capture diagnostic — 2026-11-05

Added opt-in capture of the producer's mixed stereo PCM to a WAV file. It is
bounded to 120 seconds, stores samples in memory during gameplay, and writes
the file only when the diagnostic runtime exits, avoiding disk I/O on the
real-time audio callback. Use `build/ospi-runtime-capture` with
`OSPI_AUDIO_WAV=/tmp/mi1.wav`; pair it with `OSPI_AUDIO_TRACE=1` and
`OSPI_SOUND_NOTE_TRACE=1` to compare note commands, rendered PCM, and callback
underruns. The WAV does not include host callback silence or prove what was
audible. A dummy-driver smoke check verified the WAV header, 48 kHz stereo
format, and exact sample values. No Monkey Island capture has been made yet;
the live OS Pi session was left running and was not overlapped with a second
game instance.

## Validation and safe handoff

On 2026-11-05, focused `BUILDFLEET_LOCAL=1 make build/test_ospi
build/test_native_launch && ./build/test_ospi && ./build/test_native_launch`
completed successfully (`ok`; `Native launch checks: ok`). These cover the
Finder double-click state machine and the detached native runtime launcher
separately; they do not prove the graphical Finder launched Monkey Island.
In the relaunched OS Pi window, CUA foreground clicks aimed at the full-size
Applications icon still did not change the selection or open the folder. The
prior folder-open observation stands, but desktop click delivery remains
inconsistent and game launch from Finder is unverified.

The installed REA CLI is 6.8.0, matching the official npm registry version.
`rea doctor --client codex --json` reports the Codex registration aligned, but
the active session has no REA MCP tools. Native analysis providers (Hopper,
Ghidra, IDA, Kuna) are unavailable, and the installed skill copy is stale.
No REA setup or provider installation was performed. Continue with source,
tests, runtime traces, and the opt-in PCM capture until REA reconnects or a
native provider becomes available. No MI1 producer-PCM WAV has been captured.

`BUILDFLEET_LOCAL=1 make test` passed the full local C suite earlier on this
checkout; the linker emitted its existing common-section alignment warning.
`BUILDFLEET_LOCAL=1 make runtime` reported no work because the standalone
runtime was already current. The stale bundle has now been replaced only after
the disposable game session closed. The rebuilt app is running on the desktop,
and its runtime is running MI1 from the disposable copy. The installed game
volume and its saves were not changed.

## Current continuation — 2026-11-05

The full local `BUILDFLEET_LOCAL=1 make test` suite passed, including the OS Pi
Finder state machine, Macintosh QuickDraw, sound, timer, resource-service, and
native launch checks. `BUILDFLEET_LOCAL=1 make runtime` then linked the
standalone runtime from the current dirty source tree. The linker emitted its
existing macOS common-section alignment warning; the build exited successfully.
These tests and this standalone build do not refresh or validate the running
app bundle, nor do they prove that a guest application launched from Finder.
The new standalone runtime hash is `1c5eccb4…0f96a4d`; the live app bundle still
contains `3e1a78c0…224ff7c`.

PID 47064 remains the live MI1 runtime and PID 60616 remains the OS Pi desktop.
The latest Finder automation did not select or open Applications. A subsequent
mis-targeted double-click collapsed the OS π HD window; no game launch or game
input was attempted. The earlier successful Applications-folder open remains
historical evidence, but graphical Finder launch is still unverified. The
installed games and saves were not changed. Ian's latest SCUMM Bar listening
report matches the unresolved short opening-music doubling already described
above; no new audio capture or cause was established in this pass.

To test the just-built runtime without replacing the live bundle, I staged a
separate ignored `build/OS Pi Next.app`, copied in the current `build/ospi` and
`build/ospi-runtime`, assigned it a separate bundle identifier, ad-hoc signed
it, and verified its signature. The staged runtime hash is
`1c5eccb4…0f96a4d`. PID 78542 opened the OS Pi desktop. A double-click opened
Applications. Subsequent CUA attempts to open the Monkey Island folder were
inconsistent: a click aimed there opened Castle of Dr. Brain, a later click
selected Monkey Island, and the following double-click opened NativeChecker.
No runtime child launched from PID 78542, and no game or save files were
changed. This confirms the staged build can open Applications, but does not
prove game launch from Finder. The failure may be in coordinate delivery or
hit testing; collect event/mapped coordinates from the next reproduction
before changing input scaling by guesswork.

REA CLI 6.8.0 matched the official npm release and reported the supplied
resource fork as an unsupported binary format. No native analysis provider or
client configuration was installed. This limits REA inspection of the fork;
the source tree, parser, hashes, and process logs remain directly inspectable.

## Finder launch and desktop integration — 2026-11-05

Ian manually opened Applications → Monkey Island → Monkey Island in the
staged OS Pi Next desktop. The classic runtime opened and the user reported
audible music while the game started; the live window advanced from the title
card into the opening scene. This confirms the Finder launch path for this
installed resource fork. It does not establish sustained play, save/reload,
frame rate, or audio fidelity. The earlier report of doubled opening notes for
about 20 seconds remains open; no capture was made in this session.

The game is a separate host SDL window. Ian reports that it can be dragged
outside the OS Pi desktop frame. Source inspection confirms that
`mac_runtime_window` calls `SDL_CreateWindow`, so the runtime is not yet a
child surface managed and clipped by OS Pi. R08 tracks this integration.

The Finder currently draws every regular file as the same document glyph. The
installed Monkey Island resource fork contains an application `BNDL`, `FREF`,
`ICN#`, `icl4`, and `icl8` icon family. REA 6.8.0's artifact inspector reports
resource forks as an unsupported format, so this evidence came from OS Pi's
bounded resource reader. Source now recognizes a sibling fork with `CODE`
resource 0 as a launchable application and reads its `BNDL` mapping to render
the guest's `icl8` icon using the paired `ICN#` mask. The icon remains in the
user-supplied fork; no guest art was copied into the repository. Folder,
document, disk, and trash artwork gained shading and depth. This decoder has
not yet been visually checked, and the staged desktop currently running
predates these changes. R09 remains open pending a fresh visual check. No guest
files, saves, or running game sessions were changed.

## MI2 manual lookup

I copied the installed Mac game directory to disposable `/tmp` copies and
launched each with the latest local runtime. The fresh run reaches the Voodoo
recipe lookup and stays live. In a disposable copy, 50/38 for Warts and 27/12
for Worms were rejected. On a fresh run, 26/17 was rejected for Scurvy with
Breath Mints and Boxes of Voodoo Helper; 38/17 was rejected for Scurvy with
Bloated Ticks and Servings of Fruit Cocktail. 56/16 was rejected for Gout with
Grave Digger's Socks and Servings of Fruit Cocktail. These tries disprove the
current visual/index mapping, not runtime execution. The generic
[answer table](https://steamcommunity.com/sharedfiles/filedetails/?id=440904699)
is for a classic floppy release, and the
[Mac-specific ScummVM history](https://lists.scummvm.org/pipermail/scummvm-git-logs/2021-July/088305.html)
documents separate Mac floppy and CD boot scripts; the CD release omits copy
protection. Do not treat 44/34 from the generic online wheel as a Mac answer.
The scanned [technical manual](https://www.mocagh.org/lucasfilm/mi2kixx-manual.pdf)
says any four digits work specifically for PC users; that instruction does not
explain this Mac floppy build's rejected entries. Its on-screen prompt confirms
the protected floppy path, so use Mac-edition wheel evidence before trying
another value.

The [generic classic-floppy answer table](https://steamcommunity.com/sharedfiles/filedetails/?id=440904699)
lists Warts as 50/38 (along with another variant, 19/12). The captured game
prompt already supplies the first quantity, 50, and leaves the second quantity
blank. A focused next trial is therefore to enter only 38 in that blank, once,
on a fresh disposable copy. The earlier `50/38` rejection note does not record
whether both values were entered, so it does not rule out this interpretation;
the table is still not Mac-specific proof.

An isolated test reached the copy-protection prompt and was closed without
touching the installed game or saves. During that run the guest worker logged
324 ms and 442 ms host scheduling gaps and dropped catch-up debt. The installed
floppy release's correct wheel mapping remains unconfirmed; gameplay was not
reached.

Runtime evidence from the earlier MI2 trace remains: 16.45 MHz median guest
clock, 54–60.9 presenter FPS, and 46.2–49k queued audio frames/sec. These
measure the current lookup screen, not sustained gameplay. Keep game code entry
separate from acceptance: the next milestone is a normal playable scene, then
input, sound, pacing, and save/reload checks.

## MI2 full-mode startup and Doubleback follow-up — 2026-11-05

I implemented bounded `ChangedResource` bookkeeping after the fresh Mac floppy
copy stopped on A-line trap A9AA. It sets the resource's changed attribute and
retains its data handle on release; it does not write changes back to disk. A
focused resource-service regression checks the attribute. The complete
`BUILDFLEET_LOCAL=1 make test` suite passed, then
`BUILDFLEET_LOCAL=1 make runtime` rebuilt `build/ospi-runtime` (SHA-256
`1963fe36b17f51d1e5aca307374cd8dc835be74a257ebae830303a207a66d547`).

On a new disposable copy, I entered the Mac floppy recipe shown for Trench
Mouth (Grave Digger's Socks with Cheese Squigglies). The classic-floppy table's
54/54 entry was accepted by this Mac build. Selecting the full Monkey Island 2
option previously stopped at the runtime's 10,000-instruction Doubleback
callback ceiling. I raised the bounded callback allowance to 100,000
instructions and added PC, stack, and consumed-instruction details to future
failure reports. A regression now runs a valid callback containing 12,000 NOPs
before it returns; the full suite passes.

The rebuilt runtime accepted the full-game selection and showed the opening
Scabb Island scene. After about five minutes, it was still running and had
advanced to the opening subtitle. This run used `SDL_AUDIODRIVER=dummy`, so it
provides no MI2 audio evidence. It has not yet demonstrated direct gameplay
input, sustained music, save/reload, or measured frame pacing. The game copy
and all its files remain in `build/loop-20261105/mi2/full-mode-100k`; the
installed copy and saves were not touched. This proves startup through a
normal game scene, not compatibility completion.

## MI2 timer-callback follow-up — 2026-11-05

The next scene exposed the same callback ceiling in the Time Manager. I raised
its bounded callback budget to 100,000 instructions and lifted the cycle-based
service budget to the same maximum. Failure reports now include the callback's
PC, stack pointer, and consumed instructions. A deterministic regression runs
a valid timer callback through 12,000 NOP instructions before returning. The
full local suite and runtime build passed again; the rebuilt runtime hash is
`8f8469b2f610964ee904489d92279f782d44be019750742f83cb91971d9d8861`.

A new `build/loop-20261105/mi2/timer-100k` disposable copy accepted the
classic-floppy table's 56/47 Gout answer, entered full mode, and continued
through the opening LucasArts card and multiple story scenes. More than four
minutes after launch it remained live. Space paused and resumed the game;
Escape advanced the intro. It has not reached free-play. Audio remained on
SDL's dummy driver, so sound, music, and pacing are unverified. No installed
game data or saves were changed. The current screen is still the scripted
opening, not a gameplay acceptance pass.

## Performance and icon follow-up — 2026-11-05

The live Finder-launched MI1 process (PID 44457) remains open. Its private log
confirms the Metal presenter and CoreAudio at 48 kHz, but explicitly says guest
QuickDraw and framebuffer conversion run on the CPU. The session was started
without trace flags; `ps` showed a cumulative 43.8% CPU figure, which is not an
FPS measurement. I added `OSPI_PERF_TRACE=1` as a dedicated opt-in for
once-per-second presentation measurements: average FPS and
p50/p95/p99/maximum frame intervals. The first build exposed a flag-gating
error that enabled guest-clock tracing but not presenter measurements; that is
fixed. These timings do not measure GPU execution time or input latency.

`BUILDFLEET_LOCAL=1 make build/ospi-runtime` completed with Apple clang 21.0.0;
the new standalone runtime SHA-256 is
`8b00410c6512e576a462b2afab1f891d13f6f1ab354772c6ea4eb04f44a502ad`. Homebrew
reported no pending SDL2 or FreeType updates. This build is not installed into
the running `OS Pi Next.app`, and I left the active game untouched. The source
also now classifies Finder applications and decodes their guest icon family;
the direct `volume_list` probe reports `Monkey Island` as an application with
its guest icon loaded from the installed `.rsrc` fork. An off-screen Finder
render at 1024 by 768 shows that icon beside the original game data files; it
does not replace the live staged UI. The clean-room folder, disk, document, and
trash artwork still needs comparison with the supplied reference. The staged
UI predates the change, so the live Finder needs a later fresh launch.
Window containment remains an architectural blocker: the guest still owns a
separate SDL/macOS window.

Validation for this increment: `BUILDFLEET_LOCAL=1 make test` passed the full
local suite, including deterministic frame-metric and key-mapping tests. The
runtime build emitted Apple's linker warning about reducing
`__DATA,__common` alignment from 0x8000 to 0x4000; linking and tests completed.
A temporary off-screen Finder probe rendered the current 1024 by 768 desktop
and confirmed `Monkey Island` uses its loaded guest icon. The rendered frame is
at `/tmp/ospi-finder-check.png`; no extra desktop window was opened.

Ian's 5 November screenshots confirm the live MI1 window reached the Lucasfilm
Games splash and then its title sequence, with startup audio audible. The
separate host window can be dragged outside OS Pi, which is visibly wrong and
remains R08; the runtime needs a shared guest surface composited by the Finder
window with input routed back to the guest. A window-style or title-bar change
will not contain a separate process window. The Finder screenshot also shows a
generic document icon for the game executable. That session predates the fresh
candidate: an off-screen render from current source shows the installed game's
own icon resource, but it is not yet verified in a fresh interactive Finder.
Compare built-in disk, folder, document, and Trash art against the supplied
references using original clean-room drawings only. Keep reading game icons from
user-installed resource forks; do not copy Apple artwork or user game data into
this repository. Neither the running Finder nor active MI1 session was restarted
to pick up current sources. No FPS was measured from this live session, and full
GPU acceleration remains unproven.

I built a separate fresh `build/OS Pi.app` bundle from the current sources and
confirmed its launcher can seed an isolated volume and its bundled runtime can
inspect Ian's installed MI1 fork (80 resources). The live MI1 belongs to the
older `build/OS Pi Next.app`; I did not replace or restart it. This verifies
packaging and resource loading, not a fresh interactive launch or changed live
Finder artwork.

The icon mismatch had a concrete rendering bug: indices 216–255 of the classic
8-bit icon palette were being treated as one grayscale ramp. They are four
separate 10-step red, green, blue, and gray ramps after the 6×6×6 color cube.
I corrected that mapping and added palette-boundary tests.
`BUILDFLEET_LOCAL=1 make test` passed with the new checks, and a rebuilt
`build/OS Pi.app` contains the corrected Finder renderer. I have not visually
reviewed it in a fresh interactive desktop; the live `OS Pi Next.app` still uses
its older renderer.
To avoid opening another desktop window, I rendered the installed
`Applications/Monkey Island` folder directly through `session_paint` into
`/tmp/ospi-icon-review.png`. The rendered app icon is the game's own pixel-art
icon rather than a generic document. This verifies the guest resource path and
corrected palette in the current source; the stock disk/folder/Trash set still
needs comparison against Ian's references.

I corrected the app's About and Help text plus the fresh-volume Read Me and
empty Applications notice to describe OS Pi's own runtime for supported 68k
apps. The separate legacy archive launcher is still identified as a fallback.
Regression checks verify the new-volume text and Help window. The full local
`make test` passed, and the rebuilt `build/OS Pi.app` launcher hash is
`b92f46d529f2f967e708b8f5de473a58a7be49bb2b5a81ab39a97e92f8a87df5`.
I did not overwrite the installed volume's existing documents or the running
`OS Pi Next.app`; PID 44457 remains on the previous build.

Finder-launched runtime children now receive `OSPI_PERF_TRACE=1` by default
unless the user has already set the flag. The native-launch test checks that
child environment, and the full local suite passed. This makes the next fresh
Finder game run log presenter FPS and frame-time percentiles into its private
runtime log. PID 44457 was not relaunched, so it still has no live FPS sample.

## Embedded guest-window bridge — 2026-11-05

To address the separate-window behavior Ian reported, I added a POSIX shared-
memory bridge between OS Pi and the independent runtime. The child publishes
its framebuffer and accepts mouse/key events through a bounded queue. The
desktop source now composites it as a draggable OS Pi window, confines that
window to the desktop bounds, maps input into guest coordinates, and handles
close/focus transitions. Bridge mode does not create a host SDL window. Guest
QuickDraw and framebuffer conversion remain CPU work; this is window
containment, not GPU acceleration.

The bridge tests pass for cross-process mapping, frame transfer, input delivery,
and state changes. A disposable MI1 process published READY 640x480 frames; the
last probe read sequence 398. `BUILDFLEET_LOCAL=1 make test` passed the full
suite after these source changes. The build reported Apple's linker warning
that `__DATA,__common` alignment was reduced from 0x8000 to 0x4000.

I launched an isolated desktop using `/tmp/ospi-embedded-volume.tDTEiY`, whose
Applications folder contains a copy of MI1. The fresh screenshot shows the
current built-in disk, folder, document, and Trash artwork. CUA Driver sent a
foreground double-click to Applications, but the folder did not open; no game
child was launched from the desktop. I closed only that isolated desktop. The
older live MI1 process (PID 44457), all other existing sessions, and installed
game data were left alone. The screenshot is evidence of current icons, not a
completed visual comparison; the supplied Mac OS 9 icon references still need
an item-by-item review and clean-room refinement.

R08 remains open until a real desktop-launched MI1 session visibly runs inside
the OS Pi window, responds to routed input, and stays within its bounds while
dragged. The shared frame probe alone does not prove this end-to-end behavior.
The active older session still has no measured FPS trace, and guest graphics
acceleration is not implemented.

## Finder application dispatch — 2026-11-05

Code review found `open_item()` handled files and folders but let
`ITEM_APPLICATION` fall through to `open_folder()`. I added a dedicated branch
that calls `native_open()` for the app's resource fork and reports an error if
the CODE resource is unsupported. This is the dispatch path needed for the
embedded runtime; it does not by itself prove a game launch.

I rebuilt `build/ospi` and reran `BUILDFLEET_LOCAL=1 make test`; the build and
full local suite passed, including `test_native_launch` and
`test_runtime_bridge`. `git diff --check` passed. `src/session.c` is 498 lines,
within the project's file-size limit. The built executable SHA-256 is
`4defb3681b50b50c36670920984166204c03eef72e2723ee1ee1a70f53ecc2ee1`.

An isolated GUI retry did not establish the desktop open path. A background
double-click intended for Applications instead opened System Folder; input
trace showed two button presses at SDL `(0,0)` and no selected item. A later
foreground double-click reported screen point `(737,338)` but left the desktop
unchanged. This indicates unreliable automation-to-SDL pointer delivery in
this check; it does not establish whether physical mouse input has the same
problem. No fresh Finder-launched game window, routed game input, or containment
was visually verified. The disposable test desktop was closed; PID 44457 and
installed game data were left alone. R08 stays open.

## Integration loop and logo stall — 5 November, evening

The delivered bundle tip is `1c1b1b9`, not the theme handoff's `f876cdb`.
All 1,499 files under assets matched it byte-for-byte. Local commit `1236aef`
retains those original wallpaper/icon/sound assets; nothing was pushed.
The default merge script refused untracked assets rather than overwriting them.
The theme engine, patch and theme-aware script remain absent.

The existing startup-test linkage was already fixed. Integration adds desktop
icon loading, bounded host sound playback with persisted mute/guest suppression,
startup scratch cleanup and desktop-only F9/F10 handling. Source extraction
brings main, session, typeface and runtime-video files below 500 lines.
`APP_BUNDLE` permits separate test packaging without replacing a live bundle.

PID 36647 repeatedly published the same logo frame while its guest CPU kept
executing near the configured 16 MHz. Two reads advanced the bridge sequence
from 11,457,000 to 11,681,773 with identical hash `e6e7153a10ef68eb`.
Sampling found the host main thread repeatedly converting/publishing frames;
the bridge path lacked standalone-renderer vsync. It published roughly 9,400
frames/s. A bounded 17ms wait outside the runtime mutex now limits that path.
This is a CPU-waste fix, not a proven explanation of the guest's logo wait.

Twenty-second disposable probes of the exact live bundled artifact advanced
through changing frames with both CoreAudio and dummy audio. Patched and
unpaced source builds also progressed. The original accumulated live-state
stall remains unexplained; no speculative low-memory timer change was applied.
The paced probe published only about 9–12 frames/s under this workload, so it
is not a 60 FPS result or final performance acceptance. Original runs/saves
were preserved. Probe records and the integration source-hash inventory are
in ignored `build/integration-20261105/evidence/`.

A final bridge review found a separate crash-recovery defect: a dead guest
could leave the process-shared mutex locked forever and hang desktop reads.
All acquisition is now bounded to 10ms, with an independent atomic close flag.
Tests cover a live peer holding the lock, recovery after release, and a peer
exiting while holding it. Bridge ABI is version 2; desktop and runtime must be
packaged together. This does not establish the cause of the observed logo wait.

The first integration bundle passed the complete local suite, host-sound test
and strict signature verification. It was launched against a copied volume as
PID 70049. A real window capture shows the delivered folder/disk/trash icons;
the UI sound device opened without a logged error. Listening, startup motion,
and menu interaction still require Ian's feedback. Automated menu focus timed
out; the follow-up capture showed no open menu, so no menu success is claimed.

R2 validation: `BUILDFLEET_LOCAL=1 make -j2 test all test-desktop-sound`
completed with exit 0, followed by separate R2 app packaging and strict code
signature verification. The only recorded linker warning reduces Musashi's
common-section alignment; no C compiler warning or test failure was observed.
Receipts, source hashes and test logs are under the integration evidence folder.
Startup has 22 checks; asset/sound and dead-owner bridge regressions pass.
No current Linux, device, full acceleration or sustained gameplay pass is claimed.
