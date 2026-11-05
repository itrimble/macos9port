# Runtime Loop — 2026-11-04, Part 3

## MI1 music report

Ian reports the SCUMM Bar music was audible. The first roughly 20 seconds
sounded doubled; afterward it was mostly alright and returned to normal. This
is a listening report, not a loopback capture. The prior callback trace had no
duplicate same-pitch commands, but recorded a 754 ms guest scheduling gap, a
7 FPS interval, and 34,304 callback underrun frames near startup. This is a
correlation to investigate, not a demonstrated cause or fix. Later intervals
recovered. Full audio fidelity and GPU execution of guest QuickDraw remain
unverified.

## MI2 manual lookup

I copied the installed Mac game directory to disposable `/tmp` copies and
launched each with the latest local runtime. The fresh run reaches the Voodoo
recipe lookup and stays live. Attempts using 50/38 for Warts and 27/12 for
Worms returned the game's failure messages; neither reached gameplay. The
[public answer table](https://steamcommunity.com/sharedfiles/filedetails/?id=440904699)
identifies itself as for the classic floppy release, so it is not accepted as
the Mac wheel answer. The installed game and its saves were not changed.

Runtime evidence from the earlier MI2 trace remains: 16.45 MHz median guest
clock, 54–60.9 presenter FPS, and 46.2–49k queued audio frames/sec. These
measure the current lookup screen, not sustained gameplay. Keep game code entry
separate from acceptance: the next milestone is a normal playable scene, then
input, sound, pacing, and save/reload checks.
