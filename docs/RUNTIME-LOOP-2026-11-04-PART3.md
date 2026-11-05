# Runtime Loop — 2026-11-04, Part 3

## MI1 music report

Ian reports the SCUMM Bar music was audible. In the latest listening report,
the opening sounded as though each note played twice for roughly 20 seconds;
the background music then sounded mostly alright, though imperfect, before
returning to its usual sound. This is a listening report, not a loopback
capture. The prior callback trace had no
duplicate same-pitch commands, but recorded a 754 ms guest scheduling gap, a
7 FPS interval, and 34,304 callback underrun frames near startup. This is a
correlation to investigate, not a demonstrated cause or fix. Later intervals
recovered. Keep startup ring-buffer priming, callback underruns, scheduling
gaps, and guest audio command timing distinct while reproducing the first 20
seconds. Full audio fidelity and GPU execution of guest QuickDraw remain
unverified.

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
that double-clicking the game from the OS Pi desktop works. Over the first
minute, guest execution stayed around 15.9–16.2 MHz and presentation around
59–60 FPS after startup. After the audio ring filled, trace intervals showed
about 47.6–50.6k producer frames/sec, 47.6–49k callback frames/sec, zero
underrun frames, and zero clipped frames. Initial audio startup had an empty
ring before priming; the later stable intervals had a 1,024–2,560 frame queue.
These counters do not establish how the music sounds. Ian has not yet listened
to this rebuilt run, so the reported doubled opening notes remain unresolved.

CUA could move the pointer over the desktop's Applications folder, but its
click actions did not select or open it. I did not treat that automation result
as proof that physical mouse clicks fail. Finder-based game launch and Ian's
audio acceptance remain open.

## Validation and safe handoff

`BUILDFLEET_LOCAL=1 make test` passed the full local C suite earlier on this
checkout; the linker emitted its existing common-section alignment warning.
`BUILDFLEET_LOCAL=1 make runtime` reported no work because the standalone
runtime was already current. The stale bundle has now been replaced only after
the disposable game session closed. The rebuilt app is running on the desktop,
and its runtime is running MI1 from the disposable copy. The installed game
volume and its saves were not changed.

REA CLI 6.8.0 matched the npm release checked on 4 November. The scoped Codex
registration was aligned, but the shared REA skill copy was stale and the live
MCP server status unknown. `rea analyze build/ospi-runtime` could not inspect
the native executable because Ghidra, Hopper, IDA, and Kuna were unavailable.
No provider or configuration was installed. This limits REA binary inspection;
the source tree, test suite, hashes, and process logs remain directly
inspectable.

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
